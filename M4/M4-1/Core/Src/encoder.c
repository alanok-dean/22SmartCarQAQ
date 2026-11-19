/**
 * @file    encoder.c
 * @brief   编码器测速实现（TIM3 Encoder Mode）
 */
#include "encoder.h"
#include "main.h"

extern TIM_HandleTypeDef htim3;

static uint16_t s_last_cnt;

void encoder_init(void)
{
    /* CubeMX 已生成 MX_TIM3_Init()，这里只负责启动计数 */
    s_last_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
    (void)HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
}

int16_t encoder_read_delta(void)
{
    uint16_t now = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
    uint16_t diff = (uint16_t)(now - s_last_cnt);

    s_last_cnt = now;
    return (int16_t)diff;      /* 按有符号解释，自动处理 16 位回绕 */
}

float encoder_delta_to_rpm(int16_t delta, uint32_t dt_ms, uint16_t cpr)
{
    if (cpr == 0u || dt_ms == 0u) {
        return 0.0f;
    }
    /* rpm = 脉冲 / 每圈脉冲 × 60 / 秒数 */
    return ((float)delta / (float)cpr) * 60.0f * 1000.0f / (float)dt_ms;
}
