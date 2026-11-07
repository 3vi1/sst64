#ifndef SST64_GAME_PLATFORM_H
#define SST64_GAME_PLATFORM_H

#include <time.h>
#include <stdint.h>
#include "game_math.h"

typedef long clock_t;
#define CLOCKS_PER_SEC 60

void sst64_text(uint16_t message, uint8_t mode);
void sst64_prouts(const char *text);
void sst64_puts(const char *text);
uint8_t sst64_select_help(uint8_t command);
void sst64_boot(void);
void sst64_progress(uint8_t step, uint8_t total);
int sst64_number(const char *text, double *value, int *consumed);
int sst64_selftest(void);

#endif