#ifndef SD_DEMO_H
#define SD_DEMO_H

#include <stdint.h>

typedef enum {
  SD_OK,
  SD_NO_CARD,
  SD_ERROR,
  SD_SPI_ERROR
} sd_result_t;

sd_result_t sd_probe(uint32_t sysclk_hz);

#endif
