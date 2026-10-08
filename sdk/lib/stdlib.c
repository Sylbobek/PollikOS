/* Basic stdlib routines, process exit and the exit-status contract.
 * The kernel masks the low 8 bits of the exit syscall argument. */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pollikos/syscall.h>
#include <pollikos/fs.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
extern void __stdio_flush_all(void);
void _exit(int status) {
    (void)__pollikos_syscall1(USER_EXIT, (uint64_t)(uintptr_t)(unsigned)status & 255u);
    for (;;) {}
}
void exit(int status) {
    /* Flush buffered FILE writers before the process disappears. */
    __stdio_flush_all();
    _exit(status);
}
void abort(void) {
    /* Avoid stdio/allocation here: abort may be diagnosing a damaged heap. */
    char message[] = "pollikc: abort at 0x0000000000000000\n";
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<16;i++){message[34-i]=digits[caller&15];caller>>=4;}
    (void)__pollikos_syscall3(USER_WRITE, 2, (uint64_t)(uintptr_t)message, sizeof(message)-1);
    _exit(134);
}
int atoi(const char *text) { return (int)strtol(text, NULL, 10); }
long atol(const char *text) { return strtol(text, NULL, 10); }
static int digit_value(int character) {
    if (character >= '0' && character <= '9') return character-'0';
    if (character >= 'a' && character <= 'z') return character-'a'+10;
    if (character >= 'A' && character <= 'Z') return character-'A'+10;
    return -1;
}
/* Shared 64-bit core: decimal, octal (leading 0), hexadecimal (0x) and base=0
 * auto-detection; overflow clamps and sets ERANGE like the C standard. */
static unsigned long long parse_integer(const char *text, char **end, int base,
                                        int *negative, int *overflow) {
    const char *cursor = text;
    while (*cursor == ' ' || (*cursor >= 9 && *cursor <= 13)) ++cursor;
    *negative = 0;
    if (*cursor == '+' || *cursor == '-') { *negative = *cursor == '-'; ++cursor; }
    if (base == 0) {
        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) { base = 16; cursor += 2; }
        else if (cursor[0] == '0') base = 8;
        else base = 10;
    } else if (base == 16 && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    if (base < 2 || base > 36) { if (end) *end = (char *)text; errno = EINVAL; return 0; }
    unsigned long long value = 0;
    int any = 0;
    *overflow = 0;
    for (;; ++cursor) {
        int digit = digit_value((unsigned char)*cursor);
        if (digit < 0 || digit >= base) break;
        any = 1;
        if (value > (ULLONG_MAX-(unsigned long long)digit)/(unsigned long long)base) *overflow = 1;
        else value = value*(unsigned long long)base + (unsigned long long)digit;
    }
    if (end) *end = (char *)(any ? cursor : text);
    return value;
}
long strtol(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = parse_integer(text, end, base, &negative, &overflow);
    unsigned long long limit = negative ? (unsigned long long)LONG_MAX+1ull : (unsigned long long)LONG_MAX;
    if (overflow || value > limit) {
        errno = ERANGE;
        return negative ? LONG_MIN : LONG_MAX;
    }
    return negative ? (value == (unsigned long long)LONG_MAX+1ull ? LONG_MIN : -(long)value) : (long)value;
}
unsigned long strtoul(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = parse_integer(text, end, base, &negative, &overflow);
    if (overflow || value > ULONG_MAX) {
        errno = ERANGE;
        return negative ? 0ul : ULONG_MAX;
    }
    return negative ? (unsigned long)(-(long)value) : (unsigned long)value;
}
long long strtoll(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = parse_integer(text, end, base, &negative, &overflow);
    unsigned long long limit = negative ? (unsigned long long)LLONG_MAX+1ull : (unsigned long long)LLONG_MAX;
    if (overflow || value > limit) {
        errno = ERANGE;
        return negative ? LLONG_MIN : LLONG_MAX;
    }
    return negative ? (value == (unsigned long long)LLONG_MAX+1ull ? LLONG_MIN : -(long long)value)
                    : (long long)value;
}
unsigned long long strtoull(const char *text, char **end, int base) {
    int negative, overflow;
    unsigned long long value = parse_integer(text, end, base, &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return negative ? 0ull : ULLONG_MAX;
    }
    return negative ? (unsigned long long)(-(long long)value) : value;
}
static double scale10(double value, int exponent) {
    static const double powers[] = {
        1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8,
        1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16
    };
    while (exponent >= 16) { value *= 1e16; exponent -= 16; }
    while (exponent <= -16) { value *= 1e-16; exponent += 16; }
    if (exponent > 0) value *= powers[exponent-1];
    else if (exponent < 0) value /= powers[-exponent-1];
    return value;
}
double strtod(const char *text, char **end) {
    const char *start = text, *cursor = text;
    while (*cursor == ' ' || (*cursor >= 9 && *cursor <= 13)) ++cursor;
    int negative = 0;
    if (*cursor == '+' || *cursor == '-') { negative = *cursor == '-'; ++cursor; }
    int hexadecimal = cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X');
    if (hexadecimal) cursor += 2;
    unsigned radix = hexadecimal ? 16 : 10;
    double value = 0.0, place = 1.0;
    int digits = 0, after_point = 0, nonzero = 0;
    for (;;) {
        int digit = digit_value((unsigned char)*cursor);
        if (digit >= 0 && (unsigned)digit < radix) {
            ++digits;
            if (digit) nonzero = 1;
            if (after_point) { place /= (double)radix; value += (double)digit*place; }
            else value = value*(double)radix+(double)digit;
            ++cursor;
        } else if (*cursor == '.' && !after_point) {
            after_point = 1;
            ++cursor;
        } else break;
    }
    if (!digits) { if (end) *end = (char *)start; return 0.0; }
    if (hexadecimal) {
        if (*cursor == 'p' || *cursor == 'P') {
            const char *exponent_start = cursor;
            ++cursor;
            int exponent_negative = 0;
            if (*cursor == '+' || *cursor == '-') { exponent_negative = *cursor == '-'; ++cursor; }
            int exponent = 0, exponent_digits = 0;
            while (*cursor >= '0' && *cursor <= '9') {
                exponent_digits = 1;
                if (exponent < 10000) exponent = exponent*10+(*cursor-'0');
                ++cursor;
            }
            if (exponent_digits) value = ldexp(value, exponent_negative ? -exponent : exponent);
            else cursor = exponent_start;
        }
    } else if (*cursor == 'e' || *cursor == 'E') {
        const char *exponent_start = cursor++;
        int exponent_negative = 0;
        if (*cursor == '+' || *cursor == '-') { exponent_negative = *cursor == '-'; ++cursor; }
        int exponent = 0, exponent_digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            exponent_digits = 1;
            if (exponent < 10000) exponent = exponent*10+(*cursor-'0');
            ++cursor;
        }
        if (exponent_digits) value = scale10(value, exponent_negative ? -exponent : exponent);
        else cursor = exponent_start;
    }
    if (end) *end = (char *)cursor;
    if (negative) value = -value;
    if ((value == 0.0 && nonzero) || value > 1.7976931348623157e308 ||
        value < -1.7976931348623157e308)
        errno = ERANGE;
    return value;
}
float strtof(const char *text, char **end) { return (float)strtod(text, end); }
static void swap_bytes(unsigned char *left, unsigned char *right, size_t size) {
    while (size--) { unsigned char temporary = *left; *left++ = *right; *right++ = temporary; }
}
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    if (!base || !compare || size == 0 || count < 2) return;
    unsigned char *array = base;
    for (size_t gap = count/2; gap > 0; gap /= 2) {
        for (size_t index = gap; index < count; ++index) {
            for (size_t cursor = index; cursor >= gap &&
                 compare(array+(cursor-gap)*size, array+cursor*size) > 0; cursor -= gap)
                swap_bytes(array+(cursor-gap)*size, array+cursor*size, size);
        }
    }
}
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
    const unsigned char *array = base;
    while (count && key && compare) {
        size_t middle = count/2;
        int result = compare(key, array+middle*size);
        if (result < 0) {
            count = middle;
        } else if (result > 0) {
            array += (middle+1)*size;
            count -= middle+1;
        } else {
            return (void *)(array+middle*size);
        }
    }
    return NULL;
}
int mkstemp(char *template) {
    if (!template) { errno = EINVAL; return -1; }
    size_t length = strlen(template);
    if (length < 6 || strcmp(template+length-6, "XXXXXX") != 0) { errno = EINVAL; return -1; }
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static unsigned counter;
    unsigned seed = (unsigned)getpid()*2654435761u + (unsigned)pollikos_clock_ticks()
                  + (++counter)*40503u;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        unsigned value = seed + attempt*2246822519u;
        for (int index = 0; index < 6; ++index) {
            template[length-6+index] = alphabet[value % 36];
            value /= 36;
        }
        int fd = open(template, O_RDWR|O_CREAT|O_EXCL);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}
char *getenv(const char *name) {
    size_t length = 0;
    while (name[length] && name[length] != '=') ++length;
    if (!length) return NULL;
    for (char **entry = environ; entry && *entry; ++entry) {
        size_t index = 0;
        while (index < length && entry[0][index] == name[index]) ++index;
        if (index == length && entry[0][index] == '=') return entry[0]+length+1;
    }
    return NULL;
}
/* Environment mutation is process-local: the first mutation deep-copies the
 * startup environment into malloc'd storage and repoints environ at it, so
 * neither the startup stack nor a spawned child ever shares strings. */
#define ENV_OWN_MAX 32
static char *env_owned[ENV_OWN_MAX+1];
static unsigned env_owned_count;
static int env_copied;
static int env_copy(void) {
    if (env_copied) return 0;
    env_owned_count = 0;
    for (char **entry = environ; entry && *entry && env_owned_count < ENV_OWN_MAX; ++entry) {
        size_t length = strlen(*entry);
        char *copy = malloc(length+1);
        if (!copy) return -1;
        memcpy(copy, *entry, length+1);
        env_owned[env_owned_count++] = copy;
    }
    env_owned[env_owned_count] = NULL;
    environ = env_owned;
    env_copied = 1;
    return 0;
}
static int env_slot(const char *name, size_t length) {
    for (unsigned index = 0; index < env_owned_count; ++index)
        if (!strncmp(env_owned[index], name, length) && env_owned[index][length] == '=')
            return (int)index;
    return -1;
}
int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!value) value = "";
    if (env_copy() != 0) { errno = ENOMEM; return -1; }
    size_t name_length = strlen(name), value_length = strlen(value);
    char *fresh = malloc(name_length + value_length + 2);
    if (!fresh) { errno = ENOMEM; return -1; }
    memcpy(fresh, name, name_length);
    fresh[name_length] = '=';
    memcpy(fresh + name_length + 1, value, value_length + 1);
    int existing = env_slot(name, name_length);
    if (existing >= 0) {
        if (!overwrite) { free(fresh); return 0; }
        free(env_owned[existing]);
        env_owned[existing] = fresh;
        return 0;
    }
    if (env_owned_count >= ENV_OWN_MAX) { free(fresh); errno = ENOMEM; return -1; }
    env_owned[env_owned_count++] = fresh;
    env_owned[env_owned_count] = NULL;
    return 0;
}
int putenv(char *string) {
    if (!string) { errno = EINVAL; return -1; }
    char *equals = strchr(string, '=');
    if (!equals || equals == string) { errno = EINVAL; return -1; }
    if (env_copy() != 0) { errno = ENOMEM; return -1; }
    size_t name_length = (size_t)(equals-string);
    size_t total = strlen(string);
    /* PollikOS putenv copies: the caller's buffer lifetime never matters. */
    char *fresh = malloc(total+1);
    if (!fresh) { errno = ENOMEM; return -1; }
    memcpy(fresh, string, total+1);
    int existing = env_slot(string, name_length);
    if (existing >= 0) {
        free(env_owned[existing]);
        env_owned[existing] = fresh;
        return 0;
    }
    if (env_owned_count >= ENV_OWN_MAX) { free(fresh); errno = ENOMEM; return -1; }
    env_owned[env_owned_count++] = fresh;
    env_owned[env_owned_count] = NULL;
    return 0;
}
int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (env_copy() != 0) { errno = ENOMEM; return -1; }
    size_t length = strlen(name);
    for (unsigned index = 0; index < env_owned_count; ) {
        if (!strncmp(env_owned[index], name, length) && env_owned[index][length] == '=') {
            free(env_owned[index]);
            for (unsigned move = index; move < env_owned_count; ++move)
                env_owned[move] = env_owned[move+1];
            --env_owned_count;
        } else {
            ++index;
        }
    }
    return 0;
}
