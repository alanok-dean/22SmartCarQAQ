/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "board_config.h"
#include "proto.h"
#include "mpu6050.h"
#include "filter.h"
#include "encoder.h"
#include "motor.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define _STR(x) #x
#define STR(x)  _STR(x)

#define LED_ON()   HAL_GPIO_WritePin(LED_PORT, LED_PIN, \
                   (LED_ACTIVE_LOW ? GPIO_PIN_RESET : GPIO_PIN_SET))
#define LED_OFF()  HAL_GPIO_WritePin(LED_PORT, LED_PIN, \
                   (LED_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET))

/* ================= 串口示波器录制模式（M4-2 任务 4） =================
 * VOFA+ 的 FireWater 协议只认"纯数字+逗号+换行"，行首字母会让整帧解析失败。
 * 所以这里做一个开关：
 *   VOFA_MODE = 1  纯数值 CSV（t,gyro_int,fused,acc,gy），并临时关掉
 *                  心跳/ENC/IMU 文本行和 50ms 二进制状态帧，画面才干净
 *   VOFA_MODE = 0  恢复正常的文本日志 + 二进制帧（平时用这个）
 * 录完曲线截图后记得改回 0 重新烧录。 */
#define VOFA_MODE   0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static proto_ctx_t s_proto;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
static void BOARD_GPIO_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ---- printf 重定向到 USART1（Keil: 需勾选 Use MicroLIB） ---- */
#ifdef __GNUC__
  int __io_putchar(int ch)
#else
  int fputc(int ch, FILE *f)
#endif
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
    return ch;
}

/* ---- 不依赖 stdio 的串口输出：直接调 HAL，规避 printf 卡死 ---- */
static void uart_send_str(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 200);
}

static void uart_printf(const char *fmt, ...)
{
    char    buf[128];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uart_send_str(buf);
}

/**
 * @brief 按 board_config.h 的宏初始化 LED 与按键。
 *        全部走宏，换板只改 board_config.h 一处。
 */
static void BOARD_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  LED_PORT_CLK_EN();
  KEY_PORT_CLK_EN();

  /* 上电先让 LED 熄灭 */
  HAL_GPIO_WritePin(LED_PORT, LED_PIN,
                    LED_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET);

  /* LED：推挽输出 */
  GPIO_InitStruct.Pin   = LED_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);

  /* 按键：输入。按下接地则上拉，按下接 VCC 则下拉 */
  GPIO_InitStruct.Pin  = KEY_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = KEY_ACTIVE_LOW ? GPIO_PULLUP : GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY_PORT, &GPIO_InitStruct);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint32_t t_now      = 0;
  uint32_t t_hb       = 0;          /* 上次心跳时刻 */
  uint32_t t_key      = 0;          /* 上次按键扫描时刻 */
  uint32_t hb_cnt     = 0;          /* 心跳计数 */
  uint8_t  key_cnt    = 0;          /* 消抖计数 */
  uint8_t  key_stable = 0;          /* 消抖后的按键状态：1=按下 */
  uint8_t  i          = 0;          /* 自检闪烁循环变量 */
  uint32_t t_status   = 0;          /* 上次发状态帧时刻 */
  uint16_t hb_seq     = 0;          /* 状态帧心跳计数 */
  uint8_t  rx_b       = 0;          /* 串口收到的单字节 */
  int16_t  tgt_l      = 0;          /* 上位机下发的左轮目标 */
  int16_t  tgt_r      = 0;          /* 上位机下发的右轮目标 */
  uint8_t  i2c_addrs[8] = {0};      /* I2C 扫描结果 */
  uint8_t  i2c_cnt    = 0;          /* 应答的器件个数 */
  uint8_t  imu_ok     = 0;          /* 0 = MPU6050 初始化成功 */
  uint32_t t_imu      = 0;          /* 上次读 IMU 的时刻 */
  mpu6050_data_t imu;               /* IMU 数据 */
  pitch_filter_t flt;               /* 互补滤波器 */
  uint8_t  imu_calib  = 0;          /* 0 = 还在估零偏，1 = 正常融合 */
  uint32_t t_calib    = 0;          /* 零偏估计起点 */
  uint32_t t_imu_prt  = 0;          /* 上次打印人读信息 */
  float    gy_sum     = 0.0f;       /* 零偏累加 */
  uint16_t gy_n       = 0;          /* 零偏样本数 */
  uint32_t t_enc      = 0;          /* 上次编码器采样时刻 */
  uint32_t t_log      = 0;          /* 上次打印 STEP 过程数据的时刻 */
  uint32_t t_mot_last = 0;          /* 最近一次电机"有动作"的时刻 */
  uint32_t t_key_down = 0;          /* 按键按下的时刻（判长按） */
  uint8_t  key_long   = 0;          /* 1 = 本次长按已经触发过 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  BOARD_GPIO_Init();

  /* 开机自检：闪 3 下。闪了说明程序跑到了这里（时钟配置成功）。 */
  for (i = 0; i < 3; i++)
  {
    LED_ON();
    HAL_Delay(150);
    LED_OFF();
    HAL_Delay(150);
  }

  uart_send_str("\r\n================================\r\n");
  uart_printf("[BOOT] M4-1 up, SYSCLK = %lu MHz\r\n",
              (unsigned long)(HAL_RCC_GetHCLKFreq() / 1000000U));
  uart_printf("[BOOT] LED = %s (active %s)\r\n",
              LED_PIN_NAME, LED_ACTIVE_LOW ? "LOW" : "HIGH");
  uart_printf("[BOOT] KEY = %s (active %s)\r\n",
              KEY_PIN_NAME, KEY_ACTIVE_LOW ? "LOW" : "HIGH");
  uart_printf("[BOOT] UART1 %d 8N1\r\n", (int)UART_BAUD);
  uart_send_str("================================\r\n");

  proto_init(&s_proto);
  proto_uart_start();

  /* ---------- M4-2：先证明 I2C 通，再谈算法 ---------- */
  i2c_cnt = mpu6050_scan_bus(i2c_addrs, 8);
  uart_printf("[I2C] found %u device(s):", (unsigned)i2c_cnt);
  for (i = 0; i < i2c_cnt && i < 8; i++) {
    uart_printf(" 0x%02X", (unsigned)i2c_addrs[i]);
  }
  uart_send_str("\r\n");

  imu_ok = mpu6050_init();
  if (imu_ok == 0u) {
    uart_send_str("[IMU] MPU6050 init OK\r\n");
  } else {
    uart_printf("[IMU] init FAILED, code=%u\r\n", (unsigned)imu_ok);
  }

  /* 互补滤波：τ = 0.5s，α = 0.5/(0.5+dt) */
  filter_init(&flt, 0.5f);
  t_calib = HAL_GetTick();
  uart_send_str("[CAL] 请保持静止 1 秒，正在估计陀螺零偏...\r\n");

  encoder_init();
  uart_printf("[ENC] TIM3 encoder started, CPR = %u (0 = 未标定)\r\n",
              (unsigned)ENCODER_CPR);

  /* ---------- M4-3：电机驱动 + 10ms 控制节拍 ---------- */
  motor_init();
  uart_send_str("[MOT] TB6612 ready: PWMA=PB8 PWMB=PB9 "
                "AIN=PB12/13 BIN=PB14/15 STBY=PB11\r\n");
  uart_printf("[MOT] controller = %s\r\n", motor_mode_name());
  uart_send_str("[MOT] key: press = cycle 0/40/80 rpm, hold 1s = step test\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    t_now = HAL_GetTick();

    /* ---------- 功能 B-1：500ms 心跳，非阻塞 ---------- */
    if ((uint32_t)(t_now - t_hb) >= HEARTBEAT_MS)
    {
      t_hb += HEARTBEAT_MS;         /* 累加而不是赋值 = 不会累积漂移 */
#if !VOFA_MODE
      uart_printf("[HB] t=%lu ms cnt=%lu\r\n",
                  (unsigned long)t_now, (unsigned long)hb_cnt);
#endif
      hb_cnt++;
    }

    /* ---------- M4-2：编码器累计量
     * 注意：增量已经被 10ms 控制中断取走了，这里不能再调
     * encoder_read_delta()，否则控制环会丢脉冲。 */
    if ((uint32_t)(t_now - t_enc) >= 1000u)
    {
      t_enc = t_now;
#if !VOFA_MODE
      uart_printf("[ENC] total=%ld rpm=%.2f tgt=%.1f pwm=%.0f\r\n",
                  (long)motor_get_total(), motor_get_rpm(),
                  motor_get_target(), motor_get_pwm());
#endif
    }

    /* ---------- M4-3：20ms 一行过程数据，供离线画阶跃响应 ---------- */
    if ((uint32_t)(t_now - t_log) >= 20u)
    {
      uint8_t active = (motor_get_target() != 0.0f) ||
                       (motor_profile_running() != 0u);
      t_log = t_now;
      if (active) { t_mot_last = t_now; }
      /* 目标回 0 之后再记 1.5 秒，才能看到"回到零速"的稳定过程 */
      if (active || ((t_mot_last != 0u) &&
                     ((uint32_t)(t_now - t_mot_last) < 1500u)))
      {
        uart_printf("STEP,%lu,%.1f,%.2f,%.0f,%.0f\r\n",
                    (unsigned long)(motor_get_ticks() * 10u),
                    motor_get_target(),
                    motor_get_rpm(),
                    motor_get_pwm(),
                    motor_get_integ());
      }
    }

    /* ---------- M4-3：阶跃序列跑完的提示 ---------- */
    if (motor_profile_done())
    {
      uart_send_str("[PROF] step test done\r\n");
    }

    /* ---------- M4-2：每 20ms 读 IMU，互补滤波 ---------- */
    if (imu_ok == 0u && (uint32_t)(t_now - t_imu) >= 20u)
    {
      t_imu = t_now;
      if (mpu6050_read(&imu) == 0u)
      {
        float pitch_acc = mpu6050_pitch_acc_deg(&imu);

        if (imu_calib == 0u)
        {
          /* 启动后 1 秒静止：估俯仰轴陀螺零偏 */
          gy_sum += imu.gy_dps;
          gy_n++;
          if ((uint32_t)(t_now - t_calib) >= 1000u)
          {
            imu_calib = 1u;
            filter_set_bias(&flt, gy_sum / (float)gy_n);
            filter_reset(&flt, pitch_acc);
            uart_printf("[CAL] gyro bias = %.2f dps (%u samples), init pitch = %.1f deg\r\n",
                        flt.bias_gy, (unsigned)gy_n, pitch_acc);
          }
        }
        else
        {
          float pitch = filter_update(&flt, t_now, imu.gy_dps, pitch_acc);

          /* CSV 行：供离线分析与画图；字段固定，便于 grep "^CSV," */
#if VOFA_MODE
          /* 示波器模式：只输出 1 路 rpm，通道映射不可能出错。
           * 手转输出轴一圈 → 曲线上出现一正一负两个鼓包。 */
          uart_printf("%.2f\r\n", motor_get_rpm());
#else
          uart_printf("CSV,%lu,%.2f,%.2f,%.2f,%.2f\r\n",
                      (unsigned long)t_now,
                      flt.pitch_gyro, pitch, pitch_acc, imu.gy_dps);
#endif

#if !VOFA_MODE
          if ((uint32_t)(t_now - t_imu_prt) >= 1000u)
          {
            t_imu_prt = t_now;
            uart_printf("[IMU] acc=%.1f fused=%.1f gyro_int=%.1f bias=%.2f\r\n",
                        pitch_acc, pitch, flt.pitch_gyro, flt.bias_gy);
          }
#endif
        }
      }
      else
      {
        uart_send_str("[IMU] read failed\r\n");
      }
    }

    /* ---------- M4-4 收帧：环形缓冲里的字节全部喂给状态机 ---------- */
    while (proto_uart_pop(&rx_b))
    {
      if (proto_feed(&s_proto, rx_b))
      {
        if (s_proto.frame.msg_id == MSG_CMD_SPEED && s_proto.frame.len == 4)
        {
          tgt_l = (int16_t)((uint16_t)s_proto.frame.payload[0]
                          | ((uint16_t)s_proto.frame.payload[1] << 8));
          tgt_r = (int16_t)((uint16_t)s_proto.frame.payload[2]
                          | ((uint16_t)s_proto.frame.payload[3] << 8));
          uart_printf("[CMD] L=%d R=%d\r\n", tgt_l, tgt_r);

          /* M4-3 + M4-4 打通：上位机下发的转速直接进速度环。
           * 单电机演示，先取左轮目标；motor_set_target 内部会限幅到 ±130rpm。 */
          motor_set_target((float)tgt_l);
        }
      }
    }

    /* ---------- M4-4 发帧：50ms 一帧状态（轮速暂填 0） ---------- */
    if ((uint32_t)(t_now - t_status) >= PROTO_STATUS_MS)
    {
      t_status += PROTO_STATUS_MS;
#if !VOFA_MODE
      {
        /* M4-3 接通后这里填真实轮速：上位机终于能看到闭环结果了。
         * 单电机演示，左右先填同一路；俯仰角有就带上（×10 定点）。 */
        int16_t m_l = (int16_t)motor_get_rpm();
        int16_t m_p = 0;

        if (imu_ok == 0u && imu_calib != 0u) {
          m_p = (int16_t)(flt.pitch * 10.0f);
        }
        proto_send_status((uint16_t)m_l, (uint16_t)m_l, m_p,
                          hb_seq++, (uint16_t)s_proto.bad_cnt);
      }
#endif
    }

    /* ---------- 按键扫描 + 消抖（10ms 一次，2 次一致才认） ---------- */
    if ((uint32_t)(t_now - t_key) >= KEY_SCAN_MS)
    {
      uint8_t  raw;
      uint8_t  pressed;

      t_key = t_now;
      raw     = HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN);
      pressed = KEY_ACTIVE_LOW ? (raw == GPIO_PIN_RESET)
                               : (raw == GPIO_PIN_SET);

      if (pressed != key_stable)
      {
        if (++key_cnt >= KEY_DEBOUNCE_CNT)
        {
          key_stable = pressed;
          key_cnt    = 0;
          /* ---------- 功能 B-2：按键事件打印 ---------- */
          uart_printf("[KEY] %s t=%lu ms\r\n",
                      pressed ? "DOWN" : "UP", (unsigned long)t_now);

          /* ---------- M4-3：短按轮换目标转速 ---------- */
          if (pressed)
          {
            t_key_down = t_now;
          }
          else if ((uint32_t)(t_now - t_key_down) < 1000u)
          {
            float tgt = motor_get_target();
            if      (tgt <  20.0f) { motor_set_target( 40.0f); }
            else if (tgt <  60.0f) { motor_set_target( 80.0f); }
            else                   { motor_set_target(  0.0f); }
            uart_printf("[MOT] target = %.0f rpm\r\n",
                        motor_get_target());
          }
        }
      }
      else
      {
        key_cnt = 0;
      }

      /* ---------- M4-3：长按 1 秒启动内置阶跃测试 ---------- */
      if (!key_stable)
      {
        key_long = 0u;
      }
      else if ((key_long == 0u) &&
               ((uint32_t)(t_now - t_key_down) >= 1000u))
      {
        key_long = 1u;
        motor_start_profile();
        uart_send_str("[PROF] step test: 0 -> +60 -> 0 -> -60 -> 0 rpm\r\n");
      }
    }

    /* ---------- 功能 A：按住亮、松开灭 ---------- */
    if (key_stable) { LED_ON(); } else { LED_OFF(); }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* 注意：LED / 按键的引脚配置已全部移到 BOARD_GPIO_Init()，
     由 board_config.h 的宏驱动。此处不再配置具体引脚，
     这样 CubeMX 重新生成代码时不会覆盖换板逻辑。 */
  (void)GPIO_InitStruct;
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
