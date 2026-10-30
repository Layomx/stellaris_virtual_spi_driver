
/*
 * sd_demo.c - Exercises the SPI driver against the SD card in SPI mode.
 *
 * SD-over-SPI framing (SD Physical Layer spec, SPI mode):
 *   host -> card : 0x40|cmd, arg[31:24..7:0], crc7<<1|1
 *   card -> host : R1 within 8 bytes (bit 7 = 0); 0xFF means "nothing yet"
 * The host keeps clocking 0xFF to read responses. MISO idles high, so a
 * missing card reads as an endless stream of 0xFF.
 */
#include "log.h"
#include "sd_demo.h"
#include "spi.h"

#define SD_DUMMY 0xFFu
#define SD_R1_IDLE 0x01u
#define SD_R1_ILLEGAL 0x04u
#define SD_CMD_START 0x40u
#define SD_TOKEN_BLOCK 0xFEu
#define SD_R1_MAX_POLLS 10u
#define SD_BLOCK_SIZE 512u
#define SD_INIT_SPEED_HZ 300000u  /* spec: <= 400 kHz until init ends */
#define SD_FAST_SPEED_HZ 3000000u /* sysclk / 2                        */
#define SD_ACMD41_TRIES 1000u

/* Fixed CRCs for the only commands where CRC is checked in SPI mode. */
#define CRC_CMD0 0x95u
#define CRC_CMD8 0x87u
#define CRC_DONT_CARE 0x01u

static int sd_transfer(uint8_t data, uint8_t *received)
{
  uint16_t value = SD_DUMMY;
  int status = spi_transfer(data, &value);
  if (status != SPI_OK) {
    return status;
  }
  *received = (uint8_t)value;
  return SPI_OK;
}

static void log_r1(const char *name, uint8_t r1) {
  log_puts("  ");
  log_puts(name);
  log_puts(" -> R1=");
  log_hex8(r1);
  log_puts("\r\n");
}

/* Sends a command and returns R1 (0xFF = no response). CS must be low. */
static sd_result_t sd_send(uint8_t cmd, uint32_t arg, uint8_t crc,
                           uint8_t *response) {
  uint8_t ignored;
  if (sd_transfer((uint8_t)(SD_CMD_START | cmd), &ignored) != SPI_OK ||
      sd_transfer((uint8_t)(arg >> 24), &ignored) != SPI_OK ||
      sd_transfer((uint8_t)(arg >> 16), &ignored) != SPI_OK ||
      sd_transfer((uint8_t)(arg >> 8), &ignored) != SPI_OK ||
      sd_transfer((uint8_t)arg, &ignored) != SPI_OK ||
      sd_transfer(crc, &ignored) != SPI_OK) {
    return SD_SPI_ERROR;
  }

  for (uint32_t i = 0; i < SD_R1_MAX_POLLS; i++) {
    uint8_t r1;
    if (sd_transfer(SD_DUMMY, &r1) != SPI_OK) {
      return SD_SPI_ERROR;
    }
    if ((r1 & 0x80u) == 0u) {
      *response = r1;
      return SD_OK;
    }
  }
  *response = SD_DUMMY;
  return SD_NO_CARD;
}

/*
 * Ends a command. The SD spec asks for 8 extra clocks after the response so
 * the card can finish; we send them with CS still low and then once more with
 * CS high. The first extra byte is REQUIRED by QEMU's ssi-sd model: it does not
 * resynchronise when CS rises, and without it the first byte of the next
 * command is swallowed as "end of previous response". Harmless on real cards.
 */
static sd_result_t sd_end(void) {
  uint8_t ignored;
  if (sd_transfer(SD_DUMMY, &ignored) != SPI_OK) {
    spi_cs_deassert();
    return SD_SPI_ERROR;
  }
  spi_cs_deassert();
  if (sd_transfer(SD_DUMMY, &ignored) != SPI_OK) {
    return SD_SPI_ERROR;
  }
  return SD_OK;
}

static sd_result_t sd_cmd_r1(uint8_t cmd, uint32_t arg, uint8_t crc,
                             uint8_t *response) {
  spi_cs_assert();
  sd_result_t result = sd_send(cmd, arg, crc, response);
  sd_result_t end_result = sd_end();
  return result != SD_OK ? result : end_result;
}

/* R3/R7 responses: R1 followed by 32 bits. */
static sd_result_t sd_cmd_r3r7(uint8_t cmd, uint32_t arg, uint8_t crc,
                               uint32_t *tail, uint8_t *response) {
  spi_cs_assert();
  sd_result_t result = sd_send(cmd, arg, crc, response);
  uint32_t v = 0u;
  if (result == SD_OK && *response != SD_DUMMY) {
    for (int i = 0; i < 4; i++) {
      uint8_t value;
      if (sd_transfer(SD_DUMMY, &value) != SPI_OK) {
        result = SD_SPI_ERROR;
        break;
      }
      v = (v << 8) | value;
    }
  }
  *tail = v;
  sd_result_t end_result = sd_end();
  return result != SD_OK ? result : end_result;
}

static sd_result_t sd_read_block0(void) {
  uint16_t buf[SD_BLOCK_SIZE];

  spi_cs_assert();
  uint8_t r1;
  sd_result_t result = sd_send(17u, 0u, CRC_DONT_CARE, &r1);
  if (result != SD_OK) {
    sd_end();
    return result;
  }
  log_r1("CMD17", r1);
  if (r1 != 0u) {
    sd_end();
    return SD_ERROR;
  }
  uint8_t token = SD_DUMMY;
  for (uint32_t i = 0; i < 2000u && token == SD_DUMMY; i++) {
    if (sd_transfer(SD_DUMMY, &token) != SPI_OK) {
      sd_end();
      return SD_SPI_ERROR;
    }
  }
  if (token != SD_TOKEN_BLOCK) {
    sd_end();
    return SD_ERROR;
  }
  if (spi_read(buf, SD_BLOCK_SIZE, SD_DUMMY) != SPI_OK) {
    sd_end();
    return SD_SPI_ERROR;
  }
  uint8_t ignored;
  if (sd_transfer(SD_DUMMY, &ignored) != SPI_OK ||
      sd_transfer(SD_DUMMY, &ignored) != SPI_OK) {
    sd_end();
    return SD_SPI_ERROR;
  }
  if (sd_end() != SD_OK) {
    return SD_SPI_ERROR;
  }

  log_puts("  block 0 starts with: \"");
  for (uint32_t i = 0; i < 18u; i++) {
    char c[2] = {(char)buf[i], '\0'};
    log_puts(c);
  }
  log_puts("\"\r\n");
  return SD_OK;
}

sd_result_t sd_probe(uint32_t sysclk_hz) {
  uint32_t tail;
  uint8_t r1 = SD_DUMMY;
  sd_result_t result;

  log_puts("  SCK = ");
  log_hex32(spi_set_bit_rate(sysclk_hz, SD_INIT_SPEED_HZ));
  log_puts(" Hz\r\n");

  /* >= 74 clocks with CS high wake the card into SPI-capable state. */
  spi_cs_deassert();
  for (int i = 0; i < 10; i++) {
    uint8_t ignored;
    if (sd_transfer(SD_DUMMY, &ignored) != SPI_OK) {
      return SD_SPI_ERROR;
    }
  }

  for (int tries = 0; tries < 5 && r1 != SD_R1_IDLE; tries++) {
    result = sd_cmd_r1(0u, 0u, CRC_CMD0, &r1); /* GO_IDLE_STATE */
    if (result == SD_SPI_ERROR) {
      return result;
    }
  }
  log_r1("CMD0", r1);
  /* 0xFF: nothing drives MISO (empty bus, QEMU master).
   * 0x04: "illegal command" - what QEMU 8.2.x answers for an SD slot with
   * no image attached. Either way there is no usable card. */
  if (r1 == SD_DUMMY || r1 == SD_R1_ILLEGAL) {
    return SD_NO_CARD;
  }
  if (r1 != SD_R1_IDLE) {
    return SD_ERROR;
  }

  result = sd_cmd_r3r7(8u, 0x000001AAu, CRC_CMD8, &tail, &r1);
  if (result != SD_OK) {
    return result;
  }
  log_r1("CMD8", r1);
  log_puts("  R7 echo = ");
  log_hex32(tail);
  log_puts("\r\n");
  if (r1 != SD_R1_IDLE || (tail & 0xFFFu) != 0x1AAu) {
    return SD_ERROR;
  }

  uint32_t i;
  for (i = 0; i < SD_ACMD41_TRIES; i++) {
    result = sd_cmd_r1(55u, 0u, CRC_DONT_CARE, &r1);
    if (result == SD_SPI_ERROR) {
      return result;
    }
    result = sd_cmd_r1(41u, 0x40000000u, CRC_DONT_CARE, &r1);
    if (result == SD_SPI_ERROR) {
      return result;
    }
    if (r1 != SD_R1_IDLE) {
      break;
    }
  }
  log_r1("ACMD41", r1);
  log_puts("  ACMD41 attempts = ");
  log_hex32(i + 1u);
  log_puts("\r\n");
  if (r1 != 0u) {
    return SD_ERROR;
  }

  result = sd_cmd_r3r7(58u, 0u, CRC_DONT_CARE, &tail, &r1);
  if (result != SD_OK) {
    return result;
  }
  /* QEMU 8.2 hard-codes R1=0x01 for CMD58; a real initialised card sends
   * 0x00 here. Logged, not asserted. */
  log_r1("CMD58", r1);
  log_puts("  OCR = ");
  log_hex32(tail);
  log_puts("\r\n");

  log_puts("  SCK = ");
  log_hex32(spi_set_bit_rate(sysclk_hz, SD_FAST_SPEED_HZ));
  log_puts(" Hz\r\n");

  return sd_read_block0();
}
