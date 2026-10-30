// LM3S6965 register map for the peripherals used by this project.
// Based on the TI LM3S6965 datasheet and cross-checked against QEMU.
#ifndef LM3S6965_REGS_H
#define LM3S6965_REGS_H

#include <stdint.h>

// System control
#define SYSCTL_BASE 0x400FE000u
#define SYSCTL_RCC_OFFSET 0x060u
#define SYSCTL_RCGC1_OFFSET 0x104u
#define SYSCTL_RCGC2_OFFSET 0x108u

#define SYSCTL_RCGC1_UART0 (1u << 0)
#define SYSCTL_RCGC1_SSI0 (1u << 4)
#define SYSCTL_RCGC2_GPIOA (1u << 0)
#define SYSCTL_RCGC2_GPIOD (1u << 3)

// GPIO: all ports use the same register layout
#define GPIOA_BASE 0x40004000u
#define GPIOD_BASE 0x40007000u

// Base address of the masked-data window.
#define GPIO_DATA_OFFSET 0x000u

// Set a bit to configure the corresponding pin as an output.
#define GPIO_DIR_OFFSET 0x400u

// Set a bit to assign the corresponding pin to a peripheral.
#define GPIO_AFSEL_OFFSET 0x420u

// Set a bit to enable the digital function for the corresponding pin.
#define GPIO_DEN_OFFSET 0x51Cu

// GPIODATA is a 256-word window indexed by a pin mask.
// Only the pins selected by the mask are read or written.
// This allows atomic pin updates without read-modify-write operations.
#define GPIO_DATA_MASKED(pins) (GPIO_DATA_OFFSET + ((uint32_t)(pins) << 2))

// UART0 pins.
#define GPIOA_PIN_U0RX (1u << 0)
#define GPIOA_PIN_U0TX (1u << 1)
#define GPIOA_PIN_SSI0CLK (1u << 2) // PA2: SSI clock.
#define GPIOA_PIN_SSI0FSS (1u << 3) // PA3: unused; chip select is a GPIO.
#define GPIOA_PIN_SSI0RX (1u << 4)  // PA4: MISO.
#define GPIOA_PIN_SSI0TX (1u << 5)  // PA5: MOSI.
#define GPIOD_PIN_SD_CS (1u << 0)   // PD0: active-low chip select.

// SSI0: ARM PrimeCell PL022-compatible peripheral.
#define SSI0_BASE 0x40008000u
#define SSI_CR0_OFFSET 0x000u  // Control 0: frame format, rate, and width.
#define SSI_CR1_OFFSET 0x004u  // Control 1: enable, master/slave, and loopback.
#define SSI_DR_OFFSET 0x008u   // Data register and FIFO window.
#define SSI_SR_OFFSET 0x00Cu   // Status register.
#define SSI_CPSR_OFFSET 0x010u // Clock prescale divisor.

// SSI control register 0.
#define SSI_CR0_DSS_MASK 0x000Fu        // Data size select: 4 to 16 bits.
#define SSI_CR0_FRF_FREESCALE (0u << 4) // Freescale SPI frame format.
#define SSI_CR0_SPO (1u << 6)           // Clock polarity: SCK idles high.
#define SSI_CR0_SPH (1u << 7) // Clock phase: sample on the second edge.
#define SSI_CR0_SCR_SHIFT 8u  // Serial clock rate field starts at bit 8.

// SSI control register 1.
#define SSI_CR1_LBM (1u << 0) // Enable internal loopback.
#define SSI_CR1_SSE (1u << 1) // Enable the synchronous serial port.
#define SSI_CR1_MS (1u << 2)  // 0 = master, 1 = slave.

// SSI status register.
#define SSI_SR_TFE (1u << 0) // TX FIFO is empty.
#define SSI_SR_TNF (1u << 1) // TX FIFO is not full.
#define SSI_SR_RNE (1u << 2) // RX FIFO is not empty.
#define SSI_SR_RFF (1u << 3) // RX FIFO is full.
#define SSI_SR_BSY (1u << 4) // SSI is transmitting or receiving.

// UART0: registers required by the logger.
#define UART0_BASE 0x4000C000u
#define UART_DR_OFFSET 0x000u
#define UART_FR_OFFSET 0x018u
#define UART_IBRD_OFFSET 0x024u
#define UART_FBRD_OFFSET 0x028u
#define UART_LCRH_OFFSET 0x02Cu
#define UART_CTL_OFFSET 0x030u
#define UART_FR_TXFF (1u << 5)
#define UART_LCRH_WLEN_8 (3u << 5)
#define UART_LCRH_FEN (1u << 4)
#define UART_CTL_UARTEN (1u << 0)
#define UART_CTL_TXE (1u << 8)
#define UART_CTL_RXE (1u << 9)

// Access a memory-mapped register.
// Volatile forces a real bus access for every read and write.
static inline volatile uint32_t *reg(uint32_t base, uint32_t offset) {
  return (volatile uint32_t *)(uintptr_t)(base + offset);
}

#endif
