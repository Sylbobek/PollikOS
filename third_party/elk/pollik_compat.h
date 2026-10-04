#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#ifdef POLLIK_BROWSER_STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include "../../kernel/system.h"
#endif
#define assert(x) ((void)0)
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
double strtod(const char *, char **);
