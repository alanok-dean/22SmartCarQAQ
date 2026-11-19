/**
 * @file    mpu6050.h
 * @brief   MPU6050 六轴 IMU 驱动（I2C1: PB6=SCL, PB7=SDA）
 *
 * 坐标约定（与 M4-2 文档一致）：
 *   X = 车头前方   Y = 车身右侧   Z = 垂直向上（水平静止时 az ≈ +1g）
 *   绕 Y 轴转 = 俯仰 pitch
 */
#ifndef __MPU6050_H
#define __MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 7 位地址左移一位；AD0 接 GND -> 0x68，接 VCC -> 0x69 */
#define MPU6050_ADDR        0x68

#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_WHO_AM_I     0x75

/* 量程：±2g / ±250°/s（16 位输出） */
#define MPU6050_ACCEL_LSB_PER_G     16384.0f
#define MPU6050_GYRO_LSB_PER_DPS    131.0f

typedef struct {
    int16_t ax, ay, az;    /* 原始值 */
    int16_t gx, gy, gz;    /* 原始值 */
    float   ax_g, ay_g, az_g;
    float   gx_dps, gy_dps, gz_dps;
} mpu6050_data_t;

/* 扫描 I2C1 总线，把应答的 7 位地址写进 addr_list，返回器件个数（0 = 总线无器件） */
uint8_t mpu6050_scan_bus(uint8_t *addr_list, uint8_t max);

/* 返回 0 = 成功；1 = WHO_AM_I 不对；2 = 写寄存器失败 */
uint8_t mpu6050_init(void);

/* 读原始六轴数据，返回 0 = 成功 */
uint8_t mpu6050_read(mpu6050_data_t *out);

/* 用加速度计估算俯仰角（度）：atan2(-ax, az) */
float   mpu6050_pitch_acc_deg(const mpu6050_data_t *d);

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H */
