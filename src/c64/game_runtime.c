#include "game_platform.h"
#include "reu.h"
#include "sst.h"
#include <cbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <lz4.h>
#include "game-text.h"

#define TEXT_BASE 393216UL
#define HELP_BASE 425984UL
#define HELP_CACHE 524240UL
#define SCREEN_COLUMNS 40

static uint32_t help_length;
static uint32_t help_position;
static uint32_t help_start;
static uint32_t help_end;
extern uint8_t sst64_active_bank;
static uint8_t disk_eof[8];
static uint8_t disk_used[8];
static uint8_t boot_eof;
static uint8_t input_line_start = 1;
static uint8_t output_column;
static uint8_t output_lines;
static uint8_t line_indent;
#define PAGE_LINES 22
static uint8_t output_wrapped;
static unsigned int output_spaces;
static char text_buffer[64];
int __getchar(void);

static void sst64_set_text_color(uint8_t color) {
    *(volatile uint8_t *)0x0286 = color;
}

static void sst64_set_screen_colors(void) {
    *(volatile uint8_t *)0xd020 = 0;
    *(volatile uint8_t *)0xd021 = 0;
    sst64_set_text_color(5);
}

static void sst64_more_prompt(void) {
    static const char message[] = "-- MORE --";
    uint8_t i;
    sst64_set_text_color(7);
    for (i = 0; message[i]; ++i) cbm_k_chrout(message[i]);
    while (cbm_k_getin() == 0) {}
    for (i = 0; i < 10; ++i) cbm_k_chrout(157);
    for (i = 0; i < 10; ++i) cbm_k_chrout(' ');
    for (i = 0; i < 10; ++i) cbm_k_chrout(157);
    sst64_set_text_color(5);
    output_lines = 0;
}

static void sst64_output_byte(uint8_t value, uint8_t slow) {
    cbm_k_chrout(value);
    if (slow) {
        long end_time = clock() + CLOCKS_PER_SEC * 0.05;
        while (clock() < end_time) {}
    }
    if (value == 147) {
        output_column = 0;
        output_wrapped = 0;
        output_lines = 0;
    } else if (value == 13) {
        output_column = 0;
        output_wrapped = 0;
        if (++output_lines >= PAGE_LINES) sst64_more_prompt();
    } else if (value == 157) {
        if (output_column) --output_column;
    } else if (value >= 32) {
        if (++output_column == SCREEN_COLUMNS) {
            output_column = 0;
            output_wrapped = 1;
            if (++output_lines >= PAGE_LINES) sst64_more_prompt();
        } else {
            output_wrapped = 0;
        }
    }
}

static uint8_t sst64_convert_ascii(uint8_t value) {
    if (value == '\n') return 13;
    if (value == '\f') return 147;
    if (value == '\b') return 157;
    if (value >= 'a' && value <= 'z') return value - 32;
    return value;
}

static uint8_t sst64_word_break(uint8_t value) {
    return value == ' ' || value == 13 || value == 147 || value == 157;
}

static uint8_t sst64_word_color(const uint8_t *text, unsigned int word_len, uint8_t convert_ascii) {
    char word[17];
    char *name = word;
    unsigned int i;
    if (word_len < 3 || word_len >= sizeof(word)) return 0;
    for (i = 0; i < word_len; ++i)
        word[i] = convert_ascii ? sst64_convert_ascii(text[i]) : (text[i] >= 'a' && text[i] <= 'z' ? text[i] - 32 : text[i]);
    word[word_len] = 0;
    if (!strcmp(word, "COMMAND>")) return 15;
    if (!strcmp(word, "RED")) return 2;
    if (!strcmp(word, "YELLOW")) return 7;
    if (!strcmp(word, "GREEN")) return 13;
    while (*name == '"' || *name == '\'' || *name == '(') ++name;
    i = strlen(name);
    while (i && (name[i - 1] == '.' || name[i - 1] == ',' ||
                 name[i - 1] == '"' || name[i - 1] == '\'' ||
                 name[i - 1] == ')' || name[i - 1] == '!' ||
                 name[i - 1] == '?' || name[i - 1] == ';' ||
                 name[i - 1] == ':')) name[--i] = 0;
    if (i >= 2 && name[i - 2] == '\'' && name[i - 1] == 'S') name[i - 2] = 0;
    if (!strcmp(name, "ENTERPRISE") || !strcmp(name, "FAERIE") ||
        !strcmp(name, "QUEENE")) return 1;
    return 0;
}

static void sst64_start_line_indent(const uint8_t *text, unsigned int position, unsigned int text_length,
                                    uint8_t convert_ascii) {
    unsigned int scan = position;
    line_indent = output_spaces;
    while (scan + 2 < text_length) {
        uint8_t value = convert_ascii ? sst64_convert_ascii(text[scan]) : text[scan];
        if (value == 13 || value == 147) break;
        if (text[scan] == ' ' && text[scan + 1] == '-' && text[scan + 2] == ' ') {
            unsigned int hanging = output_spaces + (scan - position) + 3;
            if (hanging <= 24) line_indent = hanging;
            break;
        }
        ++scan;
    }
}

static uint8_t pending_word[40];
static uint8_t pending_length;

static void sst64_output_chunk(const uint8_t *text, unsigned int text_length,
                               uint8_t convert_ascii, uint8_t slow, uint8_t defer) {
    unsigned int position = 0;
    while (position < text_length) {
        uint8_t value = text[position];
        if (convert_ascii) value = sst64_convert_ascii(value);
        if (value == ' ') {
            if (output_spaces < SCREEN_COLUMNS) ++output_spaces;
            ++position;
            continue;
        }
        if (value == 13 || value == 147 || value == 157) {
            output_spaces = 0;
            if (value != 157) line_indent = 0;
            sst64_output_byte(value, slow);
            ++position;
            continue;
        }

        unsigned int end = position;
        while (end < text_length) {
            uint8_t next = text[end];
            if (convert_ascii) next = sst64_convert_ascii(next);
            if (sst64_word_break(next)) break;
            ++end;
        }
        unsigned int word_length = end - position;
        if (defer && end == text_length && word_length < sizeof(pending_word)) {
            memcpy(pending_word, text + position, word_length);
            pending_length = word_length;
            return;
        }
        if (output_wrapped && !output_column) output_spaces = 0;
        else if (!output_column) sst64_start_line_indent(text, position, text_length, convert_ascii);
        if (output_column &&
            output_column + output_spaces + word_length >= SCREEN_COLUMNS) {
            output_spaces = 0;
            sst64_output_byte(13, 0);
            output_wrapped = 1;
            while (output_column < line_indent) sst64_output_byte(' ', 0);
        } else {
            while (output_spaces) {
                sst64_output_byte(' ', slow);
                --output_spaces;
            }
        }
        uint8_t color = sst64_word_color(text + position, word_length, convert_ascii);
        if (color) sst64_set_text_color(color);
        while (position < end) {
            value = text[position++];
            if (convert_ascii) value = sst64_convert_ascii(value);
            sst64_output_byte(value, slow);
        }
        if (color) sst64_set_text_color(5);
    }
}

static void sst64_flush_pending(void) {
    uint8_t length_now = pending_length;
    pending_length = 0;
    if (length_now) sst64_output_chunk(pending_word, length_now, 0, 0, 0);
}

static void sst64_output_text(const uint8_t *text, unsigned int text_length,
                              uint8_t convert_ascii, uint8_t slow) {
    sst64_flush_pending();
    sst64_output_chunk(text, text_length, convert_ascii, slow, 0);
}

static void sst64_write_text(const uint8_t *text, unsigned int text_length) {
    uint8_t joined[sizeof(pending_word)];
    if (pending_length) {
        uint8_t used = pending_length;
        unsigned int position = 0;
        memcpy(joined, pending_word, used);
        while (position < text_length && used < sizeof(joined) && !sst64_word_break(text[position]))
            joined[used++] = text[position++];
        pending_length = 0;
        sst64_output_chunk(joined, used, 0, 0, position == text_length);
        text += position;
        text_length -= position;
    }
    sst64_output_chunk(text, text_length, 0, 0, 1);
}

void sst64_puts(const char *text) {
    uint8_t converted[63];
    fflush(stdout);
    cbm_k_clrch();
    while (*text) {
        uint8_t count = 0;
        while (*text && count < sizeof(converted))
            converted[count++] = sst64_convert_ascii(*text++);
        sst64_write_text(converted, count);
    }
}

#define PROGRESS_WIDTH 20
static uint8_t progress_filled;

static void sst64_draw_progress(uint8_t step, uint8_t total) {
    uint8_t target = total ? (uint16_t)step * PROGRESS_WIDTH / total : PROGRESS_WIDTH;
    uint8_t index;
    cbm_k_clrch();
    if (!step) {
        sst64_set_text_color(7);
        cbm_k_chrout('[');
        for (index = 0; index < PROGRESS_WIDTH; ++index) cbm_k_chrout(' ');
        cbm_k_chrout(']');
        for (index = 0; index <= PROGRESS_WIDTH; ++index) cbm_k_chrout(157);
        progress_filled = 0;
    }
    if (target > PROGRESS_WIDTH || step >= total) target = PROGRESS_WIDTH;
    if (progress_filled < target) cbm_k_chrout(18);
    while (progress_filled < target) {
        cbm_k_chrout(' ');
        ++progress_filled;
    }
    cbm_k_chrout(146);
    if (step >= total) {
        cbm_k_chrout(29);
        cbm_k_chrout(13);
        sst64_set_text_color(5);
        progress_filled = 0;
    }
}

void sst64_progress(uint8_t step, uint8_t total) {
    fflush(stdout);
    sst64_flush_pending();
    output_spaces = 0;
    if (!step && output_column) sst64_output_byte(13, 0);
    sst64_draw_progress(step, total);
    if (step >= total) {
        output_column = 0;
        output_wrapped = 0;
    }
}

void sst64_fail(void) {
    const char *message = "SST64 LOAD ERROR\r";
    cbm_k_clrch();
    cbm_k_close(1);
    while (*message) cbm_k_chrout(*message++);
    for (;;) {}
}

static uint8_t sst64_read_byte(void) {
    uint8_t value;
    uint8_t status;
    if (boot_eof) sst64_fail();
    value = cbm_k_chrin();
    status = cbm_k_readst();
    if (status & 0xbf) sst64_fail();
    boot_eof = status & 0x40;
    return value;
}

void sst64_boot(void) {
    uint8_t records;
    uint8_t total_records;
    uint8_t more;
    uint8_t disk = 1;
    uint8_t index;
    uint8_t cached_tag[8];
    char *compressed = (char *)0x7000;
    sst64_set_screen_colors();
    cbm_k_chrout(147);
    if (!reu_detect_512k()) {
        const char *message = "SST64 NEEDS A 512 KIB REU\r";
        while (*message) cbm_k_chrout(*message++);
        for (;;) {}
    }
    if (reu_fetch(cached_tag, 524272UL, sizeof(cached_tag)) &&
        !memcmp(cached_tag, build_tag, sizeof(cached_tag))) {
        help_length = SST64_HELP_LENGTH;
        return;
    }
next_disk:
    boot_eof = 0;
    cbm_k_setnam(disk == 1 ? "GAME,P,R" : "GAME2,P,R");
    cbm_k_setlfs(1, 8, 2);
    if (cbm_k_open() || cbm_k_chkin(1)) sst64_fail();
    (void)sst64_read_byte();
    (void)sst64_read_byte();
    if (sst64_read_byte() != 'S' || sst64_read_byte() != 'S' ||
        sst64_read_byte() != 'T' || sst64_read_byte() != '1') sst64_fail();
    records = sst64_read_byte();
    more = sst64_read_byte();
    help_length = 0;
    for (index = 0; index < 4; ++index) help_length |= (uint32_t)sst64_read_byte() << (index * 8);
    if (help_length > SST64_REU_BYTES - HELP_BASE || records > 128) sst64_fail();
    total_records = records;
    {
        const char *message = "LOADING GAME DATA\r";
        while (*message) cbm_k_chrout(*message++);
    }
    sst64_draw_progress(0, total_records);
    cbm_k_chkin(1);
    while (records--) {
        uint8_t bank = sst64_read_byte();
        uint16_t count = sst64_read_byte();
        uint16_t position;
        count |= (uint16_t)sst64_read_byte() << 8;
        char *window = (char *)(0x8000U + (bank & 3) * 4096U);
        if (bank >= 128 || !count || count > 4096) sst64_fail();
        for (position = 0; position < count; ++position) compressed[position] = sst64_read_byte();
        if (LZ4_decompress_safe(compressed, window, count, 4096) != 4096 ||
            !reu_store((uint32_t)bank * 4096UL, window, 4096)) sst64_fail();
        cbm_k_clrch();
        sst64_draw_progress(total_records - records, total_records);
        if (records && cbm_k_chkin(1)) sst64_fail();
    }
    cbm_k_clrch();
    cbm_k_close(1);
    if (more) {
        const char *message = "INSERT DISK 2 THEN PRESS SPACE\r";
        if (disk != 1) sst64_fail();
        while (*message) cbm_k_chrout(*message++);
        while (cbm_k_getin() != ' ') {}
        ++disk;
        goto next_disk;
    }
    memset(cached_tag, 0, sizeof(cached_tag));
    if (!reu_store(HELP_CACHE, cached_tag, sizeof(cached_tag)) ||
        !reu_store(524272UL, build_tag, sizeof(build_tag))) sst64_fail();
}

static void sst64_load_help(uint8_t command) {
    char filename[] = "HELP00,P,R";
    uint8_t saved_bank = sst64_active_bank;
    uint8_t index, blocks, completed = 0;
    uint16_t resource_length;
    uint16_t remaining = help_lengths[command];
    uint32_t address = HELP_BASE + help_offsets[command];
    char *compressed = (char *)0x7000;
    const char *message = "INSERT DISK 2 FOR HELP, PRESS SPACE\r";
    cbm_k_clrch();
    while (*message) cbm_k_chrout(*message++);
    while (cbm_k_getin() != ' ') {}
    filename[4] += command / 10;
    filename[5] += command % 10;
    cbm_k_setnam(filename);
    cbm_k_setlfs(1, 8, 2);
    boot_eof = 0;
    if (cbm_k_open() || cbm_k_chkin(1)) sst64_fail();
    (void)sst64_read_byte();
    (void)sst64_read_byte();
    if (sst64_read_byte() != 'S' || sst64_read_byte() != 'S' ||
        sst64_read_byte() != 'H' || sst64_read_byte() != '1') sst64_fail();
    for (index = 0; index < sizeof(build_tag); ++index)
        if (sst64_read_byte() != build_tag[index]) sst64_fail();
    if (sst64_read_byte() != command) sst64_fail();
    resource_length = sst64_read_byte();
    resource_length |= (uint16_t)sst64_read_byte() << 8;
    blocks = sst64_read_byte();
    if (resource_length != remaining || blocks != (remaining - 1U) / 4096U + 1U)
        sst64_fail();
    cbm_k_clrch();
    message = "LOADING COMMAND HELP\r";
    while (*message) cbm_k_chrout(*message++);
    sst64_draw_progress(0, blocks);
    while (remaining) {
        uint16_t count, position;
        uint16_t decoded = remaining > 4096 ? 4096 : remaining;
        if (cbm_k_chkin(1)) sst64_fail();
        count = sst64_read_byte();
        count |= (uint16_t)sst64_read_byte() << 8;
        if (!count || count > 4096) sst64_fail();
        for (position = 0; position < count; ++position)
            compressed[position] = sst64_read_byte();
        if (LZ4_decompress_safe(compressed, (char *)0x8000, count, 4096) != 4096 ||
            !reu_store(address, (void *)0x8000, decoded)) sst64_fail();
        address += decoded;
        remaining -= decoded;
        cbm_k_clrch();
        sst64_draw_progress(++completed, blocks);
    }
    if (!boot_eof) sst64_fail();
    cbm_k_close(1);
    if (!reu_fetch((void *)0x8000, (uint32_t)saved_bank * 16384UL, 16384))
        sst64_fail();
}

uint8_t sst64_select_help(uint8_t command) {
    uint8_t cached;
    uint8_t mask = 1U << (command & 7);
    if (command >= SST64_HELP_COUNT) sst64_fail();
    if (!help_lengths[command]) return 0;
    if (!reu_fetch(&cached, HELP_CACHE + command / 8, 1)) sst64_fail();
    if (!(cached & mask)) {
        fflush(stdout);
        sst64_flush_pending();
        sst64_load_help(command);
        cached |= mask;
        if (!reu_store(HELP_CACHE + command / 8, &cached, 1)) sst64_fail();
        output_column = 0;
        output_wrapped = 0;
        output_spaces = 0;
        output_lines = 0;
    }
    help_start = help_offsets[command];
    help_end = help_start + help_lengths[command];
    return 1;
}

void sst64_text(uint16_t message, uint8_t mode) {
    uint32_t address;
    if (message >= SST64_MESSAGE_COUNT) sst64_fail();
    address = TEXT_BASE + text_offsets[message];
    for (;;) {
        uint8_t count = 0;
        bool done = false;
        if (!reu_fetch(text_buffer, address, sizeof(text_buffer) - 1)) sst64_fail();
        while (count < sizeof(text_buffer) - 1) {
            if (!text_buffer[count]) { done = true; break; }
            ++count;
        }
        text_buffer[count] = 0;
        if (mode == 2) prouts(text_buffer);
        else proutn(text_buffer);
        if (done) break;
        address += count;
    }
    if (mode == 1) skip(1);
}

void sst64_prouts(const char *text) {
    cbm_k_clrch();
    sst64_output_text((const uint8_t *)text, strlen(text), 1, 1);
}

int open(const char *name, int flags, ...) {
    uint8_t channel;
    char filename[24];
    uint8_t filename_size = 0;
    if (!strcmp(name, "sst.doc")) { help_position = help_start; return 7; }
    for (channel = 3; channel < 7 && disk_used[channel]; ++channel) {}
    if (channel == 7) return -1;
    if (flags & O_WRONLY) { filename[filename_size++] = '@'; filename[filename_size++] = '0'; filename[filename_size++] = ':'; }
    while (*name && filename_size < 19) {
        uint8_t value = *name++;
        if (value >= 'a' && value <= 'z') value -= 32;
        if (value == ':' || value == ',' || value == '*' || value == '?') return -1;
        filename[filename_size++] = value;
    }
    if (*name) return -1;
    filename[filename_size++] = ',';
    filename[filename_size++] = 'S';
    filename[filename_size++] = ',';
    filename[filename_size++] = flags & O_WRONLY ? 'W' : 'R';
    filename[filename_size] = 0;
    cbm_k_setnam(filename);
    cbm_k_setlfs(channel, 8, channel);
    if (cbm_k_open()) { cbm_k_clrch(); cbm_k_close(channel); return -1; }
    disk_eof[channel] = 0;
    disk_used[channel] = 1;
    cbm_k_clrch();
    return channel;
}

int close(int channel) {
    if (channel == 7) return 0;
    if (channel < 3 || channel >= 7 || !disk_used[channel]) return -1;
    cbm_k_clrch();
    cbm_k_close(channel);
    disk_used[channel] = 0;
    return 0;
}

int read(int channel, void *buffer, unsigned count) {
    uint8_t *destination = buffer;
    unsigned used = 0;
    if (channel == 0) {
        if (!count) return 0;
        fflush(stdout);
        sst64_flush_pending();
        while (output_spaces) {
            sst64_output_byte(' ', 0);
            --output_spaces;
        }
        cbm_k_clrch();
        output_lines = 0;
        if (input_line_start) sst64_output_byte(13, 0);
        sst64_set_text_color(14);
        destination[0] = __getchar();
        sst64_set_text_color(5);
        input_line_start = destination[0] == 13;
        return 1;
    }
    if (channel == 7) {
        if (count > help_end - help_position) count = help_end - help_position;
        if (count && !reu_fetch(buffer, HELP_BASE + help_position, count)) return -1;
        help_position += count;
        return count;
    }
    if (channel < 3 || channel >= 7 || !disk_used[channel]) return -1;
    if (disk_eof[channel]) return 0;
    if (cbm_k_chkin(channel)) { cbm_k_clrch(); return -1; }
    while (used < count) {
        uint8_t value = cbm_k_chrin();
        uint8_t status = cbm_k_readst();
        if (status & 0xbf) { cbm_k_clrch(); return -1; }
        destination[used++] = value;
        if (status & 0x40) { disk_eof[channel] = 1; break; }
    }
    cbm_k_clrch();
    return used;
}

int write(int channel, const void *buffer, unsigned count) {
    const uint8_t *source = buffer;
    unsigned used;
    if (channel == 1 || channel == 2) {
        cbm_k_clrch();
        sst64_write_text(source, count);
        return count;
    }
    if (channel < 3 || channel >= 7 || !disk_used[channel]) return -1;
    if (cbm_k_chkout(channel)) { cbm_k_clrch(); return -1; }
    for (used = 0; used < count; ++used) {
        cbm_k_chrout(source[used]);
        if (cbm_k_readst()) { cbm_k_clrch(); return -1; }
    }
    cbm_k_clrch();
    return count;
}