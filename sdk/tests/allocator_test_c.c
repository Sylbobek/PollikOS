/* Focused userspace libc and allocator tests. Mode selectors (argv[1]) let the
 * kernel test verify exit-status propagation and injected allocation failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pollikos/syscall.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C3] FAIL: %s\n", name); ++failures; } } while (0)
static void test_memory(void) {
    char source[64], copy[64], overlap[32];
    for (int i = 0; i < 64; ++i) source[i] = (char)i;
    memset(copy, 0, sizeof(copy));
    CHECK(memcpy(copy, source, 64) == copy && memcmp(copy, source, 64) == 0, "memcpy");
    CHECK(memcmp(source, copy, 0) == 0, "memcmp zero");
    memset(copy, 0x5a, 32);
    CHECK((unsigned char)copy[0] == 0x5a && (unsigned char)copy[31] == 0x5a &&
          copy[32] == source[32], "memset");
    for (int i = 0; i < 32; ++i) overlap[i] = (char)('a'+i);
    memmove(overlap+4, overlap, 16);
    CHECK(overlap[4] == 'a' && overlap[19] == 'p', "memmove forward");
    memmove(overlap, overlap+4, 16);
    CHECK(overlap[0] == 'a' && overlap[15] == 'p', "memmove backward");
    CHECK(memchr(source, 7, 64) == source+7 && memchr(source, 99, 64) == NULL, "memchr");
}
static void test_strings(void) {
    char buffer[32], padded[8];
    CHECK(strlen("") == 0 && strlen("pollikos") == 8, "strlen");
    CHECK(strnlen("abc", 2) == 2 && strnlen("abc", 4) == 3, "strnlen");
    CHECK(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0 && strcmp("abd", "abc") > 0, "strcmp");
    CHECK(strncmp("abcx", "abcy", 3) == 0 && strncmp("abc", "abd", 3) < 0, "strncmp");
    strcpy(buffer, "hello");
    strcat(buffer, " world");
    CHECK(strcmp(buffer, "hello world") == 0, "strcpy/strcat");
    CHECK(strchr(buffer, 'w') == buffer+6 && strrchr(buffer, 'l') == buffer+9, "strchr/strrchr");
    CHECK(strchr(buffer, 'z') == NULL, "strchr missing");
    memset(padded, 'x', sizeof(padded));
    strncpy(padded, "abc", sizeof(padded));
    CHECK(padded[0] == 'a' && padded[3] == 0 && padded[7] == 0, "strncpy padding");
}
static void test_format(void) {
    char buffer[128];
    int length = snprintf(buffer, sizeof(buffer), "%s %c %d %i %u %x %X %p %%",
                          "str", 'q', -12345, 7, 4294967295u, 0xdeadbeefu, 0xabcdu,
                          (void *)(uintptr_t)0x1234);
    CHECK(strcmp(buffer, "str q -12345 7 4294967295 deadbeef ABCD 0x1234 %") == 0,
          "snprintf conversions");
    CHECK(length == (int)strlen(buffer), "snprintf return");
    CHECK(snprintf(buffer, 5, "abcdef") == 6 && strcmp(buffer, "abcd") == 0, "snprintf truncation");
    CHECK(snprintf(buffer, sizeof(buffer), "%5d|%-5d|%05d", 42, 42, 42) == 17 &&
          strcmp(buffer, "   42|42   |00042") == 0, "snprintf width");
    CHECK(snprintf(buffer, sizeof(buffer), "%ld %lu %lx", 1234567890123L, 42UL, 0xabcdefUL) == 23 &&
          strcmp(buffer, "1234567890123 42 abcdef") == 0, "snprintf long");
    CHECK(snprintf(NULL, 0, "abc%d", 7) == 4, "snprintf NULL buffer");
}
static void test_allocator(void) {
    struct { void *pointer; unsigned char pattern; size_t size; } blocks[256];
    for (int i = 0; i < 256; ++i) {
        blocks[i].size = (size_t)(i%17)*8 + 1;
        blocks[i].pattern = (unsigned char)i;
        blocks[i].pointer = malloc(blocks[i].size);
        CHECK(blocks[i].pointer != NULL, "many small allocations");
        if (blocks[i].pointer) memset(blocks[i].pointer, i, blocks[i].size);
    }
    for (int i = 0; i < 256; ++i)
        CHECK(blocks[i].pointer && ((unsigned char *)blocks[i].pointer)[0] == blocks[i].pattern,
              "small allocation patterns");
    for (int i = 0; i < 256; i += 2) { free(blocks[i].pointer); blocks[i].pointer = NULL; }
    unsigned char *reused = malloc(16);
    CHECK(reused != NULL, "freed block reuse");
    free(reused);
    for (int i = 1; i < 256; i += 2) { free(blocks[i].pointer); blocks[i].pointer = NULL; }
    void *coalesced = malloc(4096);
    CHECK(coalesced != NULL, "coalesced allocation after full free");
    memset(coalesced, 0xa5, 4096);
    free(coalesced);
    unsigned int *zeroed = calloc(64, sizeof(unsigned int));
    CHECK(zeroed != NULL, "calloc");
    int all_zero = zeroed != NULL;
    for (int i = 0; zeroed && i < 64; ++i) if (zeroed[i]) all_zero = 0;
    CHECK(all_zero, "calloc zeroing");
    free(zeroed);
    char *text = malloc(16);
    CHECK(text != NULL, "realloc source");
    if (text) {
        strcpy(text, "0123456789abcde");
        char *grown = realloc(text, 256);
        CHECK(grown != NULL && strcmp(grown, "0123456789abcde") == 0, "realloc grow preserves");
        if (grown) {
            memset(grown+16, 'z', 240);
            char *shrunk = realloc(grown, 32);
            CHECK(shrunk != NULL && strcmp(shrunk, "0123456789abcde") == 0, "realloc shrink preserves");
            free(shrunk);
        }
    }
    void *fragmented[64];
    for (int i = 0; i < 64; ++i) { fragmented[i] = malloc(48); CHECK(fragmented[i] != NULL, "fragment allocation"); }
    for (int i = 0; i < 64; i += 2) { free(fragmented[i]); fragmented[i] = NULL; }
    for (int i = 0; i < 64; i += 2) { fragmented[i] = malloc(48); CHECK(fragmented[i] != NULL, "fragment reuse"); }
    for (int i = 0; i < 64; ++i) free(fragmented[i]);
    void *zero_a = calloc(0, 16), *zero_b = calloc(16, 0);
    CHECK(zero_a != NULL && zero_b != NULL, "zero-size calloc");
    free(zero_a); free(zero_b);
    void *fresh = realloc(NULL, 8);
    CHECK(fresh != NULL, "realloc NULL is malloc");
    CHECK(realloc(fresh, 0) == NULL, "realloc zero frees");
    void *zero_size = malloc(0);
    CHECK(zero_size != NULL, "malloc(0) unique pointer");
    free(zero_size);
    errno = 0;
    CHECK(calloc((size_t)-1, (size_t)-1) == NULL && errno == ENOMEM, "calloc overflow");
    errno = 0;
    CHECK(malloc((size_t)-1) == NULL && errno == ENOMEM, "malloc overflow");
    unsigned char *large = malloc(256*1024);
    CHECK(large != NULL, "large anonymous allocation");
    if (large) { large[0] = 1; large[256*1024-1] = 2; CHECK(large[0] == 1 && large[256*1024-1] == 2, "large write"); }
    unsigned char *larger = realloc(large, 512*1024);
    CHECK(larger != NULL && larger[0] == 1, "large realloc grow");
    free(larger);
    unsigned char *big = malloc(5*1024*1024);
    CHECK(big != NULL, "multi-megabyte brk allocation");
    if (big) { big[0] = 3; big[5*1024*1024-1] = 4; CHECK(big[0] == 3 && big[5*1024*1024-1] == 4, "big write"); }
    free(big);
    CHECK(getpid() > 0, "getpid");
    long before = pollikos_monotonic_ms();
    CHECK(sleep_ms(10) == 0, "sleep_ms");
    CHECK(pollikos_monotonic_ms() >= before+10, "monotonic time advances");
}
static int allocation_failure_mode(void) {
    volatile uint64_t *cell = (volatile uint64_t *)(uintptr_t)(USER_HEAP_BASE + 64);
    void *anchor = malloc(4096);
    if (!anchor) return 1;
    cell[0] = 0x1111;
    unsigned long spins = 0;
    while (cell[0] != 0x2222) { if (++spins > 2000000ul) return 2; sleep_ms(1); }
    errno = 0;
    void *huge = malloc(2*1024*1024);
    if (huge || errno != ENOMEM) return 3;
    cell[0] = 0x3333;
    spins = 0;
    while (cell[0] != 0x4444) { if (++spins > 2000000ul) return 4; sleep_ms(1); }
    void *recovered = malloc(2*1024*1024);
    if (!recovered) return 5;
    memset(recovered, 0x5a, 128);
    if (((unsigned char *)recovered)[127] != 0x5a) return 6;
    free(recovered);
    free(anchor);
    printf("[C3] allocator failure injection OK\n");
    return 42;
}
int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "exit0") == 0) exit(0);
        if (strcmp(argv[1], "exit1") == 0) exit(1);
        if (strcmp(argv[1], "exit255") == 0) exit(255);
        if (strcmp(argv[1], "return0") == 0) return 0;
        if (strcmp(argv[1], "allocfail") == 0) return allocation_failure_mode();
        if (strcmp(argv[1], "crash") == 0) {
            void *anchor = malloc(64);
            if (!anchor) return 1;
            *(volatile uint64_t *)(uintptr_t)0 = 1; /* contained user fault */
            return 1;
        }
    }
    test_memory();
    test_strings();
    test_format();
    test_allocator();
    printf("[C3] libc and allocator tests %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 42;
}
