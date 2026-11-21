/**
 * @file    motor.h
 * @brief   M4-3 直流电机驱动 + 转速闭环（TB6612FNG + 10ms 定时器 + PI）
 *
 * 结构分三层，出问题可以逐层定位：
 *   1) 驱动层：TIM4 两路 PWM（PB8=CH3 / PB9=CH4）+ 4 个方向脚 + STBY
 *   2) 采样层：TIM2 每 10ms 进一次中断，读 TIM3 编码器增量，换算成 rpm
 *   3) 控制层：增量限幅 PI（带抗积分饱和）+ 前馈，输出占空比
 *
 * 为什么用 10ms 定时器中断而不是 main 循环里判时间：
 *   控制周期必须严格等间隔。main 循环里有串口打印、I2C 读 IMU 这些
 *   长短不一的操作，靠轮询会让控制周期抖到十几毫秒，PI 参数就没意义了。
 *
 * 为什么编码器只在这里读：
 *   encoder_read_delta() 是"读并清零"的语义，谁先读谁把增量拿走。
 *   如果 main 里再读一次，10ms 控制环就会丢掉那部分脉冲 —— 这是
 *   多读者共享计数器最典型的坑。所以 total/delta 全部由本模块维护，
 *   main 只能通过 motor_get_total() 拿结果。
 */
#ifndef __MOTOR_H
#define __MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---------------- 接线（改这里即可换脚） ---------------- */
#define MOT_PORT          GPIOB
#define MOT_AIN1_PIN      GPIO_PIN_12   /* 左轮方向 1 */
#define MOT_AIN2_PIN      GPIO_PIN_13   /* 左轮方向 2 */
#define MOT_BIN1_PIN      GPIO_PIN_14   /* 右轮方向 1 */
#define MOT_BIN2_PIN      GPIO_PIN_15   /* 右轮方向 2 */
#define MOT_STBY_PIN      GPIO_PIN_11   /* 低电平待机，必须拉高才出力 */

#define MOT_PWMA_PIN      GPIO_PIN_8    /* TIM4_CH3 -> PWMA */
#define MOT_PWMB_PIN      GPIO_PIN_9    /* TIM4_CH4 -> PWMB */

/* PWM：72MHz / 72 = 1MHz 计数，满量程 1000 -> 1kHz，占空比分辨率 0.1% */
#define MOT_PWM_PERIOD    1000u

/* 装反了就把这个改成 -1，不用改接线 */
#define MOT_DIR_SIGN      (-1)

/* ---------------- 控制器结构选择（M4-3 任务 3 要求对比 P/PI/PID） ----------------
 *   0 = 纯 P          只有比例，必然有稳态误差（这是要演示的第一个结论）
 *   1 = PI            加积分消稳态误差，但阶跃超调变大
 *   2 = PI + 前馈     默认，前馈先把工作点点到位，超调最小
 *   3 = PID           再加微分抑制超调，代价是对测量噪声敏感
 * 改这里 → 编译烧录 → 录一组 STEP 数据，三组录完做对比。 */
#define MOT_CTRL_MODE     2

/* ---------------- PI 参数（答辩时要能解释怎么调的） ---------------- */
/* 电机静态增益约 1000/130 ≈ 7.7 占空比/rpm，前馈按这个给初值 */
#define MOT_KP            4.0f
#define MOT_KI            80.0f      /* 单位：占空比/(rpm·s) */
#define MOT_KFF           7.5f
#define MOT_KD            0.05f      /* 微分作用在"测量值"上，不是误差上 */
#define MOT_D_ALPHA       0.3f       /* 微分项一阶低通，否则噪声会把电机抖响 */
#define MOT_CTRL_DT       0.01f      /* 10ms */

/* ---------------- 接口 ---------------- */
void     motor_init(void);

/* 设定目标转速（rpm，带符号：正=前进，负=后退） */
void     motor_set_target(float rpm);

/* 启动内置阶跃测试序列：0 -> +60 -> 0 -> -60 -> 0 */
void     motor_start_profile(void);

uint8_t  motor_profile_running(void);
uint8_t  motor_profile_done(void);   /* 读完自动清零的"刚跑完"标志 */

float    motor_get_target(void);
float    motor_get_rpm(void);        /* 10ms 采样 + 一阶平滑后的实测转速 */
float    motor_get_pwm(void);        /* 当前输出占空比（带符号） */
float    motor_get_integ(void);      /* 积分项，看抗饱和是否生效 */
int32_t  motor_get_total(void);      /* 累计脉冲 */
uint32_t motor_get_ticks(void);      /* 控制周期计数，×10ms = 运行时间 */
const char *motor_mode_name(void);   /* 当前控制器结构名，用于开机打印 */

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H */
