#ifndef LOG_H
#define LOG_H

#include <stdint.h>

#define LOG_OK 0
#define LOG_ERR_TIMEOUT (-1)

void log_init(void);
int log_puts(const char *text);
int log_hex8(uint8_t value);
int log_hex32(uint32_t value);

#endif
