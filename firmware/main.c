/*
 * MPTT Speaker Test — I2S 方波 → WM8960 DAC → Class D → 扬声器
 *
 * 硬件连接 (E77-400M22S 模块):
 *   PA3  (Pin 10) → WM8960 MCLK    (AF1: TIM2_CH4, 2.286MHz)
 *   PA8  (Pin 16) → WM8960 BCLK    (AF5: I2S2_CK)
 *   PA9  (Pin 18) → WM8960 LRCLK   (AF3: I2S2_WS)
 *   PA10 (Pin 21) → WM8960 DACDAT  (AF5: I2S2_SD)
 *   PB6  (Pin 4)  → WM8960 SCLK    (AF4: I2C1_SCL)
 *   PB7  (Pin 5)  → WM8960 SDIN    (AF4: I2C1_SDA)
 *
 * SRAM 检查点 (pyocd read8 0x20000100 8):
 *   [0]=AA boot  [1]=WM8960 err  [2]=I2S enable  [6]=count_lo  [7]=I2C_ISR
 *
 * 已修复的 7 个 BUG:
 *   1. CHLEN=1 (32-bit帧, WM8960 Slave要求)
 *   2. SPI2S2SEL=10(SYSCLK), 01(HSI16直连)不工作
 *   3. SPI2_CR1 SPE=1 (STM32WL 即使I2SMOD=1也需要)
 *   4. PA9+PA10 MODER修复 (I2SE=1后硅片bug篡改为Analog)
 *   5. TRIS=1在VMID之前 (ADCDAT三态)
 *   6. DAC Vol VU Latch: L先VU=0, R后VU=1
 *   7. SPK Vol VU Latch: 同上
 */
#include <stdint.h>
#include "stm32wle5xx.h"

#define RESULT_BASE  ((volatile uint8_t *)0x20000100)

static void delay(volatile uint32_t n) { while(n--) __asm__ volatile("nop"); }

/* ========== I2C1 ========== */
static void i2c1_init(void) {
    RCC_APB1ENR1 &= ~RCC_APB1ENR1_I2C1EN;
    delay(1000);
    RCC_APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    delay(100);
    I2C1_CR1 = 0;

    /* I2C 总线恢复: SCL toggle 9 次 (I2S活动时必需) */
    GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~(3U<<12)) | (1U<<12);
    GPIO_OTYPER(GPIOB_BASE) &= ~(1U<<6);
    GPIO_PUPDR(GPIOB_BASE) &= ~(3U<<12);
    for (int i = 0; i < 9; i++) {
        GPIO_BSRR(GPIOB_BASE) = (1U << 22);  /* reset PB6 */
        delay(100);
        GPIO_BSRR(GPIOB_BASE) = (1U << 6);   /* set PB6 */
        delay(100);
    }

    /* PB6/PB7 = AF4 (I2C1) */
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

/* ========== WM8960 I2C 写 (地址 0x1A) ========== */
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

/* ========== I2S2 + TIM2 MCLK 初始化 ==========
 *
 * STM32WL I2S 三大铁律:
 *   1. SPI2S2SEL 必须=10(SYSCLK), 01(HSI16直连)不工作
 *   2. SPI2_CR1 SPE=1 必须设 (即使 I2SMOD=1)
 *   3. MCKOE=0 (PA3 是 TIM2 非 I2S_MCK, MCKOE=1 会导致 TXE 卡死)
 */
static void i2s2_init(void) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    RCC_APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    delay(100);

    /* GPIO AF 配置 */
    GPIO_AFRL(GPIOA_BASE) &= ~(0xFU<<12);
    GPIO_AFRL(GPIOA_BASE) |=  (1U<<12);              /* PA3 = AF1 (TIM2_CH4) */
    GPIO_AFRH(GPIOA_BASE) &= ~((0xFU<<0)|(0xFU<<4)|(0xFU<<8));
    GPIO_AFRH(GPIOA_BASE) |=  ((5U<<0)|(3U<<4)|(5U<<8)); /* PA8=AF5, PA9=AF3, PA10=AF5 */

    /* PA3/PA8/PA9/PA10 = AF 模式, Very High 速度 */
    uint32_t tmp = GPIO_MODER(GPIOA_BASE);
    tmp &= ~((3U<<6)|(3U<<16)|(3U<<18)|(3U<<20));
    tmp |=  ((2U<<6)|(2U<<16)|(2U<<18)|(2U<<20));
    GPIO_MODER(GPIOA_BASE) = tmp;
    GPIO_OSPEEDR(GPIOA_BASE) |= ((3U<<6)|(3U<<16)|(3U<<18)|(3U<<20));

    /* TIM2 CH4 PWM: MCLK = 16MHz/7 ≈ 2.286MHz → DCLKDIV=÷3 → 762kHz */
    TIM2_ARR = 6;
    TIM2_CCR4 = 3;
    TIM2_CCMR2 = (6U << 12) | (1U << 11);  /* OC4M=PWM1, OC4PE */
    TIM2_CCER = (1U << 12);                 /* CC4E */
    TIM2_CR1 = 1;                           /* CEN */

    /* I2S2: Master TX, 16-bit, I2S Philips, 32-bit帧(CHLEN=1)
     * MCKOE=0, ODD=1, I2SDIV=15 → P=31
     * BCLK=16MHz/31≈516kHz, Fs≈8064Hz */
    SPI2_I2SCFGR = 0;
    SPI2_CR1 = 0;
    SPI2_CR2 = 0;
    SPI2_I2SPR = (0U << 9) | (1U << 8) | 15;
    SPI2_I2SCFGR = I2SCFGR_I2SMOD                      /* I2S 模式 */
                 | (2U << I2SCFGR_I2SCFG_SHIFT)         /* Master TX */
                 | I2SCFGR_CHLEN;                       /* 32-bit 帧 */
    SPI2_CR1 = (1U << 6);                               /* SPE=1 */
    SPI2_I2SCFGR |= I2SCFGR_I2SE;                       /* I2SE=1 */

    /* I2SE=1 后硅片 bug: PA9+PA10 被篡改为 Analog(11), 必须修复 */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~((3U<<18)|(3U<<20)))
                           | (2U<<18) | (2U<<20);
    __asm__ volatile("dsb");
}

/* ========== WM8960 初始化 ==========
 *
 * 寄存器配置 (datasheet Rev4.1 逐位核对):
 *   R25(0x19)=0x0C0  VMIDSEL=01 + VREF + DIGENB=0(使能)
 *   R26(0x1A)=0x1F8  DACL+DACR+LOUT1+ROUT1+SPKL+SPKR
 *   R47(0x2F)=0x03C  LMIC+RMIC+LOMIX+ROMIX
 *   R7 (0x07)=0x002  FORMAT=10(I2S) + WL=00(16bit) + MS=0(Slave)
 *   R8 (0x08)=0x080  DCLKDIV=010(÷3) → 762kHz ∈ [700,800]
 *   R5 (0x05)=0x000  DACMU=0 (解除静音)
 *   R24(0x18)=0x008  TRIS=1 (三态ADCDAT, 必须在VMID之前!)
 */
static uint8_t wm8960_init(void) {
    uint8_t err = 0;
    err |= wm_write(0x0F, 0x000); delay(50000);    /* Reset */
    err |= wm_write(0x18, 0x008);                   /* TRIS=1: 在VMID之前! */
    err |= wm_write(0x19, 0x0C0);                   /* Pwr1: VMIDSEL=01, VREF=1, DIGENB=0 */
    err |= wm_write(0x1A, 0x1F8);                   /* Pwr2: DACL+DACR+LOUT1+ROUT1+SPKL+SPKR */
    err |= wm_write(0x2F, 0x03C);                   /* Pwr3: LMIC+RMIC+LOMIX+ROMIX */
    delay(200000);
    err |= wm_write(0x07, 0x002);                   /* I2S, 16bit, Slave */
    err |= wm_write(0x09, 0x040);                   /* ALRCGPIO=1 */
    err |= wm_write(0x04, 0x000);                   /* SYSCLK=MCLK 直通 */
    err |= wm_write(0x08, 0x080);                   /* DCLKDIV=÷3 → 762kHz */
    err |= wm_write(0x05, 0x000);                   /* DACMU=0 (unmute) */
    /* VU Latch: L 先 VU=0 加载, R 后 VU=1 触发两声道 */
    err |= wm_write(0x0A, 0x0FF);                   /* DAC Vol L: VU=0 */
    err |= wm_write(0x0B, 0x1FF);                   /* DAC Vol R: VU=1 (触发) */
    err |= wm_write(0x22, 0x100);                   /* LD2LO=1 (DAC→左输出混音) */
    err |= wm_write(0x25, 0x100);                   /* RD2RO=1 */
    err |= wm_write(0x28, 0x079);                   /* SPK Vol L: VU=0, ZC=0, VOL=0x79(0dB) */
    err |= wm_write(0x29, 0x179);                   /* SPK Vol R: VU=1 (触发) */
    err |= wm_write(0x31, 0x0F7);                   /* Class D Enable */
    return err;
}

/* ========== Main ========== */
int main(void) {
    /* HSI16 → SYSCLK */
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY));
    RCC_CFGR = 0x00000001;
    while ((RCC_CFGR & 0x0C) != 0x04);

    /* SPI2S2SEL=10(SYSCLK), ADCSEL=01(HSI16)
     * 铁律: SPI2S2SEL=01(HSI16直连)不工作! 必须用10 */
    RCC_CCIPR = 0x10000200;

    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;
    delay(100);

    RESULT_BASE[0] = 0xAA;

    i2s2_init();
    i2c1_init();

    uint8_t err = wm8960_init();
    RESULT_BASE[1] = err ? 0xEE : 0x00;
    RESULT_BASE[7] = (uint8_t)(I2C1_ISR & 0xFF);
    RESULT_BASE[2] = (SPI2_I2SCFGR & I2SCFGR_I2SE) ? 0x55 : 0x00;

    /* 方波播放: 每8个样本翻转一次 → 约1kHz */
    int16_t sample_hi = 0x7FFF;
    int16_t sample_lo = 0x8000;
    uint8_t toggle = 0;
    uint32_t count = 0;
    uint32_t phase = 0;

    while (1) {
        if (SPI2_SR & SPI_SR_TXE) {
            SPI2_DR = (uint16_t)(toggle ? sample_lo : sample_hi);
            count++;
            phase++;
            if (phase >= 8) {
                toggle ^= 1;
                phase = 0;
            }
        }
        RESULT_BASE[6] = (uint8_t)(count);
    }
}
