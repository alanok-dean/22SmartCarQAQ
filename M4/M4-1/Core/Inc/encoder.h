/**
 * @file    encoder.h
 * @brief   编码器测速（TIM3 硬件正交解码，CH1=PA6, CH2=PA7）
 *
 * 原理：
 *   TIM3 工作在 Encoder Mode（TI1 and TI2），硬件自动根据 A/B 两相相位差
 *   增减计数器 CNT —— 正转加、反转减，CPU 完全不用管，高速也不丢脉冲。
 *
 *   CNT 是 16 位，会回绕。读差值时按 int16_t 解释即可自动处理回绕：
 *   例如 CNT 从 65530 涨到 5，差值按 uint16 是 11，按 int16 仍是 11。
 *
 * 换算：
 *   转速(r/min) = 脉冲增量 / CPR × 60 / 窗口秒数
 *   CPR = 输出轴每转一圈的计数（4 倍频 × 线数 × 减速比）
 *        —— 必须实测，见 docs/analysis.md
 */
#ifndef __ENCODER_H
#define __ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 输出轴每圈计数（4 倍频）。
 * 实测标定：手动转输出轴 10 圈，total 由 0 增到 19897
 *   -> CPR = 19897 / 10 = 1989.7 ≈ 1990
 * 与理论值 11 线 × 4 × 减速比 45 = 1980 相差 0.5%，
 * 差异来自手数 10 圈的计数误差，取实测值。 */
#define ENCODER_CPR     1990

void    encoder_init(void);

/* 读取自上次调用以来的计数增量（有符号，正 = 正转） */
int16_t encoder_read_delta(void);

/* 按增量换算转速（r/min）。cpr 为 0 时返回 0 */
float   encoder_delta_to_rpm(int16_t delta, uint32_t dt_ms, uint16_t cpr);

#ifdef __cplusplus
}
#endif

#endif /* __ENCODER_H */
