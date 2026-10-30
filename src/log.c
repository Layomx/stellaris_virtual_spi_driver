#include "LM3S6965_regs.h"
#include "log.h"
#include <stddef.h>

#define LOG_TIMEOUT_ITERATIONS 1000000u

void log_init(void) {
  *reg(SYSCTL_BASE, SYSCTL_RCGC1_OFFSET) |= SYSCTL_RCGC1_UART0;
  *reg(SYSCTL_BASE, SYSCTL_RCGC2_OFFSET) |= SYSCTL_RCGC2_GPIOA;
  (void)*reg(SYSCTL_BASE, SYSCTL_RCGC2_OFFSET);
  *reg(GPIOA_BASE, GPIO_AFSEL_OFFSET) |= GPIOA_PIN_U0RX | GPIOA_PIN_U0TX;
  *reg(GPIOA_BASE, GPIO_DEN_OFFSET) |= GPIOA_PIN_U0RX | GPIOA_PIN_U0TX;
  *reg(UART0_BASE, UART_CTL_OFFSET) = 0u;
  *reg(UART0_BASE, UART_IBRD_OFFSET) = 3u; /* 6 MHz / (16 * 115200)  */
  *reg(UART0_BASE, UART_FBRD_OFFSET) = 16u;
  *reg(UART0_BASE, UART_LCRH_OFFSET) = UART_LCRH_WLEN_8 | UART_LCRH_FEN;
  *reg(UART0_BASE, UART_CTL_OFFSET) =
      UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;
}

static int putc_blocking(char c) {
  uint32_t timeout = LOG_TIMEOUT_ITERATIONS;

  while ((*reg(UART0_BASE, UART_FR_OFFSET) & UART_FR_TXFF) != 0u &&
         timeout-- != 0u) {
  }
  if (timeout == 0u) {
    return LOG_ERR_TIMEOUT;
  }
  *reg(UART0_BASE, UART_DR_OFFSET) = (uint8_t)c;
  return LOG_OK;
}

int log_puts(const char *s) {
  if (s == NULL) {
    return LOG_ERR_TIMEOUT;
  }
  while (*s != '\0') {
    if (putc_blocking(*s++) != LOG_OK) {
      return LOG_ERR_TIMEOUT;
    }
  }
  return LOG_OK;
}

static int log_hex(uint32_t v, int nibbles) {
  static const char digits[] = "0123456789ABCDEF";

  if (log_puts("0x") != LOG_OK) {
    return LOG_ERR_TIMEOUT;
  }
  for (int i = (nibbles - 1) * 4; i >= 0; i -= 4) {
    if (putc_blocking(digits[(v >> i) & 0xFu]) != LOG_OK) {
      return LOG_ERR_TIMEOUT;
    }
  }
  return LOG_OK;
}

int log_hex8(uint8_t v) { return log_hex(v, 2); }
int log_hex32(uint32_t v) { return log_hex(v, 8); }
