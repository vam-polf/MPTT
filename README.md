# MPTT
 
## 方案A：E77-400MBL-01 + INMP441 + MAX98357A 对讲机硬件连接
 
目标：按下 PTT（PA1）开始录音并通过 LoRa 发送；松开进入接收并立即播放。
 
关键点：INMP441 的 SD 引脚会持续输出数据，不能与 MAX98357A 的 DIN 和 MCU 的 I2S 数据输出线直接并联共用一根线，否则播放时会产生总线争用。本方案使用两套 I2S（SPI2/I2S2 录音 + SPI1/I2S1 播放）做物理隔离。
 
### 供电与地
 
- E77-400MBL-01：Pin1/18 = 3.3V；Pin6/17 = GND；Pin2(VCC) 与 Pin1 短接给模块供电
- INMP441：VDD = 3.3V；GND = GND（VDD 旁就近 0.1uF 去耦）
- MAX98357A：VIN = 3.3V；GND = GND（VIN 旁就近 0.1uF + 1uF 去耦）
 
### INMP441（录音，SPI2/I2S2）
 
| E77 端子 | GPIO | 复用功能 | INMP441 引脚 | 方向 |
|---:|---|---|---|---|
| 16 | PA8 | SPI2_SCK / I2S2_CK | SCK/BCLK | E77 → Mic |
| 15 | PA9 | SPI2_NSS / I2S2_WS | WS/LRCLK | E77 → Mic |
| 12 | PA10 | SPI2_MOSI / I2S2_SD | SD | Mic → E77 |
 
INMP441 的 L/R 选择脚：接 GND 选左声道（或接 3.3V 选右声道）。
 
### MAX98357A（播放，SPI1/I2S1）
 
| E77 端子 | GPIO | 复用功能 | MAX98357A 引脚 | 方向 |
|---:|---|---|---|---|
| 21 | PB3 | SPI1_SCK | BCLK | E77 → AMP |
| 8 | PA15 | SPI1_NSS | LRC/LRCLK | E77 → AMP |
| 23 | PB5 | SPI1_MOSI | DIN | E77 → AMP |
 
MAX98357A 的 EN/SD 引脚：可直接接 3.3V 常开，或接任意空闲 GPIO 做静音/省电控制。喇叭接 OUT+ 与 OUT-（不要把 OUT- 当作地）。
 
### PTT 按键（PA1）
 
| E77 端子 | GPIO | 连接 |
|---:|---|---|
| 28 | PA1 | 接按键一端；按键另一端接 GND；PA1 上拉到 3.3V（10k 或内部上拉） |
