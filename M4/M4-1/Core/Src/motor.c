/**
 * @file    motor.c
 * @brief   M4-3 TB6612FNG 驱动 + 10ms 定时器 + 带抗饱和的 PI 转速环
 *
 * 引脚分配（全部避开已占用的 PA0/PA6/PA7/PB6/PB7/PA9/PA10/PC13）：
 *   PB8  TIM4_CH3 -> PWMA        PB12 -> AIN1
 *   PB9  TIM4_CH4 -> PWMB        PB13 -> AIN2
 *   PB11 -> STBY                 PB14 -> BIN1
 *                                PB15 -> BIN2
 *   （PB3/PB4/PA13/PA14/PA15 是 JTAG 脚，不用；PB12~PB15 是 SPI2 默认脚，
 *     本工程没开 SPI2，可以安全占用。）
 *
 * 时间基准：
 *   TIM4: PSC=71, ARR=999  -> 1MHz 计数 / 1000 = 1kHz PWM
 *   TIM2: PSC=7199, ARR=99 -> 72MHz/7200/100 = 100Hz = 10ms 控制周期
 */
#include "main.h"
#include "motor.h"
#include "encoder.h"
#include <math.h>

/* ---------------- 内部状态 ---------------- */
static TIM_HandleTypeDef htim4;          /* PWM */
static TIM_HandleTypeDef htim2;          /* 10ms 控制节拍 */

static volatile float    s_target   = 0.0f;
static volatile float    s_rpm      = 0.0f;
static volatile float    s_pwm      = 0.0f;
static volatile float    s_integ    = 0.0f;
static volatile int32_t  s_total    = 0;
static volatile int16_t  s_delta    = 0;
static volatile uint32_t s_ticks    = 0;
static volatile uint8_t  s_prof_on  = 0;
static volatile uint8_t  s_prof_done= 0;
static volatile uint8_t  s_prof_idx = 0;

/* 阶跃序列：{该段结束时刻(ms), 目标转速(rpm)} */
typedef struct { uint32_t t_end; float tgt; } prof_step_t;
static const prof_step_t s_prof[] = {
    { 2000u,   0.0f },   /* 0~2s   静止，记录零点 */
    { 6000u,  60.0f },   /* 2~6s   +60rpm 阶跃，看上升时间/超调 */
    { 9000u,   0.0f },   /* 6~9s   回到 0，看减速 */
    {13000u, -60.0f },   /* 9~13s  -60rpm 反向阶跃 */
    {15000u,   0.0f },   /* 13~15s 停 */
};
#define PROF_N  (sizeof(s_prof) / sizeof(s_prof[0]))

/* 一个 10ms 窗口里 1 个计数 = 多少 rpm：
 *   rpm = Δ / CPR × 60 / 0.01 = Δ × 6000 / CPR  */
#define RPM_PER_COUNT   (6000.0f / (float)ENCODER_CPR)

/* ---------------- 底层：把带符号占空比打到 TB6612 ---------------- */
static void motor_apply(float u)
{
    float   uu  = u * (float)MOT_DIR_SIGN;
    float   mag = (uu >= 0.0f) ? uu : -uu;
    uint16_t ccr;

    if (mag > (float)MOT_PWM_PERIOD) { mag = (float)MOT_PWM_PERIOD; }
    ccr = (uint16_t)mag;

    if (ccr < 1u) {
        /* 占空比 0：IN1=IN2=1 是"短路刹车"，比悬空更抗扰动 */
        HAL_GPIO_WritePin(MOT_PORT, MOT_AIN1_PIN | MOT_AIN2_PIN,
                          GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOT_PORT, MOT_BIN1_PIN | MOT_BIN2_PIN,
                          GPIO_PIN_SET);
        ccr = 0u;
    } else if (uu > 0.0f) {
        HAL_GPIO_WritePin(MOT_PORT, MOT_AIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOT_PORT, MOT_AIN2_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOT_PORT, MOT_BIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOT_PORT, MOT_BIN2_PIN, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(MOT_PORT, MOT_AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOT_PORT, MOT_AIN2_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOT_PORT, MOT_BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOT_PORT, MOT_BIN2_PIN, GPIO_PIN_SET);
    }

    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, ccr);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, ccr);
    s_pwm = u;           /* 报告控制器输出（不含接线取反），便于对照目标 */
}

/* ---------------- 10ms 控制节拍 ---------------- */
static void motor_step_10ms(void)
{
    float rpm_raw, err, u;

    s_ticks++;

    /* 1) 采样：编码器增量 -> 转速 */
    s_delta  = encoder_read_delta();
    s_total += (int32_t)s_delta;
    rpm_raw  = (float)s_delta * RPM_PER_COUNT;

    /* 一阶平滑：α=0.5，等效约 2 个周期的滞后，
     * 换来的是把 ±3rpm 的量化跳变压到 ±1.5rpm 以内 */
    s_rpm += 0.5f * (rpm_raw - s_rpm);

    /* 2) 内置阶跃序列推进 */
    if (s_prof_on) {
        if (s_ticks * 10u >= s_prof[s_prof_idx].t_end) {
            s_target = s_prof[s_prof_idx].tgt;
            s_prof_idx++;
            if (s_prof_idx >= PROF_N) {
                s_prof_on   = 0u;
                s_prof_done = 1u;
                s_target    = 0.0f;
            }
        }
    }

    /* 3) PI + 前馈
     *    u = Kp·e + ∫Ki·e·dt + Kff·目标
     *    前馈直接给出维持该转速所需的占空比，积分只需补电池电压波动，
     *    这样同样的 Kp 下超调更小。 */
    err = s_target - s_rpm;
    s_integ += MOT_KI * err * MOT_CTRL_DT;
    u = MOT_KP * err + s_integ + MOT_KFF * s_target;

    /* 4) 抗积分饱和：两个方向同时做
     *    (a) 输出限幅后，如果误差还在把积分往饱和方向推，就把这次积分退回去
     *        —— 这叫条件积分，防止"目标已经回来了但积分还在满值" */
    if (u > (float)MOT_PWM_PERIOD) {
        u = (float)MOT_PWM_PERIOD;
        if (err > 0.0f) { s_integ -= MOT_KI * err * MOT_CTRL_DT; }
    } else if (u < -(float)MOT_PWM_PERIOD) {
        u = -(float)MOT_PWM_PERIOD;
        if (err < 0.0f) { s_integ -= MOT_KI * err * MOT_CTRL_DT; }
    }
    /*    (b) 积分项本身也钳死，防止长时间堵转把积分堆到天上 */
    if (s_integ >  (float)MOT_PWM_PERIOD) { s_integ =  (float)MOT_PWM_PERIOD; }
    if (s_integ < -(float)MOT_PWM_PERIOD) { s_integ = -(float)MOT_PWM_PERIOD; }

    /* 5) 输出 */
    motor_apply(u);
}

/* TIM2 中断：标志位自己清，不走 HAL_TIM_IRQHandler，
 * 少一层函数调用，也让"到底谁清了标志"一眼可见 */
void TIM2_IRQHandler(void)
{
    if ((__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(&htim2, TIM_IT_UPDATE) != RESET))
    {
        __HAL_TIM_CLEAR_IT(&htim2, TIM_IT_UPDATE);
        motor_step_10ms();
    }
}

/* ---------------- 初始化 ---------------- */
void motor_init(void)
{
    GPIO_InitTypeDef      gpio = {0};
    TIM_OC_InitTypeDef    oc   = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* 方向脚 + STBY：推挽输出 */
    HAL_GPIO_WritePin(MOT_PORT,
                      MOT_AIN1_PIN | MOT_AIN2_PIN |
                      MOT_BIN1_PIN | MOT_BIN2_PIN, GPIO_PIN_SET);  /* 先刹车 */
    HAL_GPIO_WritePin(MOT_PORT, MOT_STBY_PIN, GPIO_PIN_RESET);   /* 先待机 */

    gpio.Pin   = MOT_AIN1_PIN | MOT_AIN2_PIN |
                 MOT_BIN1_PIN | MOT_BIN2_PIN | MOT_STBY_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MOT_PORT, &gpio);

    /* PWM 脚：复用推挽，高速 */
    gpio.Pin   = MOT_PWMA_PIN | MOT_PWMB_PIN;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MOT_PORT, &gpio);

    /* --- TIM4：1kHz PWM，占空比 0~1000 --- */
    htim4.Instance               = TIM4;
    htim4.Init.Prescaler         = 72u - 1u;      /* 72MHz/72 = 1MHz */
    htim4.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim4.Init.Period            = MOT_PWM_PERIOD - 1u;
    htim4.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim4) != HAL_OK) { Error_Handler(); }

    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.Pulse        = 0u;
    oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode   = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim4, &oc, TIM_CHANNEL_3) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_TIM_PWM_ConfigChannel(&htim4, &oc, TIM_CHANNEL_4) != HAL_OK) {
        Error_Handler();
    }
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

    /* --- TIM2：10ms 更新中断，作为控制节拍 --- */
    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 7200u - 1u;     /* 72MHz/7200 = 10kHz */
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 100u - 1u;      /* 10kHz/100 = 100Hz */
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) { Error_Handler(); }

    HAL_NVIC_SetPriority(TIM2_IRQn, 1u, 0u);   /* 比串口低一级，别抢 UART */
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_Base_Start_IT(&htim2);

    /* 最后才放开 STBY，前面的配置过程电机不会乱动 */
    HAL_GPIO_WritePin(MOT_PORT, MOT_STBY_PIN, GPIO_PIN_SET);
}

/* ---------------- 对外接口 ---------------- */
void motor_set_target(float rpm)
{
    if (rpm >  130.0f) { rpm =  130.0f; }   /* 电机额定 130rpm，超出没意义 */
    if (rpm < -130.0f) { rpm = -130.0f; }
    s_target = rpm;
}

void motor_start_profile(void)
{
    s_prof_idx  = 0u;
    s_prof_on   = 1u;
    s_prof_done = 0u;
    s_integ     = 0.0f;
    s_target    = 0.0f;
    s_ticks     = 0u;
}

uint8_t  motor_profile_running(void) { return s_prof_on; }

uint8_t  motor_profile_done(void)
{
    uint8_t d = s_prof_done;
    s_prof_done = 0u;
    return d;
}

float    motor_get_target(void) { return s_target; }
float    motor_get_rpm(void)    { return s_rpm; }
float    motor_get_pwm(void)    { return s_pwm; }
float    motor_get_integ(void)  { return s_integ; }
int32_t  motor_get_total(void)  { return s_total; }
uint32_t motor_get_ticks(void)  { return s_ticks; }
