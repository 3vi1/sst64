#ifndef SST64_GAME_MATH_H
#define SST64_GAME_MATH_H

double sin(double value);
double cos(double value);
double atan2(double vertical, double horizontal);
double sqrt(double value);
double fabs(double value);
double pow(double value, double exponent);
double log(double value);
double floor(double value);
double fmod(double value, double divisor);
double scalbn(double value, int exponent);
double copysign(double value, double sign);
long double fmodl(long double value, long double divisor);
long double scalbnl(long double value, int exponent);
long double copysignl(long double value, long double sign);
long double fabsl(long double value);

#endif
