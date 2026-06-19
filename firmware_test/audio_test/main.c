/* MPTT v0.1 Audio Test Firmware - I2S Master TX + WM8960 Slave
 * Target: STM32WLE5CBU6 (E77-400M22S)
 * 
 * Strategy: MCU I2S2 Master TX generates BCLK/LRCLK/SD
 *           TIM2_CH4 PWM on PA3 provides MCLK to WM8960
 *           WM8960 in Slave mode, DAC → Left Speaker (BTL)
 *
 * Clock: HSI16=16MHz, I2SDIV=4, MCKOE=1, CHLEN=1 (32-bit frame)
 *   Fs=7812.5Hz, BCLK=500kHz, MCLK=2MHz (TIM2: 16M/8=2M)
 *
 * Pins:
 *   PA3  = AF1 (TIM2_CH4) → WM8960 MCLK (pin11)
 *   PA8  = AF5 (I2S2_CK)  → WM8960 BCLK (pin12)
 *   PA9  = AF5 (I2S2_WS)  → WM8960 DACLRC (pin13)
 *   PA10 = AF5 (I2S2_SD)  → WM8960 DACDAT (pin14)
 *   PB6  = AF4 (I2C1_SCL) → WM8960 SCLK (pin17)
 *   PB7  = AF4 (I2C1_SDA) → WM8960 SDIN (pin18)
 *
 * Test output: 977Hz sine wave on left speaker
 * Results at SRAM 0x20000100 for pyocd readback
 */

#include <stdint.h>

/* ============ Result SRAM ============ */
#define RESULT ((volatile uint8_t *)0x20000100)
#define R_BOOT      0   /* 0xAA = booted */
#define R_CLK       1   /* 0x01 = HSI16 ready */
#define R_I2C       2   /* 0x1A = WM8960 found */
#define R_WM_CFG    3   /* 0x01 = WM8960 configured */
#define R_TIM2      4   /* 0x01 = MCLK running */
#define R_I2S       5   /* 0x01 = I2S enabled, TXE fired */
#define R_DONE      6   /* 0x55 = playing audio */
#define R_LOOP      7   /* incrementing = alive */
#define R_SAMPLES   8   /* uint32: 实际I2S样本发送计数 */
#define R_I2C_ERR   12  /* 0=全部成功, >0=失败的寄存器序号 */

/* ============ RCC ============ */
#define RCC_BASE        0x58000000
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1    (*(volatile uint32_t *)(RCC_BASE + 0x58))
#define RCC_CCIPR       (*(volatile uint32_t *)(RCC_BASE + 0x88))

/* ============ GPIO ============ */
#define GPIOA_BASE      0x48000000
#define GPIOB_BASE      0x48000400
#define GPIO_MODER(b)   (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_OTYPER(b)  (*(volatile uint32_t *)((b) + 0x04))
#define GPIO_OSPEEDR(b) (*(volatile uint32_t *)((b) + 0x08))
#define GPIO_PUPDR(b)   (*(volatile uint32_t *)((b) + 0x0C))
#define GPIO_IDR(b)     (*(volatile uint32_t *)((b) + 0x10))
#define GPIO_AFRL(b)    (*(volatile uint32_t *)((b) + 0x20))
#define GPIO_AFRH(b)    (*(volatile uint32_t *)((b) + 0x24))

/* ============ TIM2 ============ */
#define TIM2_BASE       0x40000000
#define TIM2_CR1        (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_CCMR2      (*(volatile uint32_t *)(TIM2_BASE + 0x1C))
#define TIM2_CCER       (*(volatile uint32_t *)(TIM2_BASE + 0x20))
#define TIM2_ARR        (*(volatile uint32_t *)(TIM2_BASE + 0x2C))
#define TIM2_CCR4       (*(volatile uint32_t *)(TIM2_BASE + 0x40))

/* ============ I2C1 ============ */
#define I2C1_BASE       0x40005400
#define I2C1_CR1        (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2        (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_TIMINGR    (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_ISR        (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_ICR        (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TXDR       (*(volatile uint32_t *)(I2C1_BASE + 0x28))

/* ============ SPI2/I2S2 ============ */
#define SPI2_BASE       0x40003800
#define SPI2_CR1        (*(volatile uint32_t *)(SPI2_BASE + 0x00))
#define SPI2_CR2        (*(volatile uint32_t *)(SPI2_BASE + 0x04))
#define SPI2_SR         (*(volatile uint32_t *)(SPI2_BASE + 0x08))
#define SPI2_DR         (*(volatile uint32_t *)(SPI2_BASE + 0x0C))
#define SPI2_I2SCFGR    (*(volatile uint32_t *)(SPI2_BASE + 0x1C))
#define SPI2_I2SPR      (*(volatile uint32_t *)(SPI2_BASE + 0x20))

/* ============ Helpers ============ */
static void delay(volatile uint32_t n) { while(n--); }

/* 977Hz sine wave, 8 samples/cycle at Fs=7812.5Hz
 * Values: sin(n*45°) * 32767, 16-bit signed */
static const int16_t sine_table[8] = {
    0, 23170, 32767, 23170, 0, -23170, -32767, -23170
};

/* ============ Clock Init ============ */
static void clock_init(void) {
    /* Enable HSI16 */
    RCC_CR |= (1 << 8);  /* HSION */
    uint32_t timeout = 100000;
    while (!(RCC_CR & (1 << 10)) && --timeout);  /* Wait HSIRDY */
    
    /* Switch SYSCLK to HSI16 */
    RCC_CFGR = (RCC_CFGR & ~0x03) | 0x01;  /* SW = 01 (HSI16) */
    timeout = 100000;
    while (((RCC_CFGR >> 2) & 0x03) != 0x01 && --timeout);  /* Wait SWS=HSI16 */
    
    /* SPI2/I2S2 clock source = HSI16 (bits[9:8]=10) */
    RCC_CCIPR = (RCC_CCIPR & ~(3 << 8)) | (2 << 8);
    
    /* Enable peripheral clocks */
    RCC_AHB2ENR |= (1 << 0) | (1 << 1);   /* GPIOAEN + GPIOBEN */
    RCC_APB1ENR1 |= (1 << 0)              /* TIM2EN */
                  | (1 << 14)              /* SPI2EN */
                  | (1 << 21);             /* I2C1EN */
    delay(100);
    
    RESULT[R_CLK] = 0x01;
}

/* ============ GPIO Init ============ */
static void gpio_init(void) {
    /* --- PA3: AF1 (TIM2_CH4) for MCLK --- */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3 << 6)) | (2 << 6);
    GPIO_OSPEEDR(GPIOA_BASE) |= (3 << 6);  /* Very High speed */
    GPIO_AFRL(GPIOA_BASE) = (GPIO_AFRL(GPIOA_BASE) & ~(0xF << 12)) | (1 << 12);  /* AF1 */
    
    /* --- PA8: AF5 (I2S2_CK) for BCLK --- */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3 << 16)) | (2 << 16);
    GPIO_OSPEEDR(GPIOA_BASE) |= (3 << 16);
    GPIO_AFRH(GPIOA_BASE) = (GPIO_AFRH(GPIOA_BASE) & ~(0xF << 0)) | (5 << 0);   /* AF5 */
    
    /* --- PA9: AF5 (I2S2_WS) for LRCLK --- */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3 << 18)) | (2 << 18);
    GPIO_OSPEEDR(GPIOA_BASE) |= (3 << 18);
    GPIO_AFRH(GPIOA_BASE) = (GPIO_AFRH(GPIOA_BASE) & ~(0xF << 4)) | (5 << 4);   /* AF5 */
    
    /* --- PA10: AF5 (I2S2_SD) for Data --- */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3 << 20)) | (2 << 20);
    GPIO_OSPEEDR(GPIOA_BASE) |= (3 << 20);
    GPIO_AFRH(GPIOA_BASE) = (GPIO_AFRH(GPIOA_BASE) & ~(0xF << 8)) | (5 << 8);   /* AF5 */
    
    /* --- PB6: AF4 (I2C1_SCL), PB7: AF4 (I2C1_SDA) --- */
    GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~((3<<12)|(3<<14))) | ((2<<12)|(2<<14));
    GPIO_OTYPER(GPIOB_BASE) |= (1 << 6) | (1 << 7);  /* Open-drain */
    GPIO_PUPDR(GPIOB_BASE) = (GPIO_PUPDR(GPIOB_BASE) & ~((3<<12)|(3<<14))) | ((1<<12)|(1<<14));
    GPIO_AFRL(GPIOB_BASE) = (GPIO_AFRL(GPIOB_BASE) & ~((0xF<<24)|(0xF<<28))) | ((4<<24)|(4<<28));
}

/* ============ TIM2 CH4 PWM (MCLK 2MHz) ============ */
static void mclk_init(void) {
    TIM2_ARR  = 7;          /* 16MHz / (7+1) = 2MHz */
    TIM2_CCR4 = 4;          /* 50% duty */
    TIM2_CCMR2 = (6 << 12) /* OC4M = PWM mode 1 */
               | (1 << 11); /* OC4PE = preload enable */
    TIM2_CCER = (1 << 12);  /* CC4E = enable CH4 output */
    TIM2_CR1  = 1;          /* CEN = start timer */
    
    RESULT[R_TIM2] = 0x01;
}

/* ============ I2C1 ============ */
static void i2c_init(void) {
    I2C1_CR1 = 0;
    I2C1_TIMINGR = 0x00503D5B;  /* 100kHz @ 16MHz HSI */
    I2C1_CR1 = 1;
}

/* I2C write 2 bytes (WM8960: 7-bit reg addr + 9-bit data packed in 2 bytes) */
static int i2c_write2(uint8_t dev_addr, uint8_t byte1, uint8_t byte2) {
    I2C1_ICR = 0x3F38;
    /* NBYTES=2, START, AUTOEND */
    I2C1_CR2 = (dev_addr << 1) | (2 << 16) | (1 << 25) | (1 << 13);
    
    /* Wait TXIS (ready to send byte 1) */
    uint32_t timeout = 100000;
    while (!(I2C1_ISR & (1 << 1)) && --timeout) {
        if (I2C1_ISR & (1 << 4)) { /* NACK */
            I2C1_ICR = (1 << 4);
            return -1;
        }
    }
    if (!timeout) return -2;
    I2C1_TXDR = byte1;
    
    /* Wait TXIS (ready to send byte 2) */
    timeout = 100000;
    while (!(I2C1_ISR & (1 << 1)) && --timeout) {
        if (I2C1_ISR & (1 << 4)) {
            I2C1_ICR = (1 << 4);
            return -1;
        }
    }
    if (!timeout) return -2;
    I2C1_TXDR = byte2;
    
    /* Wait STOPF */
    timeout = 100000;
    while (!(I2C1_ISR & (1 << 5)) && --timeout);
    I2C1_ICR = (1 << 5);
    
    return 0;
}

/* WM8960 I2C write: 7-bit register address, 9-bit data
 * Byte 1: [R6:R0][D8]  Byte 2: [D7:D0] */
static int wm_write(uint8_t reg, uint16_t data) {
    uint8_t b1 = (reg << 1) | ((data >> 8) & 0x01);
    uint8_t b2 = data & 0xFF;
    return i2c_write2(0x1A, b1, b2);
}

/* I2C scan (0-byte write, check ACK) */
static int i2c_scan(uint8_t addr) {
    I2C1_ICR = 0x3F38;
    I2C1_CR2 = (addr << 1) | (0 << 16) | (1 << 25) | (1 << 13);
    uint32_t timeout = 100000;
    while (timeout--) {
        uint32_t isr = I2C1_ISR;
        if (isr & (1 << 5)) { I2C1_ICR = (1 << 5); return 1; }
        if (isr & (1 << 4)) { I2C1_ICR = (1 << 4);
            while(!(I2C1_ISR & (1<<5))); I2C1_ICR=(1<<5); return 0; }
    }
    return 0;
}

/* ============ WM8960 Configuration (Slave, DAC→Speaker) ============ */
static void wm8960_init(void) {
    /* === Aligned with MPTT driver (wm8960.c) + kernel-verified regs.h === */
    int step = 0;
    
    /* 1. Reset */
    if (wm_write(0x0F, 0x000)) { RESULT[R_I2C_ERR]=++step; return; }
    delay(800000);           /* 50ms */
    
    /* 2. ADCLRC→GPIO BEFORE VMID */
    if (wm_write(0x09, 0x040)) { RESULT[R_I2C_ERR]=++step; return; }
    /* TRIS=1: tristate ADCDAT (datasheet p53 R24 bit3).
     * MPTT PA10 shared ADCDAT/DACDAT — ADCDAT drives LOW even with ADC off,
     * overpowering I2S AF driver. TRIS=1 fixes the pull-down. */
    if (wm_write(0x18, 0x008)) { RESULT[R_I2C_ERR]=++step; return; }
    
    /* 3. VMID 50k + VREF (no DIGENB — it's active-low!) */
    if (wm_write(0x19, 0x0C0)) { RESULT[R_I2C_ERR]=++step; return; }
    delay(9600000);
    
    /* 4. VMID 500k + VREF */
    if (wm_write(0x19, 0x140)) { RESULT[R_I2C_ERR]=++step; return; }
    
    /* 5. DACL+DACR+LOUT1+ROUT1+SPKL+SPKR */
    if (wm_write(0x1A, 0x1F8)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 6. LOMIX+ROMIX+LMIC+RMIC */
    if (wm_write(0x2F, 0x03C)) { RESULT[R_I2C_ERR]=++step; return; }
    delay(3200000);
    
    /* 7. SYSCLK=MCLK */
    if (wm_write(0x04, 0x000)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 8. DCLKDIV=÷2 */
    if (wm_write(0x08, 0x040)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 9. I2S format: 16-bit, I2S, Slave */
    if (wm_write(0x07, 0x002)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 10. DACMU=0 */
    if (wm_write(0x05, 0x000)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 11. DAC vol — VU latch: L VU=0, R VU=1 触发 */
    if (wm_write(0x0A, 0x0FF)) { RESULT[R_I2C_ERR]=++step; return; } /* VU=0 */
    if (wm_write(0x0B, 0x1FF)) { RESULT[R_I2C_ERR]=++step; return; } /* VU=1 触发 */
    /* 12. LD2LO / RD2RO */
    if (wm_write(0x22, 0x100)) { RESULT[R_I2C_ERR]=++step; return; }
    if (wm_write(0x25, 0x100)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 13. SPK vol — VU latch: L VU=0, R VU=1 触发 */
    if (wm_write(0x28, 0x0FF)) { RESULT[R_I2C_ERR]=++step; return; } /* VU=0 */
    if (wm_write(0x29, 0x1FF)) { RESULT[R_I2C_ERR]=++step; return; } /* VU=1 触发 */
    /* 14. Anti-pop */
    if (wm_write(0x1C, 0x00C)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 15. Class D */
    if (wm_write(0x31, 0x0F7)) { RESULT[R_I2C_ERR]=++step; return; }
    /* 16. Class D boost */
    if (wm_write(0x33, 0x02D)) { RESULT[R_I2C_ERR]=++step; return; }
    
    RESULT[R_WM_CFG] = 0x01;
    RESULT[R_I2C_ERR] = 0;   /* All OK */
}

/* ============ I2S2 Master TX Init ============ */
static void i2s_init(void) {
    /* Ensure I2S disabled first */
    SPI2_I2SCFGR = 0;
    
    /* I2S Prescaler: MCKOE=1, ODD=0, I2SDIV=4
     * Fs = 16MHz / (256 * 2*4) = 7812.5 Hz */
    SPI2_I2SPR = (1 << 9)   /* MCKOE = 1 (affects internal divider) */
               | (0 << 8)   /* ODD = 0 */
               | 4;         /* I2SDIV = 4 */
    
    /* I2S Config: I2SMOD=1, Master TX, Philips, 16-bit data, 32-bit frame */
    SPI2_I2SCFGR = (1 << 11)  /* I2SMOD = 1 (I2S mode, not SPI) */
                 | (2 << 8)   /* I2SCFG = 10 (Master TX) */
                 | (0 << 4)   /* I2SSTD = 00 (Philips) */
                 | (0 << 1)   /* DATLEN = 00 (16-bit) */
                 | (1 << 0);  /* CHLEN = 1 (32-bit frame width) */
    
    /* Enable I2S */
    SPI2_I2SCFGR |= (1 << 10);  /* I2SE = 1 */
    
    /* Fix MODER: I2S hardware corrupts PA9+PA10 to Analog(11) */
    uint32_t moder = GPIO_MODER(GPIOA_BASE);
    moder = (moder & ~((3<<18)|(3<<20))) | (2<<18) | (2<<20);  /* PA9+PA10 = AF */
    GPIO_MODER(GPIOA_BASE) = moder;
    
    /* Verify TXE fires */
    uint32_t timeout = 100000;
    while (!(SPI2_SR & (1 << 1)) && --timeout);
    
    if (SPI2_SR & (1 << 1)) {
        RESULT[R_I2S] = 0x01;  /* TXE works! */
    } else {
        RESULT[R_I2S] = 0xEE;  /* TXE failed */
    }
}

/* ============ Main ============ */
int main(void) {
    RESULT[R_BOOT] = 0xAA;
    
    /* 1. Clock setup (HSI16 + peripheral clocks) */
    clock_init();
    
    /* 2. GPIO alternate functions */
    gpio_init();
    
    /* 3. Start MCLK (TIM2 PWM, must be before WM8960 init) */
    mclk_init();
    
    /* 4. I2C init */
    i2c_init();
    
    /* 5. Verify WM8960 present */
    if (i2c_scan(0x1A)) {
        RESULT[R_I2C] = 0x1A;
    } else {
        RESULT[R_I2C] = 0xFF;
        while(1);  /* Halt: no codec */
    }
    
    /* 6. Configure WM8960 (Slave, DAC→Speaker) */
    wm8960_init();
    
    /* 7. Init I2S2 Master TX */
    i2s_init();
    
    /* 8. Play sine wave */
    RESULT[R_DONE] = 0x55;
    
    uint8_t idx = 0;
    uint8_t loop = 0;
    volatile uint32_t *samples = (volatile uint32_t *)&RESULT[R_SAMPLES];
    *samples = 0;
    while (1) {
        /* Wait for TXE (transmit buffer empty) */
        if (SPI2_SR & (1 << 1)) {
            /* Write 16-bit sample (alternates L/R automatically) */
            SPI2_DR = (uint16_t)sine_table[idx];
            idx = (idx + 1) & 0x07;  /* Wrap at 8 samples */
            *samples = *samples + 1; /* 实际样本计数 */
        }
        RESULT[R_LOOP] = loop++;
    }
}
