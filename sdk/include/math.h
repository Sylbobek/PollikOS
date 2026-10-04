#ifndef POLLIKOS_MATH_H
#define POLLIKOS_MATH_H
/* Common scalar double/float math. Long double and several special functions
 * from the full C libm remain unavailable. */
double fabs(double value);
float fabsf(float value);
double trunc(double value);
float truncf(float value);
double floor(double value);
float floorf(float value);
double ceil(double value);
float ceilf(float value);
double round(double value);
float roundf(float value);
double sqrt(double value);
float sqrtf(float value);
double fmin(double left, double right);
double fmax(double left, double right);
float fminf(float left, float right);
float fmaxf(float left, float right);
double copysign(double magnitude, double sign);
float copysignf(float magnitude, float sign);
double modf(double value, double *integral);
float modff(float value, float *integral);
double frexp(double value, int *exponent);
float frexpf(float value, int *exponent);
double fmod(double value, double divisor);
float fmodf(float value, float divisor);
double ldexp(double value, int exponent);
float ldexpf(float value, int exponent);
double scalbn(double value, int exponent);
float scalbnf(float value, int exponent);
double exp(double value);
double log(double value);
double log10(double value);
double pow(double base, double exponent);
double sin(double value);
double cos(double value);
double tan(double value);
double atan(double value);
double atan2(double y, double x);
double asin(double value);
double acos(double value);
int isnan(double value);
int isinf(double value);
int isfinite(double value);
int signbit(double value);
float expf(float value);
float logf(float value);
float powf(float base, float exponent);
float sinf(float value);
float cosf(float value);
float tanf(float value);
float atanf(float value);
float atan2f(float y, float x);
float asinf(float value);
float acosf(float value);
#endif
