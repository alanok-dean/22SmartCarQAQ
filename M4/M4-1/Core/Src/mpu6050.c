/**
 * @file    mpu6050.c
 * @brief   MPU6050 驱动实现（HAL I2C，阻塞式，100~400kHz）
 *
 * 设计要点：
 *  1. init 里先读 WHO_AM_I 再写配置 —— 接线错/器件没响应时立刻返回错误，
 *     不会带着坏硬件往下跑。
 *  2. 所有 I2C 操作都带超时（100ms），总线被拉死时返回错误而不是永久阻塞。
 *  3. 量程固定 ±2g / ±250°/s，换算系数写在头文件里，原始值与物理量同时输出，
 *     便于现场核对量程是否设错。
 */
#include "mpu6050.h"
#include "main.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;

#define I2C_TIMEOUT_MS 100u

static uint8_t wr(uint8_t reg, uint8_t val)
{
    return (HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR << 1, reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1, I2C_TIMEOUT_MS)
            == HAL_OK) ? 0u : 1u;
}

static uint8_t rd(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return (HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len, I2C_TIMEOUT_MS)
            == HAL_OK) ? 0u : 1u;
}

uint8_t mpu6050_scan_bus(uint8_t *addr_list, uint8_t max)
{
    uint8_t found = 0;

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 2, 20) == HAL_OK) {
            if (found < max) {
                addr_list[found] = addr;
            }
            found++;
        }
    }
    return found;      /* 返回应答的器件个数；0 = 总线上没有任何器件 */
}

uint8_t mpu6050_init(void)
{
    uint8_t id = 0;

    /* 1. 先确认器件在总线上：读 WHO_AM_I，应为 0x68 */
    if (rd(MPU6050_REG_WHO_AM_I, &id, 1) != 0u) {
        return 2u;                      /* I2C 无应答：接线/供电问题 */
    }
    if (id != 0x68u && id != 0x69u && id != 0x70u && id != 0x98u) {
        return 1u;                      /* 读到了东西但不是 MPU6050 */
    }

    /* 2. 解除睡眠，时钟源选 PLL with X gyro（比内部振荡器稳） */
    if (wr(MPU6050_REG_PWR_MGMT_1, 0x01u) != 0u) return 2u;
    HAL_Delay(50);

    /* 3. 采样率 1kHz / (1+4) = 200Hz；DLPF 约 44Hz（抑制电机振动） */
    if (wr(MPU6050_REG_SMPLRT_DIV,   0x04u) != 0u) return 2u;
    if (wr(MPU6050_REG_CONFIG,       0x03u) != 0u) return 2u;

    /* 4. 量程 ±250°/s、±2g（对应 131 LSB/(°/s)、16384 LSB/g） */
    if (wr(MPU6050_REG_GYRO_CONFIG,  0x00u) != 0u) return 2u;
    if (wr(MPU6050_REG_ACCEL_CONFIG, 0x00u) != 0u) return 2u;

    return 0u;
}

uint8_t mpu6050_read(mpu6050_data_t *out)
{
    uint8_t buf[14];

    /* 0x3B 起连续 14 字节：ax ay az temp gx gy gz（各 2 字节，大端） */
    if (rd(MPU6050_REG_ACCEL_XOUT_H, buf, 14) != 0u) {
        return 1u;
    }

    out->ax = (int16_t)(((uint16_t)buf[0]  << 8) | buf[1]);
    out->ay = (int16_t)(((uint16_t)buf[2]  << 8) | buf[3]);
    out->az = (int16_t)(((uint16_t)buf[4]  << 8) | buf[5]);
    /* buf[6..7] 是温度，跳过 */
    out->gx = (int16_t)(((uint16_t)buf[8]  << 8) | buf[9]);
    out->gy = (int16_t)(((uint16_t)buf[10] << 8) | buf[11]);
    out->gz = (int16_t)(((uint16_t)buf[12] << 8) | buf[13]);

    out->ax_g   = (float)out->ax / MPU6050_ACCEL_LSB_PER_G;
    out->ay_g   = (float)out->ay / MPU6050_ACCEL_LSB_PER_G;
    out->az_g   = (float)out->az / MPU6050_ACCEL_LSB_PER_G;
    out->gx_dps = (float)out->gx / MPU6050_GYRO_LSB_PER_DPS;
    out->gy_dps = (float)out->gy / MPU6050_GYRO_LSB_PER_DPS;
    out->gz_dps = (float)out->gz / MPU6050_GYRO_LSB_PER_DPS;
    return 0u;
}

float mpu6050_pitch_acc_deg(const mpu6050_data_t *d)
{
    /* 绕 Y 轴转 = 俯仰；水平静止时 az≈+1g、ax≈0 */
    return atan2f(-d->ax_g, d->az_g) * 57.2957795f;
}
