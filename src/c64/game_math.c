#include "game_math.h"
#include <errno.h>
#include <float.h>
#include <stdint.h>

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Game math requires IEEE binary64 doubles");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Game math requires little-endian doubles
#endif

typedef union {
    double value;
    uint8_t bytes[8];
    uint16_t words[4];
} Number;

#define PI 3.14159265358979323846
#define HALF_PI 1.57079632679489661923
#define TWO_PI 6.28318530717958647693
#define LN2 0.69314718055994530942
#define INFINITY_VALUE (__builtin_inf())
#define NAN_VALUE (__builtin_nan(""))

static int nonfinite(double value) {
    Number bits = {.value = value};
    return (bits.words[3] & 0x7ff0) == 0x7ff0;
}

static double domain_error(void) {
    errno = EDOM;
    return NAN_VALUE;
}

double fabs(double value) {
    Number bits = {.value = value};
    bits.words[3] &= 0x7fff;
    return bits.value;
}

double copysign(double value, double sign) {
    Number bits = {.value = value};
    Number direction = {.value = sign};
    bits.words[3] = (bits.words[3] & 0x7fff) | (direction.words[3] & 0x8000);
    return bits.value;
}

/* Return a positive mantissa in [1, 2), including for subnormal inputs. */
static double normalize(double value, int *exponent) {
    Number bits = {.value = value};
    int adjustment = 0;
    if (!(bits.words[3] & 0x7ff0)) {
        bits.value *= 0x1p52;
        adjustment = -52;
    }
    *exponent = ((bits.words[3] >> 4) & 0x7ff) - 1023 + adjustment;
    bits.words[3] = (bits.words[3] & 0x000f) | 0x3ff0;
    return bits.value;
}

static double power_of_two(int exponent) {
    Number bits = {.value = 0.0};
    bits.words[3] = (uint16_t)(exponent + 1023) << 4;
    return bits.value;
}

double scalbn(double value, int exponent) {
    double result;
    if (!value || nonfinite(value)) return value;
    if (exponent > 2098) {
        errno = ERANGE;
        return copysign(INFINITY_VALUE, value);
    }
    if (exponent < -2098) {
        errno = ERANGE;
        return copysign(0.0, value);
    }
    while (exponent > 1023) {
        value *= 0x1p1023;
        exponent -= 1023;
    }
    while (exponent < -1022) {
        value *= 0x1p-1022;
        exponent += 1022;
    }
    result = value * power_of_two(exponent);
    if (!result || nonfinite(result)) errno = ERANGE;
    return result;
}

double floor(double value) {
    Number bits = {.value = value};
    int exponent = ((bits.words[3] >> 4) & 0x7ff) - 1023;
    uint8_t changed = 0;
    uint8_t index = 0;
    int remaining;
    if (!value || exponent >= 52) return value;
    if (exponent < 0) return value < 0 ? -1.0 : 0.0;
    remaining = 52 - exponent;
    while (remaining > 0) {
        uint8_t count = remaining < 8 ? remaining : 8;
        uint8_t mask = (uint16_t)0xff << count;
        if (bits.bytes[index] & (uint8_t)~mask) changed = 1;
        bits.bytes[index++] &= mask;
        remaining -= count;
    }
    return value < 0 && changed ? bits.value - 1.0 : bits.value;
}

double fmod(double value, double divisor) {
    double magnitude, step, result;
    if (value != value || divisor != divisor) return value + divisor;
    if (nonfinite(value) || divisor == 0) return domain_error();
    magnitude = fabs(divisor);
    result = fabs(value);
    if (result < magnitude) return value;
    step = magnitude;
    while (step <= result * 0.5) step *= 2.0;
    while (step >= magnitude) {
        if (result >= step) result -= step;
        step *= 0.5;
    }
    return copysign(result, value);
}

double sqrt(double value) {
    double mantissa, result;
    int exponent;
    uint8_t iteration;
    if (value < 0) return domain_error();
    if (!value || nonfinite(value)) return value;
    mantissa = normalize(value, &exponent);
    if (exponent % 2) {
        mantissa *= 2.0;
        --exponent;
    }
    result = (mantissa + 1.0) * 0.5;
    for (iteration = 0; iteration < 6; ++iteration)
        result = 0.5 * (result + mantissa / result);
    return scalbn(result, exponent / 2);
}

double log(double value) {
    double mantissa, ratio, squared, term, sum;
    int exponent;
    uint8_t denominator;
    if (!value) {
        errno = ERANGE;
        return -INFINITY_VALUE;
    }
    if (value < 0) return domain_error();
    if (nonfinite(value)) return value;
    mantissa = normalize(value, &exponent);
    if (mantissa > 1.41421356237309504880) {
        mantissa *= 0.5;
        ++exponent;
    }
    ratio = (mantissa - 1.0) / (mantissa + 1.0);
    squared = ratio * ratio;
    term = sum = ratio;
    for (denominator = 3; denominator <= 19; denominator += 2) {
        term *= squared;
        sum += term / denominator;
    }
    return 2.0 * sum + exponent * LN2;
}

static double exponential(double value) {
    int exponent;
    double remainder, sum = 1.0;
    uint8_t degree;
    if (value > 709.782712893383973) {
        errno = ERANGE;
        return INFINITY_VALUE;
    }
    if (value < -745.133219101941108) {
        errno = ERANGE;
        return 0.0;
    }
    exponent = (int)floor(value / LN2 + 0.5);
    /* Split ln(2) keeps range reduction accurate near overflow/underflow. */
    remainder = (value - exponent * 0.69314718036912381649) -
                exponent * 1.90821492927058770002e-10;
    for (degree = 13; degree; --degree)
        sum = 1.0 + remainder * sum / degree;
    return scalbn(sum, exponent);
}

double pow(double value, double exponent) {
    double result;
    uint8_t negative = 0;
    if (!exponent || value == 1) return 1.0;
    if (exponent == 1) return value;
    if (value != value || exponent != exponent) return value + exponent;
    if (nonfinite(exponent)) {
        double magnitude = fabs(value);
        if (magnitude == 1) return 1.0;
        return (magnitude > 1) == (exponent > 0) ? INFINITY_VALUE : 0.0;
    }
    if (value < 0 && !nonfinite(value) && floor(exponent) != exponent)
        return domain_error();
    if (copysign(1.0, value) < 0 && floor(exponent) == exponent) {
        negative = fmod(exponent, 2.0) != 0;
    }
    value = fabs(value);
    if (!value) {
        if (exponent < 0) errno = ERANGE;
        result = exponent > 0 ? 0.0 : INFINITY_VALUE;
    } else if (nonfinite(value)) {
        result = exponent > 0 ? INFINITY_VALUE : 0.0;
    } else if (exponent >= -32767 && exponent <= 32767 &&
               floor(exponent) == exponent) {
        int count = (int)exponent;
        result = 1.0;
        if (count < 0) {
            value = 1.0 / value;
            count = -count;
        }
        while (count) {
            if (count & 1) result *= value;
            count >>= 1;
            if (count) value *= value;
        }
        if (!result || nonfinite(result)) errno = ERANGE;
    } else {
        result = exponential(exponent * log(value));
    }
    return negative ? -result : result;
}

double sin(double value) {
    double squared, term, sum;
    uint8_t degree;
    if (!value) return value;
    if (nonfinite(value)) return value != value ? value : domain_error();
    value = fmod(value, TWO_PI);
    if (value > PI) value -= TWO_PI;
    if (value < -PI) value += TWO_PI;
    if (value > HALF_PI) value = PI - value;
    if (value < -HALF_PI) value = -PI - value;
    squared = value * value;
    sum = term = value;
    for (degree = 3; degree <= 19; degree += 2) {
        term *= -squared / ((unsigned int)degree * (degree - 1));
        sum += term;
    }
    return sum;
}

double cos(double value) {
    return sin(HALF_PI - fmod(value, TWO_PI));
}

static double arctangent(double value) {
    double offset = 0.0, squared, term, sum;
    uint8_t denominator;
    if (value > 0.41421356237309504880) {
        value = (value - 1.0) / (value + 1.0);
        offset = PI * 0.25;
    }
    squared = value * value;
    sum = term = value;
    for (denominator = 3; denominator <= 39; denominator += 2) {
        term *= -squared;
        sum += term / denominator;
    }
    return offset + sum;
}

double atan2(double vertical, double horizontal) {
    double x = fabs(horizontal), y = fabs(vertical), angle;
    if (vertical != vertical || horizontal != horizontal) return vertical + horizontal;
    if (nonfinite(x) && nonfinite(y)) angle = PI * 0.25;
    else if (!y) angle = 0.0;
    else if (x >= y) angle = arctangent(y / x);
    else angle = HALF_PI - arctangent(x / y);
    if (copysign(1.0, horizontal) < 0) angle = PI - angle;
    return copysign(angle, vertical);
}

/* LLVM-MOS uses binary64 for long double, too. */
long double fmodl(long double value, long double divisor) {
    return fmod(value, divisor);
}

long double scalbnl(long double value, int exponent) {
    return scalbn(value, exponent);
}

long double copysignl(long double value, long double sign) {
    return copysign(value, sign);
}

long double fabsl(long double value) {
    return fabs(value);
}
