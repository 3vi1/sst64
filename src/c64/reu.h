#ifndef SST64_REU_H
#define SST64_REU_H

#include <stdbool.h>
#include <stdint.h>

#define SST64_REU_BYTES 524288UL

bool reu_store(uint32_t address, const void *source, uint16_t length);
bool reu_fetch(void *destination, uint32_t address, uint16_t length);
bool reu_detect_512k(void);

#endif