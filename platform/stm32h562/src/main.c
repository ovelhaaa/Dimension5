#include <stdint.h>
#include "dimension_dsp.h"

// STM32H5 Baremetal Headers
#define FLASH_BASE    0x40022000U
#define FLASH_ACR     (*(volatile uint32_t*)(FLASH_BASE + 0x00U))

#define PWR_BASE      0x44020800U
#define PWR_VOSCR     (*(volatile uint32_t*)(PWR_BASE + 0x10U))
#define PWR_VOSSR     (*(volatile uint32_t*)(PWR_BASE + 0x14U))

#define RCC_BASE      0x44020C00U
#define RCC_CR        (*(volatile uint32_t*)(RCC_BASE + 0x00U))
#define RCC_CFGR      (*(volatile uint32_t*)(RCC_BASE + 0x1CU))
#define RCC_PLL1CFGR  (*(volatile uint32_t*)(RCC_BASE + 0x28U))
#define RCC_PLL1DIVR  (*(volatile uint32_t*)(RCC_BASE + 0x2CU))
#define RCC_PLL1FRACR (*(volatile uint32_t*)(RCC_BASE + 0x30U))
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

typedef struct {
    uint32_t CTR1;
    uint32_t CTR2;
    uint32_t BR1;
    uint32_t SAR;
    uint32_t DAR;
    uint32_t LLR;
} GPDMA_Node;

static int32_t rx_buf[2][BUFFER_SAMPLES] __attribute__((aligned(32)));
static int32_t tx_buf[2][BUFFER_SAMPLES] __attribute__((aligned(32)));

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
    __asm volatile ("dsb");
    __asm volatile ("isb");

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

#define CLOCK_TIMEOUT 0xFFFFFFU

void SystemClock_Config(void) {
    uint32_t timeout = 0;

    // Enable HSE
    RCC_CR |= (1 << 16); // HSEON
    timeout = 0;
    while((RCC_CR & (1 << 17)) == 0) { // HSERDY
        if (++timeout > CLOCK_TIMEOUT) return;
    }

    // Power Scaling VOS0
    PWR_VOSCR |= (0 << 4);
    timeout = 0;
    while((PWR_VOSSR & (1 << 4)) == 0) {
        if (++timeout > CLOCK_TIMEOUT) return;
    }

    // Flash Wait States
    FLASH_ACR = (FLASH_ACR & ~(0x1F)) | 5; // 5 WS for 250MHz at VOS0

    // Configure PLL1 for System Clock (250 MHz)
    // HSE = 25 MHz. PLL1M = 5 (5MHz ref), PLL1N = 100 (500MHz vco), PLL1P = 2 (250MHz sysclk)
    RCC_PLL1CFGR = (2 << 2) | (0 << 16) | (1 << 4); // Source HSE, DIVP enable, frac disable
    RCC_PLL1DIVR = ((2 - 1) << 9) | ((100 - 1) << 0);
    RCC_PLL1CFGR |= (5 << 8); // M = 5

    RCC_CR |= (1 << 24); // PLL1ON
    timeout = 0;
    while((RCC_CR & (1 << 25)) == 0) {
        if (++timeout > CLOCK_TIMEOUT) return;
    }

    // Switch SYSCLK to PLL1
    RCC_CFGR |= 3;
    timeout = 0;
    while(((RCC_CFGR >> 3) & 7) != 3) {
        if (++timeout > CLOCK_TIMEOUT) return;
    }

    // Configure PLL2 for I2S. We need 48kHz. Assuming 24-bit I2S -> 32-bit slot = 64 bits/frame
    // MCK = 256 * Fs = 12.288 MHz
    // Using HSE 25MHz: PLL2M = 25, PLL2N = 98, PLL2P = 8 -> 12.25 MHz (Good enough for this baremetal test without fractionals)
    RCC_PLL2CFGR = (0 << 16) | (1 << 4);
    RCC_PLL2DIVR = ((8 - 1) << 9) | ((98 - 1) << 0);

    RCC_CR |= (1 << 26);
    timeout = 0;
    while((RCC_CR & (1 << 27)) == 0) {
        if (++timeout > CLOCK_TIMEOUT) return;
    }
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
    GPIOA_AFRL &= ~0xFFFF0000;
    GPIOA_AFRL |=  0x55550000;

    // I2S2 RX (PC1 SD, PC2 ext_SD, PC3 CK)
    GPIOC_MODER &= ~0x000000FC;
    GPIOC_MODER |=  0x000000A8;
    GPIOC_AFRL &= ~0x0000FFF0;
    GPIOC_AFRL |=  0x00005550;

    // SPI1_I2SCFGR: I2SMOD(5)=1, I2SCFG(1:2)=2(Master TX), DATLEN(24bit)=1, CHLEN(32bit)=1, MCKOE=1
    SPI1_I2SCFGR = (1 << 9) | (1 << 5) | (2 << 1) | (1 << 2) | (1 << 0);
    SPI1_CFG1 = (7 << 16) | (1 << 14) | (1 << 15); // 32-bit data size, TXDMAEN
    SPI1_CR1 = (1 << 0);

    // SPI2_I2SCFGR: Master RX or Slave RX. Make it Slave RX (3) to sync to SPI1 via external wire
    SPI2_I2SCFGR = (1 << 5) | (3 << 1) | (1 << 2) | (1 << 0);
    SPI2_CFG1 = (7 << 16) | (1 << 14); // RXDMAEN
    SPI2_CR1 = (1 << 0);

    static GPDMA_Node rx_node __attribute__((aligned(32)));
    static GPDMA_Node tx_node __attribute__((aligned(32)));

    // GPDMA1 CH0 (RX) Circular Configuration via LLR
    rx_node.CTR1 = (2 << 16) | (2 << 18) | (1 << 11); // 32-bit src/dest, Dest Inc (bit 11)
    rx_node.CTR2 = 18; // REQSEL = 18 for SPI2_RX
    rx_node.BR1 = sizeof(rx_buf);
    rx_node.SAR = (uint32_t)&SPI2_RXDR;
    rx_node.DAR = (uint32_t)rx_buf;
    rx_node.LLR = ((uint32_t)&rx_node & 0xFFFFFFFCU) | 0x003F0000U; // Loop to itself, update all registers

    GPDMA1_C0CR = 0;
    GPDMA1_C0TR1 = rx_node.CTR1;
    GPDMA1_C0TR2 = rx_node.CTR2;
    GPDMA1_C0BR1 = rx_node.BR1;
    GPDMA1_C0SAR = rx_node.SAR;
    GPDMA1_C0DAR = rx_node.DAR;
    GPDMA1_C0LLR = rx_node.LLR;
    GPDMA1_C0CR = (1 << 9) | (1 << 8) | (1 << 0); // HTIE | TCIE | EN

    // GPDMA1 CH1 (TX) Circular Configuration via LLR
    tx_node.CTR1 = (2 << 16) | (2 << 18) | (1 << 3); // 32-bit src/dest, Src Inc (bit 3)
    tx_node.CTR2 = 15; // REQSEL = 15 for SPI1_TX
    tx_node.BR1 = sizeof(tx_buf);
    tx_node.SAR = (uint32_t)tx_buf;
    tx_node.DAR = (uint32_t)&SPI1_TXDR;
    tx_node.LLR = ((uint32_t)&tx_node & 0xFFFFFFFCU) | 0x003F0000U; // Loop to itself, update all registers

    GPDMA1_C1CR = 0;
    GPDMA1_C1TR1 = tx_node.CTR1;
    GPDMA1_C1TR2 = tx_node.CTR2;
    GPDMA1_C1BR1 = tx_node.BR1;
    GPDMA1_C1SAR = tx_node.SAR;
    GPDMA1_C1DAR = tx_node.DAR;
    (*(volatile uint32_t*)(GPDMA1_BASE + 0xD0U + 0x2CU)) = tx_node.LLR;
    GPDMA1_C1CR = (1 << 0); // EN

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
