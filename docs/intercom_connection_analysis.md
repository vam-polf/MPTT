# 模块连接分析

## 一、连接方案概述

```
                    ┌─────────────────────────────────────────┐
                    │         E77-400MBL-01 模块              │
                    │                                         │
                    │  I2S接口:                               │
                    │    - PA8  (MCO/BCLK)     → BCLK        │
                    │    - PA9  (TX/I2S_MCK)   → LRCK        │
                    │    - PA10 (RX/I2S_SD)    → DIN (功放)  │
                    │                                         │
                    │  I2S接口(麦克风):                       │
                    │    - PA12 (DIN/MOSI)      → I2S_DATA   │
                    │    - PB12 (NSS)           → I2S_WS     │
                    │    - PB3  (SCK/JTDO)      → I2S_SCK    │
                    │                                         │
                    │  UART接口(低功耗串口):                   │
                    │    - TXD (Pin19) → 模块TX               │
                    │    - RXD (Pin20) → 模块RX               │
                    │                                         │
                    │  GPIO接口:                               │
                    │    - PB7 (PA1功能)     → 按键1         │
                    │    - PB6 (PA0功能)     → 按键2         │
                    │    - PB5 (IRQ功能)     → 按键中断      │
                    └─────────────────────────────────────────┘
                              │    │    │
                              │    │    │
        ┌─────────────────────┘    │    └─────────────────────┐
        │                          │                          │
        ▼                          ▼                          ▼
┌───────────────┐        ┌───────────────┐        ┌───────────────┐
│  INMP441      │        │ MAX98357A     │        │  SW-PB 按键   │
│  数字麦克风   │        │ 音频功放      │        │  按键模块     │
├───────────────┤        ├───────────────┤        ├───────────────┤
│ VDD  → 3.3V   │        │ VDD  → 3.3V   │        │ VCC → 3.3V    │
│ GND  → GND    │        │ GND  → GND    │        │ GND → GND     │
│ SCK  → BCLK   │        │ DIN  → DIN    │        │ OUT → GPIO    │
│ WS   → LRCK   │        │ BCLK → BCLK   │        │               │
│ DATA → I2S    │        │ LRCK → LRCK   │        │               │
│ L/R  → GND    │        │ GAIN → GND    │        │               │
│              │        │ SD   → 3.3V   │        │               │
└───────────────┘        └───────────────┘        └───────────────┘
```

## 二、详细引脚分配

### 2.1 E77-400MBL-01 模块引脚分配

| 功能 | 引脚号 | 引脚名 | 复用功能 | 连接对象 |
|------|--------|--------|----------|----------|
| 3.3V供电 | 1, 18 | 3.3V | - | VDD |
| GND | 6, 17 | GND | - | GND |
| I2S BCLK | 16 | PA8 | MCO/TIM1_CH1 | BCLK |
| I2S LRCK | 15 | PA9 | TIM1_CH2 | LRCK |
| I2S DIN | 12 | PA10 | TIM1_CH3 | MAX98357_DIN |
| I2S SCK | 21 | PB3 | JTDO/TIM2_CH2 | INMP441_SCK |
| I2S WS | 11 | PB12 | SPI2_NSS/I2S2_WS | INMP441_WS |
| I2S DATA | 14 | PA12 | TIM1_ETR | INMP441_DATA |
| UART TXD | 19 | TXD | 模块TX | MCU_RX |
| UART RXD | 20 | RXD | 模块RX | MCU_TX |
| 按键输入 | 25 | PB7 | LPTIM1_IN2/USART1_RX | KEY1 |
| 按键输入 | 24 | PB6 | LPTIM1_ETR/USART1_TX | KEY2 |
| 按键中断 | 23 | PB5 | LPTIM1_IN1/RF_IRQ1 | KEY_INT |

### 2.2 INMP441 麦克风模块

| 引脚 | 名称 | 连接位置 |
|------|------|----------|
| VDD | 电源3.3V | 3.3V |
| GND | 接地 | GND |
| L/R | 左/右声道选择 | GND (左声道) |
| WS | 字选择 | PB12 |
| SCK | 位时钟 | PB3 |
| DATA | 数据输出 | PA12 |

### 2.3 MAX98357A 功放模块

| 引脚 | 名称 | 连接位置 |
|------|------|----------|
| VDD | 电源3.3V | 3.3V |
| GND | 接地 | GND |
| DIN | 数字输入 | PA10 |
| BCLK | 位时钟 | PA8 |
| LRCK | 字选择 | PA9 |
| GAIN | 增益控制 | GND (默认增益) |
| SD | 关断控制 | 3.3V (使能) |

### 2.4 SW-PB 按键模块

| 引脚 | 名称 | 连接位置 |
|------|------|----------|
| VCC | 电源3.3V | 3.3V |
| GND | 接地 | GND |
| OUT | 输出 | PB7/PB6 |

## 三、电源设计

```
3.3V
  │
  ├─► E77模块 Pin1, Pin18
  ├─► INMP441 VDD
  ├─► MAX98357 VDD
  └─► SW-PB VCC

GND
  │
  ├─► E77模块 Pin6, Pin17
  ├─► INMP441 GND
  ├─► MAX98357 GND
  └─► SW-PB GND
```

## 四、信号完整性注意事项

1. **I2S信号走线**：
   - BCLK和LRCK信号长度差控制在5mil以内
   - DATA信号走线尽量平行
   - 避免与电源走线平行

2. **电源滤波**：
   - 每个模块VDD引脚加100nF电容
   - 加10uF钽电容做储能

3. **按键去抖**：
   - 软件去抖(推荐)
   - 硬件RC滤波(10K+100nF)

## 五、模块通信协议

### 5.1 I2S音频流
- 采样率: 16kHz (可根据需要调整)
- 位深: 16位
- 模式: 立体声/单声道

### 5.2 无线通信
- 频段: 400MHz
- 调制: LoRa
- 波特率: 9600 (UART)

### 5.3 按键功能
- KEY1: 发送模式 (PTT)
- KEY2: 接收模式选择
