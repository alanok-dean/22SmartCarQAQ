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
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static proto_ctx_t s_proto;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
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
      uart_printf("[HB] t=%lu ms cnt=%lu\r\n",
                  (unsigned long)t_now, (unsigned long)hb_cnt);
      hb_cnt++;
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
        }
      }
    }

    /* ---------- M4-4 发帧：50ms 一帧状态（轮速暂填 0） ---------- */
    if ((uint32_t)(t_now - t_status) >= PROTO_STATUS_MS)
    {
      t_status += PROTO_STATUS_MS;
      proto_send_status(0, 0, 0, hb_seq++);
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
        }
      }
      else
      {
        key_cnt = 0;
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
