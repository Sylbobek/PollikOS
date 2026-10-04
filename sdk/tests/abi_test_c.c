/* PollikOS C ABI and language-runtime test. Built at -O0 and -O2 through the
 * SDK; exits 42 only when every check passes. Integer-only on purpose. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <pollikos/types.h>
#include <pollikos/version.h>
#include <pollikos/memory.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C4] FAIL: %s\n", name); ++failures; } } while (0)

int global_initialized = 0x12345678;
const int global_const = 42;
int global_bss[64];
char global_text[16] = "pollikos";
static char static_buffer[128];
static int static_counter;

struct pair { int32_t left; int64_t right; };
struct big { uint64_t word[6]; };

static int64_t sum8(int64_t a, int64_t b, int64_t c, int64_t d,
                    int64_t e, int64_t f, int64_t g, int64_t h) {
    return a+b+c+d+e+f+g+h;
}
static int64_t sum12(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f,
                     int64_t g, int64_t h, int64_t i, int64_t j, int64_t k, int64_t l) {
    return a+b+c+d+e+f+g+h+i+j+k+l;
}
static struct pair make_pair(int32_t left, int64_t right) {
    struct pair result = {left, right};
    return result;
}
static struct big make_big(uint64_t seed) {
    struct big result;
    for (int index = 0; index < 6; ++index) result.word[index] = seed + (uint64_t)index;
    return result;
}
static uint64_t big_total(struct big value) {
    uint64_t total = 0;
    for (int index = 0; index < 6; ++index) total += value.word[index];
    return total;
}
__attribute__((noinline)) static int recursion(int depth) {
    if (depth <= 0) return 0;
    volatile int local = depth;
    return local + recursion(depth-1);
}
static int add_one(int value) { return value + 1; }
static int times_two(int value) { return value * 2; }
__attribute__((noinline)) static int alignment_probe(int depth) {
    _Alignas(16) unsigned char aligned[16];
    if (((uintptr_t)aligned & 15u) != 0) return 0;
    if (depth <= 0) return 1;
    return alignment_probe(depth-1);
}
static int counter_function(void) {
    static int calls;
    return ++calls;
}
int main(int argc, char **argv) {
    (void)argv;
    CHECK(sizeof(int8_t) == 1 && sizeof(uint8_t) == 1, "8-bit sizes");
    CHECK(sizeof(int16_t) == 2 && sizeof(uint16_t) == 2, "16-bit sizes");
    CHECK(sizeof(int32_t) == 4 && sizeof(uint32_t) == 4, "32-bit sizes");
    CHECK(sizeof(int64_t) == 8 && sizeof(uint64_t) == 8, "64-bit sizes");
    CHECK(sizeof(size_t) == 8 && sizeof(ssize_t) == 8 && sizeof(off_t) == 8, "size types");
    CHECK(sizeof(void *) == 8 && sizeof(intptr_t) == 8 && sizeof(long) == 8, "pointer/word sizes");
    CHECK(sizeof(pid_t) == 4, "stable pid type");
    int8_t signed8 = -128;
    uint8_t unsigned8 = 255;
    int16_t signed16 = -32768;
    uint16_t unsigned16 = 65535;
    int32_t signed32 = INT32_MIN;
    uint32_t unsigned32 = UINT32_MAX;
    int64_t signed64 = INT64_MIN;
    uint64_t unsigned64 = UINT64_MAX;
    CHECK(signed8 == -128 && unsigned8 == 255, "8-bit values");
    CHECK(signed16 == -32768 && unsigned16 == 65535, "16-bit values");
    CHECK(signed32 == -2147483647-1 && unsigned32 == 4294967295u, "32-bit values");
    CHECK(signed64 == (-9223372036854775807L-1) && unsigned64 == 18446744073709551615ul, "64-bit values");
    uint32_t endian = 0x01020304u;
    const unsigned char *bytes = (const unsigned char *)&endian;
    CHECK(bytes[0] == 4 && bytes[3] == 1, "little-endian layout");
    uint64_t dividend = 0x123456789abcdef0ul;
    CHECK(dividend / 1000u == 0x4a90be587de6eul && dividend % 1000u == 320, "64-bit division");
    int32_t signed_values[4] = {-7, -1, 0, 2147483647};
    CHECK(signed_values[0]*signed_values[1] == 7, "signed multiplication");
    CHECK((uint32_t)signed_values[3]+1u == 0x80000000u, "unsigned wrap at the sign boundary");
    CHECK((int)(size_t)(uintptr_t)0xdeadbeef == 0xdeadbeef, "size_t round trip");
    struct pair combo = make_pair(-5, 0x1122334455667788L);
    CHECK(combo.left == -5 && combo.right == 0x1122334455667788L, "small struct return");
    struct big aggregate = make_big(100);
    CHECK(big_total(aggregate) == 100+101+102+103+104+105, "large struct argument/return");
    CHECK(sum8(1,2,3,4,5,6,7,8) == 36, "eight register arguments");
    CHECK(sum12(1,2,3,4,5,6,7,8,9,10,11,12) == 78, "stack arguments");
    int (*operations[2])(int) = {add_one, times_two};
    CHECK(operations[0](41) == 42 && operations[1](21) == 42, "function pointers");
    /* 128 frames stay well inside the four-page user stack at -O0 too. */
    CHECK(recursion(128) == 8256, "bounded recursion");
    CHECK(counter_function() == 1 && counter_function() == 2, "static locals persist");
    int bss_zero = 1;
    for (int index = 0; index < 64; ++index) if (global_bss[index]) bss_zero = 0;
    CHECK(bss_zero, "BSS globals start zero");
    for (int index = 0; index < 64; ++index) global_bss[index] = index;
    CHECK(global_bss[0] == 0 && global_bss[63] == 63, "BSS globals are writable");
    CHECK(global_initialized == 0x12345678, "initialized global");
    CHECK(global_const == 42, "const global");
    strcpy(static_buffer, "static data");
    CHECK(strcmp(static_buffer, "static data") == 0 && static_counter == 0, "static storage");
    _Alignas(16) unsigned char aligned_local[16];
    CHECK(((uintptr_t)aligned_local & 15u) == 0, "main 16-byte alignment");
    CHECK(alignment_probe(32) == 1, "nested stack alignment");
    CHECK(pollikos_sdk_version() != NULL && pollikos_sdk_version()[0] != 0, "SDK version");
    CHECK(pollikos_page_size() == 4096, "page size");
    void *block = malloc(1234);
    CHECK(block != NULL, "malloc");
    CHECK(pollikos_heap_bytes() > 0, "heap accounting");
    free(block);
    CHECK(argc == 1, "argc");
    if (failures) {
        printf("[C4] ABI FAILED (%d failures)\n", failures);
        return 1;
    }
    printf("[C4] ABI OK: integer widths, structs, args, globals, pointers, recursion\n");
    return 42;
}
