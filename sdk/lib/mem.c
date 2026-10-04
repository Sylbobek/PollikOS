/* Portable freestanding memory routines. no_builtin/optnone keeps the compiler
 * from recognizing these loops and replacing them with recursive calls. */
#include <string.h>
#define POLLIKOS_MEMORY_OPT __attribute__((optnone))
POLLIKOS_MEMORY_OPT void *memcpy(void *destination, const void *source, size_t count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    for (size_t i = 0; i < count; ++i) to[i] = from[i];
    return destination;
}
POLLIKOS_MEMORY_OPT void *memmove(void *destination, const void *source, size_t count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    if (to < from) {
        for (size_t i = 0; i < count; ++i) to[i] = from[i];
    } else if (to > from) {
        for (size_t i = count; i; --i) to[i-1] = from[i-1];
    }
    return destination;
}
POLLIKOS_MEMORY_OPT void *memset(void *destination, int value, size_t count) {
    unsigned char *to = destination;
    for (size_t i = 0; i < count; ++i) to[i] = (unsigned char)value;
    return destination;
}
POLLIKOS_MEMORY_OPT int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
void *memchr(const void *memory, int value, size_t count) {
    const unsigned char *bytes = memory;
    for (size_t i = 0; i < count; ++i)
        if (bytes[i] == (unsigned char)value) return (void *)(bytes+i);
    return NULL;
}
