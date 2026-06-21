/*
 * MPTT Mode 1 — PTT 录音回放 (应用层)
 *
 * 本文件只负责"业务逻辑": 系统时钟bring-up、PTT 按键、录放状态机、
 * 数字后处理(去DC/高通/逐块AGC/噪声门/淡入淡出)、以及诊断(RESULT/DIAG)。
 *
 * 所有音频"底层"(MCLK/I2C/I2S/WM8960 寄存器、录放模式切换、逐样本读写)
 * 已封装到 audio_wm8960.c, 通过 audio_wm8960.h 接口调用 —— 默认不要改动底层。
 *
 * 诊断内存:
 *   RESULT (0x20000100): [0]=AA boot [1]=I2C err [2]=I2S(55) [3]=state
 *                        [4]=len_lo [5]=len_hi  [6]=idx     [7]=I2C_ISR
 *   DIAG   (0x20000110): [0]=录音错误计数 [7]=首次RXNE SR
 *                        [8]=dc [9]=vmin|vmax [10]=abs_mean [11]=buf_len
 *                        [13]=gain [14]=录音bmin|bmax / 播放UDR [15]=播放迭代
 *
 * Build: firmware\build.bat
 * Flash: pyocd flash -t stm32wle5cbux firmware\build\fw.bin
 * Test:  python firmware\tools\selftest.py
 */
#include <stdint.h>
#include "stm32wle5xx.h"
#include "audio_wm8960.h"

/* ─── SRAM 检查点 ─── */
#define RESULT  ((volatile uint8_t *)0x20000100)
#define DIAG    ((volatile uint32_t *)0x20000110)

/* ─── PTT 按键: PB8 (E77 pin 6), 上拉输入, 按下=低 ─── */
#define PTT_GPIO      GPIOB_BASE
#define PTT_PIN       8

/* ─── 录音缓冲: SRAM2 (0x20008000, 16KB), 单声道 int16 ─── */
#define AUDIO_BUF     ((int16_t *)0x20008000)
#define MAX_SAMPLES   8000   /* 8000/8929Hz ≈ 0.9s, 用满 SRAM2 */

/* ─── 状态机 ─── */
enum { ST_IDLE = 0, ST_RECORD = 1, ST_PLAYBACK = 2 };

/* ==================== PTT 按键 ==================== */
static void ptt_init(void) {
    GPIO_MODER(PTT_GPIO) &= ~(3U << (PTT_PIN * 2));   /* Input */
    GPIO_PUPDR(PTT_GPIO) &= ~(3U << (PTT_PIN * 2));
    GPIO_PUPDR(PTT_GPIO) |=  (1U << (PTT_PIN * 2));   /* Pull-up */
}

static inline int ptt_pressed(void) {
    return !(GPIO_IDR(PTT_GPIO) & (1U << PTT_PIN));
}

/* ==================== Main ==================== */
int main(void) {
    /* HSI16 → SYSCLK = 16MHz (系统级 bring-up) */
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY));
    RCC_CFGR = 0x00000001;
    while ((RCC_CFGR & 0x0C) != 0x04);
    RESULT[0] = 0xAA;

    /* 音频底层一次性初始化 (MCLK + I2S + I2C + WM8960, R19 设定一次永不改) */
    uint8_t err = audio_init();
    RESULT[1] = err ? 0xEE : 0x00;
    RESULT[7] = (uint8_t)(I2C1_ISR & 0xFF);
    RESULT[2] = (audio_dbg_i2scfgr() & I2SCFGR_I2SE) ? 0x55 : 0x00;

    ptt_init();
    RESULT[3] = ST_IDLE;

    uint8_t  state    = ST_IDLE;
    uint16_t buf_idx  = 0;
    uint16_t buf_len  = 0;
    uint8_t  rec_err  = 0;   /* audio_enter_record() 返回的 I2C 错误码 */

    while (1) {
        int pressed = ptt_pressed();

        switch (state) {

        /* ─── IDLE: 等 PTT 按下 ─── */
        case ST_IDLE:
            if (pressed) {
                rec_err = audio_enter_record();   /* WM8960→主, STM32→Slave RX, 含稳定延时 */
                DIAG[2] = audio_dbg_i2scfgr();
                buf_idx = 0;
                buf_len = 0;
                state = ST_RECORD;
                RESULT[3] = ST_RECORD;
            }
            break;

        /* ─── RECORD: PTT 按住 → 录音 ─── */
        case ST_RECORD:
            if (!pressed || buf_idx >= MAX_SAMPLES) {
                buf_len = buf_idx;

                /* 原始采集统计 → DIAG[8..12] (单声道连续, stride 1) */
                {
                    int32_t dc_sum = 0, lc = 0;
                    int16_t vmin = 32767, vmax = -32768;
                    for (uint16_t i = 0; i < buf_len; i++) {
                        int16_t v = AUDIO_BUF[i];
                        dc_sum += v; lc++;
                        if (v < vmin) vmin = v;
                        if (v > vmax) vmax = v;
                    }
                    int32_t dc = (lc > 0) ? (dc_sum / lc) : 0;
                    int32_t abs_sum = 0;
                    for (uint16_t i = 0; i < buf_len; i++) {
                        int16_t v = AUDIO_BUF[i] - (int16_t)dc;
                        abs_sum += (v > 0) ? v : -v;
                    }
                    int32_t abs_mean = (lc > 0) ? (abs_sum / lc) : 0;
                    DIAG[8]  = (uint32_t)(uint16_t)dc;
                    DIAG[9]  = (uint32_t)((uint16_t)vmin | ((uint16_t)vmax << 16));
                    DIAG[10] = (uint32_t)abs_mean;
                    DIAG[11] = (uint32_t)buf_len;
                    DIAG[12] = (uint32_t)(lc);
                }

                /* 后处理(单声道): 去DC → 二阶高通(滤工频) → 逐块AGC + 噪声门 → 淡入淡出 */
                if (buf_len > 200) {
                    int32_t dc_sum = 0;
                    for (uint16_t i = 0; i < buf_len; i++) dc_sum += AUDIO_BUF[i];
                    int32_t dc = dc_sum / buf_len;
                    /* 去DC + 二阶高通(截止≈250Hz): 滤掉 50Hz 工频嗡声及谐波 */
                    int32_t lp1 = 0, lp2 = 0;
                    for (uint16_t i = 0; i < buf_len; i++) {
                        int32_t x = (int32_t)AUDIO_BUF[i] - dc;
                        lp1 += ((x - lp1) * 45) >> 8; int32_t h1 = x - lp1;   /* 1级HPF */
                        lp2 += ((h1 - lp2) * 45) >> 8; int32_t h2 = h1 - lp2; /* 2级HPF */
                        AUDIO_BUF[i] = (int16_t)h2;
                    }

                    /* 分块统计: 每 BLK 个样本(≈16ms)算绝对均值 */
                    #define BLK 128
                    #define MAXBLK 80
                    uint16_t nblk = buf_len / BLK;
                    if (nblk > MAXBLK) nblk = MAXBLK;
                    int32_t blev[MAXBLK];
                    int32_t bgain[MAXBLK];   /* 逐块增益, ×16 定点 */
                    int32_t bmin = 0x7FFFFFFF, bmax = 0;
                    for (uint16_t b = 0; b < nblk; b++) {
                        int32_t s = 0;
                        for (uint16_t k = 0; k < BLK; k++) {
                            int16_t v = AUDIO_BUF[b * BLK + k];
                            s += (v < 0) ? -v : v;
                        }
                        blev[b] = s / BLK;
                        if (blev[b] < bmin) bmin = blev[b];
                        if (blev[b] > bmax) bmax = blev[b];
                    }
                    int32_t thr = bmin + (bmax - bmin) / 8;   /* 噪声门限 */
                    DIAG[14] = (uint32_t)((uint16_t)bmin | ((uint16_t)bmax << 16));
                    uint8_t has_speech = (bmax > bmin * 3) && (bmax > 35);

                    /* 逐块AGC: 语音块→拉到 TARGET(响且均匀); 噪声块→增益0(静音) */
                    const int32_t TARGET = 14000;
                    for (uint16_t b = 0; b < nblk; b++) {
                        if (!has_speech || blev[b] < thr) {
                            bgain[b] = 0;
                        } else {
                            int32_t g = TARGET * 16 / blev[b];
                            if (g < 16)      g = 16;
                            if (g > 32 * 16) g = 32 * 16;
                            bgain[b] = g;
                        }
                    }
                    /* 平滑增益包络: attack 快、release 慢, 防抽吸/顿挫 */
                    {
                        int32_t g = 0;
                        for (uint16_t b = 0; b < nblk; b++) {
                            int32_t t = bgain[b];
                            if (t > g) g += (t - g) >> 1;
                            else       g += (t - g) >> 3;
                            bgain[b] = g;
                        }
                    }
                    /* 应用: 块间增益逐样本线性插值, 硬限幅 */
                    for (uint16_t b = 0; b < nblk; b++) {
                        uint16_t base = b * BLK;
                        int32_t g0 = bgain[b];
                        int32_t g1 = (b + 1 < nblk) ? bgain[b + 1] : bgain[b];
                        for (uint16_t k = 0; k < BLK; k++) {
                            int32_t gc = g0 + ((g1 - g0) * (int32_t)k) / BLK;
                            int32_t v = ((int32_t)AUDIO_BUF[base + k] * gc) >> 4;
                            if (v >  31000) v =  31000;
                            if (v < -31000) v = -31000;
                            AUDIO_BUF[base + k] = (int16_t)v;
                        }
                    }
                    DIAG[13] = (uint32_t)(bgain[nblk / 2] >> 4);
                    /* 尾部不足一块的样本: 当静音 */
                    for (uint16_t i = nblk * BLK; i < buf_len; i++) AUDIO_BUF[i] = 0;

                    /* 首尾淡入淡出, 消播放起止"咔哒" */
                    uint16_t fade = 256;
                    if (fade > buf_len / 4) fade = buf_len / 4;
                    for (uint16_t k = 0; k < fade; k++) {
                        AUDIO_BUF[k] =
                            (int16_t)((int32_t)AUDIO_BUF[k] * k / fade);
                        uint16_t j = buf_len - 1 - k;
                        AUDIO_BUF[j] =
                            (int16_t)((int32_t)AUDIO_BUF[j] * k / fade);
                    }
                }

                /* 记录录音期诊断, 然后切到播放 */
                DIAG[0] = (uint32_t)rec_err + audio_rx_error_frames();
                DIAG[7] = audio_rx_first_sr();

                uint8_t e = audio_enter_playback();   /* STM32→Master TX, WM8960→从 */
                DIAG[1] = e;
                DIAG[5] = audio_dbg_i2scfgr();
                buf_idx = 0;
                DIAG[14] = 0;   /* 播放 UDR 计数清零(覆盖录音期 bmin/bmax) */
                DIAG[15] = 0;   /* 播放迭代计数清零 */
                RESULT[3] = ST_PLAYBACK;
                RESULT[4] = (uint8_t)(buf_len);
                RESULT[5] = (uint8_t)(buf_len >> 8);
                state = ST_PLAYBACK;
            } else {
                int16_t s;
                if (audio_read_sample(&s)) {
                    AUDIO_BUF[buf_idx++] = s;
                }
            }
            break;

        /* ─── PLAYBACK: 单声道 → 同一样本发左右两次(驱动内部处理) ─── */
        case ST_PLAYBACK:
            if (buf_idx >= buf_len) {
                RESULT[3] = ST_IDLE; state = ST_IDLE;
            } else {
                DIAG[15]++;
                DIAG[14] = audio_tx_underruns();
                if (audio_write_sample(AUDIO_BUF[buf_idx])) buf_idx++;
            }
            break;
        }

        RESULT[6] = (uint8_t)(buf_idx);
    }
}
