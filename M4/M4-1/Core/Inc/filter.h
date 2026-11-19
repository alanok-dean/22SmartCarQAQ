/**
 * @file    filter.h
 * @brief   互补滤波（陀螺仪 + 加速度计融合俯仰角）
 *
 * 原理：
 *   陀螺积分  pitch += (gy - bias) * dt   短期准、长期漂
 *   加速度计  pitch_acc = atan2(-ax, az)  长期准、短期噪声大、受线加速度干扰
 *   互补滤波  pitch = α*(pitch + gy*dt) + (1-α)*pitch_acc
 *
 *   α = τ / (τ + dt)，τ 是时间常数：τ 越大越信陀螺（平滑但跟得慢），
 *   τ 越小越信加速度计（跟得快但噪声大）。
 *
 * 同时维护 pitch_gyro（纯积分）用于对比，证明融合确实抑制了漂移。
 */
#ifndef __FILTER_H
#define __FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    float    pitch;        /* 融合后的俯仰角（度） */
    float    pitch_gyro;   /* 纯陀螺积分（度），仅作对比 */
    float    bias_gy;      /* 俯仰轴陀螺零偏（°/s） */
    float    tau;          /* 时间常数（秒） */
    uint32_t t_prev_ms;
    uint8_t  inited;
} pitch_filter_t;

void  filter_init(pitch_filter_t *f, float tau_s);
void  filter_set_bias(pitch_filter_t *f, float bias_dps);
void  filter_reset(pitch_filter_t *f, float pitch_deg);

/* 每来一帧 IMU 调用一次，返回融合后的俯仰角（度） */
float filter_update(pitch_filter_t *f, uint32_t now_ms,
                    float gyro_y_dps, float pitch_acc_deg);

#ifdef __cplusplus
}
#endif

#endif /* __FILTER_H */
