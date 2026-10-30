/*
 * main.c - SPI/SSI driver tests.
 *
 *  A. Register state after init.
 *  B. Loopback, 8-bit frames   (SSE/LBM/DSS + FIFO path; real in QEMU).
 *  C. Loopback, 4-bit frames   (DSS masking;             real in QEMU).
 *  D. SD card over SPI         (full protocol; needs -drive in QEMU, else
 *                               reports "no card" - the MISO-high case).
 */
#include "LM3S6965_regs.h"
#include "log.h"
#include "sd_demo.h"
#include "spi.h"
#include <stdbool.h>
#include <stdint.h>

#define SYSCLK_HZ 6000000u /* RCC reset value: 6 MHz crystal, no PLL */

static unsigned failures;

static void check(const char *name, bool ok) {
  log_puts(ok ? "PASS " : "FAIL ");
  log_puts(name);
  log_puts("\r\n");
  if (!ok) {
    failures++;
  }
}

static void print_reg(const char *name, uint32_t offset) {
  log_puts("  ");
  log_puts(name);
  log_puts(" = ");
  log_hex32(*reg(SSI0_BASE, offset));
  log_puts("\r\n");
}

static void dump_ssi(void) {
  print_reg("SSICR0 ", SSI_CR0_OFFSET);
  print_reg("SSICR1 ", SSI_CR1_OFFSET);
  print_reg("SSICPSR", SSI_CPSR_OFFSET);
  print_reg("SSISR  ", SSI_SR_OFFSET);
}

static bool loopback_roundtrip(const uint16_t *pattern, uint32_t n,
                               uint16_t mask) {
  bool ok = true;
  for (uint32_t i = 0; i < n; i++) {
    uint16_t rx = 0u;
    if (spi_transfer(pattern[i], &rx) != SPI_OK ||
        rx != (pattern[i] & mask)) {
      log_puts("  mismatch: tx=");
      log_hex32(pattern[i]);
      log_puts(" rx=");
      log_hex32(rx);
      log_puts("\r\n");
      ok = false;
    }
  }
  return ok;
}

int main(void) {
  static const uint16_t pattern[] = {0xA5, 0x3C, 0x00, 0xFF, 0x81};
  spi_config_t cfg = {
      .bit_rate = 1000000u,
      .mode = SPI_MODE_0,
      .data_bits = 8u,
      .loopback = true,
  };

  log_init();
  log_puts("\r\n=== stellaris-spi-driver (LM3S6965 / QEMU) ===\r\n");

  log_puts("[A] init (8 bits, mode 0, 1 MHz, loopback)\r\n");
  check("spi_init", spi_init(&cfg, SYSCLK_HZ) == SPI_OK);
  dump_ssi();
  check("SR: TX FIFO empty, RX FIFO empty",
        (*reg(SSI0_BASE, SSI_SR_OFFSET) & (SSI_SR_TFE | SSI_SR_RNE)) ==
            SSI_SR_TFE);

  log_puts("[B] loopback 8-bit\r\n");
  check("loopback 8-bit round trip",
        loopback_roundtrip(pattern, sizeof pattern, 0xFFu));
  check("RX FIFO drained", !spi_rx_ready());

  log_puts("[C] loopback 4-bit\r\n");
  cfg.data_bits = 4u;
  check("spi_init 4-bit", spi_init(&cfg, SYSCLK_HZ) == SPI_OK);
  check("loopback 4-bit keeps low nibble only",
        loopback_roundtrip(pattern, sizeof pattern, 0x0Fu));

  log_puts("[C2] loopback 16-bit\r\n");
  cfg.data_bits = 16u;
  check("spi_init 16-bit", spi_init(&cfg, SYSCLK_HZ) == SPI_OK);
  {
    const uint16_t wide_pattern[] = {0xA55Au, 0x1234u, 0xFFFFu};
    check("loopback 16-bit round trip",
          loopback_roundtrip(wide_pattern, sizeof wide_pattern, 0xFFFFu));
  }

  log_puts("[D] SD card over SPI (mode 0, 8-bit, real MISO/MOSI)\r\n");
  cfg.data_bits = 8u;
  cfg.loopback = false;
  cfg.bit_rate = 300000u;
  check("spi_init for SD", spi_init(&cfg, SYSCLK_HZ) == SPI_OK);
  dump_ssi();
  sd_result_t sd = sd_probe(SYSCLK_HZ);
  if (sd == SD_NO_CARD) {
    log_puts("SD: no usable card\r\n");
  } else {
    check("SD init + read block 0", sd == SD_OK);
    if (sd == SD_SPI_ERROR) {
      log_puts("SD: SPI transfer error\r\n");
    }
  }

  log_puts(failures == 0u ? "RESULT: ALL PASS\r\n" : "RESULT: FAILURES\r\n");
  for (;;) {
    __asm__ volatile("wfi");
  }
}
