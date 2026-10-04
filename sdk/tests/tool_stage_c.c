/* C7 compiler-like helper tool used by the multi-stage workflow test. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pollikos/time.h>
static long read_all(const char *path, char *buffer, size_t capacity) {
    FILE *stream = fopen(path, "rb");
    if (!stream) return -1;
    size_t got = fread(buffer, 1, capacity-1, stream);
    fclose(stream);
    buffer[got] = 0;
    return (long)got;
}
static int write_all(const char *path, const char *buffer, size_t length) {
    FILE *stream = fopen(path, "wb");
    if (!stream) return -1;
    size_t written = fwrite(buffer, 1, length, stream);
    if (fclose(stream) != 0) return -1;
    return written == length ? 0 : -1;
}
static int stage1(const char *input, const char *output) {
    char buffer[4096];
    long length = read_all(input, buffer, sizeof(buffer));
    if (length < 0) return 1;
    for (long index = 0; index < length; ++index)
        if (buffer[index] >= 'a' && buffer[index] <= 'z') buffer[index] -= 32;
    if (write_all(output, buffer, (size_t)length) != 0) return 2;
    return 42;
}
static int stage2(const char *input, const char *output) {
    char buffer[4096], result[4200];
    long length = read_all(input, buffer, sizeof(buffer));
    if (length < 0) return 1;
    unsigned long checksum = 0;
    for (long index = 0; index < length; ++index) checksum += (unsigned char)buffer[index];
    int header = snprintf(result, sizeof(result), "FINAL:%ld:%lu\n", length, checksum % 1000);
    if (header < 0 || (size_t)header >= sizeof(result)) return 2;
    memcpy(result+header, buffer, (size_t)length);
    if (write_all(output, result, (size_t)header+(size_t)length) != 0) return 3;
    return 42;
}
static int tempfile_mode(void) {
    char name[64];
    strcpy(name, "/tmp/c7/tmp-XXXXXX");
    int fd = mkstemp(name);
    if (fd < 0) { printf("[stage] mkstemp %s failed (errno=%s)\n", name, strerror(errno)); return 1; }
    close(fd);
    char payload[64];
    int length = snprintf(payload, sizeof(payload), "pid=%d:%s", getpid(), name);
    FILE *stream = fopen(name, "r+");
    if (!stream) { printf("[stage] fopen r+ failed (errno=%s)\n", strerror(errno)); close(fd); unlink(name); return 2; }
    if (fwrite(payload, 1, (size_t)length, stream) != (size_t)length) { printf("[stage] fwrite failed (errno=%s)\n", strerror(errno)); return 3; }
    if (fseek(stream, 0, SEEK_SET) != 0) { printf("[stage] fseek 0 failed (errno=%s)\n", strerror(errno)); return 4; }
    char check[64];
    size_t got = fread(check, 1, sizeof(check)-1, stream);
    check[got] = 0;
    if (strcmp(check, payload) != 0) { printf("[stage] verify mismatch %u '%s' vs '%s'\n", (unsigned)got, check, payload); return 5; }
    /* Yield so concurrent helpers interleave, then re-verify isolation. */
    sleep_ms(20);
    if (fseek(stream, 0, SEEK_SET) != 0) return 6;
    got = fread(check, 1, sizeof(check)-1, stream);
    check[got] = 0;
    if (strcmp(check, payload) != 0) return 7;
    fclose(stream);
    if (unlink(name) != 0) return 8;
    return 42;
}
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *mode = argv[1];
    if (strcmp(mode, "stage1") == 0 && argc == 4) return stage1(argv[2], argv[3]);
    if (strcmp(mode, "stage2") == 0 && argc == 4) return stage2(argv[2], argv[3]);
    if (strcmp(mode, "tempfile") == 0) return tempfile_mode();
    if (strcmp(mode, "argcheck") == 0) {
        /* Verifies large bounded argv arrays arrive intact. */
        if (argc < 50 || strcmp(argv[1], "argcheck") != 0) return 1;
        for (int index = 2; index < argc; ++index) {
            char expected[16];
            snprintf(expected, sizeof(expected), "arg%02d", index);
            if (strcmp(argv[index], expected) != 0) return 2;
        }
        return 42;
    }
    return 1;
}
