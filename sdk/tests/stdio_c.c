/* C7 FILE stream tests: modes, buffered IO, seeking, EOF/error state. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C7] FAIL: %s (errno=%s)\n", name, strerror(errno)); ++failures; } } while (0)
#define WORK "/tmp/c7"
static void under(char *out, const char *name) {
    strcpy(out, WORK "/");
    strcat(out, name);
}
static int mode_basic(void) {
    char file[96];
    under(file, "stdio.txt");
    remove(file);
    FILE *stream = fopen(file, "w");
    CHECK(stream != NULL, "fopen w");
    if (!stream) return 1;
    CHECK(fprintf(stream, "line %d %s\n", 7, "value") == 13, "fprintf");
    CHECK(fputs("second\n", stream) == 0, "fputs");
    CHECK(fputc('X', stream) == 'X', "fputc");
    CHECK(ftell(stream) == 21, "ftell after writes");
    CHECK(fclose(stream) == 0, "fclose flushes");
    struct stat info;
    CHECK(stat(file, &info) == 0 && info.st_size == 21, "flushed size");
    stream = fopen(file, "r");
    CHECK(stream != NULL, "fopen r");
    if (!stream) return 1;
    char buffer[64];
    CHECK(fgets(buffer, sizeof(buffer), stream) != NULL && strcmp(buffer, "line 7 value\n") == 0,
          "fgets line");
    CHECK(getc(stream) == 's', "getc");
    ungetc('s', stream);
    CHECK(getc(stream) == 's', "ungetc");
    CHECK(fgets(buffer, 4, stream) != NULL && strcmp(buffer, "eco") == 0, "bounded fgets");
    CHECK(fgets(buffer, sizeof(buffer), stream) != NULL && strcmp(buffer, "nd\n") == 0, "fgets rest");
    CHECK(getc(stream) == 'X', "getc last");
    CHECK(feof(stream) == 0, "not EOF yet");
    CHECK(getc(stream) == EOF, "EOF byte");
    CHECK(feof(stream) != 0, "feof set");
    clearerr(stream);
    CHECK(feof(stream) == 0, "clearerr clears EOF");
    CHECK(fseek(stream, 5, SEEK_SET) == 0, "fseek set");
    CHECK(ftell(stream) == 5, "ftell after seek");
    CHECK(fgetc(stream) == '7', "read after seek");
    CHECK(fseek(stream, -1, SEEK_END) == 0, "fseek end");
    CHECK(fgetc(stream) == 'X', "read at end-1");
    rewind(stream);
    CHECK(ftell(stream) == 0 && fgetc(stream) == 'l', "rewind");
    CHECK(fclose(stream) == 0, "fclose read");
    CHECK(remove(file) == 0, "remove basic file");
    return failures ? 1 : 42;
}
static int mode_modes(void) {
    char file[96];
    under(file, "modes.txt");
    remove(file);
    /* w+ creates, reads and writes through one stream. */
    FILE *stream = fopen(file, "w+");
    CHECK(stream != NULL, "fopen w+");
    if (!stream) return 1;
    CHECK(fwrite("abcdef", 1, 6, stream) == 6, "w+ write");
    CHECK(fseek(stream, 2, SEEK_SET) == 0, "w+ seek");
    char byte = 0;
    CHECK(fread(&byte, 1, 1, stream) == 1 && byte == 'c', "w+ read");
    CHECK(fclose(stream) == 0, "w+ close");
    /* r+ overwrites without truncating. */
    stream = fopen(file, "r+");
    CHECK(stream != NULL, "fopen r+");
    if (stream) {
        CHECK(fseek(stream, 0, SEEK_SET) == 0, "r+ seek");
        CHECK(fwrite("XY", 1, 2, stream) == 2, "r+ write");
        CHECK(fclose(stream) == 0, "r+ close");
    }
    stream = fopen(file, "r");
    char buffer[16] = {0};
    CHECK(stream && fread(buffer, 1, 6, stream) == 6 && memcmp(buffer, "XYcdef", 6) == 0,
          "r+ overwrite result");
    if (stream) fclose(stream);
    /* a appends at the current EOF for every write. */
    stream = fopen(file, "a");
    CHECK(stream != NULL, "fopen a");
    if (stream) {
        CHECK(fwrite("GH", 1, 2, stream) == 2, "append write");
        CHECK(fclose(stream) == 0, "append close");
    }
    stream = fopen(file, "a+");
    CHECK(stream != NULL, "fopen a+");
    if (stream) {
        CHECK(fseek(stream, 0, SEEK_SET) == 0, "a+ seek start");
        CHECK(fwrite("IJ", 1, 2, stream) == 2, "a+ append ignores seek");
        CHECK(fclose(stream) == 0, "a+ close");
    }
    struct stat info;
    CHECK(stat(file, &info) == 0 && info.st_size == 10, "append size");
    /* b variants are accepted and equivalent. */
    stream = fopen(file, "rb");
    CHECK(stream != NULL, "fopen rb");
    if (stream) fclose(stream);
    stream = fopen(file, "wb");
    CHECK(stream != NULL, "fopen wb truncates");
    if (stream) fclose(stream);
    CHECK(stat(file, &info) == 0 && info.st_size == 0, "wb truncated");
    /* Malformed modes are rejected. */
    errno = 0;
    CHECK(fopen(file, "q") == NULL && errno == EINVAL, "bad mode EINVAL");
    errno = 0;
    CHECK(fopen(file, "r++") == NULL && errno == EINVAL, "bad mode r++");
    CHECK(remove(file) == 0, "remove file");
    errno = 0;
    CHECK(remove(file) == -1 && errno == ENOENT, "remove missing ENOENT");
    return failures ? 1 : 42;
}
static int mode_seek(void) {
    /* WINDOW covers the block boundary at 4096 plus the overwrite probe. */
    enum { SIZE = 20480, WINDOW = 8192 };
    char file[96];
    under(file, "seek.bin");
    remove(file);
    FILE *stream = fopen(file, "w+");
    CHECK(stream != NULL, "seek create");
    if (!stream) return 1;
    enum { SEED = 4096 };
    static char buffer[WINDOW];
    for (int window = 0; window < SIZE/SEED; ++window) {
        memset(buffer, 'a'+window, SEED);
        CHECK(fwrite(buffer, 1, SEED, stream) == SEED, "seek seed write");
    }
    /* Overwrite a block boundary and a middle offset through the stream. */
    CHECK(fseek(stream, 4090, SEEK_SET) == 0, "seek boundary");
    CHECK(fwrite("BOUNDARY", 1, 8, stream) == 8, "boundary overwrite");
    CHECK(fseek(stream, 10000, SEEK_SET) == 0, "seek middle");
    CHECK(fwrite("MIDDLE", 1, 6, stream) == 6, "middle overwrite");
    CHECK(ftell(stream) == 10006, "ftell after overwrite");
    CHECK(fseek(stream, -6, SEEK_END) == 0, "seek end");
    CHECK(ftell(stream) == SIZE-6, "ftell at end");
    CHECK(fseek(stream, 0, SEEK_SET) == 0, "rewind seek");
    static char readback[WINDOW];
    CHECK(fread(readback, 1, WINDOW, stream) == WINDOW, "read first block");
    CHECK(memcmp(readback+4090, "BOUNDARY", 8) == 0, "boundary bytes");
    CHECK(fseek(stream, 9998, SEEK_SET) == 0, "seek to middle");
    CHECK(fread(readback, 1, 10, stream) == 10 && memcmp(readback+2, "MIDDLE", 6) == 0,
          "middle bytes");
    CHECK(fclose(stream) == 0, "seek close");
    struct stat info;
    CHECK(stat(file, &info) == 0 && info.st_size == SIZE, "seek final size");
    CHECK(remove(file) == 0, "seek cleanup");
    return failures ? 1 : 42;
}
static int mode_errors(void) {
    char file[96];
    under(file, "errors.txt");
    remove(file);
    errno = 0;
    FILE *stream = fopen(WORK "/missing-file.txt", "r");
    CHECK(stream == NULL && errno == ENOENT, "fopen missing ENOENT");
    stream = fopen(file, "w");
    CHECK(stream != NULL, "error fixture create");
    if (!stream) return 1;
    char byte = 0;
    CHECK(fread(&byte, 1, 1, stream) == 0 && ferror(stream) != 0, "read on write-only stream");
    clearerr(stream);
    CHECK(fseek(stream, -1, SEEK_SET) == -1, "negative seek rejected");
    CHECK(fclose(stream) == 0, "error fixture close");
    stream = fopen(file, "r");
    CHECK(stream != NULL, "error fixture read");
    if (stream) {
        CHECK(fwrite("x", 1, 1, stream) == 0 && ferror(stream) != 0, "write on read-only stream");
        clearerr(stream);
        CHECK(fclose(stream) == 0, "error fixture read close");
    }
    errno = 0;
    CHECK(fopen("", "r") == NULL, "empty path rejected");
    errno = 0;
    CHECK(fopen(WORK, "r") == NULL && errno == EISDIR, "directory open rejected");
    CHECK(access(file, F_OK) == 0, "access F_OK");
    CHECK(access(file, R_OK) == 0, "access R_OK");
    CHECK(access(file, W_OK) == 0, "access W_OK");
    errno = 0;
    CHECK(access(file, R_OK|W_OK|X_OK) == 0, "access RXW regular file");
    errno = 0;
    CHECK(access(WORK "/missing", F_OK) == -1 && errno == ENOENT, "access missing ENOENT");
    errno = 0;
    CHECK(access(WORK, X_OK) == -1 && errno == EACCES, "access X_OK directory");
    CHECK(remove(file) == 0, "error fixture cleanup");
    return failures ? 1 : 42;
}
static unsigned long stack_walk(unsigned depth) {
    volatile unsigned char frame[128];
    frame[0] = (unsigned char)depth;
    frame[127] = (unsigned char)(depth >> 8);
    if (!depth) return frame[0]+frame[127];
    return stack_walk(depth-1)+frame[0]+frame[127];
}
static int mode_deep(void) {
    /* Recursive live frames exceed the old 128 KiB bound. */
    volatile unsigned long total = stack_walk(1500);
    if (total == 0) return 1;
    printf("[c7] deep stack ok\n");
    return 42;
}
static int mode_fds(void) {
    /* Open the same file up to the raised descriptor limit. */
    char file[96];
    under(file, "fds.txt");
    remove(file);
    FILE *create = fopen(file, "w");
    if (!create) return 1;
    fputs("descriptor limit\n", create);
    fclose(create);
    int descriptors[USER_FD_LIMIT];
    int count = 0;
    for (; count < USER_FD_LIMIT-3; ++count) {
        int fd = open(file, O_RDONLY);
        if (fd < 0) break;
        descriptors[count] = fd;
    }
    CHECK(count == USER_FD_LIMIT-3, "descriptor limit raised");
    for (int index = 0; index < count; ++index) CHECK(close(descriptors[index]) == 0, "close fd");
    CHECK(remove(file) == 0, "fd fixture cleanup");
    return failures ? 1 : 42;
}
static int mode_dir(void) {
    DIR *directory = opendir(WORK);
    CHECK(directory != NULL, "opendir /tmp/c7");
    if (!directory) return 1;
    struct dirent *entry;
    int entries = 0;
    while ((entry = readdir(directory)) != NULL) ++entries;
    CHECK(closedir(directory) == 0, "closedir");
    if (entries < 0) return 1;
    return 42;
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "basic";
    if (mkdir(WORK, 0755) != 0 && errno != EEXIST) return 1;
    if (strcmp(mode, "basic") == 0) return mode_basic();
    if (strcmp(mode, "modes") == 0) return mode_modes();
    if (strcmp(mode, "seek") == 0) return mode_seek();
    if (strcmp(mode, "errors") == 0) return mode_errors();
    if (strcmp(mode, "deep") == 0) return mode_deep();
    if (strcmp(mode, "fds") == 0) return mode_fds();
    if (strcmp(mode, "dir") == 0) return mode_dir();
    printf("[C7] unknown stdio mode %s\n", mode);
    return 1;
}
