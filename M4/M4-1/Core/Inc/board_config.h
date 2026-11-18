/**
 * @file    board_config.h
 * @brief   板级引脚与参数唯一配置点。
 *
 *          换板时只改本文件，其它代码一律通过这里的宏访问硬件。
 *          现场换板演练：改 LED_PORT/LED_PIN 与 KEY_PORT/KEY_PIN 即可。
 */
#ifndef __BOARD_CONFIG_H
#define __BOARD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== LED ======================== */
#define LED_PORT            GPIOC
#define LED_PIN             GPIO_PIN_13
#define LED_PIN_NAME        "PC13"                /* 仅用于打印，换板同步改 */
#define LED_ACTIVE_LOW      1                     /* 1 = 低电平点亮（板载接法） */
#define LED_PORT_CLK_EN()   __HAL_RCC_GPIOC_CLK_ENABLE()

/* ======================== KEY ======================== */
#define KEY_PORT            GPIOA
#define KEY_PIN             GPIO_PIN_0
#define KEY_PIN_NAME        "PA0"                 /* 仅用于打印，换板同步改 */
#define KEY_ACTIVE_LOW      1                     /* 1 = 按下接地，需上拉 */
#define KEY_PORT_CLK_EN()   __HAL_RCC_GPIOA_CLK_ENABLE()

/* ====================== 时序参数 ====================== */
#define UART_BAUD           115200
#define HEARTBEAT_MS        500                   /* 心跳周期 */
#define KEY_SCAN_MS         10                    /* 按键扫描周期 */
#define KEY_DEBOUNCE_CNT    2                     /* 连续一致次数才算稳定 */

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_CONFIG_H */
