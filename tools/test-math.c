#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Keep the reference libm and the target implementation in the same process. */
#define sin compact_sin
#define cos compact_cos
#define atan2 compact_atan2
#define sqrt compact_sqrt
#define fabs compact_fabs
#define pow compact_pow
#define log compact_log
#define floor compact_floor
#define fmod compact_fmod
#define scalbn compact_scalbn
#define copysign compact_copysign
#define fmodl compact_fmodl
#define scalbnl compact_scalbnl
#define copysignl compact_copysignl
#define fabsl compact_fabsl
#include "../src/c64/game_math.c"
#undef sin
#undef cos
#undef atan2
#undef sqrt
#undef fabs
#undef pow
#undef log
#undef floor
#undef fmod
#undef scalbn
#undef copysign
#undef fmodl
#undef scalbnl
#undef copysignl
#undef fabsl

static unsigned long checks;
static double worst_trig, worst_pow, worst_log, worst_sqrt;

static void check(const char *name, double input, double actual, double expected,
                  double absolute, double relative, double *worst) {
    double error = fabs(actual - expected);
    ++checks;
    if (isnan(expected) ? isnan(actual) : actual == expected) return;
    if (!isfinite(actual) || !isfinite(expected) ||
        error > absolute + relative * fabs(expected)) {
        fprintf(stderr, "%s(%.17g): %.17g, expected %.17g\n",
                name, input, actual, expected);
        exit(1);
    }
    if (worst) {
        double scaled = relative ? error / fabs(expected) : error;
        if (scaled > *worst) *worst = scaled;
    }
}

int main(void) {
    int i, j;
    for (i = -1074; i <= 1023; ++i) {
        double value = ldexp(1.23456789012345, i);
        check("sqrt", value, compact_sqrt(value), sqrt(value), 0, 3e-15, &worst_sqrt);
        check("log", value, compact_log(value), log(value), 2e-13, 0, &worst_log);
        check("floor", value, compact_floor(value), floor(value), 0, 0, NULL);
        check("floor", -value, compact_floor(-value), floor(-value), 0, 0, NULL);
        for (j = -1100; j <= 1100; j += 137) {
            check("scalbn", value, compact_scalbn(value, j), scalbn(value, j),
                  0x1p-1074, 3e-15, NULL);
            check("fmod", value, compact_fmod(value, ldexp(1.375, j)),
                  fmod(value, ldexp(1.375, j)), 0, 0, NULL);
        }
    }
    for (i = -10000; i <= 10000; ++i) {
        double angle = i * 0.99997;
        double x = i * 0.001;
        check("sin", angle, compact_sin(angle), sin(angle), 1e-11, 0, &worst_trig);
        check("cos", angle, compact_cos(angle), cos(angle), 1e-11, 0, &worst_trig);
        check("atan2", x, compact_atan2(x, 1.0), atan2(x, 1.0), 2e-14, 0, &worst_trig);
        check("atan2", x, compact_atan2(x, -1.0), atan2(x, -1.0), 2e-14, 0, &worst_trig);
        check("floor", x, compact_floor(x), floor(x), 0, 0, NULL);
        check("fmod", x, compact_fmod(x, 0.3), fmod(x, 0.3), 0, 0, NULL);
    }
    for (i = 1; i <= 1000; ++i) {
        const double bases[] = {0.9, 0.98, 7.0, i * 0.031, i * 50.0};
        const double exponents[] = {1.0 / 3.0, -i * 0.001, i * 0.01};
        unsigned b, e;
        for (b = 0; b < sizeof(bases) / sizeof(*bases); ++b)
            for (e = 0; e < sizeof(exponents) / sizeof(*exponents); ++e)
                check("pow", bases[b], compact_pow(bases[b], exponents[e]),
                      pow(bases[b], exponents[e]), 1e-300, 1e-10, &worst_pow);
    }
    check("pow near overflow", DBL_MAX,
          compact_pow(DBL_MAX, nextafter(1.0, 0.0)),
          pow(DBL_MAX, nextafter(1.0, 0.0)), 0, 1e-10, &worst_pow);
    for (i = -4300; i <= 4100; ++i) {
        double exponent = i * 0.25;
        check("pow binary range", exponent, compact_pow(2.0, exponent),
              pow(2.0, exponent), 0x1p-1074, 1e-10, NULL);
    }
    {
        const double values[] = {0, -0.0, INFINITY, -INFINITY, NAN, DBL_MAX,
                                 DBL_MIN, 0x1p-1074, -1, 1, 0.5, -0.5};
        unsigned a, b;
        for (a = 0; a < sizeof(values) / sizeof(*values); ++a) {
            double x = values[a];
            check("sqrt", x, compact_sqrt(x), sqrt(x), 0, 3e-15, NULL);
            check("log", x, compact_log(x), log(x), 2e-13, 0, NULL);
            check("floor", x, compact_floor(x), floor(x), 0, 0, NULL);
            for (b = 0; b < sizeof(values) / sizeof(*values); ++b) {
                double y = values[b];
                check("atan2", x, compact_atan2(x, y), atan2(x, y), 2e-14, 0, NULL);
                check("fmod", x, compact_fmod(x, y), fmod(x, y), 0, 0, NULL);
                check("pow", x, compact_pow(x, y), pow(x, y), 1e-300, 1e-10, NULL);
            }
        }
    }
    assert(signbit(compact_sqrt(-0.0)));
    assert(signbit(compact_fmod(-2.0, 1.0)));
    assert(signbit(compact_copysign(0.0, -1.0)));
    assert(signbit(compact_pow(-0.0, 3.0)));
    assert(compact_pow(-2.0, 3.0) == -8.0);
    assert(compact_pow(-2.0, 4.0) == 16.0);
    assert(compact_fabsl(-0.5L) == 0.5L);
    assert(compact_scalbnl(1.0L, -10) == 0.0009765625L);
    assert(compact_fmodl(1.25L, 0.5L) == 0.25L);
    assert(compact_copysignl(1.0L, -1.0L) == -1.0L);
    errno = 0;
    assert(isnan(compact_sqrt(-1)) && errno == EDOM);
    errno = 0;
    assert(isnan(compact_pow(-2, 0.5)) && errno == EDOM);
    errno = 0;
    assert(isinf(compact_log(0)) && errno == ERANGE);
    printf("PASS: %lu math comparisons; max absolute trig/atan %.3g, log %.3g; "
           "max relative sqrt %.3g, pow %.3g\n",
           checks, worst_trig, worst_log, worst_sqrt, worst_pow);
    return 0;
}
