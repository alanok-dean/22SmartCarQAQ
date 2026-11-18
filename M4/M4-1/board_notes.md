# M4-1 板卡资源识别

> 本文所有结论均来自**实测**，不是抄模板。每项都写明「怎么确认的」，现场可当场复核。

## 1. 引脚与资源总表

| 项 | 值 | 怎么确认的 |
|---|---|---|
| 主控型号 | **STM32F103C8T6**（Cortex-M3，Flash 64KB，RAM 20KB，LQFP48） | 芯片丝印 + ST-Link 读回 `Device ID = 0x410`、`Revision ID = Rev A` |
| 外部晶振 | **HSE 8 MHz**（板载贴片晶振，丝印 `8.000`） | 板上晶振丝印 + CubeMX 时钟树 HSE 输入频率 |
| 系统主频 | **72 MHz**（HSE 8MHz × PLL9） | 运行时打印 `HAL_RCC_GetHCLKFreq()`，实测输出 `SYSCLK = 72 MHz` |
| 总线分频 | HCLK 72MHz / PCLK1 36MHz / PCLK2 72MHz | CubeMX Clock Configuration，APB1 最高 36MHz 是器件限制 |
| 下载/调试口 | **SWD 4 针**：`3V3` / `SWDIO(PA13)` / `SWCLK(PA14)` / `GND` | ST-Link 能读到 `IDCODE`，可正常下载 |
| LED | **PC13**，**低电平点亮** | 代码写 `GPIO_PIN_RESET` 亮、`GPIO_PIN_SET` 灭，实测一致 |
| 按键 | **PA0**，**按下接地（低有效）**，内部上拉 | 万用表测 PA0 对 GND：松开 3.3V，按下 0V |
| 串口 | **USART1**：`PA9 = TX`、`PA10 = RX`，115200 8N1 | USB-TTL 接上后串口助手收到心跳 |
| 板载按键 | 仅 **NRST 复位键**，**没有用户按键** | 板上丝印 + 实测：PA0 需外接按键或杜邦线短接 GND |
| 供电 | micro-USB 5V 输入，板载 LDO 转 3.3V | 插 USB 后 `PWR` 指示灯亮 |

## 2. 时钟配置过程（现场必问）

```
HSE 8MHz  --[/1]-->  PLL 输入 8MHz
          --[×9]-->  PLLCLK 72MHz
          --[SYSCLK MUX 选 PLLCLK]-->  SYSCLK 72MHz
          --[AHB /1]-->  HCLK 72MHz
          --[APB1 /2]--> PCLK1 36MHz   （F103 的 APB1 上限 36MHz）
          --[APB2 /1]--> PCLK2 72MHz
```

对应代码在 `Core/Src/main.c` 的 `SystemClock_Config()`：
`RCC_PLL_MUL9` + `RCC_SYSCLKSOURCE_PLLCLK` + `RCC_HCLK_DIV2`（APB1）。

**为什么 APB1 要 /2**：APB1 上挂的外设（USART2/3、I2C、TIM2~4）最高只能跑 36MHz，
72MHz 直连会超规格。USART1 在 APB2 上，所以它能拿满 72MHz。

## 3. 串口波特率（现场必问）

```
USARTDIV = PCLK2 / (16 × BaudRate)
         = 72,000,000 / (16 × 115,200)
         = 39.0625
```

BRR 寄存器：整数部分 `39 = 0x27`，小数部分 `0.0625 × 16 = 1`，故 `BRR = 0x271`。

实际波特率 = 72,000,000 / (16 × 39.0625) = **115,200**，误差 ≈ **0%**。

## 4. 功能说明

### 功能 A：按键控 LED
采用 **「按住亮、松开灭」**（不是翻转）。

### 功能 B：串口心跳 + 按键事件
- 每 **500ms** 打印一行 `[HB] t=<ms> cnt=<n>`
- 按键状态变化时打印 `[KEY] DOWN/UP t=<ms>`
- **不使用 `HAL_Delay` 阻塞**，用 `HAL_GetTick()` 差值 + 累加式计时（`t_hb += 500`），
  因此不会累积漂移

### 按键消抖
主循环 **10ms 轮询一次**，连续 **2 次**读到的电平一致才认定状态改变。
放在应用层而不是中断里，因为：中断里做延时消抖会阻塞其他中断；轮询实现简单、可观测。

## 5. 烧录配置（复现关键）

| 项 | 值 |
|---|---|
| 下载器 | ST-Link V2（FW V2J37S7） |
| 接口 | SWD |
| 频率 | **100 kHz**（4MHz 时偶发 `Unable to get core ID`） |
| Connect | **Under Reset** |
| Reset | **Hardware reset** |
| 软件 | STM32CubeProgrammer（Keil 的 Debug 设置在本机偶发闪退，改用 CubeProgrammer 烧 `.hex`） |

## 6. 接线照片

见 `docs/photos/`：
- `wiring_overview.jpg`：整体接线（ST-Link + USB-TTL + 板子）
- `board_silkscreen.jpg`：板子丝印特写（能看清 PA0 / PC13 / SWD 排针标注）

## 7. 换板流程（现场考点）

所有引脚定义集中在 **`Core/Inc/board_config.h`**，换板只改这一个文件。

### 7.1 必须同时改四处

以 PC13（低有效）→ PB1（高有效）为例：

```c
#define LED_PORT            GPIOB                        /* ① 端口 */
#define LED_PIN             GPIO_PIN_1                   /* ② 引脚 */
#define LED_PIN_NAME        "PB1"
#define LED_ACTIVE_LOW      0                            /* ③ 极性 */
#define LED_PORT_CLK_EN()   __HAL_RCC_GPIOB_CLK_ENABLE() /* ④ 端口时钟 */
```

### 7.2 实测踩到的两个坑

**坑 1：只改引脚号，没改端口和时钟 → 完全不亮**

第一次改动时写成了 `LED_PORT = GPIOC` + `LED_PIN = GPIO_PIN_1`，实际驱动的是 **PC1**，
而 LED 接在 PB1，所以面包板上的灯毫无反应。

更隐蔽的是**端口时钟**：`LED_PORT_CLK_EN()` 若仍为 `__HAL_RCC_GPIOC_CLK_ENABLE()`，
则 GPIOB 处于关闭状态，此时**写 ODR 寄存器不会生效，也不产生任何报错**——
这是"代码看起来没错、引脚就是不动"的最常见原因。

**坑 2：极性没改 → 逻辑整体反转**

LED 从"低电平点亮（灌电流）"改成飞线接法的"高电平点亮（拉电流）"后，
若 `LED_ACTIVE_LOW` 仍为 1，现象是**不按时灯亮、按下时灯灭**，
功能完全反了，但编译和烧录都正常。

### 7.3 验证方式

无万用表时可用 LED + 220Ω 电阻接在新引脚上验证；有条件时直接万用表量
新引脚对 GND 的电压，按住/松开按键看电平翻转，更快且不必焊接。

### 7.4 实测耗时

改 4 行 → Rebuild → 烧录 → 验证：**约 4 分钟**（含编译与烧录，不含排查上述两坑的时间）。
排查两坑额外耗时约 20 分钟，结论已沉淀到 7.2。

