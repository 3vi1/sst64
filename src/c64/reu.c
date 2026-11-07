#include "reu.h"
#include <stddef.h>

#define REU_REGISTERS ((volatile uint8_t *)0xdf00)

static bool transfer(uint32_t address, const void *buffer, uint16_t length,
                     uint8_t command) {
    uintptr_t local = (uintptr_t)buffer;
    uint8_t status;
    if (!length || address >= SST64_REU_BYTES ||
        length > SST64_REU_BYTES - address || local < 2 ||
        local >= 0xd000 || length > 0xd000 - local) {
        return false;
    }
    __asm__ volatile("php\nsei" ::: "memory");
    status = REU_REGISTERS[0];
    REU_REGISTERS[9] = 0;
    REU_REGISTERS[10] = 0;
    REU_REGISTERS[2] = (uint8_t)local;
    REU_REGISTERS[3] = (uint8_t)(local >> 8);
    REU_REGISTERS[4] = (uint8_t)address;
    REU_REGISTERS[5] = (uint8_t)(address >> 8);
    REU_REGISTERS[6] = (uint8_t)(address >> 16);
    REU_REGISTERS[7] = (uint8_t)length;
    REU_REGISTERS[8] = (uint8_t)(length >> 8);
    REU_REGISTERS[1] = command;
    status = REU_REGISTERS[0];
    __asm__ volatile("plp" ::: "memory");
    return (status & 0x40) != 0;
}

bool reu_store(uint32_t address, const void *source, uint16_t length) {
    return transfer(address, source, length, 0x90);
}

bool reu_fetch(void *destination, uint32_t address, uint16_t length) {
    return transfer(address, destination, length, 0x91);
}

bool reu_detect_512k(void) {
    uint8_t original[4];
    uint8_t marker;
    uint8_t actual;
    uint8_t bank;
    bool valid = true;
    for (bank = 0; bank < 4; ++bank) {
        if (!reu_fetch(&original[bank], (uint32_t)bank * 131072UL, 1)) {
            return false;
        }
    }
    for (bank = 0; bank < 4; ++bank) {
        marker = 0x31 + bank;
        if (!reu_store((uint32_t)bank * 131072UL, &marker, 1)) {
            valid = false;
        }
    }
    for (bank = 0; bank < 4; ++bank) {
        actual = 0;
        if (!reu_fetch(&actual, (uint32_t)bank * 131072UL, 1) ||
            actual != 0x31 + bank) {
            valid = false;
        }
    }
    for (bank = 0; bank < 4; ++bank) {
        if (!reu_store((uint32_t)bank * 131072UL, &original[bank], 1)) {
            valid = false;
        }
    }
    return valid;
}