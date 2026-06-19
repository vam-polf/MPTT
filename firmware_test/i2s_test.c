	/* Speaker Test: I2S square wave → WM8960 DAC → Class D Amp → Speaker
	 * 硬件连接 (E77-xxxM22S 模块):
	 *   PA3  (Pin 10) → WM8960 MCLK    (AF1: TIM2_CH4, 2.286MHz 自由运行)
	 *   PA8  (Pin 16) → WM8960 BCLK    (AF5: I2S2_CK)
	 *   PA9  (Pin 18) → WM8960 LRCLK   (AF3: I2S2_WS)  <-- 必须是AF3!
	 *   PA10 (Pin 21) → WM8960 DACDAT  (AF5: I2S2_SD)
	 *   PB6  (Pin 4)  → WM8960 SCLK    (AF4: I2C1_SCL)
	 *   PB7  (Pin 5)  → WM8960 SDIN    (AF4: I2C1_SDA)
	 */
	#include <stdint.h>
	/* ========== 寄存器地址定义 ========== */
	#define RCC_BASE      0x58000000
	#define RCC_CR        (*(volatile uint32_t *)(RCC_BASE + 0x00))
	#define RCC_CFGR      (*(volatile uint32_t *)(RCC_BASE + 0x08))
	#define RCC_CCIPR     (*(volatile uint32_t *)(RCC_BASE + 0x88))
	#define RCC_AHB2ENR   (*(volatile uint32_t *)(RCC_BASE + 0x4C))
	#define RCC_APB1ENR1  (*(volatile uint32_t *)(RCC_BASE + 0x58))
	#define GPIOA_BASE    0x48000000
	#define GPIOB_BASE    0x48000400
	#define GPIO_MODER(b)   (*(volatile uint32_t *)((b) + 0x00))
	#define GPIO_OTYPER(b)  (*(volatile uint32_t *)((b) + 0x04))
	#define GPIO_OSPEEDR(b) (*(volatile uint32_t *)((b) + 0x08))
	#define GPIO_PUPDR(b)   (*(volatile uint32_t *)((b) + 0x0C))
	#define GPIO_IDR(b)     (*(volatile uint32_t *)((b) + 0x10))
	#define GPIO_ODR(b)     (*(volatile uint32_t *)((b) + 0x14))
	#define GPIO_BSRR(b)    (*(volatile uint32_t *)((b) + 0x18))
	#define GPIO_AFRL(b)    (*(volatile uint32_t *)((b) + 0x20))
	#define GPIO_AFRH(b)    (*(volatile uint32_t *)((b) + 0x24))
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
	#define TIM2_BASE     0x40000000
	#define TIM2_CR1      (*(volatile uint32_t *)(TIM2_BASE + 0x00))
	#define TIM2_CCMR2    (*(volatile uint32_t *)(TIM2_BASE + 0x1C))
	#define TIM2_CCER     (*(volatile uint32_t *)(TIM2_BASE + 0x20))
	#define TIM2_ARR      (*(volatile uint32_t *)(TIM2_BASE + 0x2C))
	#define TIM2_CCR4     (*(volatile uint32_t *)(TIM2_BASE + 0x40))
	#define RESULT_BASE   ((volatile uint8_t *)0x20000100)
	static void delay(volatile uint32_t n) { while(n--); }
	static void i2c1_init(void) {
	    RCC_APB1ENR1 &= ~(1 << 21);
	    delay(1000);
	    RCC_APB1ENR1 |= (1 << 21);
	    delay(100);
	    I2C1_CR1 = 0;
	    /* I2C总线恢复 */
	    GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~(3<<12)) | (1<<12);
	    GPIO_OTYPER(GPIOB_BASE) &= ~(1<<6);
	    GPIO_PUPDR(GPIOB_BASE) &= ~(3<<12);
	    for (int i = 0; i < 9; i++) {
	        GPIO_BSRR(GPIOB_BASE) = (1 << 22);
	        delay(100);
	        GPIO_BSRR(GPIOB_BASE) = (1 << 6);
	        delay(100);
	    }
	    /* 配置 PB6/PB7 为 I2C AF4 */
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
	    RCC_APB1ENR1 |= (1 << 0);  /* TIM2EN */
	    delay(100);
	    /* PA3 = AF1 (TIM2_CH4 MCLK) */
	    GPIO_AFRL(GPIOA_BASE) &= ~(0xF<<12);
	    GPIO_AFRL(GPIOA_BASE) |=  (1<<12);
	    /* PA8 = AF5 (I2S2_CK) */
	    GPIO_AFRH(GPIOA_BASE) &= ~(0xF<<0);
	    GPIO_AFRH(GPIOA_BASE) |=  (5<<0);
	    /* PA9 = AF3 (I2S2_WS)  *** 核心修复：必须是AF3 *** */
	    GPIO_AFRH(GPIOA_BASE) &= ~(0xF<<4);
	    GPIO_AFRH(GPIOA_BASE) |=  (3<<4);
	    /* PA10 = AF5 (I2S2_SD) */
	    GPIO_AFRH(GPIOA_BASE) &= ~(0xF<<8);
	    GPIO_AFRH(GPIOA_BASE) |=  (5<<8);
	    /* 全部设为AF模式，Very High速度 */
	    uint32_t tmp = GPIO_MODER(GPIOA_BASE);
	    tmp &= ~((3<<6)|(3<<16)|(3<<18)|(3<<20));
	    tmp |=  ((2<<6)|(2<<16)|(2<<18)|(2<<20));
	    GPIO_MODER(GPIOA_BASE) = tmp;
	    GPIO_OSPEEDR(GPIOA_BASE) |= ((3<<6)|(3<<16)|(3<<18)|(3<<20));
	    /* TIM2 CH4 PWM: 生成 2.286MHz MCLK (16MHz / 7) */
	    TIM2_ARR = 6;        /* 周期 7 */
	    TIM2_CCR4 = 3;       /* 占空比 3/7 ≈ 43% */
	    TIM2_CCMR2 = (6 << 12) | (1 << 11); /* OC4M=110(PWM1), OC4PE=1 */
	    TIM2_CCER = (1 << 12);  /* CC4E: enable CH4 */
	    TIM2_CR1 = 1;        /* CEN: start timer */
    /* I2S2配置: Master TX, 16-bit, Philips标准, 32-bit帧(CHLEN=1)
     * MCKOE=0(PA3是TIM2非I2S_MCK), ODD=1, I2SDIV=15 → P=31
     * BCLK=16MHz/31=516kHz, Fs=516k/64=8064Hz
     * WM8960 Slave需要32-bit channel frame (CHLEN=1)
     * SPI2S2SEL必须=10(SYSCLK), 01(HSI16直连)不工作! */
    SPI2_I2SCFGR = 0;
    SPI2_CR1 = 0;       /* Clear CR1 (SPE=0 during config) */
    SPI2_CR2 = 0;       /* Clear CR2 (no DMA for polling mode) */
    SPI2_I2SPR = (0 << 9) | (1 << 8) | 15;   /* MCKOE=0, ODD=1, I2SDIV=15 → P=31, BCLK=516kHz */
    SPI2_I2SCFGR = (1 << 11) | (2 << 8) | (0 << 4) | (0 << 1) | (1 << 0); /* CHLEN=1! */
    SPI2_CR1 = (1 << 6); /* SPE=1 — STM32WL needs SPI enable even in I2S mode! */
    SPI2_I2SCFGR |= (1 << 10); /* I2SE=1 */
    /* I2SE=1后硅片bug会篡改PA9+PA10为Analog(11)，必须修复 */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~((3<<18)|(3<<20))) | (2<<18)|(2<<20);
    __asm__ volatile("dsb");
}
	static uint8_t wm8960_init(void) {
	    uint8_t err = 0;
    err |= wm_write(0x0F, 0x000); delay(50000);
    err |= wm_write(0x18, 0x008); /* TRIS=1: 必须在VMID之前! 三态ADCDAT */
    err |= wm_write(0x19, 0x0C0); /* Pwr1: VMIDSEL=01, VREF=1 */
	    err |= wm_write(0x1A, 0x1F8); /* Pwr2: DACL, DACR, LOUT1, ROUT1, SPKL, SPKR */
	    err |= wm_write(0x2F, 0x03C); /* Pwr3: LOMIX, ROMIX */
	    delay(200000);
	    err |= wm_write(0x07, 0x002); /* I2S, 16b, Slave */
    err |= wm_write(0x09, 0x040); /* ALRCGPIO=1 */
    err |= wm_write(0x04, 0x000); /* SYSCLK from MCLK */
	    err |= wm_write(0x08, 0x080); /* DCLKDIV=010(÷3) → 762kHz */
	    err |= wm_write(0x05, 0x000); /* DACMU=0 (unmute) */
    err |= wm_write(0x0A, 0x0FF); /* DAC Vol L: VU=0(仅加载latch) */
    err |= wm_write(0x0B, 0x1FF); /* DAC Vol R: VU=1(触发两声道) */
	    err |= wm_write(0x22, 0x100); /* LD2LO=1 */
	    err |= wm_write(0x25, 0x100); /* RD2RO=1 */
    /* ZC=0 (强制更新音量，不等待过零) */
    err |= wm_write(0x28, 0x079); /* SPK Vol L: VU=0(仅加载latch), ZC=0, VOL=0x79(0dB) */
    err |= wm_write(0x29, 0x179); /* SPK Vol R: VU=1(触发两声道), ZC=0, VOL=0x79 */
	    err |= wm_write(0x31, 0x0F7); /* Class D Enable */
	    return err;
	}
	int main(void) {
	    /* 切换到HSI16时钟 */
	    RCC_CR |= (1 << 8);
	    while (!(RCC_CR & (1 << 10)));
	    RCC_CFGR = 0x00000001;
	    while ((RCC_CFGR & 0x0C) != 0x04);
    /* SPI2S2SEL=10(SYSCLK=HSI16), ADCSEL=01(HSI16) */
    RCC_CCIPR = 0x10000200;
	    RCC_AHB2ENR |= (1<<0) | (1<<1);
	    delay(100);
	    RESULT_BASE[0] = 0xAA;
	    i2s2_init();
	    i2c1_init();
	    uint8_t err = wm8960_init();
	    RESULT_BASE[1] = err ? 0xEE : 0x00;
	    RESULT_BASE[7] = (uint8_t)(I2C1_ISR & 0xFF);
	    RESULT_BASE[2] = (SPI2_I2SCFGR & (1<<10)) ? 0x55 : 0x00;
    /* 发送约 558Hz 方波 */
    int16_t sample_hi  = 0x7FFF;
    int16_t sample_lo  = 0x8000;
    uint8_t toggle = 0;
    uint32_t count = 0;
    uint32_t phase_counter = 0;
    volatile uint32_t *count32 = (volatile uint32_t *)0x20000110;
    volatile uint32_t *sr_debug = (volatile uint32_t *)0x20000114;
    while (1) {
        *sr_debug = SPI2_SR;
        if (SPI2_SR & (1 << 1)) {
            SPI2_DR = (uint16_t)(toggle ? sample_lo : sample_hi);
            count++;
            phase_counter++;
            if (phase_counter >= 8) {
                toggle ^= 1;
                phase_counter = 0;
            }
        }
        *count32 = count;
        RESULT_BASE[6] = (uint8_t)(count);
    }
}