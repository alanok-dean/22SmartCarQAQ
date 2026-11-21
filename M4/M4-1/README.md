# M4-1 ｜ 点亮板子：从空目录到跑通

STM32F103C8T6 最小系统板，Keil MDK + STM32CubeMX(HAL) 工程。
实现：按键控 LED + 串口 500ms 心跳 + 按键事件上报，系统主频 72MHz。

---

## 1. 目录结构

```
M4-1/
├── board_notes.md              # 板卡资源识别（引脚/晶振/时钟/波特率推导）
├── Core/
│   ├── Inc/
│   │   ├── board_config.h      # ★ 所有引脚与参数定义，换板只改这里
│   │   ├── main.h
│   │   └── proto.h             # M4-4 协议（本题未使用，同一工程复用）
│   ├── Src/
│   │   ├── main.c              # 业务逻辑：心跳、消抖、LED
│   │   ├── proto.c             # M4-4 协议实现
│   │   ├── stm32f1xx_it.c
│   │   └── stm32f1xx_hal_msp.c
│   └── Startup/
├── Drivers/                    # ST HAL 库 + CMSIS（不修改）
├── MDK-ARM/
│   ├── M4_1_LED_LIGHTEN.uvprojx   # Keil 工程
│   └── M4_1_LED_LIGHTEN/          # 构建输出（.hex 在此，不进 git）
├── M4_1_LED_LIGHTEN.ioc        # CubeMX 配置
└── docs/photos/                # 接线照片
```

> **工程文件名说明**：目录按交付规范命名为 `M4-1`，Keil 工程沿用创建时的
> `M4_1_LED_LIGHTEN.uvprojx`（改名需同步改 `.uvprojx` 内部 TargetName 与输出名，风险大于收益）。因为刚开始我要整个简单看的，名字，然后就用这个了呃啊

---

## 2. 环境

| 项 | 版本 |
|---|---|
| 编译器 | Keil MDK，ARMCLANG **V6.19** |
| 生成工具 | STM32CubeMX |
| 库 | STM32F1xx HAL |
| 芯片包 | STM32F1xx_DFP（`STM32F103C8`） |
| 下载 | ST-Link V2 + STM32CubeProgrammer |
| 串口 | USB-TTL（CH340） |

---

## 3. 从零到跑通

### 3.1 编译

```
双击 MDK-ARM/M4_1_LED_LIGHTEN.uvprojx
Project → Rebuild all target files (F7)
```

应输出 `0 Error(s), 0 Warning(s)`，生成 `MDK-ARM/M4_1_LED_LIGHTEN/M4_1_LED_LIGHTEN.hex`。

### 3.2 烧录

Keil 的 Debug 设置在本机偶发闪退（`Cortex-M Target Driver Setup` 直接关闭），   就是keil一直跑不通，我就换成stm32cubeprogrammer了，没想到还真成功了哈哈

改用 **STM32CubeProgrammer** 烧 `.hex`：

1. 打开 CubeProgrammer → 选 `ST-LINK`
2. `Connect mode = Under Reset`，`Reset mode = Hardware reset`，`Frequency = 100 kHz`
3. **按住板子复位键 → 点 Connect → 松开**  这个真的很玄学，我自己乱摸摸出来的
4. `Open file` 选上面的 `.hex` → `Download`   其实这里软件里面是中文嗯自己翻译就好

> **为什么 100kHz**：4MHz 下本机偶发 `Unable to get core ID`；
> 100kHz + Under Reset 稳定复现成功。这是实测结论，不是默认值。

### 3.3 接线

| 连接 | ST-Link | 板子 |
|---|---|---|
| SWD | `GND` / `SWCLK` / `SWDIO` | `GND` / `SWCLK(PA14)` / `SWDIO(PA13)` |
| 串口 | USB-TTL `RXD` → `PA9`(TX)，`TXD` → `PA10`(RX)，`GND` → `GND` |

⚠️ **不要给板子接 ST-Link 的 3.3V**：板子已由 micro-USB 供电，双电源会互相顶。
⚠️ USB-TTL 的 `VCC/5V` 一律不接。

### 3.4 验证

串口助手 **115200 / 8N1**，按一下复位键：

```
================================
[BOOT] M4-1 up, SYSCLK = 72 MHz
[BOOT] LED = PC13 (active LOW)
[BOOT] KEY = PA0 (active LOW)
[BOOT] UART1 115200 8N1
================================
[HB] t=925 ms cnt=0
[HB] t=1425 ms cnt=1
[HB] t=1925 ms cnt=2
```

- `SYSCLK = 72 MHz` → 时钟配置正确
- `[HB]` 间隔精确 500ms，`cnt` 每行 +1 → 心跳无漂移
- 开机先闪 3 下是**自检指示**（`for (i=0;i<3;i++)` 闪灯），所以首个心跳在 ~925ms
- 用杜邦线把 **PA0 碰一下 GND** → 灯亮 + `[KEY] DOWN`；松开 → 灯灭 + `[KEY] UP`

---

## 4. 设计要点

### 4.1 引脚集中
`board_config.h` 是唯一的硬件配置点，`main.c` 只引用宏（`LED_PORT`/`LED_PIN`/`KEY_PORT`…），
并把 GPIO 初始化放在 `USER CODE` 区的 `BOARD_GPIO_Init()` 里。
**好处**：CubeMX 重新生成代码时不会覆盖引脚逻辑（生成的代码只动 `USER CODE` 区外的部分）。

### 4.2 非阻塞心跳
```c
if ((uint32_t)(t_now - t_hb) >= HEARTBEAT_MS) {
    t_hb += HEARTBEAT_MS;   /* 累加而非赋值：单次延迟不累积成漂移 */
    ...
}
```
用 `HAL_GetTick()` 差值比较，全程没有 `HAL_Delay`，主循环始终可响应按键。

### 4.3 按键消抖
10ms 轮询 + 连续 2 次电平一致才确认状态切换。不放在中断里，避免中断内延
时阻塞其他中断。

---

## 5. 换板演练

改 `Core/Inc/board_config.h` 中 LED 的四个宏 → Rebuild → 烧录，**不碰 `main.c`**。
实测约 4 分钟（含编译 + 烧录）。

---

## 6. 已知问题

- Keil `Options → Debug → Settings` 在本机打开时闪退（ST-Link DLL 与本机固件组合问题），
  不影响编译；烧录走 CubeProgrammer 绕过。
- 上电瞬间串口会有少量乱码：复位后 PA9 处于浮空输入，引脚电平不确定，
  USART 初始化完成后才有确定的空闲高电平。

---

## 7. 一键编译 / 烧录（命令行）

平时点 Keil 的按钮没法复现，也没法交给队友批量构建。`UV4.exe` 支持命令行，
所以封装成 `tools/build.ps1`：

```powershell
cd D:\SmartCar_Projects\M4\M4-1\tools

# 增量编译
powershell -ExecutionPolicy Bypass -File .\build.ps1

# 全量重编（改过 CubeMX 配置后用这个）
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Rebuild

# 编译 + 烧录
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Flash
```

脚本行为：

| 情况 | 输出 |
|---|---|
| 0 error 0 warning | `[OK] 编译通过` |
| 有 warning | `[OK] 编译通过：有 warning` |
| 有 error | `[FAIL]` + 打印 `build.log` 末尾 10 行 |
| Keil GUI 开着 | 直接拒绝执行（避免两边抢 `.uvprojx`） |

底层命令：

```bat
D:\Keil\UV4\UV4.exe -b MDK-ARM\M4_1_LED_LIGHTEN.uvprojx -j0 -o build.log
D:\Keil\UV4\UV4.exe -f MDK-ARM\M4_1_LED_LIGHTEN.uvprojx -j0 -o flash.log
```

`-b` 增量编译、`-r` 全量重编、`-f` 下载，返回码 0/1 = 通过，2 = 有错。

### 已知限制：烧录仍然要在 GUI 里做

命令行烧录实测失败：

```
ST-LINK USB communication error
Target DLL has been cancelled
```

原因不是配置写错，而是**这块板子的 ST-Link 只能在 "Connect under Reset" 模式下握手**，
而它的 4 针调试口**没有 NRST 线**，无法让工具自己产生复位时序。
GUI 里能成功，靠的是人工动作：按住 RST → 点 Connect → 松开 RST。命令行注入不了这个时序。

因此本项目采用的做法是：

| 环节 | 方式 | 可复现性 |
|---|---|---|
| 编译 | `tools/build.ps1` 命令行 | ✅ 完全可复现，队友拿到仓库即可构建 |
| 烧录 | Keil GUI 按 F8 | ⚠️ 需人工点一次 |

**结论**：从源码到可烧录的 `.hex/.axf` 这一段已经完全脚本化；
最后一步依赖硬件复位时序，属于工具链限制，不是工程没做。
若要彻底解决，需要把 ST-Link 的 RST 接到板子的 NRST，让 `Under Reset` 由工具控制。

> 下载完**仍需按一下板子的 RST**：Keil 默认不勾 "Reset and Run" 时芯片会停在挂起状态。

---

## 8. AI 使用声明

本题中 **`board_config.h` 的宏封装、`BOARD_GPIO_Init()`、
非阻塞心跳与消抖的实现、`board_notes.md` 与本文档的初稿**使用了 AI 辅助生成。

使用者已逐行验证：
1. 编译通过（0 error 0 warning），烧录后串口实测输出与文档一致；
2. 按键功能实测（PA0 短接 GND，灯亮灭与 `[KEY]` 事件同步）；
3. 时钟 72MHz 由运行时 `HAL_RCC_GetHCLKFreq()` 打印确认；
4. 换板演练实测通过（改 `board_config.h` 后重新编译烧录，功能正常）。
