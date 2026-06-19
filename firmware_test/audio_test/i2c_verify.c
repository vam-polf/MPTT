/* MPTT - I2C Write Verification Test
 * Toggles WM8960 Class D on/off every 2 seconds.
 * User watches SPK_LP voltage: should toggle 3V ↔ 0V.
 * If voltage toggles: I2C works → problem is I2S/audio config
 * If voltage stays fixed: I2C fails → problem is I2C hardware
 */

#include <stdint.h>

#define RESULT ((volatile uint8_t *)0x20000100)
#define R_BOOT   0
#define R_STATE  1  /* 0=OFF, 1=ON */

/* RCC */
#define RCC_BASE       0x58000000
#define RCC_CR         (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR       (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_AHB2ENR    (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1   (*(volatile uint32_t *)(RCC_BASE + 0x58))

/* GPIO */
#define GPIOA_BASE     0x48000000
#define GPIOB_BASE     0x48000400
#define GPIO_MODER(b)  (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_OTYPER(b) (*(volatile uint32_t *)((b) + 0x04))
#define GPIO_PUPDR(b)  (*(volatile uint32_t *)((b) + 0x0C))
#define GPIO_AFRL(b)   (*(volatile uint32_t *)((b) + 0x20))
#define GPIO_AFRH(b)   (*(volatile uint32_t *)((b) + 0x24))

/* TIM2 */
#define TIM2_BASE      0x40000000
#define TIM2_CR1       (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_CCMR2     (*(volatile uint32_t *)(TIM2_BASE + 0x1C))
#define TIM2_CCER      (*(volatile uint32_t *)(TIM2_BASE + 0x20))
#define TIM2_ARR       (*(volatile uint32_t *)(TIM2_BASE + 0x2C))
#define TIM2_CCR4      (*(volatile uint32_t *)(TIM2_BASE + 0x40))

/* I2C1 */
#define I2C1_BASE      0x40005400
#define I2C1_CR1       (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2       (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_TIMINGR   (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_ISR       (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_ICR       (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TXDR      (*(volatile uint32_t *)(I2C1_BASE + 0x28))

static void delay(volatile uint32_t n) { while(n--); }

static void clock_init(void) {
    RCC_CR |= (1 << 8);
    uint32_t t = 100000;
    while (!(RCC_CR & (1 << 10)) && --t);
    RCC_CFGR = (RCC_CFGR & ~0x03) | 0x01;
    t = 100000;
    while (((RCC_CFGR >> 2) & 0x03) != 0x01 && --t);
    RCC_AHB2ENR |= (1<<0) | (1<<1);
    RCC_APB1ENR1 |= (1<<0) | (1<<14) | (1<<13);
}

static void gpio_init(void) {
    /* PA3 = AF1 (TIM2_CH4 MCLK) */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3<<6)) | (2<<6);
    GPIO_AFRL(GPIOA_BASE) = (GPIO_AFRL(GPIOA_BASE) & ~(0xF<<12)) | (1<<12);
    /* PB6/PB7 = AF4 (I2C1) */
    GPIO_MODER(GPIOB_BASE) |= (2<<12) | (2<<14);
    GPIO_OTYPER(GPIOB_BASE) |= (1<<6) | (1<<7);
    GPIO_PUPDR(GPIOB_BASE) = (GPIO_PUPDR(GPIOB_BASE) & ~((3<<12)|(3<<14))) | ((1<<12)|(1<<14));
    GPIO_AFRL(GPIOB_BASE) = (GPIO_AFRL(GPIOB_BASE) & ~((0xF<<24)|(0xF<<28))) | ((4<<24)|(4<<28));
}

static void mclk_init(void) {
    TIM2_ARR  = 7;
    TIM2_CCR4 = 4;
    TIM2_CCMR2 = (6 << 12) | (1 << 11);
    TIM2_CCER = (1 << 12);
    TIM2_CR1  = 1;
}

static void i2c_init(void) {
    I2C1_CR1 = 0;
    I2C1_TIMINGR = 0x00503D5B;
    I2C1_CR1 = 1;
}

static int i2c_write2(uint8_t dev, uint8_t b1, uint8_t b2) {
    I2C1_ICR = 0x3F38;
    I2C1_CR2 = (dev << 1) | (2 << 16) | (1 << 25) | (1 << 13);
    
    uint32_t t = 100000;
    while (!(I2C1_ISR & (1<<1)) && --t) {
        if (I2C1_ISR & (1<<4)) { I2C1_ICR = (1<<4); return -1; }
    }
    if (!t) return -2;
    I2C1_TXDR = b1;
    
    t = 100000;
    while (!(I2C1_ISR & (1<<1)) && --t) {
        if (I2C1_ISR & (1<<4)) { I2C1_ICR = (1<<4); return -1; }
    }
    if (!t) return -2;
    I2C1_TXDR = b2;
    
    t = 100000;
    while (!(I2C1_ISR & (1<<5)) && --t);
    I2C1_ICR = (1<<5);
    return 0;
}

static int wm_write(uint8_t reg, uint16_t data) {
    uint8_t b1 = (reg << 1) | ((data >> 8) & 0x01);
    uint8_t b2 = data & 0xFF;
    return i2c_write2(0x1A, b1, b2);
}

int main(void) {
    RESULT[R_BOOT] = 0xAA;
    clock_init();
    gpio_init();
    mclk_init();
    i2c_init();
    
    /* Quick I2C scan */
    int ok = 0;
    I2C1_ICR = 0x3F38;
    I2C1_CR2 = (0x1A << 1) | (0 << 16) | (1 << 25) | (1 << 13);
    uint32_t t = 100000;
    while (t--) {
        if (I2C1_ISR & (1<<5)) { I2C1_ICR=(1<<5); ok=1; break; }
        if (I2C1_ISR & (1<<4)) { I2C1_ICR=(1<<4); break; }
    }
    if (!ok) { RESULT[R_BOOT] = 0xFF; while(1); }
    
    /* Minimal WM8960 init: just power up Class D */
    wm_write(0x0F, 0x000);  /* Reset */
    delay(16000000);          /* ~1s */
    
    wm_write(0x19, 0x0C0);  /* VMID 50k + VREF */
    delay(32000000);          /* ~2s VMID charge */
    
    wm_write(0x1A, 0x198);  /* DACL+DACR+SPKL+SPKR */
    wm_write(0x2F, 0x00C);  /* LOMIX+ROMIX */
    
    /* Main loop: toggle Class D every 2 seconds */
    while (1) {
        /* ON */
        wm_write(0x31, 0x0F7);
        RESULT[R_STATE] = 1;
        delay(32000000);  /* ~2s */
        
        /* OFF */
        wm_write(0x31, 0x000);
        RESULT[R_STATE] = 0;
        delay(32000000);  /* ~2s */
    }
}
