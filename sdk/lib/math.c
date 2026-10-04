#include <math.h>
#include <stdint.h>

typedef union { double value; uint64_t bits; } double_bits;
typedef union { float value; uint32_t bits; } float_bits;

#define M_PI 3.14159265358979323846264338327950288
#define M_PI_2 1.57079632679489661923132169163975144
#define M_PI_4 0.78539816339744830961566084581987572
#define M_2PI 6.28318530717958647692528676655900576
#define M_LN2 0.69314718055994530941723212145817657
#define M_LOG2E 1.44269504088896340735992468100189214
#define M_LN10 2.30258509299404568401799145468436421

static double make_nan(void) {
    return (double_bits){.bits=UINT64_C(0x7ff8000000000000)}.value;
}
static double make_infinity(int negative) {
    return (double_bits){.bits=UINT64_C(0x7ff0000000000000) |
        (negative ? UINT64_C(0x8000000000000000) : 0)}.value;
}
static int double_is_nan(double value) {
    uint64_t bits = ((double_bits){.value=value}).bits;
    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
        (bits & UINT64_C(0x000fffffffffffff)) != 0;
}
static int double_is_inf(double value) {
    uint64_t bits = ((double_bits){.value=value}).bits;
    return (bits & UINT64_C(0x7fffffffffffffff)) == UINT64_C(0x7ff0000000000000);
}
static int double_signbit(double value) {
    return (int)(((double_bits){.value=value}).bits >> 63);
}
static int is_odd_integer(double value) {
    double magnitude = fabs(value);
    return magnitude < 0x1p53 && trunc(magnitude) == magnitude && fmod(magnitude, 2.0) == 1.0;
}

double fabs(double value) {
    double_bits number = {value};
    number.bits &= UINT64_C(0x7fffffffffffffff);
    return number.value;
}
float fabsf(float value) {
    float_bits number = {value};
    number.bits &= UINT32_C(0x7fffffff);
    return number.value;
}
double trunc(double value) {
    double_bits number = {value};
    int exponent = (int)((number.bits >> 52) & 0x7ff) - 1023;
    if (exponent < 0) { number.bits &= UINT64_C(0x8000000000000000); return number.value; }
    if (exponent >= 52 || exponent == 1024) return value;
    number.bits &= ~((UINT64_C(1) << (52-exponent))-1);
    return number.value;
}
float truncf(float value) {
    float_bits number = {value};
    int exponent = (int)((number.bits >> 23) & 0xff) - 127;
    if (exponent < 0) { number.bits &= UINT32_C(0x80000000); return number.value; }
    if (exponent >= 23 || exponent == 128) return value;
    number.bits &= ~((UINT32_C(1) << (23-exponent))-1);
    return number.value;
}
double floor(double value) { double n = trunc(value); return value < n ? n-1.0 : n; }
float floorf(float value) { float n = truncf(value); return value < n ? n-1.0f : n; }
double ceil(double value) { double n = trunc(value); return value > n ? n+1.0 : n; }
float ceilf(float value) { float n = truncf(value); return value > n ? n+1.0f : n; }
double round(double value) { return value < 0.0 ? ceil(value-0.5) : floor(value+0.5); }
float roundf(float value) { return value < 0.0f ? ceilf(value-0.5f) : floorf(value+0.5f); }
double fmin(double left, double right) {
    if (left != left) return right;
    if (right != right) return left;
    if (left == 0.0 && right == 0.0)
        return ((double_bits){.value=left}.bits >> 63) ? left : right;
    return left < right ? left : right;
}
double fmax(double left, double right) {
    if (left != left) return right;
    if (right != right) return left;
    if (left == 0.0 && right == 0.0)
        return ((double_bits){.value=left}.bits >> 63) ? right : left;
    return left > right ? left : right;
}
double copysign(double magnitude, double sign) {
    double_bits result = {.value=magnitude};
    result.bits = (result.bits & UINT64_C(0x7fffffffffffffff)) |
        (((double_bits){.value=sign}).bits & UINT64_C(0x8000000000000000));
    return result.value;
}
int isnan(double value) { return double_is_nan(value); }
int isinf(double value) { return double_is_inf(value); }
int isfinite(double value) { return !double_is_nan(value) && !double_is_inf(value); }
int signbit(double value) { return double_signbit(value); }
double modf(double value, double *integral) {
    if (double_is_nan(value)) {
        if (integral) *integral = value;
        return value;
    }
    if (double_is_inf(value)) {
        if (integral) *integral = value;
        return copysign(0.0, value);
    }
    double whole = trunc(value);
    if (integral) *integral = whole;
    if (value == whole) return copysign(0.0, value);
    return value - whole;
}
double frexp(double value, int *exponent) {
    int power = 0;
    if (exponent) *exponent = 0;
    if (value == 0.0 || !isfinite(value)) return value;
    double magnitude = fabs(value);
    while (magnitude >= 1.0) { value *= 0.5; magnitude *= 0.5; ++power; }
    while (magnitude < 0.5) { value *= 2.0; magnitude *= 2.0; --power; }
    if (exponent) *exponent = power;
    return value;
}
double fmod(double value, double divisor) {
    if (double_is_nan(value) || double_is_nan(divisor) || divisor == 0.0 || double_is_inf(value))
        return make_nan();
    if (double_is_inf(divisor) || fabs(value) < fabs(divisor)) return value;
    double remainder = fabs(value);
    double modulus = fabs(divisor);
    int value_exponent, divisor_exponent;
    (void)frexp(remainder, &value_exponent);
    (void)frexp(modulus, &divisor_exponent);
    for (int shift = value_exponent - divisor_exponent; shift >= 0; --shift) {
        double scaled = ldexp(modulus, shift);
        if (scaled <= remainder) remainder -= scaled;
    }
    return copysign(remainder, value);
}
double ldexp(double value, int exponent) {
    if (value == 0.0 || value != value || double_is_inf(value) || exponent == 0) return value;
    if (exponent > 4096) {
        double_bits infinity = {.bits=UINT64_C(0x7ff0000000000000)};
        if ((double_bits){.value=value}.bits >> 63) infinity.bits |= UINT64_C(1) << 63;
        return infinity.value;
    }
    if (exponent < -4096) {
        double_bits zero = {.bits=(double_bits){.value=value}.bits & UINT64_C(0x8000000000000000)};
        return zero.value;
    }
    double scaled = value;
    while (exponent > 512) {
        scaled *= 0x1p512;
        exponent -= 512;
        if (scaled != scaled || scaled > 1.7976931348623157e308 ||
            scaled < -1.7976931348623157e308) return scaled;
    }
    while (exponent < -512) {
        scaled *= 0x1p-512;
        exponent += 512;
        if (scaled == 0.0) return scaled;
    }
    double_bits factor = {.bits=(uint64_t)(exponent+1023) << 52};
    return scaled*factor.value;
}
double scalbn(double value, int exponent) { return ldexp(value, exponent); }
double sqrt(double value) {
    if (value != value || value == 0.0) return value;
    double_bits input = {value};
    if (input.bits >> 63) return (double_bits){.bits=UINT64_C(0x7ff8000000000000)}.value;
    if (((input.bits >> 52) & 0x7ff) == 0x7ff) return value;
    int subnormal = ((input.bits >> 52) & 0x7ff) == 0;
    if (subnormal) { value *= 0x1p54; input.value = value; }
    double_bits estimate = {.bits=(input.bits >> 1) + UINT64_C(0x1ff8000000000000)};
    double root = estimate.value;
    for (unsigned i = 0; i < 8; ++i) root = 0.5*(root+value/root);
    return subnormal ? ldexp(root, -27) : root;
}
float sqrtf(float value) { return (float)sqrt((double)value); }

double exp(double value) {
    if (double_is_nan(value)) return value;
    if (value > 709.782712893383973096) return make_infinity(0);
    if (value < -745.13321910194110842) return 0.0;
    if (double_is_inf(value)) return value > 0.0 ? value : 0.0;
    int exponent = (int)round(value * M_LOG2E);
    double reduced = (value - exponent * 0.693147180369123816490) -
        exponent * 1.90821492927058770002e-10;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 24; ++i) {
        term *= reduced / (double)i;
        sum += term;
    }
    return ldexp(sum, exponent);
}

double log(double value) {
    if (double_is_nan(value)) return value;
    if (value == 0.0) return make_infinity(1);
    if (value < 0.0) return make_nan();
    if (double_is_inf(value)) return value;
    int exponent;
    double mantissa = frexp(value, &exponent);
    mantissa *= 2.0;
    --exponent;
    double z = (mantissa - 1.0) / (mantissa + 1.0);
    double z2 = z*z, power = z, sum = z;
    for (int divisor = 3; divisor <= 81; divisor += 2) {
        power *= z2;
        sum += power / (double)divisor;
    }
    return (2.0*sum + exponent*M_LN2);
}
double log10(double value) { return log(value) / M_LN10; }

double pow(double base, double exponent) {
    if (exponent == 0.0 || base == 1.0) return 1.0;
    if (double_is_nan(base) || double_is_nan(exponent)) return make_nan();
    int negative_result = double_signbit(base) && is_odd_integer(exponent);
    double magnitude = fabs(base);
    if (double_is_inf(exponent)) {
        if (magnitude == 1.0) return 1.0;
        return (magnitude > 1.0) == (exponent > 0.0) ? make_infinity(0) : 0.0;
    }
    if (double_is_inf(base)) {
        if (exponent < 0.0) return copysign(0.0, negative_result ? -1.0 : 1.0);
        return make_infinity(negative_result);
    }
    if (base == 0.0) {
        if (exponent < 0.0) return make_infinity(negative_result);
        return copysign(0.0, negative_result ? -1.0 : 1.0);
    }
    if (base < 0.0 && trunc(exponent) != exponent) return make_nan();
    double result = exp(exponent * log(magnitude));
    return negative_result ? -result : result;
}

static double reduce_trig(double value) {
    double reduced = fmod(value, M_2PI);
    if (reduced > M_PI) reduced -= M_2PI;
    if (reduced < -M_PI) reduced += M_2PI;
    return reduced;
}
static void sincos_reduced(double value, double *sine, double *cosine) {
    double x = reduce_trig(value);
    double cos_sign = 1.0;
    if (x > M_PI_2) { x = M_PI - x; cos_sign = -1.0; }
    else if (x < -M_PI_2) { x = -M_PI - x; cos_sign = -1.0; }
    double x2 = x*x;
    double s_term = x, s_sum = x;
    double c_term = 1.0, c_sum = 1.0;
    for (int n = 1; n <= 14; ++n) {
        s_term *= -x2 / ((double)(2*n) * (double)(2*n+1));
        s_sum += s_term;
        c_term *= -x2 / ((double)(2*n-1) * (double)(2*n));
        c_sum += c_term;
    }
    *sine = s_sum;
    *cosine = c_sum*cos_sign;
}
double sin(double value) {
    if (!isfinite(value)) return make_nan();
    double sine, cosine;
    sincos_reduced(value, &sine, &cosine);
    (void)cosine;
    return sine;
}
double cos(double value) {
    if (!isfinite(value)) return make_nan();
    double sine, cosine;
    sincos_reduced(value, &sine, &cosine);
    (void)sine;
    return cosine;
}
double tan(double value) {
    if (!isfinite(value)) return make_nan();
    double sine, cosine;
    sincos_reduced(value, &sine, &cosine);
    return sine / cosine;
}

static double atan_series(double value) {
    double squared = value*value, power = value, sum = value;
    for (int divisor = 3; divisor <= 121; divisor += 2) {
        power *= -squared;
        sum += power/(double)divisor;
    }
    return sum;
}
double atan(double value) {
    if (double_is_nan(value)) return value;
    if (double_is_inf(value)) return copysign(M_PI_2, value);
    int negative = value < 0.0;
    double x = fabs(value), result;
    if (x > 1.0) {
        x = 1.0/x;
        if (x > 0.4142135623730950488)
            result = M_PI_4 - atan_series((x - 1.0)/(x + 1.0));
        else
            result = M_PI_2 - atan_series(x);
    } else if (x > 0.4142135623730950488) {
        result = M_PI_4 + atan_series((x - 1.0)/(x + 1.0));
    } else {
        result = atan_series(x);
    }
    return negative ? -result : result;
}
double atan2(double y, double x) {
    if (double_is_nan(x) || double_is_nan(y)) return make_nan();
    if (y == 0.0) return (x < 0.0 || double_signbit(x)) ? copysign(M_PI, y) : y;
    if (x == 0.0) return copysign(M_PI_2, y);
    if (double_is_inf(x) && double_is_inf(y))
        return copysign(x < 0.0 ? 3.0*M_PI_4 : M_PI_4, y);
    if (double_is_inf(y)) return copysign(M_PI_2, y);
    if (double_is_inf(x)) return x < 0.0 ? copysign(M_PI, y) : copysign(0.0, y);
    double angle = atan(fabs(y/x));
    if (x < 0.0) angle = M_PI - angle;
    return copysign(angle, y);
}
double asin(double value) {
    if (double_is_nan(value) || fabs(value) > 1.0) return make_nan();
    return atan2(value, sqrt((1.0-value)*(1.0+value)));
}
double acos(double value) {
    if (double_is_nan(value) || fabs(value) > 1.0) return make_nan();
    return atan2(sqrt((1.0-value)*(1.0+value)), value);
}

float expf(float value) { return (float)exp((double)value); }
float logf(float value) { return (float)log((double)value); }
float powf(float base, float exponent) { return (float)pow((double)base, (double)exponent); }
float sinf(float value) { return (float)sin((double)value); }
float cosf(float value) { return (float)cos((double)value); }
float tanf(float value) { return (float)tan((double)value); }
float atanf(float value) { return (float)atan((double)value); }
float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
float asinf(float value) { return (float)asin((double)value); }
float acosf(float value) { return (float)acos((double)value); }
float fmodf(float value, float divisor) { return (float)fmod((double)value, (double)divisor); }
float fminf(float left, float right) { return (float)fmin((double)left, (double)right); }
float fmaxf(float left, float right) { return (float)fmax((double)left, (double)right); }
float copysignf(float magnitude, float sign) { return (float)copysign((double)magnitude, (double)sign); }
float modff(float value, float *integral) {
    double whole;
    double fraction = modf((double)value, &whole);
    if (integral) *integral = (float)whole;
    return (float)fraction;
}
float frexpf(float value, int *exponent) { return (float)frexp((double)value, exponent); }
float ldexpf(float value, int exponent) { return (float)ldexp((double)value, exponent); }
float scalbnf(float value, int exponent) { return ldexpf(value, exponent); }
