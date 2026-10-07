#ifndef POLLIK_QUICKJS_MATH_H
#define POLLIK_QUICKJS_MATH_H
#include "../../../sdk/include/math.h"
/* Additional scalar functions supplied by the pinned OpenLibm sources. */
long lrint(double);
double rint(double);
double hypot(double,double);
double cosh(double);
double sinh(double);
double tanh(double);
double acosh(double);
double asinh(double);
double atanh(double);
double expm1(double);
double log1p(double);
double log2(double);
double cbrt(double);
#endif
