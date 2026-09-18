#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "../../kernel/system.h"
#define assert(x) ((void)0)
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
double strtod(const char *, char **);
