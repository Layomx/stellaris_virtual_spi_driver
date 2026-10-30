#include "LM3S6965_regs.h"
#include "spi.h"

#define SSI_MAX_RATE_DIV 2u
#define SSI_CPSDVDSR_MAX 254u
#define SSI_SCR_MAX_PLUS_1 256u
#define SPI_TIMEOUT_ITERATIONS 1000000u

static uint16_t data_mask = 0xFFu;

/*
 * SSICLK = sysclk / (CPSDVSR * (1 + SCR)).
 * Pick the smallest even CPSDVSR for which SCR fits in 8 bits. This gives the
 * finest SCR granularity while keeping the result at or below the requested
 * rate.
 */
static int compute_rate(uint32_t sysclk_hz, uint32_t rate, uint32_t *cpsdvsr,
                        uint32_t *scr) {
  if (cpsdvsr == NULL || scr == NULL || sysclk_hz == 0u || rate == 0u ||
      rate > sysclk_hz / SSI_MAX_RATE_DIV) {
    return SPI_ERR_PARAM;
  }
  for (uint32_t div = 2u; div <= SSI_CPSDVDSR_MAX; div += 2u) {
    uint64_t divisor = (uint64_t)div * rate;
    uint32_t scr_plus_1 =
        (uint32_t)(((uint64_t)sysclk_hz + divisor - 1u) / divisor);
    if (scr_plus_1 >= 1u && scr_plus_1 <= SSI_SCR_MAX_PLUS_1) {
      *cpsdvsr = div;
      *scr = scr_plus_1 - 1u;
      return SPI_OK;
    }
  }
  return SPI_ERR_PARAM;
}

static uint32_t build_cr0(const spi_config_t *cfg, uint32_t scr) {
  uint32_t cr0 = SSI_CR0_FRF_FREESCALE |
                 ((uint32_t)(cfg->data_bits - 1u) & SSI_CR0_DSS_MASK) |
                 (scr << SSI_CR0_SCR_SHIFT);

  if (cfg->mode == SPI_MODE_2 || cfg->mode == SPI_MODE_3) {
    cr0 |= SSI_CR0_SPO;
  }
  if (cfg->mode == SPI_MODE_1 || cfg->mode == SPI_MODE_3) {
    cr0 |= SSI_CR0_SPH;
  }
  return cr0;
}

int spi_init(const spi_config_t *cfg, uint32_t sysclk_hz) {
  uint32_t cpsdvsr, scr;

  if (cfg == NULL || cfg->mode > SPI_MODE_3 || cfg->data_bits < 4u ||
      cfg->data_bits > 16u) {
    return SPI_ERR_PARAM;
  }
  if (compute_rate(sysclk_hz, cfg->bit_rate, &cpsdvsr, &scr) != SPI_OK) {
    return SPI_ERR_PARAM;
  }

  // Clock gating: SSI0, GPIOA (SCK/MISO/MOSI), and GPIOD.
  *reg(SYSCTL_BASE, SYSCTL_RCGC1_OFFSET) |= SYSCTL_RCGC1_SSI0;
  *reg(SYSCTL_BASE, SYSCTL_RCGC2_OFFSET) |=
      SYSCTL_RCGC2_GPIOA | SYSCTL_RCGC2_GPIOD;
  (void)*reg(SYSCTL_BASE, SYSCTL_RCGC2_OFFSET); // clock gating delay

  // Pins
  const uint32_t ssi_pins =
      GPIOA_PIN_SSI0CLK | GPIOA_PIN_SSI0RX | GPIOA_PIN_SSI0TX;
  *reg(GPIOA_BASE, GPIO_AFSEL_OFFSET) |= ssi_pins;
  *reg(GPIOA_BASE, GPIO_DEN_OFFSET) |= ssi_pins;

  // CS set the level first, then make the pin an output, so it never glitches
  // low
  *reg(GPIOD_BASE, GPIO_DATA_MASKED(GPIOD_PIN_SD_CS)) = GPIOD_PIN_SD_CS;
  *reg(GPIOD_BASE, GPIO_DIR_OFFSET) |= GPIOD_PIN_SD_CS;
  *reg(GPIOD_BASE, GPIO_DEN_OFFSET) |= GPIOD_PIN_SD_CS;

  // SSI must be disabled while it is configured
  *reg(SSI0_BASE, SSI_CR1_OFFSET) = 0u;

  // prescaler then format/width/rate
  *reg(SSI0_BASE, SSI_CPSR_OFFSET) = cpsdvsr;
  *reg(SSI0_BASE, SSI_CR0_OFFSET) = build_cr0(cfg, scr);
  data_mask = (uint16_t)((1u << cfg->data_bits) - 1u);

  // enable
  *reg(SSI0_BASE, SSI_CR1_OFFSET) =
      SSI_CR1_SSE | (cfg->loopback ? SSI_CR1_LBM : 0u);

  return SPI_OK;
}

uint32_t spi_set_bit_rate(uint32_t sysclk_hz, uint32_t bit_rate) {
  uint32_t cpsdvsr, scr;

  if (compute_rate(sysclk_hz, bit_rate, &cpsdvsr, &scr) != SPI_OK) {
    return 0u;
  }
  uint32_t cr1 = *reg(SSI0_BASE, SSI_CR1_OFFSET);
  *reg(SSI0_BASE, SSI_CR1_OFFSET) = cr1 & ~SSI_CR1_SSE;

  *reg(SSI0_BASE, SSI_CPSR_OFFSET) = cpsdvsr;
  uint32_t cr0 = *reg(SSI0_BASE, SSI_CR0_OFFSET);
  cr0 &= ~(0xFFu << SSI_CR0_SCR_SHIFT);
  *reg(SSI0_BASE, SSI_CR0_OFFSET) = cr0 | (scr << SSI_CR0_SCR_SHIFT);

  *reg(SSI0_BASE, SSI_CR1_OFFSET) = cr1 | SSI_CR1_SSE;
  return sysclk_hz / (cpsdvsr * (1u + scr));
}

bool spi_tx_ready(void) {
  return (*reg(SSI0_BASE, SSI_SR_OFFSET) & SSI_SR_TNF) != 0u;
}

bool spi_rx_ready(void) {
  return (*reg(SSI0_BASE, SSI_SR_OFFSET) & SSI_SR_RNE) != 0u;
}

bool spi_busy(void) {
  return (*reg(SSI0_BASE, SSI_SR_OFFSET) & SSI_SR_BSY) != 0u;
}

int spi_transfer(uint16_t data, uint16_t *received) {
  if (received == NULL) {
    return SPI_ERR_PARAM;
  }

  uint32_t timeout = SPI_TIMEOUT_ITERATIONS;
  while (!spi_tx_ready() && timeout-- != 0u) {
  }
  if (timeout == 0u) {
    return SPI_ERR_TIMEOUT;
  }

  *reg(SSI0_BASE, SSI_DR_OFFSET) = data & data_mask;

  timeout = SPI_TIMEOUT_ITERATIONS;
  while (!spi_rx_ready() && timeout-- != 0u) {
  }
  if (timeout == 0u) {
    return SPI_ERR_TIMEOUT;
  }

  *received = (uint16_t)*reg(SSI0_BASE, SSI_DR_OFFSET) & data_mask;
  return SPI_OK;
}

int spi_write(const uint16_t *data, size_t len) {
  if (data == NULL && len != 0u) {
    return SPI_ERR_PARAM;
  }

  for (size_t i = 0; i < len; i++) {
    uint16_t received;
    int status = spi_transfer(data[i], &received);
    if (status != SPI_OK) {
      return status;
    }
  }
  return SPI_OK;
}

int spi_read(uint16_t *data, size_t len, uint16_t fill) {
  if (data == NULL && len != 0u) {
    return SPI_ERR_PARAM;
  }

  for (size_t i = 0; i < len; i++) {
    int status = spi_transfer(fill, &data[i]);
    if (status != SPI_OK) {
      return status;
    }
  }
  return SPI_OK;
}

void spi_cs_assert(void) {
  *reg(GPIOD_BASE, GPIO_DATA_MASKED(GPIOD_PIN_SD_CS)) = 0u;
}

void spi_cs_deassert(void) {
  *reg(GPIOD_BASE, GPIO_DATA_MASKED(GPIOD_PIN_SD_CS)) = GPIOD_PIN_SD_CS;
}
