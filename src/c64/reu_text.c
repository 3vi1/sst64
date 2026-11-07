#include "reu_text.h"
#include <cbm.h>

static uint8_t buffer[64];

bool reu_load_text(const char *filename, uint32_t address, uint32_t capacity,
                   uint32_t *length) {
    // ...existing code...
    do {
        uint8_t value = cbm_k_chrin();
        status = cbm_k_readst();
        if (!value || (status & 0xbf) || total == capacity) {
            goto done;
        }
        buffer[used++] = value;
// ...existing code...
    valid = true;
done:
    cbm_k_clrch();
    cbm_k_close(2);
    return valid;
}

bool reu_print_text(uint32_t address, uint32_t length) {
    if (address >= SST64_REU_BYTES || length > SST64_REU_BYTES - address) {
        return false;
    }
    while (length) {
        uint8_t index;
        uint8_t count = length > sizeof(buffer) ? sizeof(buffer) : length;
        if (!reu_fetch(buffer, address, count)) {
            return false;
        }
        for (index = 0; index < count; ++index) {
            uint8_t char_byte = buffer[index];
            // Skip hard returns/form feeds to prevent premature line breaks
            if (char_byte == '\r' || char_byte == '\f') continue;

            // Output the attribute byte first to set color (Green on Black = Attribute 2).
            // This is the requested "CHR$ code" for setting color before printing the character.
            cbm_k_chrout(2); 
            cbm_k_chrout(char_byte);
        }
        address += count;
        length -= count;
    }
    return true;
}