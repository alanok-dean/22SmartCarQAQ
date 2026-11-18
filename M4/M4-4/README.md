# M4-4 ｜ 车与大脑之间的桥：上下位机协议

自定义二进制帧协议 + STM32 下位机 + Linux C 上位机，实现速度指令下行、状态数据
上行，并能在**改错波特率 / 插入脏字节 / 拔插 USB-TTL** 三种破坏下不死机、坏帧计数正确。

---

## 1. 目录结构

```
M4-4/
├── docs/
│   ├── protocol_v1.0.md       # ★ 协议契约（帧格式/字节序/CRC/消息定义/状态机）
│   └── analysis.md            # 测试记录与归因分析
├── firmware/                  # 下位机源码（与 M4-1 同一 Keil 工程，此处为同步副本）
│   ├── proto.h
│   └── proto.c
├── linux/                     # 上��机（Linux C）
│   ├── upper.c
│   └── Makefile
├── tools/
│   ├── frame_gen.py           # 示例帧生成 + CRC 独立复算（考官可现场跑）
│   ├── host_test.py           # Windows 联调/破坏测试脚本（上位机参考实现）
│   └── serial_mon.py          # 串口文本监视（看 [HB]/[CMD]）
├── data/                      # 测试日志
└── README.md
```

> 下位机代码同时存在于 `M4/M4-1/Core/`（Keil 工程实际编译的那份），
> `firmware/` 是同步副本，便于本题独立查阅。两边内容一致。

---

## 2. 协议摘要

详见 [`docs/protocol_v1.0.md`](docs/protocol_v1.0.md)。

```
SOF(2) | VER(1) | LEN(2,LE) | MSG(1) | SEQ(1) | PAYLOAD(LEN) | CRC16(2,LE)
```

| 项 | 值 |
|---|---|
| 帧头 | `0xAA 0x55` |
| 字节序 | **全部小端** |
| CRC | CRC-16/CCITT-FALSE（poly `0x1021`，init `0xFFFF`，不反转，无 xorout） |
| CRC 覆盖 | **VER 起，到 PAYLOAD 末字节**（不含 SOF、不含 CRC 自身） |
| 长度上限 | `LEN_MAX = 64`，超限直接丢帧并计数 |
| `0x01` CMD_SPEED | 上位机→下位机，`int16 left_rpm, int16 right_rpm`，LEN=4 |
| `0x02` STATUS | 下位机→上位机，50ms 周期，LEN=10，含轮速/俯仰/心跳/坏帧数 |

示例帧（可由 `tools/frame_gen.py` 复算）：

```
CMD_SPEED 300/300 : AA 55 01 04 00 01 01 2C 01 2C 01 2B 0F   CRC=0x0F2B
STATUS            : AA 55 01 0A 00 02 11 2A 01 2D 01 7B 00 2A 00 00 00 51 7C
```

---

## 3. 编译与运行

### 3.1 下位机

在 `M4/M4-1/` 的 Keil 工程里编译（`proto.c` 已加入 `Application/User/Core` 组），
烧录用 STM32CubeProgrammer：100kHz / Under Reset / Hardware reset。

### 3.2 Linux 上位机

```bash
cd linux
make
./upper -d /dev/ttyUSB0 -b 115200
```

参数：

| 参数 | 说明 |
|---|---|
| `-d` | 串口设备，**必填**，不硬编码 |
| `-b` | 波特率，默认 115200 |
| `--send L,R` | 连上后下发一次速度指令 |
| `--inject N` | 每 N 个好帧注入一帧 CRC 错误的帧（破坏测试） |
| `--time` | 运行秒数 |

实现要点：`termios` + `cfmakeraw`；`select()` 带 100ms 超时（**无裸阻塞 read**）；
`read()` 返回 0/-1 时不退出，等设备重新出现；逐字节状态机解析。

### 3.3 Windows 联调（不依赖 Linux）

```powershell
python tools/host_test.py -p COM8 --seconds 20
```

---

## 4. 测试

见 [`docs/analysis.md`](docs/analysis.md)。四种场景：

| 场景 | 命令 | 结果 |
|---|---|---|
| 正常联调 | `host_test.py --seconds 20` | 好帧持续增长，坏帧 0 |
| 注入坏帧（考验下位机） | `--inject 20` | 下位机 `bad_frame_cnt` 0→4，好帧不中断 |
| 注入坏帧（考验上位机） | `--rx-noise 20` | 上位机 `bad` 0→4，好帧不中断 |
| 拔插 USB-TTL | 运行中拔插 | 上位机不退出，自动重连并恢复收帧 |

---

## 5. 设计取舍

| 决策 | 理由 |
|---|---|
| 逐字节状态机而非"读一整帧" | 串口是字节流，一次 `read()` 可能拿到半帧或多帧；逐字节才能保证任意切分都正确 |
| 噪声不计数、CRC 错才计数 | 链路噪声（非 `0xAA` 字节）不属于坏帧，否则插一个字节就让计数虚高，失去意义 |
| LEN 上限 64 | 无上限时一个脏字节把 LEN 改成 `0xFFFF` 就能让接收缓冲越界 |
| 俯仰角用 `int16` ×10 | 跨平台无浮点字节序问题，省带宽 |
| 中断只入环形缓冲，解析放主循环 | 中断越短越好；解析若在中断里做，长帧会阻塞其他中断 |
| `USART1_IRQHandler` 写在 `proto.c` | CubeMX 重新生成会覆盖 `stm32f1xx_it.c`，写在 `proto.c` 里不会被冲掉 |

---

## 6. 已知问题与边界

- **状态帧未接真实数据**：`left_rpm/right_rpm/pitch` 目前填 0，等 M4-2 的编码器与
  MPU6050 完成后接入。协议与传输层已完整，接入只需改 `proto_send_status` 的入参。
- **50ms 周期由主循环轮询实现**，不是硬件定时器。当前负载下实测稳定，
  但若未来主循环加入耗时任务，需改为定时器中断驱动。
- **UART 发送用阻塞 `HAL_UART_Transmit`**：19 字节 @115200 约 1.6ms，50ms 周期下
  占用可接受；若要进一步降低抖动应改 DMA。

---

## 7. AI 使用声明

本题中 **`proto.c/proto.h` 的帧解析状态机与 CRC 实现、`protocol_v1.0.md` 协议文档、
`upper.c` 上位机、`frame_gen.py` / `host_test.py` / `serial_mon.py` 脚本、本文档与
`analysis.md`** 使用了 AI 辅助生成。

使用者已完成的验证：

1. `frame_gen.py` 生成的两条示例帧 CRC 与文档一致（`0x0F2B` / `0x7C51`）；
2. 下位机实测：`[CMD] L=300 R=300` 正确解析上位机指令；
3. 上位机实测：连续收 305 个好帧、坏帧 0，`hb` 与 `seq` 连续；
4. 破坏测试：注入 CRC 错误帧后，两端坏帧计数各自 +4，好帧不中断；
5. 拔插 USB-TTL：上位机不退出，打印重连日志后恢复收帧；
6. `upper.c` 在 WSL 下 `gcc -O2 -Wall -Wextra` 编译 0 error 0 warning。
