/* C7 allocator stress approximating compiler data structures. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C7] FAIL: %s (errno=%s)\n", name, strerror(errno)); ++failures; } } while (0)
static int compare_strings(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}
static int mode_symbols(void) {
    enum { SYMBOLS = 1500 };
    char **names = malloc(SYMBOLS * sizeof(char *));
    unsigned long *values = malloc(SYMBOLS * sizeof(unsigned long));
    CHECK(names && values, "symbol table arrays");
    if (!names || !values) return 1;
    for (unsigned index = 0; index < SYMBOLS; ++index) {
        char generated[32];
        snprintf(generated, sizeof(generated), "symbol_%04u_%lu", index, (unsigned long)index*2654435761u);
        names[index] = strdup(generated);
        values[index] = (unsigned long)index*31u + 7u;
    }
    for (unsigned index = 0; index < SYMBOLS; ++index)
        if (!names[index] || strlen(names[index]) < 10) { CHECK(0, "symbol strings"); break; }
    qsort(names, SYMBOLS, sizeof(char *), compare_strings);
    for (unsigned index = 1; index < SYMBOLS; ++index)
        if (strcmp(names[index-1], names[index]) > 0) { CHECK(0, "sorted symbol order"); break; }
    for (unsigned index = 0; index < SYMBOLS; index += 97) {
        const char *key = names[index];
        void *found = bsearch(&key, names, SYMBOLS, sizeof(char *), compare_strings);
        CHECK(found != NULL, "bsearch symbol");
    }
    for (unsigned index = 0; index < SYMBOLS; ++index) free(names[index]);
    free(names);
    free(values);
    return failures ? 1 : 42;
}
static int mode_growth(void) {
    /* Token stream with repeated realloc growth plus temporary buffers. */
    size_t capacity = 64, count = 0;
    unsigned long *tokens = malloc(capacity * sizeof(unsigned long));
    CHECK(tokens != NULL, "token stream");
    if (!tokens) return 1;
    for (unsigned index = 0; index < 5000; ++index) {
        if (count == capacity) {
            capacity *= 2;
            unsigned long *grown = realloc(tokens, capacity * sizeof(unsigned long));
            CHECK(grown != NULL, "realloc growth");
            if (!grown) break;
            tokens = grown;
        }
        tokens[count++] = (unsigned long)index ^ 0x5a5aa5a5u;
    }
    unsigned long checksum = 1469598103934665603ul;
    for (size_t index = 0; index < count; ++index) {
        checksum ^= tokens[index];
        checksum *= 1099511628211ul;
    }
    CHECK(count == 5000 && checksum != 0, "token stream intact");
    free(tokens);
    for (unsigned index = 0; index < 200; ++index) {
        char *temporary = malloc(512 + (index % 4) * 512);
        CHECK(temporary != NULL, "temporary buffers");
        if (!temporary) break;
        memset(temporary, (int)(index & 0xff), 512 + (index % 4) * 512);
        char *shrunk = realloc(temporary, 256);
        CHECK(shrunk != NULL, "temporary shrink");
        free(shrunk);
    }
    return failures ? 1 : 42;
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "symbols";
    if (strcmp(mode, "symbols") == 0) return mode_symbols();
    if (strcmp(mode, "growth") == 0) return mode_growth();
    printf("[C7] unknown allocator mode %s\n", mode);
    return 1;
}
