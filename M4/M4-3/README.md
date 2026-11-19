# M4-3 ｜ 电机控制

本题目与 M4-1 共用同一块 STM32F103C8T6 与同一个 Keil 工程
（`../M4-1/MDK-ARM/M4_1_LED_LIGHTEN.uvprojx`），属于递进式开发，
未单独拆分工程，避免重复维护 HAL 与 CubeMX 配置。

| 项 | 位置 |
|---|---|
| 源码 | `../M4-1/Core/Inc/motor.h`、`../M4-1/Core/Src/motor.c` |
| 数据 | `./data/step_test.txt`（内置阶跃序列的完整串口日志） |
| 分析脚本 | `./tools/analyze_step.py` |
| 曲线 | `./docs/plots/step_response.png` |
| 分析文档 | `./docs/analysis.md` |

## 引脚

| 功能 | 引脚 | 说明 |
|---|---|---|
| PWMA | PB8 | TIM4_CH3，1 kHz |
| PWMB | PB9 | TIM4_CH4，1 kHz |
| AIN1 / AIN2 | PB12 / PB13 | 左轮方向 |
| BIN1 / BIN2 | PB14 / PB15 | 右轮方向 |
| STBY | PB11 | 必须拉高才出力，程序最后一步才放开 |
| 编码器 | PA6 / PA7 | TIM3 硬件正交解码（M4-2 已配） |

## 怎么跑

```powershell
cd D:\SmartCar_Projects\M4\M4-4\tools
python -u serial_mon.py -p COM11 --match STEP --save ..\M4-3\data\step_test.txt
```

短按按键：目标在 0 / 40 / 80 rpm 之间轮换。
长按 1 秒：自动跑内置阶跃序列 `0 → +60 → 0 → −60 → 0`（15 秒）。

离线分析：

```powershell
cd D:\SmartCar_Projects\M4\M4-3\tools
python analyze_step.py ..\data\step_test.txt --plot ..\docs\plots\step_response.png
```

## 设计要点

- **10 ms 严格等间隔**：用 TIM2 更新中断驱动控制环，而不是在主循环里判时间。
  主循环里有阻塞串口打印和 I2C 读 IMU，靠轮询会让控制周期抖到十几毫秒。
- **前馈 + PI**：`u = Kp·e + ∫Ki·e·dt + Kff·目标`。前馈按静态增益
  （1000/130 ≈ 7.7 占空比/rpm）给出维持转速所需占空比，积分只负责补电池电压波动。
- **双重抗积分饱和**：输出饱和时条件积分回退；积分项本身钳在 ±1000。
- **零速短路刹车**：占空比为 0 时 IN1=IN2=1，用短路刹车而不是悬空，抗扰动更好。
- **编码器独占读取**：`encoder_read_delta()` 是读并清零语义，
  只允许 10 ms 中断调用，`main` 通过 `motor_get_total()` 取累计值。
