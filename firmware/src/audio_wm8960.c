/*
 * audio_wm8960.c — MPTT 音频底层驱动实现 (STM32WLE5 + WM8960)
 *
 * ███ 本文件是"已实测调通的底层", 默认冻结。修改前务必先读: ███
 *     D:/kb/wm8960/mptt-从零调试实战手册.md
 *
 * 半双工要点:
 *   播放: STM32 Master TX + WM8960 Slave  (已验证, 同 test_tone)
 *   录音: WM8960 Master + STM32 Slave RX   (已验证, 同 rx_test)
 *   R19(PWR1)=0x1FE 在 init 设定一次, 永不改变 → ADC/MICB/VMID 始终上电, 无启动瞬态。
 *
 * STM32WL I2S 三大铁律:
 *   1. SPI2S2SEL=10(SYSCLK)   2. SPE=1(即使 I2SMOD=1)   3. MCKOE=0(PA3 用 TIM2/AF1)
 *
 * WM8960 共线: ADCDAT(pin16)+DACDAT(pin14) 短接到 I2S_SD(PA10)
 *   录音: WM8960 Master, R18 TRIS=0(ADCDAT 驱动), LRCM=1
 *   播放: WM8960 Slave,  R18 TRIS=1(ADCDAT 三态), STM32 驱动 DACDAT
 */
#include "audio_wm8960.h"
#include "stm32wle5xx.h"

/* ==================== 私有状态 ==================== */
static uint8_t  s_rx_lr;        /* 录音帧内声道: 0=左(存) 1=右(丢) */
static uint16_t s_rx_discard;   /* 录音上电预热丢弃计数 */
static uint8_t  s_tx_lr;        /* 播放: 0=待发左 1=待发右(同一单声道发两次) */
static uint32_t s_rx_err;       /* 录音期 I2S 错误帧累计 */
static uint32_t s_rx_first_sr;  /* 录音首个 RXNE 时 SR 快照(0=未发生) */
static uint32_t s_tx_udr;       /* 播放期 UDR 欠载累计 */

/* ==================== 私有工具 ==================== */
static void delay(volatile uint32_t n) {
    while (n--) __asm__ volatile("nop");
}

/* ==================== I2C1 ==================== */
static void i2c1_init(void) {
    RCC_APB1ENR1 &= ~RCC_APB1ENR1_I2C1EN;
    delay(1000);
    RCC_APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    delay(100);
    I2C1_CR1 = 0;

    /* Bus recovery: SCL toggle 9x */
    GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~(3U<<12)) | (1U<<12);
    GPIO_OTYPER(GPIOB_BASE) &= ~(1U<<6);
    GPIO_PUPDR(GPIOB_BASE) &= ~(3U<<12);
    for (int i = 0; i < 9; i++) {
        GPIO_BSRR(GPIOB_BASE) = (1U << 22);
        delay(100);
        GPIO_BSRR(GPIOB_BASE) = (1U << 6);
        delay(100);
    }

    GPIO_MODER(GPIOB_BASE) &= ~((3U<<12)|(3U<<14));
    GPIO_MODER(GPIOB_BASE) |=  ((2U<<12)|(2U<<14));
    GPIO_OTYPER(GPIOB_BASE) |= (1U<<6)|(1U<<7);
    GPIO_PUPDR(GPIOB_BASE) &= ~((3U<<12)|(3U<<14));
    GPIO_PUPDR(GPIOB_BASE) |=  ((1U<<12)|(1U<<14));
    GPIO_AFRL(GPIOB_BASE) &= ~((0xFU<<24)|(0xFU<<28));
    GPIO_AFRL(GPIOB_BASE) |=  ((4U<<24)|(4U<<28));
    I2C1_CR1 = 0;
    I2C1_TIMINGR = 0x00503D5B;
    I2C1_CR1 = 1;
}

/* WM8960 I2C 写: 返回 0=成功, 1/2/3=超时, 4=NACK */
static uint8_t wm_write(uint8_t reg, uint16_t val) {
    uint8_t b1 = (reg << 1) | ((val >> 8) & 1);
    uint8_t b2 = val & 0xFF;
    I2C1_ICR = 0x3F38;
    I2C1_CR2 = (0x1A << 1) | (2U << 16) | (1U << 25) | (1U << 13);
    uint32_t t = 100000;
    while (!(I2C1_ISR & I2C_ISR_TXIS) && --t); if (!t) return 1;
    I2C1_TXDR = b1;
    t = 100000;
    while (!(I2C1_ISR & I2C_ISR_TXIS) && --t); if (!t) return 2;
    I2C1_TXDR = b2;
    t = 100000;
    while (!(I2C1_ISR & I2C_ISR_STOPF) && --t); if (!t) return 3;
    I2C1_ICR = I2C_ISR_STOPF;
    if (I2C1_ISR & I2C_ISR_NACKF) return 4;
    return 0;
}

/* ==================== TIM2 MCLK (PA3, 2.286MHz) ==================== */
static void mclk_init(void) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    delay(100);
    GPIO_AFRL(GPIOA_BASE) &= ~(0xFU<<12);
    GPIO_AFRL(GPIOA_BASE) |=  (1U<<12);   /* PA3 = AF1 (TIM2_CH4) */
    GPIO_MODER(GPIOA_BASE) &= ~(3U<<6);
    GPIO_MODER(GPIOA_BASE) |=  (2U<<6);
    GPIO_OSPEEDR(GPIOA_BASE) |= (3U<<6);
    TIM2_ARR = 6;       /* Period=7 → MCLK=16MHz/7=2.286MHz */
    TIM2_CCR4 = 3;      /* 占空比 43% */
    TIM2_CCMR2 = (6U << 12) | (1U << 11);
    TIM2_CCER = (1U << 12);
    TIM2_CR1 = 1;
}

/* ==================== I2S2 Master TX (播放态打底) ==================== */
static void i2s2_master_tx_init(void) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    delay(100);

    /* GPIO AF: PA3=AF1(TIM2), PA8=AF5(SPI2_CK), PA9=AF3(SPI2_WS), PA10=AF5(SPI2_SD) */
    GPIO_AFRL(GPIOA_BASE) &= ~(0xFU<<12);
    GPIO_AFRL(GPIOA_BASE) |=  (1U<<12);
    GPIO_AFRH(GPIOA_BASE) &= ~((0xFU<<0)|(0xFU<<4)|(0xFU<<8));
    GPIO_AFRH(GPIOA_BASE) |=  ((5U<<0)|(3U<<4)|(5U<<8));

    uint32_t tmp = GPIO_MODER(GPIOA_BASE);
    tmp &= ~((3U<<6)|(3U<<16)|(3U<<18)|(3U<<20));
    tmp |=  ((2U<<6)|(2U<<16)|(2U<<18)|(2U<<20));
    GPIO_MODER(GPIOA_BASE) = tmp;
    GPIO_OSPEEDR(GPIOA_BASE) |= ((3U<<6)|(3U<<16)|(3U<<18)|(3U<<20));

    /* TIM2 已在 mclk_init 中启动 */

    /* I2S2 Master TX: I2SPR ODD=0, DIV=14 (P=28) + CHLEN=1(32位帧)
     *   → Fs = 16M/(64×28) = 8929Hz = MCLK/256, DAC 时钟同步(关键, 否则回放杂音) */
    SPI2_I2SCFGR = 0;
    SPI2_CR1 = 0;
    SPI2_CR2 = 0;
    SPI2_I2SPR = (0U << 9) | (0U << 8) | 14;  /* ODD=0, DIV=14 → Fs=8929Hz=MCLK/256 */
    SPI2_I2SCFGR = I2SCFGR_I2SMOD | (2U << I2SCFGR_I2SCFG_SHIFT) | I2SCFGR_CHLEN;  /* Master TX, CHLEN=1 */
    SPI2_CR1 = (1U << 6);  /* SPE=1 */
    SPI2_I2SCFGR |= I2SCFGR_I2SE;

    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~((3U<<18)|(3U<<20)))
                           | (2U<<18) | (2U<<20);
    __asm__ volatile("dsb");
}

/* 完全复位 SPI2 外设(关时钟→开时钟), 清除多次切换后的内部残留状态 */
static void i2s2_clear_flags(void) {
    RCC_APB1ENR1 &= ~RCC_APB1ENR1_SPI2EN;
    for (volatile int i = 0; i < 100; i++) __asm__ volatile("nop");
    RCC_APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    for (volatile int i = 0; i < 100; i++) __asm__ volatile("nop");
}

/* TX→RX: STM32 Slave RX (WM8960 Master)
 * CHLEN=0(16bit)+BCLKDIV=/8 → 32 SCLK/LRCLK 匹配 WM8960 WL=00; CKPOL=1 下降沿采样 */
static void i2s_switch_to_rx(void) {
    SPI2_I2SCFGR &= ~I2SCFGR_I2SE;
    for (volatile int i = 0; i < 500; i++) __asm__ volatile("nop");
    i2s2_clear_flags();
    SPI2_I2SCFGR = 0; SPI2_CR1 = 0; SPI2_CR2 = 0;
    SPI2_I2SPR = 0;
    SPI2_I2SCFGR = I2SCFGR_I2SMOD
                 | (1U << I2SCFGR_I2SCFG_SHIFT)   /* Slave RX */
                 | (1U << 3)                      /* CKPOL=1: 下降沿采样 */
                 | (1U << 12);                    /* ASTRTEN=1: 电平检测, 非边沿 */
    SPI2_CR1 = (1U << 6);
    SPI2_I2SCFGR |= I2SCFGR_I2SE;
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~((3U<<16)|(3U<<18)|(3U<<20)))
                           | (2U<<16) | (2U<<18) | (2U<<20);
    __asm__ volatile("dsb"); __asm__ volatile("isb");
}

/* RX→TX: STM32 Master TX (WM8960 Slave) */
static void i2s_switch_to_tx(void) {
    SPI2_I2SCFGR &= ~I2SCFGR_I2SE;
    for (volatile int i = 0; i < 500; i++) __asm__ volatile("nop");
    i2s2_clear_flags();   /* 清除上一轮 RX 的状态标志 */

    SPI2_I2SCFGR = 0;
    SPI2_CR1 = 0;
    SPI2_CR2 = 0;
    SPI2_I2SPR = (0U << 9) | (0U << 8) | 14;  /* ODD=0, DIV=14 → Fs=8929Hz=MCLK/256 */
    SPI2_I2SCFGR = I2SCFGR_I2SMOD
                 | (2U << I2SCFGR_I2SCFG_SHIFT)    /* Master TX */
                 | I2SCFGR_CHLEN;                  /* CHLEN=1 */
    SPI2_CR1 = (1U << 6);
    SPI2_I2SCFGR |= I2SCFGR_I2SE;

    GPIO_PUPDR(GPIOA_BASE) &= ~(3U << 20);
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~((3U<<18)|(3U<<20)))
                           | (2U<<18) | (2U<<20);
    __asm__ volatile("dsb");
}

/* ==================== WM8960 寄存器序列 ==================== */
/* 一次性配置: ADC+DAC 全上电, VMID=5kΩ, Slave 模式(播放态打底)
 * ★★★ R19=0x1FE 设定后永不再改! ADC 始终上电! ★★★ */
static uint8_t wm8960_init_regs(void) {
    uint8_t err = 0;
    err |= wm_write(0x0F, 0x000); delay(50000);  /* Reset */

    err |= wm_write(0x18, 0x008);   /* R24: TRIS=1(ADCDAT三态, bit3) — 必须在 VMID 前 */

    /* R19 PWR1: VMID(5kΩ)+VREF+AINL+AINR+ADCL+ADCR+MICB — ★永不再改 */
    err |= wm_write(0x19, 0x1FE);
    delay(300000);                  /* 等 VMID 电容稳定(~19ms) — 关键! */

    err |= wm_write(0x1A, 0x1F8);   /* R1A PWR2: DACL+DACR+LOUT1+ROUT1+SPKL+SPKR */
    err |= wm_write(0x2F, 0x03C);   /* R2F PWR3: LMIC+RMIC+LOMIX+ROMIX */
    err |= wm_write(0x04, 0x000);   /* R4 : SYSCLK=MCLK, ADCDIV=000 */
    err |= wm_write(0x08, 0x080);   /* R8 : DCLKDIV=÷3, BCLKDIV=0000(Slave 不生成 BCLK) */
    err |= wm_write(0x07, 0x002);   /* R7 : MS=0(Slave), WL=16bit, FORMAT=I2S */
    err |= wm_write(0x09, 0x040);   /* R9 : ALRCGPIO=1 */
    err |= wm_write(0x05, 0x000);   /* R5 : DACMU=0(出声), ADCHPD=0(HPF启用) */

    /* 输入路径(麦克风, 左声道) */
    err |= wm_write(0x00, 0x028);   /* LINVOL: +12.75dB — 降增益留 ADC 余量防削波 */
    err |= wm_write(0x01, 0x180);   /* RINVOL: MUTED */
    err |= wm_write(0x20, 0x128);   /* LINPATH: LMN1=1, LMICBOOST=+20dB, ★LMIC2B=1(否则录音无声) */
    err |= wm_write(0x21, 0x100);   /* RINPATH: RMN1=1 only(不接麦, 降噪) */
    err |= wm_write(0x15, 0x0C3);   /* LADC: 0dB */
    err |= wm_write(0x16, 0x180);   /* RADC: MUTED, VU=1 */

    /* 输出路径(DAC→喇叭) */
    err |= wm_write(0x0A, 0x0FF);   /* DAC L: 0dB, VU=0 */
    err |= wm_write(0x0B, 0x1FF);   /* DAC R: 0dB, VU=1 */
    err |= wm_write(0x22, 0x100);   /* LD2LO */
    err |= wm_write(0x25, 0x100);   /* RD2RO */
    err |= wm_write(0x28, 0x07F);   /* SPK L: +6dB(max), VU=0 */
    err |= wm_write(0x29, 0x17F);   /* SPK R: +6dB(max), VU=1 触发 */
    err |= wm_write(0x31, 0x0F7);   /* Class D ON */
    return err;
}

/* 切到录音: WM8960 Master, R18 TRIS=0(drive)+LRCM=1。R19 不动! */
static uint8_t wm8960_to_record(void) {
    uint8_t err = 0;
    err |= wm_write(0x05, 0x000);   /* ADCHPD=0: HPF ENABLED, 去 DC 保护 ADC */
    err |= wm_write(0x2F, 0x030);   /* PWR3: 只开 LMIC+RMIC */
    err |= wm_write(0x1A, 0x000);   /* PWR2: 关输出 */
    err |= wm_write(0x18, 0x004);   /* R24: TRIS=0(drive)+LRCM=1 */
    err |= wm_write(0x09, 0x000);   /* ALRCGPIO=0 */
    err |= wm_write(0x08, 0x087);   /* DCLKDIV=÷3, BCLKDIV=0111(/8) → LRCLK=8929Hz */
    err |= wm_write(0x07, 0x042);   /* MS=1(WM8960 当主) */
    return err;
}

/* 切回播放: WM8960 Slave + DAC 出声。R19 不动! */
static uint8_t wm8960_to_playback(void) {
    uint8_t err = 0;
    err |= wm_write(0x18, 0x008);   /* R24: TRIS=1(三态)+LRCM=0 */
    err |= wm_write(0x07, 0x002);   /* MS=0(Slave) */
    err |= wm_write(0x09, 0x040);   /* ALRCGPIO=1 恢复 */
    err |= wm_write(0x08, 0x080);   /* BCLKDIV=0(Slave)+DCLKDIV=÷3 */
    err |= wm_write(0x1A, 0x1F8);   /* PWR2: DAC+SPK */
    err |= wm_write(0x2F, 0x00C);   /* PWR3: 只开 LOMIX+ROMIX, 关输入 PGA → 断噪声源 */
    err |= wm_write(0x05, 0x000);   /* DACMU=0 */
    err |= wm_write(0x0A, 0x0FF);   /* DAC L */
    err |= wm_write(0x0B, 0x1FF);   /* DAC R */
    err |= wm_write(0x22, 0x100);   /* LD2LO */
    err |= wm_write(0x25, 0x100);   /* RD2RO */
    err |= wm_write(0x28, 0x07F);   /* SPK L: +6dB(max), VU=0 */
    err |= wm_write(0x29, 0x17F);   /* SPK R: +6dB(max), VU=1 触发 */
    err |= wm_write(0x31, 0x0F7);   /* Class D ON */
    return err;
}

/* ==================== 公共 API ==================== */
uint8_t audio_init(void) {
    /* I2S 时钟源=SYSCLK(SPI2S2SEL=10), ADCSEL=01(HSI16) */
    RCC_CCIPR = 0x10000200;
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;
    delay(100);

    s_rx_lr = 0; s_rx_discard = 0; s_tx_lr = 0;
    s_rx_err = 0; s_rx_first_sr = 0; s_tx_udr = 0;

    mclk_init();            /* TIM2 MCLK 2.286MHz on PA3 */
    i2s2_master_tx_init();  /* I2S2 Master TX (播放态打底) */
    i2c1_init();            /* I2C1 on PB6/PB7 */
    return wm8960_init_regs();
}

uint8_t audio_enter_record(void) {
    uint8_t err = wm8960_to_record();
    delay(50000);           /* WM8960 当主, BCLK 需更长稳定时间 */
    i2s_switch_to_rx();
    delay(5000);
    s_rx_lr = 0;
    s_rx_discard = 100;     /* 上电预热丢弃 */
    s_rx_err = 0;
    s_rx_first_sr = 0;
    return err;
}

uint8_t audio_enter_playback(void) {
    uint8_t err = wm8960_to_playback();
    delay(10000);           /* WM8960 寄存器稳定(~2.5ms) */
    i2s_switch_to_tx();
    delay(5000);
    s_tx_lr = 0;
    s_tx_udr = 0;
    return err;
}

int audio_read_sample(int16_t *out) {
    uint32_t sr = SPI2_SR;
    if (!(sr & SPI_SR_RXNE)) return 0;
    int16_t s = (int16_t)(uint16_t)SPI2_DR;
    if (s_rx_first_sr == 0) s_rx_first_sr = sr;
    /* 错误帧: FRE(bit4)/OVR(bit6)/UDR(bit3) */
    if (sr & ((1U<<4)|(1U<<6)|(1U<<3))) s_rx_err++;
    if (s_rx_discard > 0) { s_rx_discard--; return 0; }
    uint8_t was_left = (s_rx_lr == 0);
    s_rx_lr ^= 1;
    if (was_left) { *out = s; return 1; }  /* 只取左声道 */
    return 0;                               /* 右声道丢弃 */
}

int audio_write_sample(int16_t s) {
    uint32_t sr = SPI2_SR;
    if (sr & (1U << 3)) s_tx_udr++;         /* UDR 欠载(bit3) */
    if (!(sr & SPI_SR_TXE)) return 0;
    SPI2_DR = (uint16_t)s;
    uint8_t was_right = s_tx_lr;            /* 同一单声道样本发左、右两次 */
    s_tx_lr ^= 1;
    return was_right ? 1 : 0;               /* 发完右声道才允许前进 */
}

uint32_t audio_rx_error_frames(void) { return s_rx_err; }
uint32_t audio_rx_first_sr(void)     { return s_rx_first_sr; }
uint32_t audio_tx_underruns(void)    { return s_tx_udr; }
uint32_t audio_dbg_i2scfgr(void)     { return SPI2_I2SCFGR; }
