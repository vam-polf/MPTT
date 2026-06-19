/* MPTT Click Test v2 — toggle SPKLVOL Analog Mute ↔ Max (+6dB)
 * MCLK: TIM2 PWM ~2.286MHz (ARR=6) → DCLKDIV=÷3→762kHz ∈ [700,800]
 * Datasheet p43: "volume adjusted while signal non-zero → audible click" */
#include <stdint.h>

#define RCC_BASE      0x58000000
#define RCC_CR        (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_AHB2ENR   (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1  (*(volatile uint32_t *)(RCC_BASE + 0x58))

#define GPIOA_BASE    0x48000000
#define GPIOB_BASE    0x48000400
#define GPIO_MODER(b)  (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_OTYPER(b) (*(volatile uint32_t *)((b) + 0x04))
#define GPIO_PUPDR(b)  (*(volatile uint32_t *)((b) + 0x0C))
#define GPIO_AFRL(b)   (*(volatile uint32_t *)((b) + 0x20))

#define I2C1_BASE     0x40005400
#define I2C1_CR1      (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2      (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_TIMINGR  (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_ISR      (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_ICR      (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TXDR     (*(volatile uint32_t *)(I2C1_BASE + 0x28))

#define RESULT ((volatile uint8_t *)0x20000100)
static void delay(volatile uint32_t n) { while(n--); }

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

int main(void) {
    /* 1. HSI16 -> SYSCLK */
    RCC_CR |= (1 << 8);
    while (!(RCC_CR & (1 << 10)));
    (*(volatile uint32_t *)(RCC_BASE + 0x08)) = 0x00000001;
    while (((*(volatile uint32_t *)(RCC_BASE + 0x08)) & 0x0C) != 0x04);
    RCC_AHB2ENR |= (1<<0)|(1<<1);
    RCC_APB1ENR1 |= (1 << 0);    /* TIM2EN */
    RCC_APB1ENR1 |= (1 << 21);   /* I2C1EN */
    delay(1000);
    RESULT[0] = 0xAA;

    /* 2. MCLK: TIM2 CH4 PWM on PA3, ~2.286MHz
     * ARR=6 → 16MHz/7≈2.286MHz → DCLKDIV=÷3=762kHz ∈ [700,800]kHz ✅ */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(3<<6)) | (2<<6);
    GPIO_AFRL(GPIOA_BASE) = (GPIO_AFRL(GPIOA_BASE) & ~(0xF<<12)) | (1<<12);
    (*(volatile uint32_t *)(0x40000000 + 0x2C)) = 6;    /* ARR=6 → 16MHz/7≈2.286MHz → DCLKDIV=÷3→762kHz ✅ 700-800k */
    (*(volatile uint32_t *)(0x40000000 + 0x40)) = 3;    /* CCR4=3 → 占空比 3/7≈43% (40~60%内) */
    (*(volatile uint32_t *)(0x40000000 + 0x1C)) = (6<<12)|(1<<11);
    (*(volatile uint32_t *)(0x40000000 + 0x20)) = (1<<12);
    (*(volatile uint32_t *)(0x40000000 + 0x00)) = 1;    /* CR1 */
    RESULT[1] = 0x01;

    /* 3. I2C init */
    GPIO_MODER(GPIOB_BASE) &= ~((3<<12)|(3<<14));
    GPIO_MODER(GPIOB_BASE) |=  ((2<<12)|(2<<14));
    GPIO_OTYPER(GPIOB_BASE) |= (1<<6)|(1<<7);
    GPIO_PUPDR(GPIOB_BASE) |= ((1<<12)|(1<<14));
    GPIO_AFRL(GPIOB_BASE) |= ((4<<24)|(4<<28));
    I2C1_TIMINGR = 0x00503D5B;
    I2C1_CR1 = 1;
    RESULT[2] = 0x1A;

    /* 4. WM8960 init (minimal, no I2S needed for click test) */
    uint8_t e = 0;
    e |= wm_write(0x0F, 0x000); delay(50000);     /* Reset */
    e |= wm_write(0x19, 0x080); delay(800000);      /* VMID=50k, ~50ms */
    e |= wm_write(0x19, 0x140);                     /* VMID=250k+VREF */
    e |= wm_write(0x1A, 0x198);                     /* DACL+DACR+SPKL+SPKR */
    e |= wm_write(0x2F, 0x00C);                     /* LOMIX+ROMIX */
    delay(3200000);                                  /* 200ms settle */
    e |= wm_write(0x04, 0x000);                     /* SYSCLK=MCLK */
    e |= wm_write(0x08, 0x044);                     /* R8: DCLKDIV=010(÷3→762kHz ∈ [700,800] ✅) BCLKDIV=4 */
    e |= wm_write(0x07, 0x002);                     /* I2S 16-bit Slave */
    e |= wm_write(0x09, 0x040);                     /* ALRCGPIO */
    e |= wm_write(0x0A, 0x0FF);                     /* LDAC VU=0 */
    e |= wm_write(0x0B, 0x1FF);                     /* RDAC VU=1 */
    e |= wm_write(0x22, 0x100);                     /* LD2LO */
    e |= wm_write(0x05, 0x000);                     /* DACMU=0: unmute DAC (default=1!) — needed so DAC outputs VMID into PGA */
    e |= wm_write(0x28, 0x07F);                     /* SPK L VU=0 */
    e |= wm_write(0x29, 0x1FF);                     /* SPK R VU=1 */
    e |= wm_write(0x31, 0x0F7);                     /* Class D ON */
    delay(3200000);                                  /* 200ms */
    RESULT[3] = e ? 0xEE : 0x00;

    /* 5. Toggle SPKLVOL: Analog Mute ↔ Max (+6dB) → audible clicks
     * Datasheet p43/p79: SPKLVOL 0~47=Analog MUTE, 127=+6dB
     * ZC=0 → change immediately (no zero-cross wait)
     * VU latch: L(VU=0) loads latch, R(VU=1) triggers both channels */
    RESULT[4] = 0x55;
    while (1) {
        /* MUTE: L(VU=0, VOL=0) → R(VU=1, VOL=0) → both mute */
        e |= wm_write(0x28, 0x000);  /* R40: VU=0, ZC=0, VOL=0 (analog mute) → load L latch */
        e |= wm_write(0x29, 0x100);  /* R41: VU=1, ZC=0, VOL=0 (analog mute) → trigger BOTH */
        delay(16000000);    /* ~1 second — speaker silent */

        /* UNMUTE: L(VU=0, VOL=127) → R(VU=1, VOL=127) → both max */
        e |= wm_write(0x28, 0x07F);  /* R40: VU=0, ZC=0, VOL=127 (+6dB) → load L latch */
        e |= wm_write(0x29, 0x1FF);  /* R41: VU=1, ZC=0, VOL=127 (+6dB) → trigger BOTH → CLICK! */
        delay(16000000);    /* ~1 second — listen for click */

        RESULT[5]++;         /* click cycle count */
    }
}
