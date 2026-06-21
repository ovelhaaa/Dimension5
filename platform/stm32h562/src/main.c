#include <stdint.h>
#include "dimension_dsp.h"

// STM32H5 Baremetal Headers
#define RCC_BASE      0x44020C00U
#define RCC_CR        (*(volatile uint32_t*)(RCC_BASE + 0x00U))
#define RCC_CFGR      (*(volatile uint32_t*)(RCC_BASE + 0x1CU))
#define RCC_PLL2CFGR  (*(volatile uint32_t*)(RCC_BASE + 0x34U))
#define RCC_PLL2DIVR  (*(volatile uint32_t*)(RCC_BASE + 0x38U))
#define RCC_PLL2FRACR (*(volatile uint32_t*)(RCC_BASE + 0x3CU))
#define RCC_AHB1ENR   (*(volatile uint32_t*)(RCC_BASE + 0x0D8U))
#define RCC_AHB2ENR   (*(volatile uint32_t*)(RCC_BASE + 0x0DCU))
#define RCC_APB1LENR  (*(volatile uint32_t*)(RCC_BASE + 0x0E8U))
#define RCC_APB2ENR   (*(volatile uint32_t*)(RCC_BASE + 0x0F0U))

#define GPIOA_BASE    0x42020000U
#define GPIOA_MODER   (*(volatile uint32_t*)(GPIOA_BASE + 0x00U))
#define GPIOA_OTYPER  (*(volatile uint32_t*)(GPIOA_BASE + 0x04U))
#define GPIOA_OSPEEDR (*(volatile uint32_t*)(GPIOA_BASE + 0x08U))
#define GPIOA_AFRL    (*(volatile uint32_t*)(GPIOA_BASE + 0x20U))
#define GPIOA_AFRH    (*(volatile uint32_t*)(GPIOA_BASE + 0x24U))

#define GPIOC_BASE    0x42020800U
#define GPIOC_MODER   (*(volatile uint32_t*)(GPIOC_BASE + 0x00U))
#define GPIOC_OSPEEDR (*(volatile uint32_t*)(GPIOC_BASE + 0x08U))
#define GPIOC_AFRL    (*(volatile uint32_t*)(GPIOC_BASE + 0x20U))
#define GPIOC_AFRH    (*(volatile uint32_t*)(GPIOC_BASE + 0x24U))

#define I2S1_BASE     0x40013000U
#define SPI1_CR1      (*(volatile uint32_t*)(I2S1_BASE + 0x00U))
#define SPI1_CR2      (*(volatile uint32_t*)(I2S1_BASE + 0x04U))
#define SPI1_CFG1     (*(volatile uint32_t*)(I2S1_BASE + 0x08U))
#define SPI1_CFG2     (*(volatile uint32_t*)(I2S1_BASE + 0x0CU))
#define SPI1_IER      (*(volatile uint32_t*)(I2S1_BASE + 0x10U))
#define SPI1_SR       (*(volatile uint32_t*)(I2S1_BASE + 0x14U))
#define SPI1_I2SCFGR  (*(volatile uint32_t*)(I2S1_BASE + 0x50U))
#define SPI1_TXDR     (*(volatile uint32_t*)(I2S1_BASE + 0x20U))

#define I2S2_BASE     0x40003800U
#define SPI2_CR1      (*(volatile uint32_t*)(I2S2_BASE + 0x00U))
#define SPI2_CR2      (*(volatile uint32_t*)(I2S2_BASE + 0x04U))
#define SPI2_CFG1     (*(volatile uint32_t*)(I2S2_BASE + 0x08U))
#define SPI2_CFG2     (*(volatile uint32_t*)(I2S2_BASE + 0x0CU))
#define SPI2_IER      (*(volatile uint32_t*)(I2S2_BASE + 0x10U))
#define SPI2_SR       (*(volatile uint32_t*)(I2S2_BASE + 0x14U))
#define SPI2_I2SCFGR  (*(volatile uint32_t*)(I2S2_BASE + 0x50U))
#define SPI2_RXDR     (*(volatile uint32_t*)(I2S2_BASE + 0x30U))

#define GPDMA1_BASE   0x40020000U
#define GPDMA1_C0CR   (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x00U))
#define GPDMA1_C0TR1  (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x04U))
#define GPDMA1_C0TR2  (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x08U))
#define GPDMA1_C0BR1  (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x0CU))
#define GPDMA1_C0SAR  (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x10U))
#define GPDMA1_C0DAR  (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x14U))
#define GPDMA1_C0SR   (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x24U))
#define GPDMA1_C0FCR  (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x28U))
#define GPDMA1_C0LLR  (*(volatile uint32_t*)(GPDMA1_BASE + 0x50U + 0x2CU))

#define GPDMA1_C1CR   (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x00U))
#define GPDMA1_C1TR1  (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x04U))
#define GPDMA1_C1TR2  (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x08U))
#define GPDMA1_C1BR1  (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x0CU))
#define GPDMA1_C1SAR  (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x10U))
#define GPDMA1_C1DAR  (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x14U))

#define NVIC_ISER0    (*(volatile uint32_t*)(0xE000E100U))
#define NVIC_ISER1    (*(volatile uint32_t*)(0xE000E104U))
#define NVIC_ISER2    (*(volatile uint32_t*)(0xE000E108U))

extern void Dimension_ExampleInit(void);
extern void Audio_ProcessHalfBuffer(int32_t* rx, int32_t* tx, uint32_t frames);

#define BUFFER_SAMPLES (DIMENSION_MAX_BLOCK_SIZE * 2)

static int32_t rx_buf[2][BUFFER_SAMPLES];
static int32_t tx_buf[2][BUFFER_SAMPLES];

volatile uint32_t dma_rx_ht = 0;
volatile uint32_t dma_rx_tc = 0;

extern uint32_t _estack;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _sidata;

void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    uint32_t *dest = &_sdata;
    while (dest < &_edata) *dest++ = *src++;
    dest = &_sbss;
    while (dest < &_ebss) *dest++ = 0;

    // FPU Enable
    *(volatile uint32_t*)0xE000ED88 |= (0xF << 20);

    int main(void);
    main();
    while (1);
}

void GPDMA1_Channel0_IRQHandler(void);

__attribute__((section(".isr_vector")))
void (*const g_pfnVectors[])(void) = {
    (void*)&_estack, Reset_Handler, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    GPDMA1_Channel0_IRQHandler, // Vector 48 (IRQ 32 for GPDMA1_CH0 on STM32H5)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void SystemClock_Config(void) {
    // 25MHz HSE to 250MHz sysclock, and precise PLL2 for audio
    RCC_CR |= (1 << 16); // HSEON
    while((RCC_CR & (1 << 17)) == 0); // HSERDY

    // Configure PLL2 for I2S. We need 48kHz. Assuming 24-bit I2S -> 32-bit slot = 64 bits/frame
    // MCK = 256 * Fs = 12.288 MHz
    // Using HSE 25MHz: PLL2M = 25, PLL2N = 98, PLL2P = 8 -> 12.25 MHz (Good enough for this baremetal test without fractionals)
    RCC_PLL2CFGR = (0 << 16) | (1 << 4);
    RCC_PLL2DIVR = ((8 - 1) << 9) | ((98 - 1) << 0);

    RCC_CR |= (1 << 26);
    while((RCC_CR & (1 << 27)) == 0);
}

void Periph_Init(void) {
    RCC_AHB2ENR |= (1 << 0) | (1 << 2);
    RCC_APB2ENR |= (1 << 12);
    RCC_APB1LENR |= (1 << 14);
    RCC_AHB1ENR |= (1 << 0);

    // PA0 Output (Bench)
    GPIOA_MODER &= ~0x00000003;
    GPIOA_MODER |=  0x00000001;

    // I2S1 TX (PA4 WS, PA5 CK, PA6 MCK, PA7 SD)
    GPIOA_MODER &= ~0x0000FF00;
    GPIOA_MODER |=  0x0000AA00;
    GPIOA_AFRL &= ~0xFFF00000;
    GPIOA_AFRL |=  0x55500000;

    // I2S2 RX (PC1 SD, PC2 ext_SD, PC3 CK) (Arbitrary examples, adjusting AF5 mapping)
    GPIOC_MODER &= ~0x000000FC;
    GPIOC_MODER |=  0x000000A8;
    GPIOC_AFRL &= ~0x0000FF00;
    GPIOC_AFRL |=  0x00005500;

    // SPI1_I2SCFGR: I2SMOD(5)=1, I2SCFG(1:2)=2(Master TX), DATLEN(24bit)=1, CHLEN(32bit)=1, MCKOE=1
    SPI1_I2SCFGR = (1 << 9) | (1 << 5) | (2 << 1) | (1 << 2) | (1 << 0);
    SPI1_CFG1 = (7 << 16) | (1 << 14) | (1 << 15); // 32-bit data size, TXDMAEN
    SPI1_CR1 = (1 << 0);

    // SPI2_I2SCFGR: Master RX or Slave RX. Make it Slave RX (3) to sync to SPI1 via external wire
    SPI2_I2SCFGR = (1 << 5) | (3 << 1) | (1 << 2) | (1 << 0);
    SPI2_CFG1 = (7 << 16) | (1 << 14); // RXDMAEN
    SPI2_CR1 = (1 << 0);

    // GPDMA1 CH0 (RX)
    GPDMA1_C0CR = 0;
    GPDMA1_C0TR1 = (2 << 16) | (2 << 18) | (1 << 1) | (0 << 0); // Dest Inc
    GPDMA1_C0TR2 = (1 << 1) | (1 << 0); // HTIE | TCIE
    GPDMA1_C0TR2 |= (18 << 24); // REQSEL = 18 for SPI2_RX on STM32H562
    GPDMA1_C0BR1 = sizeof(rx_buf);
    GPDMA1_C0SAR = (uint32_t)&SPI2_RXDR;
    GPDMA1_C0DAR = (uint32_t)rx_buf;
    GPDMA1_C0LLR = (1 << 16); // Circular mode (dummy/simplified LLR config)
    GPDMA1_C0CR = (1 << 0);

    // GPDMA1 CH1 (TX)
    GPDMA1_C1CR = 0;
    GPDMA1_C1TR1 = (2 << 16) | (2 << 18) | (0 << 1) | (1 << 0); // Src Inc
    GPDMA1_C1TR2 = (1 << 1) | (1 << 0);
    GPDMA1_C1TR2 |= (15 << 24); // REQSEL = 15 for SPI1_TX
    GPDMA1_C1BR1 = sizeof(tx_buf);
    GPDMA1_C1SAR = (uint32_t)tx_buf;
    GPDMA1_C1DAR = (uint32_t)&SPI1_TXDR;
    (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x2CU)) = (1 << 16);
    GPDMA1_C1CR = (1 << 0);

    // Enable IRQ32 in NVIC
    NVIC_ISER1 |= (1 << 0);

    SPI2_CR1 |= (1 << 1); // CSTART Slave
    SPI1_CR1 |= (1 << 1); // CSTART Master
}

void GPDMA1_Channel0_IRQHandler(void) {
    uint32_t sr = GPDMA1_C0SR;
    if (sr & (1 << 9)) {
        GPDMA1_C0FCR = (1 << 9);
        dma_rx_ht = 1;
    }
    if (sr & (1 << 8)) {
        GPDMA1_C0FCR = (1 << 8);
        dma_rx_tc = 1;
    }
}

int main(void) {
    SystemClock_Config();
    Dimension_ExampleInit();
    Periph_Init();

    while(1) {
        if (dma_rx_ht) {
            dma_rx_ht = 0;
            Audio_ProcessHalfBuffer(rx_buf[0], tx_buf[0], DIMENSION_MAX_BLOCK_SIZE);
        }
        if (dma_rx_tc) {
            dma_rx_tc = 0;
            Audio_ProcessHalfBuffer(rx_buf[1], tx_buf[1], DIMENSION_MAX_BLOCK_SIZE);
        }
    }
    return 0;
}
