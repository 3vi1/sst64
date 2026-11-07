#ifndef SST64_REU_TEXT_H
#define SST64_REU_TEXT_H

#include "reu.h"

bool reu_load_text(const char *filename, uint32_t address, uint32_t capacity,
                   uint32_t *length);
bool reu_print_text(uint32_t address, uint32_t length);

#endif