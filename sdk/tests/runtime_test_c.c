/* Concurrent C runtime: process-private heap patterns across preemption and
 * blocking sleeps, plus a real file read. The kernel test runs four peers and
 * checks identical virtual heap addresses map distinct PID-tagged contents. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pollikos/fs.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t pid = (uint64_t)getpid();
    uint64_t *pattern = malloc(4096);
    unsigned char *small = malloc(64);
    void *zero = calloc(16, 16);
    if (!pattern || !small || !zero) return 1;
    pattern[0] = pid << 32;
    int fd = open("/etc/read_test.txt", O_RDONLY);
    char buffer[8];
    long count = -1;
    if (fd >= 0) { count = read(fd, buffer, 5); close(fd); }
    if (count != 5 || memcmp(buffer, "Alpha", 5) != 0) return 2;
    for (uint64_t i = 0; i < 4; ++i) {
        pattern[i] = (pid << 32) | i;
        for (volatile unsigned long spin = 0; spin < 800000ul; ++spin) {}
        if (sleep_ms(5) != 0) return 3;
        for (uint64_t j = 0; j <= i; ++j)
            if (pattern[j] != ((pid << 32) | j)) return 4;
    }
    if ((uint64_t)getpid() != pid) return 5;
    printf("[C3] runtime pid=%d ticks=%ld\n", (int)pid, pollikos_clock_ticks());
    free(pattern);
    free(small);
    free(zero);
    return 42;
}
