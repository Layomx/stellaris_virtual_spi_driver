#ifndef SPI_H
#define SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * SPI mode:
 *   CPOL (clock polarity) selects the idle level of SCK:
 *     0 = SCK idles low, 1 = SCK idles high.
 *   CPHA (clock phase) selects the edge used to sample data:
 *     0 = sample on the first edge, 1 = sample on the second edge.
 */
typedef enum {
  SPI_MODE_0, // CPOL = 0, CPHA = 0: idle low, sample on rising edge.
  SPI_MODE_1, // CPOL = 0, CPHA = 1: idle low, sample on falling edge.
  SPI_MODE_2, // CPOL = 1, CPHA = 0: idle high, sample on falling edge.
  SPI_MODE_3  // CPOL = 1, CPHA = 1: idle high, sample on rising edge.
} spi_mode_t;

typedef struct {
  uint32_t bit_rate; // SCK clock frequency in Hz.
  spi_mode_t mode;   // Clock polarity and phase, using one of the four modes.
  uint8_t data_bits; // Number of bits in each data frame.
  bool loopback;     // Enable internal TX-to-RX loopback for self-tests.
} spi_config_t;

#define SPI_OK 0
#define SPI_ERR_PARAM (-1)
#define SPI_ERR_TIMEOUT (-2)

// Configures SSI0 as master on PA2/PA4/PA5 and PD0 as a software chip select
int spi_init(const spi_config_t *cfg, uint32_t sysclk_hz);

// Re-programs SCK. Returns the achieve rate in Hz, 0 on error
uint32_t spi_set_bit_rate(uint32_t sysclk_hz, uint32_t bit_rate);

bool spi_tx_ready(void);
bool spi_rx_ready(void);
bool spi_busy(void);

// Full duplex: shift 'data' out on MOSI while shifting a frame in on MISO
int spi_transfer(uint16_t data, uint16_t *received);
int spi_write(const uint16_t *data, size_t len);
int spi_read(uint16_t *data, size_t len, uint16_t fill);

void spi_cs_assert(void);
void spi_cs_deassert(void);

#endif
