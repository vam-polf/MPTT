/*
 * audio_wm8960.h — MPTT 音频底层驱动 (STM32WLE5 + WM8960, 半双工)
 *
 * 职责: 把"录音 / 播放"全部硬件细节封装在一处, 对外只暴露结构化接口。
 *   - 时钟:  MCLK (TIM2_CH4 / PA3, 2.286MHz)
 *   - 数据:  I2S2  (PA8=BCLK, PA9=LRCLK, PA10=SD, 半双工共线)
 *   - 控制:  I2C1  (PB6=SCL, PB7=SDA) → WM8960 寄存器
 *   - codec: WM8960 全寄存器初始化 + 录/放模式切换
 *
 * ★ 这是已实测调通的底层, 配置铁律(改前必读):
 *     D:\kb\wm8960\mptt-从零调试实战手册.md
 *   关键: MCLK=2.286MHz、fs=8929Hz=MCLK/256、R19=0x1FE 上电设一次永不切换。
 *
 * 设计意图: 业务层(状态机 / DSP / PTT)只调用本文件接口, 以后尽量不动本驱动。
 */
#ifndef AUDIO_WM8960_H
#define AUDIO_WM8960_H

#include <stdint.h>

/* 录、放统一采样率 = MCLK / 256 (从机模式铁律) */
#define AUDIO_SAMPLE_RATE_HZ   8929

/* ── 生命周期 ───────────────────────────────────────────────
 * 一次性初始化整条音频链路。
 *   前置: 调用方已将 SYSCLK 配为 HSI16 = 16MHz。
 *   动作: 使能 GPIOA/B、TIM2、SPI2、I2C1 时钟; 设 I2S 时钟源=SYSCLK;
 *         启动 MCLK; 配置 I2S2(默认播放/Master TX 态); 初始化 WM8960 全寄存器。
 *   返回: 0=成功; 非0 = WM8960 I2C 写错误(各步按位或)。
 */
uint8_t audio_init(void);

/* ── 模式切换(半双工) ──────────────────────────────────────
 * 进入录音: WM8960=主, STM32=Slave RX。内部含必要稳定延时并复位 RX 诊断计数。
 * 进入播放: STM32=Master TX, WM8960=从。内部含必要稳定延时并复位 TX 诊断计数。
 *   返回: 0=成功; 非0 = WM8960 I2C 写错误。
 * 注意: 全程不改 WM8960 R19(电源域), 避免音量逐次劣化。
 */
uint8_t audio_enter_record(void);
uint8_t audio_enter_playback(void);

/* ── 逐样本数据流(非阻塞, 轮询式) ──────────────────────────
 * 读: 收到一个有效左声道样本 → 写入 *out 并返回 1; 否则返回 0。
 *     内部自动处理: L/R 帧交织(只取左、丢右)、上电预热丢弃、I2S 错误帧统计。
 *
 * 写: 写入一个单声道样本(驱动自动左、右各发一次)。
 *     返回 1 = 该样本已"整帧发完"(右声道也写出), 调用方可前进到下一个样本;
 *     返回 0 = 发送缓冲忙, 或仅发了左声道 → 调用方应保持同一样本重试。
 *   典型用法:  if (audio_write_sample(buf[idx])) idx++;
 */
int audio_read_sample(int16_t *out);
int audio_write_sample(int16_t s);

/* ── 诊断(可选, 供验证脚本 / 排错) ─────────────────────────
 * 计数在对应 enter_record / enter_playback 时清零。
 */
uint32_t audio_rx_error_frames(void); /* 录音期 FRE/OVR/UDR 错误帧累计 */
uint32_t audio_rx_first_sr(void);     /* 录音首个 RXNE 时的 SPI2_SR 快照 */
uint32_t audio_tx_underruns(void);    /* 播放期 UDR 欠载累计 */
uint32_t audio_dbg_i2scfgr(void);     /* 当前 SPI2_I2SCFGR (确认 I2SE/方向) */

#endif /* AUDIO_WM8960_H */
