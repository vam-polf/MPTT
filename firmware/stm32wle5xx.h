/*
 * Minimal CMSIS-style header for STM32WLE5CBU6
 * Only includes register definitions needed by MPTT drivers.
 * Extracted from RM0461 Reference Manual.
 */
#ifndef STM32WLE5XX_H
#define STM32WLE5XX_H

#include <stdint.h>

/* ===== RCC (Reset & Clock Control) ===== */
#define RCC_BASE        0x58000000U

#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CR_HSION    (1 << 8)
#define RCC_CR_HSIRDY   (1 << 10)

#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x08))

#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_AHB2ENR_GPIOAEN  (1 << 0)
#define RCC_AHB2ENR_GPIOBEN  (1 << 1)

#define RCC_APB1ENR1    (*(volatile uint32_t *)(RCC_BASE + 0x58))
#define RCC_APB1ENR1_TIM2EN   (1 << 0)
#define RCC_APB1ENR1_SPI2EN   (1 << 14)
#define RCC_APB1ENR1_I2C1EN   (1 << 21)

#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x60))
#define RCC_APB2ENR_ADCEN    (1 << 9)

#define RCC_CCIPR       (*(volatile uint32_t *)(RCC_BASE + 0x88))

#define RCC_CSR         (*(volatile uint32_t *)(RCC_BASE + 0x94))
#define RCC_CSR_LSION   (1 << 0)
#define RCC_CSR_LSIRDY  (1 << 1)

/* ===== GPIO ===== */
#define GPIOA_BASE      0x48000000U
#define GPIOB_BASE      0x48000400U

#define GPIO_MODER(base)   (*(volatile uint32_t *)((base) + 0x00))
#define GPIO_OTYPER(base)  (*(volatile uint32_t *)((base) + 0x04))
#define GPIO_OSPEEDR(base) (*(volatile uint32_t *)((base) + 0x08))
#define GPIO_PUPDR(base)   (*(volatile uint32_t *)((base) + 0x0C))
#define GPIO_IDR(base)     (*(volatile uint32_t *)((base) + 0x10))
#define GPIO_ODR(base)     (*(volatile uint32_t *)((base) + 0x14))
#define GPIO_BSRR(base)    (*(volatile uint32_t *)((base) + 0x18))
#define GPIO_AFRL(base)    (*(volatile uint32_t *)((base) + 0x20))
#define GPIO_AFRH(base)    (*(volatile uint32_t *)((base) + 0x24))

/* ===== I2C1 ===== */
#define I2C1_BASE       0x40005400U
#define I2C1_CR1        (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2        (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_TIMINGR    (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_ISR        (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_ICR        (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TXDR       (*(volatile uint32_t *)(I2C1_BASE + 0x28))
#define I2C1_RXDR       (*(volatile uint32_t *)(I2C1_BASE + 0x24))

/* I2C ISR flags */
#define I2C_ISR_TXE     (1 << 0)
#define I2C_ISR_TXIS    (1 << 1)
#define I2C_ISR_RXNE    (1 << 2)
#define I2C_ISR_NACKF   (1 << 4)
#define I2C_ISR_STOPF   (1 << 5)
#define I2C_ISR_TC      (1 << 6)
#define I2C_ISR_BUSY    (1 << 15)

/* ===== SPI2 / I2S2 ===== */
#define SPI2_BASE       0x40003800U
#define SPI2_CR1        (*(volatile uint32_t *)(SPI2_BASE + 0x00))
#define SPI2_CR2        (*(volatile uint32_t *)(SPI2_BASE + 0x04))
#define SPI2_SR         (*(volatile uint32_t *)(SPI2_BASE + 0x08))
#define SPI2_DR         (*(volatile uint32_t *)(SPI2_BASE + 0x0C))
#define SPI2_I2SCFGR    (*(volatile uint32_t *)(SPI2_BASE + 0x1C))
#define SPI2_I2SPR      (*(volatile uint32_t *)(SPI2_BASE + 0x20))

/* SPI2/I2S status flags */
#define SPI_SR_RXNE     (1 << 0)
#define SPI_SR_TXE      (1 << 1)
#define SPI_SR_UDR      (1 << 3)  /* I2S underrun */
#define SPI_SR_OVR      (1 << 6)  /* Overrun */
#define SPI_SR_BSY      (1 << 7)

/* I2S CFGR bits */
#define I2SCFGR_CHLEN   (1 << 0)
#define I2SCFGR_DATLEN_SHIFT 1
#define I2SCFGR_I2SSTD_SHIFT 4
#define I2SCFGR_I2SCFG_SHIFT 8
#define I2SCFGR_I2SE    (1 << 10)
#define I2SCFGR_I2SMOD  (1 << 11)

/* I2S prescaler */
#define I2SPR_MCKOE     (1 << 9)

/* ===== TIM2 ===== */
#define TIM2_BASE       0x40000000U
#define TIM2_CR1        (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_CCMR2      (*(volatile uint32_t *)(TIM2_BASE + 0x1C))
#define TIM2_CCER       (*(volatile uint32_t *)(TIM2_BASE + 0x20))
#define TIM2_ARR        (*(volatile uint32_t *)(TIM2_BASE + 0x2C))
#define TIM2_CCR4       (*(volatile uint32_t *)(TIM2_BASE + 0x40))

/* ===== ADC ===== */
#define ADC_BASE        0x40012400U
#define ADC_ISR         (*(volatile uint32_t *)(ADC_BASE + 0x00))
#define ADC_CR          (*(volatile uint32_t *)(ADC_BASE + 0x08))
#define ADC_CFGR1       (*(volatile uint32_t *)(ADC_BASE + 0x0C))
#define ADC_CFGR2       (*(volatile uint32_t *)(ADC_BASE + 0x10))
#define ADC_SMPR        (*(volatile uint32_t *)(ADC_BASE + 0x14))
#define ADC_CHSELR      (*(volatile uint32_t *)(ADC_BASE + 0x28))
#define ADC_DR          (*(volatile uint32_t *)(ADC_BASE + 0x40))

/* ADC flags */
#define ADC_ISR_ADRDY   (1 << 0)
#define ADC_ISR_EOC     (1 << 2)

/* ADC CR bits */
#define ADC_CR_ADEN     (1 << 0)
#define ADC_CR_ADSTART  (1 << 2)
#define ADC_CR_ADVREGEN (1 << 28)
#define ADC_CR_DEEPPWD  (1 << 29)
#define ADC_CR_ADCAL    (1 << 31)

/* ===== DMA1 ===== */
#define DMA1_BASE       0x40020000U
#define DMA1_ISR        (*(volatile uint32_t *)(DMA1_BASE + 0x00))
#define DMA1_IFCR       (*(volatile uint32_t *)(DMA1_BASE + 0x04))

/* DMA Channel 1 (SPI2_TX) registers */
#define DMA1_CH1_BASE   0x40020008U
#define DMA_CCR(base)   (*(volatile uint32_t *)((base) + 0x08))
#define DMA_CNDTR(base) (*(volatile uint32_t *)((base) + 0x0C))
#define DMA_CPAR(base)  (*(volatile uint32_t *)((base) + 0x10))
#define DMA_CMAR(base)  (*(volatile uint32_t *)((base) + 0x14))

/* DMA CCR bits */
#define DMA_CCR_EN      (1 << 0)
#define DMA_CCR_TCIE    (1 << 1)
#define DMA_CCR_HTIE    (1 << 2)
#define DMA_CCR_DIR     (1 << 4)   /* 1=Mem→Periph, 0=Periph→Mem */
#define DMA_CCR_CIRC    (1 << 5)
#define DMA_CCR_PINC    (1 << 6)
#define DMA_CCR_MINC    (1 << 7)
#define DMA_CCR_MSIZE_16 (1 << 10)  /* 16-bit */
#define DMA_CCR_PSIZE_16 (1 << 8)
#define DMA_CCR_PL_SHIFT 12

/* DMA channel mapping: SPI2_TX = channel 1, SPI2_RX = channel 2 */
#define DMA1_CH1        0x40020008U  /* SPI2_TX */
#define DMA1_CH2        0x4002001CU  /* SPI2_RX */

/* DMA request mapping (RM0461 Table 41) */
#define DMA_REQ_SPI2_TX  0  /* channel 1 default */
#define DMA_REQ_SPI2_RX  0  /* channel 2 default */

/* ===== DMAMUX ===== */
#define DMAMUX1_BASE    0x40020800U
#define DMAMUX_C0CR     (*(volatile uint32_t *)(DMAMUX1_BASE + 0x00))  /* Channel 0 */
#define DMAMUX_C1CR     (*(volatile uint32_t *)(DMAMUX1_BASE + 0x04))  /* Channel 1 */
#define DMAMUX_C2CR     (*(volatile uint32_t *)(DMAMUX1_BASE + 0x08))  /* Channel 2 */

/* DMAMUX request IDs (RM0461 Table 80) */
#define DMAMUX_REQ_SPI2_TX  35  /* SPI2 TX */
#define DMAMUX_REQ_SPI2_RX  36  /* SPI2 RX */

/* ===== SysTick ===== */
#define SYSTICK_BASE    0xE000E010U
#define SYSTICK_CSR     (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_RVR     (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_CVR     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

#define SYSTICK_CSR_ENABLE   (1 << 0)
#define SYSTICK_CSR_TICKINT  (1 << 1)
#define SYSTICK_CSR_CLKSOURCE (1 << 2)
#define SYSTICK_CSR_COUNTFLAG (1 << 16)

/* ===== IWDG ===== */
#define IWDG_BASE       0x40003000U
#define IWDG_KR         (*(volatile uint32_t *)(IWDG_BASE + 0x00))
#define IWDG_PR         (*(volatile uint32_t *)(IWDG_BASE + 0x04))
#define IWDG_RLR        (*(volatile uint32_t *)(IWDG_BASE + 0x08))
#define IWDG_SR         (*(volatile uint32_t *)(IWDG_BASE + 0x0C))

#define IWDG_KR_UNLOCK  0x5555
#define IWDG_KR_RELOAD  0xAAAA
#define IWDG_KR_START   0xCCCC

#endif /* STM32WLE5XX_H */
