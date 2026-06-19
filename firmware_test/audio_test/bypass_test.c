/* MPTT v0.1 - WM8960 Analog Bypass Test
 * Purpose: Verify the analog path (Output Mixer → Speaker) works
 *          WITHOUT involving I2S/DAC at all.
 * 
 * Method: Enable LINPUT3 → Left Output Mixer bypass → Speaker
 *         This creates a DC offset that should produce a click/pop on power-up
 *         OR: use VMID as a reference to confirm Class D is switching
 *
 * If SPK_LP shows ~VBAT/2 (~2V) = Class D powered and biased
 * If we hear a pop/click on reset = analog path is working
 * If total silence = hardware issue (Class D, power, or speaker)
 */

#include <stdint.h>

#define RESULT ((volatile uint8_t *)0x20000100)

/* RCC */
#define RCC_BASE        0x58000000
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1    (*(volatile uint32_t *)(RCC_BASE + 0x58))

/* GPIO */
#define GPIOB_BASE      0x48000400
#define GPIO_MODER(b)   (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_OTYPER(b)  (*(volatile uint32_t *)((b) + 0x04))
#define GPIO_PUPDR(b)   (*(volatile uint32_t *)((b) + 0x0C))
#define GPIO_AFRL(b)    (*(volatile uint32_t *)((b) + 0x20))

/* I2C1 */
#define I2C1_BASE       0x40005400
#define I2C1_CR1        (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2        (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_TIMINGR    (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_ISR        (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_ICR        (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TXDR       (*(volatile uint32_t *)(I2C1_BASE + 0x28))

static void delay(volatile uint32_t n) { while(n--); }

static void clock_init(void) {
    RCC_CR |= (1 << 8);
    while (!(RCC_CR & (1 << 10)));
    RCC_AHB2ENR |= (1 << 0) | (1 << 1);
    RCC_APB1ENR1 |= (1 << 21);
    delay(100);
}

static void i2c_init(void) {
    GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~((3<<12)|(3<<14))) | ((2<<12)|(2<<14));
    GPIO_OTYPER(GPIOB_BASE) |= (1 << 6) | (1 << 7);
    GPIO_PUPDR(GPIOB_BASE) = (GPIO_PUPDR(GPIOB_BASE) & ~((3<<12)|(3<<14))) | ((1<<12)|(1<<14));
    GPIO_AFRL(GPIOB_BASE) = (GPIO_AFRL(GPIOB_BASE) & ~((0xF<<24)|(0xF<<28))) | ((4<<24)|(4<<28));
    I2C1_CR1 = 0;
    I2C1_TIMINGR = 0x00503D5B;
    I2C1_CR1 = 1;
}

static int i2c_write2(uint8_t dev_addr, uint8_t byte1, uint8_t byte2) {
    I2C1_ICR = 0x3F38;
    I2C1_CR2 = (dev_addr << 1) | (2 << 16) | (1 << 25) | (1 << 13);
    uint32_t timeout = 100000;
    while (!(I2C1_ISR & (1 << 1)) && --timeout) {
        if (I2C1_ISR & (1 << 4)) { I2C1_ICR = (1<<4); return -1; }
    }
    if (!timeout) return -2;
    I2C1_TXDR = byte1;
    timeout = 100000;
    while (!(I2C1_ISR & (1 << 1)) && --timeout) {
        if (I2C1_ISR & (1 << 4)) { I2C1_ICR = (1<<4); return -1; }
    }
    if (!timeout) return -2;
    I2C1_TXDR = byte2;
    timeout = 100000;
    while (!(I2C1_ISR & (1 << 5)) && --timeout);
    I2C1_ICR = (1 << 5);
    return 0;
}

static int wm_write(uint8_t reg, uint16_t data) {
    uint8_t b1 = (reg << 1) | ((data >> 8) & 0x01);
    uint8_t b2 = data & 0xFF;
    return i2c_write2(0x1A, b1, b2);
}

int main(void) {
    RESULT[0] = 0xAA;  /* boot */
    clock_init();
    RESULT[1] = 0x01;  /* clk ok */
    i2c_init();
    RESULT[2] = 0x02;  /* i2c init */

    /* --- WM8960 Bypass Test --- */
    int r;
    
    /* Reset */
    r = wm_write(0x0F, 0x000);
    RESULT[3] = (r == 0) ? 0x10 : 0xE0;
    delay(800000);  /* 50ms */

    /* Power 1: VMID=01(50k), VREF=1 — minimal power for analog */
    r = wm_write(0x19, 0x0C0);  /* bits[8:7]=01, bit6=1 */
    RESULT[3] = (r == 0) ? 0x11 : 0xE1;
    
    /* Power 2: SPKL=1, SPKR=1 (speaker amp power) */
    r = wm_write(0x1A, 0x018);  /* bit4=1 SPKL, bit3=1 SPKR */
    RESULT[3] = (r == 0) ? 0x12 : 0xE2;

    /* Power 3: LOMIX=1, ROMIX=1 (output mixer power) */
    r = wm_write(0x2F, 0x00C);  /* bit3=1, bit2=1 */
    RESULT[3] = (r == 0) ? 0x13 : 0xE3;

    /* Wait 300ms for VMID to charge */
    delay(4800000);

    /* Clocking 2: DCLKDIV=001(÷2) → DCLK=1MHz for Class D
     * NOTE: Even in bypass (no DAC), Class D needs DCLK from SYSCLK.
     * SYSCLK comes from MCLK. But we haven't started MCLK yet!
     * In bypass mode without MCLK: does Class D still work?
     * WM8960 internal oscillator? No — SYSCLK is REQUIRED.
     * → We MUST provide MCLK even for bypass/Class D only! */
    
    /* Actually — SYSCLK only matters for DAC/ADC digital.
     * The Class D switching clock (DCLK) is derived from SYSCLK.
     * Without SYSCLK, DCLK = 0, Class D won't switch.
     * 
     * SOLUTION: Enable TIM2 MCLK even for bypass test! */

    /* Enable TIM2 for MCLK */
    RCC_APB1ENR1 |= (1 << 0);  /* TIM2EN */
    delay(100);
    
    /* PA3 = AF1 (TIM2_CH4) */
    #define GPIOA_BASE 0x48000000
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3 << 6)) | (2 << 6);
    *(volatile uint32_t *)(GPIOA_BASE + 0x08) |= (3 << 6);  /* OSPEEDR VeryHigh */
    GPIO_AFRL(GPIOA_BASE) = (GPIO_AFRL(GPIOA_BASE) & ~(0xF << 12)) | (1 << 12);

    /* TIM2 CH4 PWM: 16MHz/8=2MHz */
    *(volatile uint32_t *)(0x40000000 + 0x2C) = 7;    /* ARR */
    *(volatile uint32_t *)(0x40000000 + 0x40) = 4;    /* CCR4 */
    *(volatile uint32_t *)(0x40000000 + 0x1C) = (6<<12)|(1<<11); /* CCMR2 */
    *(volatile uint32_t *)(0x40000000 + 0x20) = (1<<12); /* CCER */
    *(volatile uint32_t *)(0x40000000 + 0x00) = 1;    /* CR1 CEN */
    
    RESULT[3] = 0x14;  /* MCLK started */
    delay(160000);  /* 10ms for MCLK to stabilize */

    /* Now configure clocking */
    wm_write(0x04, 0x000);  /* CLKSEL=0(MCLK), no div */
    wm_write(0x08, 0x040);  /* DCLKDIV=001(÷2) → DCLK=1MHz */

    /* Left Output Mixer: enable LINPUT3 bypass (bit7=LI2LO) */
    wm_write(0x22, 0x080);  /* LI2LO=1, volume=0dB(bits[6:4]=000) */
    
    /* Speaker volume: max */
    wm_write(0x28, 0x17F);  /* SPKVU=1, VOL=127(+6dB) */
    wm_write(0x29, 0x17F);  /* SPKVU=1, VOL=127(+6dB) */
    
    /* Class D enable: only bits[7:6], rest=0 */
    wm_write(0x31, 0x0C0);  /* SPK_OP_EN=11 */
    
    /* Class D boost */
    wm_write(0x33, 0x02D);  /* DCGAIN=5, ACGAIN=5 */

    RESULT[4] = 0x20;  /* bypass config done */

    /* ALRCGPIO=1 (ADCLRC pin unused) */
    wm_write(0x09, 0x040);

    RESULT[5] = 0x55;  /* done */

    /* If Class D works, SPK_LP should be ~VBAT/2 (~2V)
     * and there should be a click/pop when this firmware starts.
     * 
     * Additionally: touch LINPUT3 pin with finger → should hear buzz
     * (50Hz hum from body capacitance) */
    
    uint8_t loop = 0;
    while(1) {
        RESULT[7] = loop++;
        delay(500000);
    }
}
