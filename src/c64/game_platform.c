#include "game_platform.h"
#include <cbm.h>
#include <stdlib.h>
#include <stdio.h>
#include "game_floatscan.h"

__asm__(".section .init.275,\"ax\",@progbits\nlda #142\njsr __CHROUT\n");

static unsigned int random_state = 1;

int rand(void) {
    random_state ^= random_state << 7;
    random_state ^= random_state >> 9;
    random_state ^= random_state << 8;
    return random_state >> 1;
}

void srand(unsigned int seed) {
    random_state = seed ? seed : 1;
}

int __from_ascii(char value, void *context, int (*output)(char, void *)) {
    if (value == '\n') value = '\r';
    else if (value == '\f') value = (char)147;
    else if (value == '\b') value = (char)157;
    else if (value >= 'a' && value <= 'z') value -= 32;
    return output(value, context);
}

int sst64_number(const char *text, double *value, int *consumed) {
    SST64_FLOAT_STREAM stream = {(const unsigned char *)text, (const unsigned char *)text};
    *value = __floatscan(&stream, 1, 1);
    *consumed = stream.cursor - stream.begin;
    return *consumed != 0;
}

int sst64_selftest(void) {
    volatile int integer = 32767;
    volatile double first = integer;
    volatile double second = 0.5;
    volatile double result = first * second;
    if (first != 32767.0 || result != 16383.5) {
        puts("ARITHMETIC SELFTEST FAILED");
        return 0;
    }
    for (integer = 0; integer < 64; ++integer) {
        int sample = rand();
        if (sample < 0 || sample > RAND_MAX) {
            puts("RANDOM SELFTEST FAILED");
            return 0;
        }
    }
    puts("ARITHMETIC SELFTEST PASSED");
    return 1;
}

__attribute__((used)) int sst64_math_selftest(void) {
    volatile double first = 81.0;
    volatile double second;
    volatile double result;
    result = sqrt(first);
    if (result != 9.0) {
        puts("MATH SELFTEST FAILED");
        return 0;
    }
    first = 0.9;
    second = 12.5;
    result = pow(first, second);
    if (fabs(result - 0.26793618413567305) > 1e-11) {
        puts("MATH SELFTEST FAILED");
        return 0;
    }
    first = 0.125;
    result = log(first);
    if (fabs(result + 2.0794415416798359) > 1e-12) {
        puts("MATH SELFTEST FAILED");
        return 0;
    }
    first = -0.75;
    second = -0.25;
    if (fabs(sin(first) + 0.6816387600233341) > 1e-12 ||
        fabs(cos(first) - 0.7316888688738209) > 1e-12 ||
        fabs(atan2(first, second) + 1.892546881191539) > 1e-12 ||
        floor(first) != -1.0 || fmod(first, 0.5) != -0.25 ||
        scalbn(first, -10) != -0.000732421875) {
        puts("MATH SELFTEST FAILED");
        return 0;
    }
    puts("MATH SELFTEST PASSED");
    return 1;
}

int min(int left, int right) {
    return left < right ? left : right;
}

int max(int left, int right) {
    return left > right ? left : right;
}

long clock(void) {
    const volatile unsigned char *jiffies = (const volatile unsigned char *)0xa0;
    unsigned long ticks;
    __asm__ volatile("php\nsei" ::: "memory");
    ticks = ((unsigned long)jiffies[0] << 16) |
            ((unsigned long)jiffies[1] << 8) | jiffies[2];
    __asm__ volatile("plp" ::: "memory");
    return ticks;
}

void randomize(void) {
    srand((unsigned int)clock());
}

int getch(void) {
    unsigned char key;
    fflush(stdout);
    do {
        key = cbm_k_getin();
    } while (!key);
    return key;
}