#ifndef POLLIKOS_STDLIB_H
#define POLLIKOS_STDLIB_H
#include <stddef.h>
int abs(int value);
long labs(long value);
long long llabs(long long value);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
/* malloc family: process-private brk arena plus optional anonymous mmap for
 * large blocks. All pointers are 16-byte aligned. */
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *memory, size_t size);
void free(void *memory);
void exit(int status) __attribute__((noreturn));
void _exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atoi(const char *text);
long atol(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
long long strtoll(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
double strtod(const char *text, char **end);
float strtof(const char *text, char **end);
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
/* Creates an exclusively-created temporary file; template must end in XXXXXX. */
int mkstemp(char *template);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
extern char **environ;
#endif
