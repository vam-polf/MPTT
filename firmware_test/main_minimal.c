/* Speaker Test: I2S square wave → WM8960 DAC → Class D Amp → Speaker */
#include <stdint.h>

#define RESULT_BASE ((volatile uint8_t *)0x20000100)
#define RCC_BASE      0x58000000
#define RCC_CR        (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_AHB2ENR   (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1  (*(volatile uint32_t *)(RCC_BASE + 0x58))

#define GPIOA_BASE    0x48000000
#define GPIOB_BASE    0x48000400
#define GPIO_MODER(b)  (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_OTYPER(b) (*(volatile uint32_t *)((b) + 0x04))
#define GPIO_OSPEEDR(b)(*(volatile uint32_t *)((b) + 0x08))
#define GPIO_PUPDR(b)  (*(volatile uint32_t *)((b) + 0x0C))
#define GPIO_IDR(b)    (*(volatile uint32_t *)((b) + 0x10))
#define GPIO_ODR(b)    (*(volatile uint32_t *)((b) + 0x14))
#define GPIO_BSRR(b)   (*(volatile uint32_t *)((b) + 0x18))
#define GPIO_AFRL(b)   (*(volatile uint32_t *)((b) + 0x20))
#define GPIO_AFRH(b)   (*(volatile uint32_t *)((b) + 0x24))

#define I2C1_BASE     0x40005400
#define I2C1_CR1      (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2      (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_TIMINGR  (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_ISR      (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_ICR      (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TXDR     (*(volatile uint32_t *)(I2C1_BASE + 0x28))

#define SPI2_BASE     0x40003800
#define SPI2_CR1      (*(volatile uint32_t *)(SPI2_BASE + 0x00))
#define SPI2_CR2      (*(volatile uint32_t *)(SPI2_BASE + 0x04))
#define SPI2_SR       (*(volatile uint32_t *)(SPI2_BASE + 0x08))
#define SPI2_DR       (*(volatile uint32_t *)(SPI2_BASE + 0x0C))
#define SPI2_I2SCFGR  (*(volatile uint32_t *)(SPI2_BASE + 0x1C))
#define SPI2_I2SPR    (*(volatile uint32_t *)(SPI2_BASE + 0x20))

static void delay(volatile uint32_t n) { while(n--); }

static void i2c1_init(void) {
    /* Force I2C peripheral off first */
    RCC_APB1ENR1 &= ~(1 << 21); /* I2C1EN = 0 */
    delay(1000);
    RCC_APB1ENR1 |= (1 << 21);  /* I2C1EN = 1 */
    delay(100);
    I2C1_CR1 = 0;  /* PE=0 */

    /* I2C bus recovery: manually toggle SCL to unstick SDA */
    GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~(3<<12)) | (1<<12); /* PB6 = output */
    GPIO_OTYPER(GPIOB_BASE) &= ~(1<<6); /* PB6 push-pull */
    GPIO_PUPDR(GPIOB_BASE) &= ~(3<<12);
    for (int i = 0; i < 9; i++) {
        GPIO_BSRR(GPIOB_BASE) = (1 << 22); /* PB6 LOW */
        delay(100);
        GPIO_BSRR(GPIOB_BASE) = (1 << 6);  /* PB6 HIGH */
        delay(100);
    }
    /* Check if SDA is released */
    GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~(3<<14)); /* PB7 = input */
    delay(100);
    volatile uint8_t *dbg = (volatile uint8_t *)0x2000010A;
    *dbg = (GPIO_IDR(GPIOB_BASE) >> 7) & 1;
    /* Reconfigure PB6/PB7 for I2C AF4 */
    GPIO_MODER(GPIOB_BASE) &= ~((3<<12)|(3<<14));
    GPIO_MODER(GPIOB_BASE) |=  ((2<<12)|(2<<14));
    GPIO_OTYPER(GPIOB_BASE) |= (1<<6)|(1<<7);
    GPIO_PUPDR(GPIOB_BASE) &= ~((3<<12)|(3<<14));
    GPIO_PUPDR(GPIOB_BASE) |=  ((1<<12)|(1<<14));
    GPIO_AFRL(GPIOB_BASE) &= ~((0xF<<24)|(0xF<<28));
    GPIO_AFRL(GPIOB_BASE) |=  ((4<<24)|(4<<28));
    I2C1_CR1 = 0;
    I2C1_TIMINGR = 0x00503D5B;
    I2C1_CR1 = 1;
}

static uint8_t wm_write(uint8_t reg, uint16_t val) {
    uint8_t b1 = (reg << 1) | ((val >> 8) & 1);
    uint8_t b2 = val & 0xFF;
    I2C1_ICR = 0x3F38;
    I2C1_CR2 = (0x1A << 1) | (2 << 16) | (1 << 25) | (1 << 13);
    uint32_t t = 100000;
    while (!(I2C1_ISR & (1<<1)) && --t); if(!t) return 1;
    I2C1_TXDR = b1;
    t = 100000;
    while (!(I2C1_ISR & (1<<1)) && --t); if(!t) return 2;
    I2C1_TXDR = b2;
    t = 100000;
    while (!(I2C1_ISR & (1<<5)) && --t); if(!t) return 3;
    I2C1_ICR = (1<<5);
    if (I2C1_ISR & (1<<4)) return 4;
    return 0;
}

static void i2s2_init(void) {
    RCC_APB1ENR1 |= (1 << 14); /* SPI2EN */
    delay(100);

    /* PA3=AF1(TIM2_CH4) for MCLK via timer PWM, PA8=AF5(I2S2_CK), PA9=AF5(I2S2_WS), PA10=AF5(I2S2_SD) */
    volatile uint32_t *moder_ptr = (volatile uint32_t *)0x48000000;
    uint32_t tmp = *moder_ptr;
    tmp &= ~((3<<6)|(3<<16)|(3<<18)|(3<<20));   /* Clear PA3,PA8,PA9,PA10 */
    tmp |=  ((2<<6)|(2<<16)|(2<<18)|(2<<20));   /* Set all to AF mode (10) */
    *moder_ptr = tmp;
    __asm__ volatile("dsb" ::: "memory");

    /* Enable TIM2 CH4 PWM on PA3 (AF1) for MCLK */
    RCC_APB1ENR1 |= 1; /* TIM2EN */
    delay(100);
    GPIO_AFRL(GPIOA_BASE) = (GPIO_AFRL(GPIOA_BASE) & ~(0xF<<12)) | (1<<12); /* PA3 = AF1 */
    GPIO_OSPEEDR(GPIOA_BASE) |= ((3<<6)|(3<<16)|(3<<18)|(3<<20)); /* Very High speed */
    GPIO_AFRH(GPIOA_BASE) &= ~((0xF<<0)|(0xF<<4)|(0xF<<8));
    GPIO_AFRH(GPIOA_BASE) |=  ((5<<0)|(5<<4)|(5<<8)); /* PA8,PA9,PA10 = AF5 */

    /* TIM2 CH4 PWM: generate 2MHz MCLK on PA3 */
    #define TIM2_BASE 0x40000000
    #define TIM2_CR1  (*(volatile uint32_t *)(TIM2_BASE + 0x00))
    #define TIM2_CCMR2 (*(volatile uint32_t *)(TIM2_BASE + 0x1C))
    #define TIM2_CCER (*(volatile uint32_t *)(TIM2_BASE + 0x20))
    #define TIM2_ARR  (*(volatile uint32_t *)(TIM2_BASE + 0x2C))
    #define TIM2_CCR4 (*(volatile uint32_t *)(TIM2_BASE + 0x40))

    (*(volatile uint32_t *)(RCC_BASE + 0x58)) |= (1 << 0); /* TIM2EN */
    delay(10);
    TIM2_ARR = 6;        /* 16MHz / 7 ≈ 2.286MHz → DCLKDIV=÷3→762kHz ∈ [700,800] */
    TIM2_CCR4 = 3;       /* duty 3/7≈43% (40-60% OK per p13) */
    TIM2_CCMR2 = (6 << 12) | (1 << 11); /* OC4M=110(PWM1), OC4PE=1 */
    TIM2_CCER = (1 << 12);  /* CC4E: enable CH4 output */
    TIM2_CR1 = 1;        /* CEN: start timer */

    /* I2S2: MASTER TX (MCU generates BCLK/LRCLK, WM8960 is slave) */
    SPI2_I2SCFGR = 0;
    SPI2_I2SPR = (1 << 9) | (0 << 8) | 4; /* MCKOE=1, ODD=0, I2SDIV=4 → Fs=7812.5Hz */
    SPI2_I2SCFGR = (1 << 11) | (2 << 8) | (0 << 4) | (0 << 1) | (1 << 0); /* I2S Philips */
    SPI2_I2SCFGR |= (1 << 10); /* I2SE=1 */

    /* Debug: store GPIOA_MODER BEFORE force-write */
    volatile uint8_t *res = (volatile uint8_t *)0x20000100;
    uint32_t moder_before = GPIO_MODER(GPIOA_BASE);
    res[3] = (uint8_t)(moder_before >> 16);  /* PA8-PA15 modes BEFORE */

    /* Force PA9+PA10 = AF mode AFTER I2SE enable (silicon bug: I2S hw corrupts MODER) */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~((3<<18)|(3<<20))) | (2<<18)|(2<<20);
    __asm__ volatile("dsb");  /* Data synchronization barrier */
    __asm__ volatile("isb");  /* Instruction synchronization barrier */

    uint32_t moder_after = GPIO_MODER(GPIOA_BASE);
    res[4] = (uint8_t)(moder_after >> 16);   /* PA8-PA15 modes AFTER */
}

int main(void) {
    /* Switch system clock to HSI16 (16MHz) for proper I2S clocks */
    (*(volatile uint32_t *)(RCC_BASE + 0x00)) |= (1 << 8);  /* HSION */
    while (!((*(volatile uint32_t *)(RCC_BASE + 0x00)) & (1 << 10))); /* HSIRDY */
    (*(volatile uint32_t *)(RCC_BASE + 0x08)) = 0x00000001; /* SW=01: HSI16 as SYSCLK */
    while (((*(volatile uint32_t *)(RCC_BASE + 0x08)) & 0x0C) != 0x04); /* Wait SWS=01 */

    /* Set clock sources: SPI2S2SEL=HSI16(bits9:8=10), ADCSEL=HSI16(bits29:28=01) */
    (*(volatile uint32_t *)(RCC_BASE + 0x88)) = 0x10000200;

    RCC_AHB2ENR |= (1<<0)|(1<<1);
    delay(100);
    RESULT_BASE[0] = 0xAA;

    /* Start MCLK FIRST (WM8960 needs MCLK for clocking) */
    i2s2_init();

    i2c1_init();

    /* WM8960 config: DAC → Left Speaker */
    uint8_t err = 0;
    wm_write(0x0F, 0x000); delay(50000); /* Reset */

    /* Pwr1: VMIDSEL=11(fast), VREF=1. DO NOT enable ADC! PA10 is shared ADCDAT/DACDAT —
     * opening ADCL/ADCR causes bus conflict on the half-duplex SD line.
     * Only enable AINL/AINR/MICB if doing recording (then close DAC first). */
    err |= wm_write(0x19, 0x0C0);
    /* Pwr2: DACL, DACR, LOUT1, ROUT1, SPKL, SPKR - enable ALL */
    err |= wm_write(0x1A, 0x1F8);
    /* Pwr3: LOMIX, ROMIX, LMIC, RMIC - enable ALL */
    err |= wm_write(0x2F, 0x03C);
    delay(200000); /* VMID charge */

    /* Audio interface: I2S, 16-bit, WM8960 as SLAVE (MS=0) */
    err |= wm_write(0x07, 0x002);                            /* I2S,16b,Slave */
    /* Audio interface 1: ALRCGPIO=1 (ADCLRC pin = GPIO, ignore it) */
    err |= wm_write(0x09, 0x040);
    /* TRIS=1: tristate ADCDAT (datasheet p53/p75 R24 bit3).
     * MPTT PA10 is shared ADCDAT/DACDAT — ADCDAT drives LOW even when ADC off,
     * overpowering I2S AF driver. TRIS=1 makes ADCDAT high-Z, fixing the pull-down. */
    err |= wm_write(0x18, 0x008);
    /* Clocking: SYSCLK from MCLK */
    err |= wm_write(0x04, 0x000);
    /* Clocking 2: DCLKDIV=010(÷3) → 2.286M/3=762kHz ∈ [700,800] (datasheet p57) */
    err |= wm_write(0x08, 0x044);

    /* DACMU=0: unmute DAC (default=1=muted!) */
    err |= wm_write(0x05, 0x000);
    /* DAC volume: direct VU=1 (known working) */
    err |= wm_write(0x0A, 0x1FF); /* VU=1 */
    err |= wm_write(0x0B, 0x1FF); /* VU=1 */

    /* Left Out Mix: LD2LO=1 (DAC to left output mixer) */
    err |= wm_write(0x22, 0x100);

    /* Speaker volume: VU=1 direct, VOL=63 ~0dB (known working from fw_minimal.bin) */
    err |= wm_write(0x28, 0x13F); /* VU=1, ZC=0, VOL=63 */
    err |= wm_write(0x29, 0x13F); /* VU=1, ZC=0, VOL=63 */

    /* Class D enable: left speaker + boost */
    err |= wm_write(0x31, 0x0F7); /* SPK_OP_EN=11 + silicon default Class D params (0x37) */

    RESULT_BASE[1] = err ? 0xEE : 0x00;
    /* Store I2C1_ISR for debug */
    volatile uint8_t *dbg2 = (volatile uint8_t *)0x2000010B;
    dbg2[0] = (uint8_t)(I2C1_ISR & 0xFF);

    /* I2S already initialized above (before WM8960 config) */
    RESULT_BASE[2] = (SPI2_I2SCFGR & (1<<10)) ? 0x55 : 0x00;

    RESULT_BASE[5] = 0x55;

    /* Send square wave tone through I2S → DAC → Speaker */
    /* Toggle every 2 samples → ~2kHz tone */
    int16_t sample = 0x7FFF;
    uint32_t count = 0;
    while (1) {
        if (SPI2_SR & (1 << 1)) {
            SPI2_DR = (uint16_t)sample;
            count++;
        }
        /* Toggle DACMU every ~1s to check if DAC is processing I2S data */
        if ((count & 0x3FFF) == 0) {
            wm_write(0x05, 0x000);  /* DACMU=0: unmute */
        } else if ((count & 0x3FFF) == 0x2000) {
            wm_write(0x05, 0x008);  /* DACMU=1: mute */
        }
        RESULT_BASE[6] = (uint8_t)(count >> 8);
    }
}
