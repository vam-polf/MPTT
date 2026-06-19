/* MPTT v0.1 Hardware Test Firmware
 * Target: STM32WLE5CBU6 (inside E77-400M22S)
 * Tests: Boot, I2C (WM8960), GPIO (PTT), ADC (Battery)
 * Results stored at 0x20000100, readable via pyocd
 */

#include <stdint.h>

/* Result memory at fixed SRAM address */
#define RESULT_BASE ((volatile uint8_t *)0x20000100)
#define RES_BOOT     0   /* 0xAA = booted OK */
#define RES_I2C      1   /* 0x1A = WM8960 found, 0xFF = not found */
#define RES_PTT      2   /* 0 = not pressed, 1 = pressed */
#define RES_ADC_L    3   /* ADC value low byte */
#define RES_ADC_H    4   /* ADC value high byte */
#define RES_DONE     5   /* 0x55 = all tests complete */
#define RES_LOOP     6   /* Loop counter */

/* RCC */
#define RCC_BASE        0x58000000
#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1    (*(volatile uint32_t *)(RCC_BASE + 0x58))
#define RCC_CCIPR       (*(volatile uint32_t *)(RCC_BASE + 0x88))

/* GPIO */
#define GPIOA_BASE      0x48000000
#define GPIOB_BASE      0x48000400
#define GPIO_MODER(b)   (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_OTYPER(b)  (*(volatile uint32_t *)((b) + 0x04))
#define GPIO_PUPDR(b)   (*(volatile uint32_t *)((b) + 0x0C))
#define GPIO_IDR(b)     (*(volatile uint32_t *)((b) + 0x10))
#define GPIO_AFRL(b)    (*(volatile uint32_t *)((b) + 0x20))
#define GPIO_AFRH(b)    (*(volatile uint32_t *)((b) + 0x24))

/* I2C1 */
#define I2C1_BASE       0x40005400
#define I2C1_CR1        (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2        (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_TIMINGR    (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_ISR        (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_ICR        (*(volatile uint32_t *)(I2C1_BASE + 0x1C))

/* ADC */
#define ADC_BASE        0x40012400
#define ADC_ISR         (*(volatile uint32_t *)(ADC_BASE + 0x00))
#define ADC_CR          (*(volatile uint32_t *)(ADC_BASE + 0x08))
#define ADC_CFGR1       (*(volatile uint32_t *)(ADC_BASE + 0x0C))
#define ADC_CFGR2       (*(volatile uint32_t *)(ADC_BASE + 0x10))
#define ADC_SMPR        (*(volatile uint32_t *)(ADC_BASE + 0x14))
#define ADC_CHSELR      (*(volatile uint32_t *)(ADC_BASE + 0x28))
#define ADC_DR          (*(volatile uint32_t *)(ADC_BASE + 0x40))

static void delay(volatile uint32_t n) { while(n--); }

/* I2C1 init: PB6=SCL, PB7=SDA, 100kHz @ 16MHz HSI */
static void i2c1_init(void) {
    /* Enable I2C1 clock */
    RCC_APB1ENR1 |= (1 << 21);
    delay(100);

    /* PB6 = AF4 (I2C1_SCL), PB7 = AF4 (I2C1_SDA) */
    /* MODER: AF mode (10) for PB6 and PB7 */
    GPIO_MODER(GPIOB_BASE) &= ~((3 << 12) | (3 << 14));
    GPIO_MODER(GPIOB_BASE) |=  ((2 << 12) | (2 << 14));
    /* Open-drain */
    GPIO_OTYPER(GPIOB_BASE) |= (1 << 6) | (1 << 7);
    /* Pull-up */
    GPIO_PUPDR(GPIOB_BASE) &= ~((3 << 12) | (3 << 14));
    GPIO_PUPDR(GPIOB_BASE) |=  ((1 << 12) | (1 << 14));
    /* AF4 for PB6 (AFRL bits 27:24) and PB7 (AFRL bits 31:28) */
    GPIO_AFRL(GPIOB_BASE) &= ~((0xF << 24) | (0xF << 28));
    GPIO_AFRL(GPIOB_BASE) |=  ((4 << 24) | (4 << 28));

    /* I2C1 timing: 100kHz @ 16MHz HSI */
    I2C1_CR1 = 0; /* Disable I2C */
    I2C1_TIMINGR = 0x00503D5B; /* 100kHz timing for 16MHz */
    I2C1_CR1 = 1; /* Enable I2C */
}

/* I2C scan: returns 1 if device ACKs at given 7-bit address */
static int i2c_scan(uint8_t addr) {
    /* Clear any previous flags */
    I2C1_ICR = 0x3F38;

    /* Configure: 7-bit addr, write, 0 bytes, AUTOEND, START */
    I2C1_CR2 = (addr << 1) | (0 << 16) | (1 << 25) | (1 << 13);

    /* Wait for STOPF or NACKF */
    uint32_t timeout = 100000;
    while (timeout--) {
        uint32_t isr = I2C1_ISR;
        if (isr & (1 << 5)) { /* STOPF */
            I2C1_ICR = (1 << 5); /* Clear STOPF */
            return 1; /* ACK received */
        }
        if (isr & (1 << 4)) { /* NACKF */
            I2C1_ICR = (1 << 4); /* Clear NACKF */
            /* Wait for STOP */
            while (!(I2C1_ISR & (1 << 5)));
            I2C1_ICR = (1 << 5);
            return 0; /* NACK */
        }
    }
    return 0; /* Timeout */
}

/* PB0 (PTT) init as input with pull-down */
static void ptt_init(void) {
    /* PB0: input mode (00), pull-down (10) */
    GPIO_MODER(GPIOB_BASE) &= ~(3 << 0);  /* Input */
    GPIO_PUPDR(GPIOB_BASE) &= ~(3 << 0);
    GPIO_PUPDR(GPIOB_BASE) |=  (2 << 0);   /* Pull-down */
}

static int ptt_read(void) {
    return (GPIO_IDR(GPIOB_BASE) & 1) ? 1 : 0;
}

/* ADC init: PA0 = ADC channel 0 */
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x60))

static void adc_init(void) {
    /* Set ADC clock source = HSI16 (RCC_CCIPR bits[29:28]=01) */
    RCC_CCIPR = (RCC_CCIPR & ~(3 << 28)) | (1 << 28);
    /* Enable ADC clock via RCC_APB2ENR bit 9 (STM32WLE5 specific!) */
    RCC_APB2ENR |= (1 << 9);
    delay(100);

    /* PA0 as analog (MODER = 11) */
    GPIO_MODER(GPIOA_BASE) |= (3 << 0);

    /* Exit deep power down and enable regulator */
    ADC_CR &= ~(1 << 29);  /* Clear DEEPPWD */
    ADC_CR |= (1 << 28);   /* ADVREGEN = 1 */
    delay(50000);           /* Wait for regulator startup (~20us) */

    /* Calibration with timeout */
    ADC_CR |= (1 << 31); /* ADCAL */
    uint32_t timeout = 1000000;
    while ((ADC_CR & (1 << 31)) && --timeout);

    /* Configure: 12-bit, single conversion */
    ADC_CFGR1 = 0;
    ADC_SMPR = 0x07; /* Longest sampling time */
    ADC_CHSELR = (1 << 0); /* Channel 0 (PA0) */

    /* Enable ADC with timeout */
    ADC_ISR = 1; /* Clear ADRDY by writing 1 */
    ADC_CR |= 1; /* ADEN */
    timeout = 1000000;
    while (!(ADC_ISR & 1) && --timeout); /* Wait for ADRDY */
}

static uint16_t adc_read(void) {
    ADC_CR |= (1 << 2); /* ADSTART */
    while (!(ADC_ISR & (1 << 2))); /* Wait for EOC */
    return (uint16_t)ADC_DR;
}

int main(void) {
    /* Enable GPIO clocks: GPIOA, GPIOB */
    RCC_AHB2ENR |= (1 << 0) | (1 << 1);
    delay(1000);

    /* Mark boot success */
    RESULT_BASE[RES_BOOT] = 0xAA;

    /* Init peripherals */
    ptt_init();
    i2c1_init();
    adc_init();

    /* Test I2C: scan for WM8960 at 0x1A */
    if (i2c_scan(0x1A)) {
        RESULT_BASE[RES_I2C] = 0x1A; /* Found */
    } else {
        RESULT_BASE[RES_I2C] = 0xFF; /* Not found */
    }

    /* Mark tests complete */
    RESULT_BASE[RES_DONE] = 0x55;

    /* Main loop: continuously update PTT and ADC */
    uint8_t loop = 0;
    while (1) {
        RESULT_BASE[RES_PTT] = ptt_read();

        uint16_t adc = adc_read();
        RESULT_BASE[RES_ADC_L] = adc & 0xFF;
        RESULT_BASE[RES_ADC_H] = (adc >> 8) & 0xFF;

        RESULT_BASE[RES_LOOP] = loop++;

        delay(500000); /* ~200ms at 16MHz */
    }
}
