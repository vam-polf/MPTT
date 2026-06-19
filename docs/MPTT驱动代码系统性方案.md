# MPTT 驱动代码系统性方案

> **目标**: 将 MPTT 固件从"裸寄存器验证代码"演进为"可维护、可测试、可复用的驱动框架"
> **作者**: Hermes → ShiTingShui
> **日期**: 2026-06-17
> **状态**: ✅ v1.1 已审查修正 (P0+P1 全部补入)

---

## 目录

1. [问题的本质：什么是驱动？](#1-问题的本质什么是驱动)
2. [MPTT 硬件外设全景清单](#2-mptt-硬件外设全景清单)
3. [驱动分层架构设计](#3-驱动分层架构设计)
4. [各驱动模块详细设计](#4-各驱动模块详细设计)
5. [驱动代码质量标准](#5-驱动代码质量标准)
6. [分阶段实施路线图](#6-分阶段实施路线图)
7. [待讨论的关键决策](#7-待讨论的关键决策)

---

## 1. 问题的本质：什么是驱动？

### 1.1 定义

在嵌入式系统中，**驱动 (Driver)** 是软件栈中**直接与硬件交互**的一层。它的职责是：

```
┌────────────────────────────────────┐
│         应用层 (Application)        │  ← "对讲机按PTT，播放提示音"
├────────────────────────────────────┤
│         驱动层 (Driver)             │  ← "初始化WM8960，发送音频数据"
├────────────────────────────────────┤
│    硬件抽象层 (HAL / BSP)           │  ← "写I2C寄存器0x1A，读GPIO PB0"
├────────────────────────────────────┤
│         硬件 (Hardware)             │  ← E77模组、WM8960、按键、电池
└────────────────────────────────────┘
```

### 1.2 驱动代码包含哪些内容

一个**完整的驱动模块**通常包含以下要素：

| 要素 | 说明 | 例子 (WM8960) |
|------|------|---------------|
| **初始化/反初始化** | 上电配置、时钟、引脚复用 | `wm8960_init(cfg)` → 配I2C寄存器17步 |
| **配置接口** | 运行时修改参数 | `wm8960_set_volume(ch, db)` |
| **数据通路** | 读/写数据流 | `wm8960_tx_start(buf, len)` → 启动I2S DMA |
| **状态查询** | 读取设备状态 | `wm8960_is_pll_locked()` |
| **错误处理** | 超时、NACK、欠压 | `wm8960_get_error()` |
| **省电管理** | 休眠/唤醒 | `wm8960_sleep()` / `wm8960_wake()` |
| **中断/DMA回调** | 异步事件处理 | I2S半满中断 → 填充下一帧 |
| **调试接口** | 寄存器dump、自检 | `wm8960_dump_regs()` |

### 1.3 好驱动 vs 差驱动的区别

| 维度 | ❌ 差 | ✅ 好 |
|------|------|------|
| **耦合** | 所有寄存器操作散落在main()中 | 每个外设一个.c/.h，清晰边界 |
| **硬编码** | 魔数遍地：`wm_write(0x19, 0x0C0)` | 命名常量+解释：`WM8960_PWR1_VMID_50K | WM8960_PWR1_VREF` |
| **错误处理** | 无超时，I2C失败直接hang | 每个I/O操作返回错误码，有超时 |
| **可配置性** | 改一个参数要翻遍代码 | 配置结构体+默认值，运行时可变 |
| **可测试性** | 只能在板上跑，依赖全硬件 | HAL抽象接口，可mock测试 |
| **可读性** | 无注释，不知道bit7是什么意思 | 每个寄存器操作注释bit含义和数据手册出处 |
| **可移植性** | 和STM32寄存器绑死 | 芯片相关代码集中在HAL层 |

### 1.4 从现有代码看问题

当前 `main_minimal.c` (233行) 的问题：

```c
// ❌ 魔数遍布，不知道每个位什么意思
wm_write(0x19, 0x0C0);  // 哪个bit？为什么？
wm_write(0x1A, 0x198);  // 为什么是0x198而不是0x1F8？

// ❌ 没有超时保护
while (!(I2C1_ISR & (1<<5)) && --t); // 万一死锁，永远挂在这里

// ❌ 时序耦合：I2S init必须在WM8960 init之前？之后？代码没说
i2s2_init();  // 第165行
i2c1_init();  // 第167行
// 顺序错了会怎样？不知道
```

**目标**：同样的功能，写成约 500-800 行（多个文件），但每一行都**清晰、可靠、有文档**。

---

## 2. MPTT 硬件外设全景清单

### 2.1 外设目录

```
MPTT v0.1 硬件
├── MCU: STM32WLE5CBU6 (Cortex-M4F, 48MHz, 128KB Flash, 48KB SRAM)
│   ├── 时钟系统: HSI16 / MSI / LSE / HSE / PLL
│   ├── GPIO: PA0-PA15, PB0-PB15, PC14-PC15
│   ├── I2C1: PB6(SCL), PB7(SDA) → WM8960
│   ├── SPI2/I2S2: PA8(CK), PA9(WS), PA10(SD) → WM8960 I2S
│   ├── TIM2: PA3(CH4 PWM) → WM8960 MCLK
│   ├── ADC: PA0(CH0) → BAT_ADC 电池检测
│   ├── USART1/LPUART1: (预留, 串口调试)
│   ├── SPI1/SUBGHZSPI: 内部SX1262 LoRa射频
│   └── SWD: PA13(SWDIO), PA14(SWCLK), NRST
│
├── 音频: WM8960CGEFL/RV (I2C addr=0x1A)
│   ├── DAC 播放通路: I2S_SD → DAC → Output Mixer → Class D → Speaker
│   ├── ADC 录音通路: Mic → PGA → ADC → I2S_SD
│   ├── Class D 功放: 1W@8Ω BTL (SPKVDD=电池直供)
│   └── 半双工约束: ADCDAT(pin16) + DACDAT(pin14) 共享 PA10
│
├── 电源: TP4056充电 + DW01A保护 + ME6211 LDO (3.3V)
├── 交互: PTT按键(PB0) + 充电LED(红+绿) + 电池ADC
└── 接口: USB-C充电, SMA天线, SWD调试, ZH1.5扬声器/麦克风
```

### 2.2 需要驱动的外设（按优先级）

| 优先级 | 驱动模块 | 依赖 | 难度 | 说明 |
|--------|----------|------|------|------|
| 🔴 P0 | **看门狗 (wdg)** | sysclk | ⭐ | IWDG独立看门狗, 防固件跑飞 |
| 🔴 P0 | **系统时钟 (sysclk)** | 无 | ⭐⭐ | HSI16/MSI切换, PLL, 外设时钟分发 |
| 🔴 P0 | **GPIO (gpio)** | sysclk | ⭐ | 引脚复用, 上下拉, 速度配置 |
| 🔴 P0 | **I2C (i2c)** | sysclk + gpio | ⭐⭐⭐ | 主机模式, 100/400kHz, 超时+NACK处理 |
| 🔴 P0 | **Timer PWM (timer)** | sysclk + gpio | ⭐⭐ | TIM2 CH4 → MCLK 2MHz 50%占空比 |
| 🔴 P0 | **I2S (i2s)** | sysclk + gpio + timer | ⭐⭐⭐⭐ | Master TX/RX, DMA, 半双工切换 |
| 🟡 P1 | **WM8960 Codec (wm8960)** | i2c + i2s | ⭐⭐⭐⭐⭐ | 寄存器配置, 播放/录音状态机, 半双工管理 |
| 🟡 P1 | **ADC (adc)** | sysclk + gpio | ⭐⭐ | 单次/连续转换, 电池电压 |
| 🟡 P1 | **音频缓冲 (audio_buf)** | 无(纯软件) | ⭐⭐ | Ping-pong buffer, DMA回调, 采样率转换 |
| 🟢 P2 | **GPIO按键 (button)** | gpio + timer | ⭐⭐ | 消抖, 长按/短按, PTT状态机 |
| 🟢 P2 | **电源管理 (power)** | adc + gpio | ⭐⭐ | 电池百分比, 低电关机, 休眠 |
| 🟢 P2 | **LoRa Radio (lora)** | spi | ⭐⭐⭐⭐ | SX1262 AT命令或寄存器驱动 |
| 🟢 P3 | **调试日志 (debug)** | usart | ⭐ | printf重定向, 日志级别 |

---

## 3. 驱动分层架构设计

### 3.1 分层原则

```
┌──────────────────────────────────────────────────┐
│                Application Layer                  │
│  main.c: 对讲机状态机 (待机/PTT发射/接收/充电)     │
│  调用驱动API，不碰任何寄存器                        │
├──────────────────────────────────────────────────┤
│              Device Driver Layer                  │
│  wm8960.c   │  audio_buf.c  │  power.c  │  ...   │
│  每个驱动管理一个外设，提供语义化API                │
│  此层可以有状态机和业务逻辑                         │
├──────────────────────────────────────────────────┤
│           Hardware Abstraction Layer (HAL)        │
│  i2c_hal.c  │  i2s_hal.c  │  gpio_hal.c │  ...   │
│  寄存器级操作，平台相关，可被替换                    │
│  每个函数做一件事：init/write/read/deinit           │
├──────────────────────────────────────────────────┤
│              CMSIS / Register Map                 │
│  stm32wle5xx.h: 外设基址、位定义                   │
└──────────────────────────────────────────────────┘
```

### 3.2 为什么不直接用 STM32 HAL/CubeMX？

| 方案 | 优点 | 缺点 | 决策 |
|------|------|------|------|
| STM32 HAL | 成熟、跨系列 | 代码量大(ROM占~20KB)、回调机制复杂、中断延迟高 | ❌ 不用 |
| STM32 LL | 轻量、高效 | 仍需CubeMX生成，代码生成器不可控 | ⚠️ 备选 |
| **自研轻量驱动** | 完全可控、ROM<2KB、延迟低 | 需要自己写、需要验证 | ✅ **选用** |
| libopencm3 | 开源、社区维护 | STM32WLE5支持不完整 | ❌ 不成熟 |

**决策**: 基于 CMSIS 头文件的**自研轻量驱动**。参考 STM32 LL 库的宏定义风格，但不依赖 CubeMX。

### 3.3 目录结构

```
mptt/
├── firmware/                      ← 固件根目录
│   ├── Makefile                   ← 构建系统
│   ├── link.ld                    ← 链接脚本
│   ├── startup.c                  ← 启动代码 (向量表)
│   │
│   ├── hal/                       ← HAL: 硬件抽象层
│   │   ├── hal_gpio.h / .c        ← GPIO: 引脚配置、读写
│   │   ├── hal_i2c.h / .c         ← I2C: 主机发送/接收
│   │   ├── hal_i2s.h / .c         ← I2S: Master TX/RX + DMA
│   │   ├── hal_timer.h / .c       ← Timer: PWM / 延时
│   │   ├── hal_adc.h / .c         ← ADC: 单次/连续转换
│   │   ├── hal_clock.h / .c       ← 时钟树: HSI/PLL/外设时钟
│   │   ├── hal_dma.h / .c         ← DMA: 通道配置
│   │   └── hal_wdg.h / .c         ← 看门狗: IWDG独立看门狗
│   │
│   ├── drivers/                   ← 设备驱动层
│   │   ├── wm8960/
│   │   │   ├── wm8960.h           ← 公共API + 寄存器定义
│   │   │   ├── wm8960.c           ← 驱动实现 (init/config/tx/rx)
│   │   │   └── wm8960_regs.h      ← 寄存器地址和位域宏
│   │   ├── audio_buf.h / .c       ← 音频缓冲管理 (ping-pong)
│   │   ├── button.h / .c          ← 按键消抖/长按/短按
│   │   ├── power.h / .c           ← 电池检测/电源管理
│   │   └── lora.h / .c            ← LoRa 射频控制
│   │
│   ├── utils/                     ← 通用工具
│   │   ├── debug.h / .c           ← printf重定向 + 日志
│   │   ├── assert.h               ← 断言宏
│   │   └── ringbuf.h / .c         ← 环形缓冲区 (音频用)
│   │
│   ├── app/                       ← 应用层
│   │   ├── main.c                 ← 入口 + 系统初始化
│   │   └── radio_fsm.c / .h       ← 对讲机状态机
│   │
│   └── tests/                     ← 测试
│       ├── test_i2c.c             ← I2C 扫描 + 写读验证
│       ├── test_wm8960.c          ← WM8960 寄存器读写 + 自检
│       ├── test_i2s.c             ← I2S 回环 (PA10→PA10?)
│       └── test_audio.c           ← 正弦波播放 + 录音验证
│
├── hardware/                      ← (已有)
├── docs/                          ← (已有)
└── reference/                     ← (已有)
```

### 3.4 命名规范

| 元素 | 规范 | 例子 |
|------|------|------|
| HAL层函数 | `hal_<periph>_<action>()` | `hal_i2c_write(addr, data, len)` |
| 驱动层函数 | `<device>_<action>()` | `wm8960_init(cfg)` |
| 寄存器宏 | `<DEVICE>_<REG>_<FIELD>` | `WM8960_PWR1_VMID_50K` |
| 错误码 | `ERR_<MODULE>_<REASON>` | `ERR_I2C_NACK`, `ERR_WM8960_NO_MCLK` |
| 结构体 | `snake_case_t` | `wm8960_cfg_t`, `i2c_handle_t` |
| 枚举 | `snake_case_t` (值用大写) | `wm8960_state_t { WM8960_OFF, ... }` |

---

## 4. 各驱动模块详细设计

### 4.1 HAL层: GPIO

```c
// hal_gpio.h — 极简GPIO抽象

typedef enum {
    GPIO_MODE_INPUT      = 0,
    GPIO_MODE_OUTPUT     = 1,
    GPIO_MODE_AF         = 2,
    GPIO_MODE_ANALOG     = 3,
} gpio_mode_t;

typedef enum {
    GPIO_PULL_NONE  = 0,
    GPIO_PULL_UP    = 1,
    GPIO_PULL_DOWN  = 2,
} gpio_pull_t;

// 核心API (仅3个函数)
void hal_gpio_config(uint32_t port, uint8_t pin, gpio_mode_t mode,
                     uint8_t af, gpio_pull_t pull);
void hal_gpio_write(uint32_t port, uint8_t pin, bool high);
bool hal_gpio_read(uint32_t port, uint8_t pin);

// 使用示例
hal_gpio_config(GPIOB, 6, GPIO_MODE_AF, 4, GPIO_PULL_UP);  // PB6=I2C1_SCL
hal_gpio_config(GPIOB, 0, GPIO_MODE_INPUT, 0, GPIO_PULL_DOWN);  // PB0=PTT
```

### 4.2 HAL层: I2C

```c
// hal_i2c.h

typedef struct {
    uint32_t base;         // I2C1_BASE
    uint32_t timing;       // 时序寄存器值 (100kHz/400kHz)
    uint8_t  scl_pin, sda_pin;
    uint32_t port;         // GPIOB
    uint8_t  af;           // AF4 for I2C1
} i2c_cfg_t;

// 核心API
int  hal_i2c_init(const i2c_cfg_t *cfg);
int  hal_i2c_write(uint8_t dev_addr, const uint8_t *data, uint8_t len);  // 返回0成功
int  hal_i2c_read(uint8_t dev_addr, uint8_t reg, uint8_t *data, uint8_t len);
int  hal_i2c_scan(uint8_t dev_addr);  // 返回1=ACK, 0=NACK, -1=超时
void hal_i2c_deinit(void);

// 错误码
#define ERR_I2C_TIMEOUT   -1
#define ERR_I2C_NACK      -2
#define ERR_I2C_BUSY      -3
```

**设计要点**:
- 每个 I/O 操作都有 timeout (可配置)
- 返回明确错误码，不静默失败
- NACK 时自动发 STOP + 清标志
- 预留 I2C 总线恢复 (SCL toggle 9次) 作为 `hal_i2c_recover()`

### 4.3 HAL层: I2S + DMA

这是最复杂的HAL模块，需要仔细设计：

```c
// hal_i2s.h

typedef enum {
    I2S_MODE_MASTER_TX = 0,   // MCU发BCLK/LRCLK，MCU→Codec
    I2S_MODE_MASTER_RX = 1,   // MCU发BCLK/LRCLK，Codec→MCU
    I2S_MODE_SLAVE_TX  = 2,   // Codec发BCLK/LRCLK，MCU→Codec
    I2S_MODE_SLAVE_RX  = 3,   // Codec发BCLK/LRCLK，Codec→MCU
} i2s_mode_t;

typedef struct {
    i2s_mode_t mode;
    uint32_t   sample_rate;        // 目标: 8000Hz, 实际7812.5Hz (MCLK=2MHz I2SDIV=4, 误差2.3%用于窄带语音可接受)
    uint8_t    bits_per_sample;    // 16
    uint8_t    chlen;              // 帧长: CHLEN=1 (32-bit帧) — WM8960 WL=16时MCU必须用32-bit帧!
    uint32_t   mclk_hz;            // 外部MCLK频率 (TIM2 PWM=2MHz)
} i2s_cfg_t;

typedef struct {
    uint32_t bclk_hz;      // 实际BCLK
    uint32_t lrclk_hz;     // 实际采样率
    uint32_t mclk_hz;
} i2s_status_t;

// API
int  hal_i2s_init(const i2s_cfg_t *cfg);
int  hal_i2s_deinit(void);
int  hal_i2s_start(void);    // 使能I2S+DMA
int  hal_i2s_stop(void);     // 停止I2S+DMA

// DMA双缓冲
int  hal_i2s_tx_dma_start(const int16_t *buf0, const int16_t *buf1, uint16_t len);
// buf0和buf1是ping-pong缓冲，DMA自动切换，半满/全满中断触发回调

void hal_i2s_set_tx_done_callback(void (*cb)(const int16_t *completed_buf));
// 当DMA完成一个buffer时回调，应用层填入新数据

i2s_status_t hal_i2s_get_status(void);
```

**MPTT 关键设计: 半双工共线**

PA10 同时连接 WM8960 的 DACDAT(pin14) 和 ADCDAT(pin16)。

```
         PA10 (I2S2_SD)
            │
    ┌───────┴───────┐
    │               │
DACDAT(pin14)   ADCDAT(pin16)
(MCU→Codec)     (Codec→MCU)
```

播放时: WM8960 ADCDAT 必须高阻 (设 R25 ADCL/ADCR=0，或 R24 TRI=1)
录音时: STM32 I2S 方向切换为 RX, 同时关 WM8960 DAC

**完整半双工切换序列** (参考 `/mnt/d/kb/wm8960/stm32-wle5-integration.md` §七):

```c
// ===== 播放→录音 =====
// 1. 停止I2S (I2SE=0)
// 2. 关DAC相关模块
//    - R34(0x22) LD2LO=0   // DAC→Mixer断开
//    - R37(0x25) RD2RO=0
//    - R26(0x1A) DACL=DACR=0
// 3. 开ADC相关模块
//    - R25(0x19) VMIDSEL保持, VREF保持, 
//               AINL=AINR=1, ADCL=ADCR=1, MICB=1
//    - R0(0x00) LINVOL: LIPGA=0dB (bits[5:0]=0x0B for +0dB)
//    - R1(0x01) RINVOL: RIPGA=0dB
//    - R32(0x20): LMIC2B=1 (Mic→Left ADC), LINPATH bits[5:4]=01
//    - R33(0x21): RMIC2B=1, 同理
// 4. 切I2S方向: Master TX → Master RX
//    - SPI2_I2SCFGR I2SCFG = 01 (Master RX)
// 5. 修正PA9 MODER (I2S使能后可能被篡改回GPIO)
//    - GPIO_MODER(PA9) = AF mode
// 6. delay >128μs (1个I2S帧周期)
// 7. 使能I2S (I2SE=1)

// ===== 录音→播放 =====
// 1. 停止I2S (I2SE=0)
// 2. 关ADC相关模块
//    - R25 ADCL=ADCR=0, AINL=AINR=0, MICB=0
// 3. 开DAC相关模块
//    - R26 DACL=DACR=1, SPKL=SPKR=1
//    - R34 LD2LO=1
//    - R37 RD2RO=1
//    - R5(0x05) DACMU=0  // 🔴 必须! DAC默认静音
//    - R10(0x0A) LDACVOL=0xFF, DACVU=1
//    - R11(0x0B) RDACVOL=0xFF, DACVU=1
// 4. 切I2S方向: Master RX → Master TX
// 5. 修正PA9 MODER
// 6. delay >128μs
// 7. 使能I2S (I2SE=1)
```

**注意**: 
- 任何时候 PA10 只有一个方向有驱动 (MCU TX 或 WM8960 ADCDAT)
- 切换时序必须>1帧周期 (~128μs @ Fs=7812.5Hz)
- 如果切换后 PA9 MODER 被 I2S 硬件篡改 (已知 STM32 硬件bug), 必须在 I2SE=1 后再次写回 AF 模式

### 4.4 驱动层: WM8960 Codec

这是整个项目的**核心驱动**，也是最复杂的。

```c
// wm8960.h

/* ===== 配置结构体 ===== */
typedef struct {
    // 时钟
    uint32_t mclk_hz;          // MCLK频率 (当前2MHz)
    uint32_t sample_rate;      // 采样率 (目标8kHz, MCLK=2MHz I2SDIV=4→实际7812.5Hz, 误差2.3%可接受)
    uint8_t  dclk_div;         // D类功放时钟分频 (1/2/4/8/16)
    
    // 音频接口
    uint8_t  word_len;         // 位宽: 16/20/24/32
    uint8_t  format;           // I2S / Left-Justified / DSP
    
    // 初始路由
    bool     dac_enabled;      // 上电后是否使能DAC播放
    bool     adc_enabled;      // 上电后是否使能ADC录音
    bool     speaker_enabled;  // 是否使能扬声器功放
    
    // 初始音量 (0=-97dB, 255=0dB)
    uint8_t  dac_vol;          // DAC数字音量
    uint8_t  speaker_vol;      // 扬声器音量 0-127 (7-bit, bit8=VU, bit7=ZC, bits[6:0]=VOL; 127=+6dB)
    
    // 麦克风
    uint8_t  mic_boost;        // 麦克风增益 (0/13/20/29dB)
    bool     mic_pga_enabled;
    bool     micbias_enabled;   // 驻极体偏置电压 (R25 bit1 MICB)
    
    // Anti-pop
    bool     anti_pop_enabled;  // 开关机pop抑制 (R28 APOP1)
    
    // Bypass 模拟直通 (PTT监听用: Mic→Speaker 低延迟)
    // ⚠️  待验证: 模拟直通是否需要 MCLK? (skill stm32-wm8960-audio v2.1 标注)
    bool     bypass_enabled;    // 初始化后进入Bypass模式
} wm8960_cfg_t;

/* ===== 默认配置 ===== */
#define WM8960_CFG_DEFAULT {           \
    .mclk_hz        = 2000000,        \
    .sample_rate    = 8000,           \
    .dclk_div       = 2,              \
    .word_len       = 16,             \
    .format         = 0,              \
    .dac_enabled    = true,           \
    .adc_enabled    = false,          \
    .speaker_enabled = true,          \
    .dac_vol        = 255,            \
    .speaker_vol    = 127,            \
    .mic_boost      = 20,             \
    .mic_pga_enabled = false,         \
    .micbias_enabled = false,         \
    .anti_pop_enabled = true,         \
    .bypass_enabled  = false,         \
}

/* ===== 状态枚举 ===== */
typedef enum {
    WM8960_OFF,               // 未上电/复位
    WM8960_STANDBY,           // VMID已充电，DAC/ADC关闭
    WM8960_PLAYING,           // 播放中 (DAC active)
    WM8960_RECORDING,         // 录音中 (ADC active)
    WM8960_ERROR,             // 错误状态
} wm8960_state_t;

/* ===== API ===== */
int  wm8960_init(const wm8960_cfg_t *cfg);      // 完整初始化 (17步)
int  wm8960_deinit(void);                         // 关断+省电
int  wm8960_reset(void);                          // 软复位

// 音频路由
int  wm8960_set_direction(audio_dir_t dir);       // TX(播放) ↔ RX(录音) ↔ BYPASS(模拟直通)
int  wm8960_set_mode(wm8960_state_t mode);        // 播放/录音/旁路/待机模式切换
int  wm8960_mute(bool mute);                      // 全局静音

// 音量控制
int  wm8960_set_dac_vol(uint8_t vol);             // 0-255
int  wm8960_set_speaker_vol(uint8_t vol);         // 0-127 (7-bit, bits[6:0]; bit6≠SPKMUTE!)
int  wm8960_set_mic_boost(uint8_t gain_db);       // 0/13/20/29

// 状态
wm8960_state_t wm8960_get_state(void);
int  wm8960_self_test(void);                      // 读取ID? (WM8960无ID寄存器，需验证ACK)
void wm8960_dump_regs(char *buf, int len);        // 寄存器dump (只能dump shadow)

/* ===== 错误码 ===== */
#define ERR_WM8960_I2C        -10
#define ERR_WM8960_TIMEOUT    -11
#define ERR_WM8960_NO_MCLK    -12
#define ERR_WM8960_INVALID_STATE -13
```

**wm8960.c 内部实现结构**:

```c
// 内部状态
static wm8960_state_t g_state = WM8960_OFF;
static wm8960_cfg_t   g_cfg;

// 寄存器shadow (WM8960只写不读，必须shadow)
static uint16_t reg_cache[WM8960_REG_COUNT];  // 56个寄存器

// 写寄存器的内部函数
static int reg_write(uint8_t reg, uint16_t val) {
    if (reg >= WM8960_REG_COUNT) return -1;
    
    uint8_t b1 = (reg << 1) | ((val >> 8) & 1);
    uint8_t b2 = val & 0xFF;
    
    int ret = hal_i2c_write(WM8960_I2C_ADDR, (uint8_t[]){b1, b2}, 2);
    if (ret == 0) {
        reg_cache[reg] = val;  // 更新shadow
    }
    return ret;
}

// 修改寄存器指定位 (读shadow → 改 → 写)
static int reg_update(uint8_t reg, uint16_t mask, uint16_t val) {
    uint16_t new_val = (reg_cache[reg] & ~mask) | (val & mask);
    return reg_write(reg, new_val);
}
```

### 4.5 驱动层: 音频缓冲 (audio_buf)

```c
// audio_buf.h

#define AUDIO_BUF_SIZE  256   // 每帧samples数 (32ms @ 8kHz)

typedef struct {
    int16_t buf0[AUDIO_BUF_SIZE];   // Ping buffer
    int16_t buf1[AUDIO_BUF_SIZE];   // Pong buffer
    int16_t *active;                // 当前DMA正在使用的
    int16_t *ready;                 // 应用层填充完毕，等DMA切换
    bool     new_data;              // ready中有新数据
} audio_pingpong_t;

// API
void audio_buf_init(audio_pingpong_t *ap);
int16_t* audio_buf_get_write_buffer(audio_pingpong_t *ap);  // 获取可写的buffer
void audio_buf_commit(audio_pingpong_t *ap, uint16_t len);  // 标记已写入

// DMA回调中调用 (中断上下文!)
int16_t* audio_buf_swap(audio_pingpong_t *ap);  // 交换活跃buffer
```

### 4.6 驱动层: 其他

**按键 (button)**:
```c
typedef enum { BTN_UP, BTN_DOWN, BTN_SHORT, BTN_LONG, BTN_DOUBLE } btn_event_t;
int  button_init(void);
btn_event_t button_poll(void);  // 非阻塞，10ms消抖
```

**电源管理 (power)**:
```c
int  power_init(void);
int  power_get_battery_mv(void);    // 返回电池电压mV
int  power_get_battery_pct(void);   // 返回百分比 0-100
bool power_is_charging(void);       // 充电状态
bool power_is_low_battery(void);    // <3.3V 低电
```

**LoRa (lora)** - P2阶段:
```c
int  lora_init(uint32_t freq_hz, uint8_t sf, uint8_t bw);
int  lora_send(const uint8_t *data, uint16_t len);
int  lora_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);
int  lora_set_frequency(uint32_t freq_hz);
int  lora_set_power(int8_t dbm);        // -9 ~ +22 dBm
```

---

## 5. 驱动代码质量标准

### 5.1 必须遵守的铁律

| # | 规则 | 反例 | 正例 |
|---|------|------|------|
| 1 | **零魔数** | `wm_write(0x19, 0x0C0)` | `wm_write(WM8960_PWR1, WM8960_PWR1_VMID_50K \| WM8960_PWR1_VREF)` |
| 2 | **每行有出处** | 无注释的位操作 | `// R25(0x19) bit[8:7]=VMIDSEL: 01=50kΩ startup, bit[6]=VREF (DS §Power Mgmt)` |
| 3 | **所有I/O有超时** | `while(!(REG & FLAG));` | `while(!(REG & FLAG) && --timeout); if(!timeout) return ERR_TIMEOUT;` |
| 4 | **错误不静默** | `return;` | `return ERR_I2C_NACK;` 上层必须处理 |
| 5 | **状态显式** | 全局变量随意改 | 状态机枚举 + `state_transition()` 函数检查合法性 |
| 6 | **可逆操作** | `init()` 后没有 `deinit()` | 每个 `init()` 配 `deinit()` |
| 7 | **断言用于编程错误** | `if(p == NULL)` 运行时检查 | `ASSERT(p != NULL);` — 调用者传NULL是bug, 不是runtime |
| 8 | **头文件最小包含** | `#include "all.h"` | 每个.h只包含自己需要的 |

### 5.2 WM8960 寄存器宏定义示例

```c
// wm8960_regs.h — 数据手册 §Register Map 的精确转译

// 寄存器地址
#define WM8960_LINVOL    0x00
#define WM8960_RINVOL    0x01
#define WM8960_LOUT1     0x02
#define WM8960_ROUT1     0x03
#define WM8960_CLOCK1    0x04
#define WM8960_DACCTL1   0x05
#define WM8960_DACCTL2   0x06
#define WM8960_IFACE1    0x07
#define WM8960_CLOCK2    0x08
#define WM8960_IFACE2    0x09
#define WM8960_LDAC      0x0A
#define WM8960_RDAC      0x0B
#define WM8960_RESET     0x0F
#define WM8960_PWR1      0x19
#define WM8960_PWR2      0x1A
// ... (共56个)

// R25 (0x19) POWER MANAGEMENT 1
#define WM8960_PWR1_VMIDSEL_SHIFT   7
#define WM8960_PWR1_VMIDSEL_MASK    (3 << 7)
#define WM8960_PWR1_VMIDSEL_OFF     (0 << 7)   // 00: 关闭VMID
#define WM8960_PWR1_VMIDSEL_50K     (1 << 7)   // 01: 50kΩ启动
#define WM8960_PWR1_VMIDSEL_250K    (2 << 7)   // 10: 2×250kΩ低功耗
#define WM8960_PWR1_VMIDSEL_5K      (3 << 7)   // 11: 5kΩ快速启动
#define WM8960_PWR1_VREF            (1 << 6)   // bit[6]: VREF enable
#define WM8960_PWR1_AINL            (1 << 5)   // bit[5]: Left input PGA
#define WM8960_PWR1_AINR            (1 << 4)   // bit[4]: Right input PGA
#define WM8960_PWR1_ADCL            (1 << 3)   // bit[3]: Left ADC
#define WM8960_PWR1_ADCR            (1 << 2)   // bit[2]: Right ADC
#define WM8960_PWR1_MICB            (1 << 1)   // bit[1]: Mic Bias
#define WM8960_PWR1_DIGENB          (1 << 0)   // bit[0]: Digital enable (DIGENB=0→off!)

// R26 (0x1A) POWER MANAGEMENT 2
#define WM8960_PWR2_DACL            (1 << 8)   // bit[8]: Left DAC
#define WM8960_PWR2_DACR            (1 << 7)   // bit[7]: Right DAC
#define WM8960_PWR2_LOUT1           (1 << 6)   // bit[6]: Left Out1
#define WM8960_PWR2_ROUT1           (1 << 5)   // bit[5]: Right Out1
#define WM8960_PWR2_SPKL            (1 << 4)   // bit[4]: Left Speaker
#define WM8960_PWR2_SPKR            (1 << 3)   // bit[3]: Right Speaker
#define WM8960_PWR2_OUT3            (1 << 2)   // bit[2]: OUT3
#define WM8960_PWR2_PLL_EN          (1 << 1)   // bit[1]: PLL enable
// bit[0]: reserved

// R5 (0x05) DAC Control 1
#define WM8960_DACCTL1_DACMU        (1 << 3)   // bit[3]: DAC Mute (1=mute, 0=unmute!)
#define WM8960_DACCTL1_DEEMPH_MASK  (3 << 1)   // bits[2:1]: De-emphasis
#define WM8960_DACCTL1_DACDIV2      (1 << 0)   // bit[0]: DAC 2x oversampling rate

// ... (所有56个寄存器的位域宏)
```

### 5.3 错误处理模式

```c
// 模式1: 错误码传递 (用于初始化等线性流程)
// WM8960 初始化序列 (18寄存器, 含延时)
// 🔴 前置条件: MCLK (TIM2 PWM) 必须先于WM8960 I2C配置启动!
// 顺序不可乱, 每步都有原因 (详见 wm8960_regs.h 和数据手册)
int wm8960_init(const wm8960_cfg_t *cfg) {
    int ret;
    
    // Step 0: MCLK must be running before any I2C command
    // (handled by caller — hal_timer_mclk_start() must precede this call)
    
    // Step 1: 软复位所有寄存器到默认值
    ret = reg_write(WM8960_RESET, 0x000);
    if (ret) goto err;
    delay_ms(50);  // 复位后稳定时间
    
    // Step 2: 设ADCLRC引脚为GPIO模式 (浮空引脚, 避免噪声触发)
    ret = reg_write(WM8960_IFACE2, WM8960_IFACE2_ALRCGPIO);
    if (ret) goto err;
    
    // Step 3: VMID启动 (50kΩ分压, 快速充电)
    ret = reg_write(WM8960_PWR1, WM8960_PWR1_VMIDSEL_50K | WM8960_PWR1_VREF);
    if (ret) goto err;
    delay_ms(100);  // VMID电容充电 (~100ms)
    
    // Step 4: 切换VMID到低功耗模式 (500kΩ分压)
    ret = reg_write(WM8960_PWR1, WM8960_PWR1_VMIDSEL_250K | WM8960_PWR1_VREF);
    if (ret) goto err;
    
    // Step 5: 使能DAC+Speaker功放电源 (不开ADC, 避免半双工冲突)
    ret = reg_write(WM8960_PWR2, WM8960_PWR2_DACL | WM8960_PWR2_DACR 
                                  | WM8960_PWR2_SPKL  | WM8960_PWR2_SPKR);
    if (ret) goto err;
    
    // Step 6: 使能Output Mixer
    ret = reg_write(WM8960_PWR3, WM8960_PWR3_LOMIX | WM8960_PWR3_ROMIX);
    if (ret) goto err;
    delay_ms(50);  // 模拟电路稳定
    
    // Step 7: 时钟配置 — SYSCLK = MCLK直通 (无PLL)
    ret = reg_write(WM8960_CLOCK1, 0x000);  // CLKSEL=0(MCLK), MS=0(Slave)
    if (ret) goto err;
    
    // Step 8: DCLKDIV — 🔴 必须覆盖默认÷16! D类功放需要600k-1.5MHz
    //          MCLK=2MHz, DCLKDIV=÷2 → DCLK=1MHz ✅
    ret = reg_write(WM8960_CLOCK2, WM8960_DCLKDIV_2);
    if (ret) goto err;
    
    // Step 9: I2S接口配置 — 16-bit, I2S格式, WM8960 Slave
    ret = reg_write(WM8960_IFACE1, WM8960_IFACE1_FMT_I2S | WM8960_IFACE1_WL_16BIT);
    if (ret) goto err;
    
    // Step 10: 解除DAC静音 — 🔴 致命坑! DACMU默认=1(静音)
    ret = reg_write(WM8960_DACCTL1, 0x000);  // DACMU=0
    if (ret) goto err;
    
    // Step 11: DAC数字音量 — 最大 (0dB)
    ret = reg_write(WM8960_LDAC, WM8960_DACVU | 0xFF);
    if (ret) goto err;
    ret = reg_write(WM8960_RDAC, WM8960_DACVU | 0xFF);
    if (ret) goto err;
    
    // Step 12: 路由 — DAC→Output Mixer
    ret = reg_write(WM8960_LOUTMIX, WM8960_LD2LO);
    if (ret) goto err;
    ret = reg_write(WM8960_ROUTMIX, WM8960_RD2RO);
    if (ret) goto err;
    
    // Step 13: 扬声器音量 — 7-bit (0-127), 从cfg读取
    ret = reg_write(WM8960_SPKL, WM8960_SPKVU | (cfg->speaker_vol & 0x7F));
    if (ret) goto err;
    ret = reg_write(WM8960_SPKR, WM8960_SPKVU | (cfg->speaker_vol & 0x7F));
    if (ret) goto err;
    
    // Step 14: Anti-pop抑制 (如果使能)
    // R28(0x1C) APOP1: 抑制开关机/切换时的 pop/click
    // 内核驱动 DAPM bias ON 路径使用慢速 ramp-up
    if (cfg->anti_pop_enabled) {
        ret = reg_write(WM8960_APOP1, 0x000C);  // VCOP_RMP(bit3)+VREF_RMP(bit2)=慢速ramp
        if (ret) goto err;
    }
    
    // Step 15: Class D使能 — 🔴 R49=0x00F7 (不是0x00C0!)
    //          bits[7:6]=SPK_OP_EN=11, bits[5:0]=0x37 必须保持硅片默认参数!
    ret = reg_write(WM8960_CLASSD1, 0x00F7);
    if (ret) goto err;
    
    // Step 16: Class D增益增强
    ret = reg_write(WM8960_CLASSD3, WM8960_DCGAIN_5 | WM8960_ACGAIN_5);
    if (ret) goto err;
    
    // Step 17: 如果使能MICBIAS (录音模式)
    if (cfg->micbias_enabled) {
        ret = reg_write(WM8960_PWR1, 
            WM8960_PWR1_VMIDSEL_250K | WM8960_PWR1_VREF | WM8960_PWR1_MICB);
        if (ret) goto err;
    }
    
    g_state = cfg->bypass_enabled ? WM8960_BYPASS : WM8960_STANDBY;
    return 0;

err:
    g_state = WM8960_ERROR;
    return ret;
}

// 模式2: 状态机 (用于运行时事件)
void wm8960_on_i2s_error(void) {
    if (g_state == WM8960_PLAYING) {
        g_state = WM8960_ERROR;
        // 通知上层: 播放异常
    }
}
```

### 5.4 断言与调试

```c
// assert.h
#ifdef DEBUG
#define ASSERT(expr) do { \
    if (!(expr)) { \
        debug_printf("ASSERT FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        while(1); \
    } \
} while(0)
#else
#define ASSERT(expr) ((void)0)
#endif

// 使用
void wm8960_set_volume(uint8_t vol) {
    ASSERT(vol <= 255);  // 编程错误，不是runtime error
    // ...
}
```

---

### 5.5 已知陷阱清单 🔴

MPTT 项目在 bringup 过程中已验证的 10 个致命和非致命陷阱。**每个编写驱动的人必须逐条理解**。

| # | 陷阱 | 现象 | 正确做法 | 陷阱来源 |
|---|------|------|---------|---------|
| 1 | **MODER (1<<N) = Output 而非 AF** | I2S/I2C 完全无信号 | `GPIO_MODER = (2 << N)` (AF mode = 0b10) | STM32 GPIO |
| 2 | **PA3 AF5 (I2S2_MCK) 不输出** | MCLK=0V, WM8960 不工作 | 用 TIM2_CH4 PWM 替代 (PA3=AF1) | E77 模组限制 |
| 3 | **DACMU 默认=1 (静音)** | 喇叭完全无声 | R5 写 0x000 → DACMU=0 | WM8960 数据手册 |
| 4 | **DCLKDIV 默认÷16** | 功放极轻/无声/发热 | R8 写 DCLKDIV=÷2 (0x044) | WM8960 数据手册 |
| 5 | **R49 bits[5:0] 非保留位** | 功放参数错误, 音量异常 | 必须写 0x0F7 (保持硅片默认 0x37) | MPTT bringup 实测 |
| 6 | **ADCLRC 浮空 (pin15)** | ADC 随机触发, PA10 总线冲突 | R9 bit6 ALRCGPIO=1 (设为 GPIO 模式) | MPTT bringup 实测 |
| 7 | **I2S 使能后 PA9 MODER 被篡改** | WS 无输出, I2S 静默 | I2SE=1 后重新写回 PA9 AF mode | STM32 已知硬件 bug |
| 8 | **CHLEN=0 (16-bit帧)** | WM8960 帧解析错误, 左右声道交换 | CHLEN=1 (32-bit帧), WL=16 | STM32 + WM8960 |
| 9 | **R40 bit6 ≠ SPKMUTE** | 被误导写错误值 (0x1BF=静音而非0x1FF) | bits[6:0]=7-bit 音量, bit6 是音量数据 | MPTT bringup 实测 |
| 10 | **无电池时 TP4056 供流不足** | Class D 大音量时 SPKVDD 掉电 | 电池充电中禁止大功率发射 | 硬件设计约束 |
| 11 | **HSI16 精度 ±1% (温漂)** | 极端温度下采样率漂移 | 可接受 (窄带语音容错高). 如需高精度可加 PLL | STM32 数据手册 |
| 12 | **WM8960 只写不读** | 无法验证寄存器写入是否正确 | Shadow cache 记录上次写入值; 通过行为验证 (音量变化/波形改变) | WM8960 数据手册 |

### 5.6 WM8960 初始化速查卡 (播放模式)

从知识库 `/mnt/d/kb/wm8960/README.md` 移植，17 步完整序列：

```c
// 播放模式 (Speaker DAC) — 已验证可用
{0x0F, 0x0000},  // ① Reset
{0x09, 0x0040},  // ② ALRCGPIO=1 (浮空ADCLRC)
{0x19, 0x0080},  // ③ VMID=50kΩ
{delay, 100ms},  // ④ VMID充电
{0x19, 0x01C0},  // ⑤ VMID=250kΩ + VREF
{0x1A, 0x0198},  // ⑥ DACL+DACR+SPKL+SPKR
{0x2F, 0x000C},  // ⑦ LOMIX+ROMIX
{delay, 50ms},   // ⑧ 模拟稳定
{0x04, 0x0000},  // ⑨ MCLK直通
{0x08, 0x0044},  // ⑩ DCLKDIV=÷2 ← 必须覆盖默认值!
{0x07, 0x0002},  // ⑪ I2S 16-bit Slave
{0x05, 0x0000},  // ⑫ DACMU=0 ← 必须! 否则无声!
{0x0A, 0x01FF},  // ⑬ L DAC max
{0x0B, 0x01FF},  // ⑭ R DAC max
{0x22, 0x0100},  // ⑮ LD2LO (DAC→Mixer)
{0x25, 0x0100},  // ⑯ RD2RO (DAC→Mixer)
{0x28, 0x01FF},  // ⑰ SPK L (max, unmuted)
{0x29, 0x01FF},  // ⑱ SPK R (max, unmuted)
{0x31, 0x00F7},  // ⑲ Class D ON ← 必须0x0F7不是0x0C0!
{0x33, 0x002D},  // ⑳ Class D +5.1dB boost
```

### 5.7 中断优先级设计

I2S DMA 中断驱动模式下，各中断源的抢占优先级分配：

| 中断源 | 抢占优先级 | 响应优先级 | 理由 |
|--------|-----------|-----------|------|
| I2S DMA 半满/全满 | 1 (最高) | 0 | 音频实时性最高, 16ms内必须填充下一帧 |
| SysTick (1ms) | 2 | 0 | 系统节拍, 按键消抖/超时计时 |
| I2C (如果用中断) | 3 | 0 | 寄存器配置优先级低于音频 |
| 按键 EXTI | 4 | 0 | PTT 按键响应 <100ms 足够 |
| ADC 完成 | 4 | 1 | 电池检测 1秒一次即可 |

**裸机轮询模式** (Phase 2): 主循环顺序检查标志位, 无需上述优先级表。
**中断模式** (Phase 3+): 启用 I2S DMA 中断, 按上表配置 NVIC。

---

### 5.8 错误检测与恢复

| 错误场景 | 检测方式 | 恢复策略 |
|---------|---------|---------|
| I2C NACK | `I2C1_ISR & NACKF` | 重试3次, 每次间隔1ms, 仍失败→ERR_I2C_NACK |
| I2C 超时 | TXIS/STOPF 超时计数器 | I2C总线恢复(SCL toggle 9次)→重试→失败告警 |
| MCLK 丢失 | 间接检测: I2S 停止产生 BCLK (检查 SPI2_SR) | 复位 TIM2 → 重新初始化音频通路 |
| I2S underrun | SPI2_SR UDR 标志 | 停止DMA→重新填充buffer→重启 |
| I2S overrun | SPI2_SR OVR 标志 | 丢弃溢出帧, 继续接收 |
| WM8960 无响应 | I2C scan 连续3次NACK | 复位 WM8960 → 重新初始化 |
| 半双工切换失败 | 切换后1秒内无预期数据 | 回退到 IDLE → 重新尝试切换 |
| 时钟切换失败 | RCC_CFGR SWS 位未切换到目标时钟 | 回退到 HSI16 → 告警 |

### 5.9 省电/休眠策略 (概要)

电池供电设备，PTT 释放后需进入低功耗：

| 状态 | 保持供电的模块 | 唤醒延迟 | 功耗估算 |
|------|---------------|---------|---------|
| **活跃** (PTT按下/接收中) | 所有模块全开 | 0 | ~120mA TX, ~11mA RX |
| **待机** (PTT释放, 等待中) | RCC, GPIO, SysTick, IWDG, I2C | ~100μs | ~2mA (MCU RUN + WM8960 STANDBY) |
| **休眠** (5分钟无操作) | IWDG, GPIO(PTT唤醒引脚) | ~2ms | ~50μA (MCU STOP + WM8960 OFF) |

---

## 6. 分阶段实施路线图

### Phase 0: 基础设施 (预计 1-2 天)

**目标**: 可编译、可烧录、可调试的基础框架

```
□ 0.1 搭建Makefile (arm-none-eabi-gcc) + link.ld
□ 0.2 提取 CMSIS 头 (stm32wle5xx.h) 到 hal/
□ 0.3 实现 hal_clock.c (HSI16 初始化 + 系统时钟配置)
□ 0.4 实现 hal_gpio.c (pin config/read/write)
□ 0.5 实现 debug.c (SWO/Semihosting printf, 或 UART printf)
□ 0.6 实现 assert.h
□ 0.7 实现 hal_wdg.c (IWDG 1.6s timeout, 主循环喂狗)
□ 0.8 编写 test_blinky.c → LED闪烁验证框架可用
```

**验证标准**: 充电LED闪烁 → 框架OK

### Phase 1: HAL层核心外设 (预计 2-3 天)

**目标**: I2C和I2S可以可靠通信

```
□ 1.1 实现 hal_i2c.c
      - 主机发送/接收 (2字节模式)
      - 超时 + NACK处理
      - I2C扫描 (验证WM8960存在)
      - I2C总线恢复
□ 1.2 实现 hal_timer.c
      - TIM2 CH4 PWM → MCLK 2MHz
      - 延时函数 (SysTick或DWT)
□ 1.3 实现 hal_i2s.c + hal_dma.c
      - SPI2 I2S Master TX 配置
      - 双缓冲DMA传输
      - TX完成回调机制
      - I2S时钟计算和验证
□ 1.4 实现 hal_adc.c
      - PA0 ADC单次转换
      - 校准流程
```

**验证标准**: 
- I2C scan 找到 0x1A (WM8960)
- I2S BCLK 示波器验证 = 500kHz (CHLEN=1, 32-bit帧)
- MCLK 示波器验证 = 2MHz
- 🔴 关键时序: MCLK必须先于I2S启动 (TIM2 PWM启动→delay 10ms→I2S init)

### Phase 2: WM8960 驱动 + 音频通路 (预计 3-4 天)

**目标**: 喇叭出声音、麦克风能录音

```
□ 2.1 创建 wm8960_regs.h (56个寄存器完整位域宏)
□ 2.2 实现 wm8960.c:
      - 初始化状态机 (OFF→STANDBY→PLAYING/RECORDING)
      - 寄存器shadow管理
      - 播放通路配置 (DAC→Mixer→Class D→Speaker)
      - 音量控制 API
      - 静音控制
      - 自检函数
□ 2.3 实现 audio_buf.c:
      - Ping-pong双缓冲
      - DMA完成回调填充
□ 2.4 编写 test_audio.c:
      - 977Hz正弦波播放 (已有代码迁移到驱动框架)
      - 音量渐变测试
      - 长时间稳定性测试 (1小时连续播放)
```

**验证标准**:
- 正弦波从喇叭清晰播放
- 音量可调 (I2C写寄存器，响度变化)
- 1小时无卡顿/无声

### Phase 3: 半双工 + 录音 (预计 2-3 天)

**目标**: 播放↔录音切换可靠

```
□ 3.1 实现 I2S Master RX 配置
□ 3.2 实现 wm8960 录音通路:
      - Mic PGA配置
      - ADC通路使能
      - MICBIAS配置
□ 3.3 实现半双工切换:
      - audio_set_direction(TX↔RX)
      - 完整序列: 停I2S → 关DAC → 开ADC → 切I2S方向 → 重启
      - 切换时间测量和优化
□ 3.4 编写 test_half_duplex.c:
      - 播放1秒 → 切换 → 录音1秒 → 切换 → 循环
      - 验证无数据冲突
```

**验证标准**:
- 录音数据可读回 (先录再放对比)
- TX↔RX切换1000次无异常
- 无pop/click噪音 (需DAC soft mute)

### Phase 4: 外设驱动完善 (预计 2-3 天)

```
□ 4.1 button.c: PTT按键消抖、长按识别
□ 4.2 power.c: 电池电压→百分比、充电检测
□ 4.3 lora.c: SX1262基本收发 (AT命令方式)
```

### Phase 5: 应用层 + 集成 (预计 2-3 天)

```
□ 5.1 radio_fsm.c: 对讲机状态机
      IDLE → PTT_PRESSED → TX_BEEP → TX_AUDIO → TX_END
      IDLE → RX_ACTIVE → RX_AUDIO → RX_END
□ 5.2 main.c: 系统初始化调度
□ 5.3 E2E测试: 两台MPTT互通
```

### 总时间估算

| 阶段 | 内容 | 预计工作日 |
|------|------|-----------|
| Phase 0 | 基础设施 | 1-2 |
| Phase 1 | HAL核心外设 | 2-3 |
| Phase 2 | WM8960 + 音频 | 3-4 |
| Phase 3 | 半双工 + 录音 | 2-3 |
| Phase 4 | 外设驱动 | 2-3 |
| Phase 5 | 应用层集成 | 2-3 |
| **合计** | | **12-18 天** |

---

## 7. 待讨论的关键决策

### 决策1: 中断 vs 轮询

| 方案 | 优点 | 缺点 |
|------|------|------|
| **轮询** (当前) | 简单，时间可预测 | CPU占用100%，不能同时做别的事 |
| **中断驱动** | CPU可休眠省电 | 中断延迟+优先级需要仔细设计 |

**建议**: Phase 2 先用轮询跑通，Phase 3 改为 I2S DMA中断 + 主循环轮询其他。
中断优先级设计见 §5.7。

### 决策2: RTOS 还是裸机

| 方案 | 优点 | 缺点 |
|------|------|------|
| **裸机 + 状态机** (推荐) | 简单、ROM<2KB、确定性强 | 复杂任务需要手动调度 |
| FreeRTOS | 多任务、有现成组件 | ROM>8KB、学习成本、调试复杂 |

**建议**: 裸机状态机。MPTT任务简单(播/录/收/发 + 按键)，无需RTOS。Phase 5如果复杂度增长再评估。

### 决策3: 采样率

| 方案 | 优点 | 缺点 |
|------|------|------|
| **8kHz** (推荐) | LoRa窄带语音标准，带宽占用小 | 音质不高 |
| 16kHz | 音质好 | LoRa带宽可能不够 |
| 48kHz | Hi-Fi | 完全浪费，LoRa根本带不动 |

**建议**: 8kHz/16bit/Mono。LoRa发射1秒音频约16KB，SF7 BW125kHz可支持。

### 决策4: WM8960 Master还是Slave

| 方案 | 优点 | 缺点 |
|------|------|------|
| **MCU Master** (当前) | MCU控制时钟，精确采样率 | MCLK需额外TIM2 PWM |
| WM8960 Master | 无需MCU MCLK | WM8960 PLL配置复杂，频率不准 |

**建议**: 保持当前 MCU Master 方案（已验证可用）。

### 决策5: 自研 vs 移植 Linux驱动

| 方案 | 优点 | 缺点 |
|------|------|------|
| **自研驱动** (推荐) | 简单可控，适合MCU | 需要自己写 |
| 移植 Linux wm8960.c | 功能完整 | 依赖ASoC框架，大量裁剪工作 |

**建议**: 自研。Linux驱动面向多场景(耳机/喇叭/多采样率/PLL)，MPTT只需要其中10%功能。

### 决策6: 代码风格

建议统一使用:
- **C99** (不使用C11特性，保持兼容性)
- **stdint.h** 固定宽度类型 (`uint8_t`, `int16_t` 等)
- **4空格缩进**
- **函数名**: `module_action()` snake_case
- **宏/枚举值**: `MODULE_VALUE` UPPER_CASE
- **文件头注释**: 每个.c/.h 顶部有模块说明

---

## 附录A: 参考资源

| 资源 | 用途 |
|------|------|
| [WM8960 datasheet v4.4](https://www.cirrus.com/products/wm8960/) | 寄存器定义权威源 |
| [Linux kernel wm8960.c](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/wm8960.c) | 驱动参考实现 (Cirrus官方) |
| [STM32WLE5 RM0461](https://www.st.com/resource/en/reference_manual/rm0461-stm32wlex-advanced-armbased-32bit-mcus-with-subghz-radio-solution-stmicroelectronics.pdf) | MCU寄存器权威源 |
| [ARM CMSIS](https://github.com/ARM-software/CMSIS_5) | Cortex-M4 标准头文件 |
| `/mnt/d/kb/wm8960/register-bit-reference.md` | MPTT专用寄存器逐位手册 |
| `/mnt/d/kb/wm8960/stm32-wle5-integration.md` | STM32+WM8960 集成指南 |

## 附录B: 现有代码迁移清单

当前代码 → 新框架的映射：

| 现有文件 | 迁移到 |
|----------|--------|
| `main.c` (I2C扫描) | `hal/hal_i2c.c` + `tests/test_i2c.c` |
| `main.c` (ADC读取) | `hal/hal_adc.c` |
| `main_minimal.c` (TIM2 PWM) | `hal/hal_timer.c` |
| `main_minimal.c` (I2S配置) | `hal/hal_i2s.c` + `hal/hal_dma.c` |
| `main_minimal.c` (WM8960寄存器写) | `drivers/wm8960/wm8960.c` + `wm8960_regs.h` |
| `audio_test/main.c` (正弦波) | `drivers/audio_buf.c` + `tests/test_audio.c` |
| `bypass_test.c` (模拟旁路测试) | `tests/test_wm8960.c` |

---

> **下一步**: 方案已通过子智能体审查并修正了3个P0错误和8个P1补充。
> 可以开始 Phase 0 实施。如需在开始前讨论任何具体设计细节，随时提出。
