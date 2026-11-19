/**
 * @file    filter.c
 * @brief   互补滤波实现
 */
#include "filter.h"

void filter_init(pitch_filter_t *f, float tau_s)
{
    f->pitch      = 0.0f;
    f->pitch_gyro = 0.0f;
    f->bias_gy    = 0.0f;
    f->tau        = (tau_s > 0.0f) ? tau_s : 0.5f;
    f->t_prev_ms  = 0;
    f->inited     = 0;
}

void filter_set_bias(pitch_filter_t *f, float bias_dps)
{
    f->bias_gy = bias_dps;
}

void filter_reset(pitch_filter_t *f, float pitch_deg)
{
    f->pitch      = pitch_deg;
    f->pitch_gyro = pitch_deg;
    f->inited     = 0;
}

float filter_update(pitch_filter_t *f, uint32_t now_ms,
                    float gyro_y_dps, float pitch_acc_deg)
{
    float dt;
    float rate;
    float alpha;

    if (f->inited == 0u) {
        f->t_prev_ms  = now_ms;
        f->pitch      = pitch_acc_deg;
        f->pitch_gyro = pitch_acc_deg;
        f->inited     = 1u;
        return f->pitch;
    }

    dt = (float)(uint32_t)(now_ms - f->t_prev_ms) / 1000.0f;
    if (dt <= 0.0f || dt > 0.5f) {
        dt = 0.02f;                     /* 异常间隔兜底，避免单帧把角度带偏 */
    }
    f->t_prev_ms = now_ms;

    rate = gyro_y_dps - f->bias_gy;     /* 先补零偏，否则积分必然漂 */

    f->pitch_gyro += rate * dt;         /* 纯积分：用于对比，证明会漂 */

    f->pitch += rate * dt;              /* 先按陀螺积分 */
    alpha = f->tau / (f->tau + dt);     /* α = τ/(τ+dt) */
    f->pitch = alpha * f->pitch + (1.0f - alpha) * pitch_acc_deg;

    return f->pitch;
}
