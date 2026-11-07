#include "reu.h"
#include "reu_text.h"
#include <stdio.h>
#include <string.h>

static uint8_t source[64];
static struct {
    uint8_t before;
    uint8_t bytes[64];
    uint8_t after;
} destination;

static bool round_trip(uint32_t address) {
    uint8_t index;
    for (index = 0; index < sizeof(source); ++index) {
        source[index] = index ^ 0xa5;
    }
    memset(&destination, 0x5a, sizeof(destination));
    return reu_store(address, source, sizeof(source)) &&
           reu_fetch(destination.bytes, address, sizeof(source)) &&
           !memcmp(source, destination.bytes, sizeof(source)) &&
           destination.before == 0x5a && destination.after == 0x5a;
}

int main(void) {
    uint32_t text_length;
    puts("sst64 reu probe");
    if (!reu_detect_512k()) {
        puts("reu missing or less than 512 kib");
        return 1;
    }
    if (!round_trip(0xffe0UL)) {
        puts("reu bank crossing failed");
        return 1;
    }
    if (!round_trip(SST64_REU_BYTES - sizeof(source))) {
        puts("reu final bank failed");
        return 1;
    }
    if (reu_store(SST64_REU_BYTES - 1, source, 2) ||
        reu_fetch(destination.bytes, 0, 0)) {
        puts("reu bounds failed");
        return 1;
    }
    puts("reu 512 kib verified");
    puts("bank crossing and guards passed");
    if (reu_load_text("ENDING,P,R", 0x10000UL, 1, &text_length) ||
        text_length != 0) {
        puts("reu text bounds failed");
        return 1;
    }
    if (!reu_load_text("ENDING,P,R", 0x10000UL, 512, &text_length) ||
        !reu_print_text(0x10000UL, text_length)) {
        puts("reu text load failed");
        return 1;
    }
    puts("disk to reu text passed");
    return 0;
}