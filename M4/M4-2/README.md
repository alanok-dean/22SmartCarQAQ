# M4-2 ｜ 传感器

本题目与 M4-1 共用同一块 STM32F103C8T6 与同一个 Keil 工程
（`../M4-1/MDK-ARM/M4_1_LED_LIGHTEN.uvprojx`），属于递进式开发，
未单独拆分工程，避免重复维护 HAL 与 CubeMX 配置。

- 源码：`../M4-1/Core/`（驱动代码将新增 `encoder.c/h`、`mpu6050.c/h`、`filter.c/h`）
- 数据：`./data/`（60s 静置漂移、转动一圈）
- 离线脚本：`./tools/offline_solve.py`
- 分析文档：`./docs/analysis.md`