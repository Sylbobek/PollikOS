/* C5 userspace filesystem mutation tests. All operations use the public SDK
 * API (no inline assembly, no private headers). Modes are selected by argv[1].
 * Success exits 42; failures print a diagnostic and exit 1. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pollikos/fs.h>
#include <sys/stat.h>
#include <pollikos/process.h>
#include <pollikos/time.h>
#include <pollikos/syscall.h>
#include <time.h>
#include <sys/time.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { \
        printf("[C5] FAIL: %s (errno=%s, ret=%ld)\n", name, strerror(errno), (long)result); \
        ++failures; \
    } } while (0)
#define DIRECTORY "/mutation"
static void under(char *out, const char *name) {
    strcpy(out, DIRECTORY "/");
    strcat(out, name);
}
static void pattern(char *buffer, unsigned long length, unsigned long seed) {
    for (unsigned long index = 0; index < length; ++index)
        buffer[index] = (char)((index*7u+seed) & 0x7f);
}
static long result;
static int mode_basic(void) {
    static const char content[] = "hello pollikos";
    char file[128];
    under(file, "basic.txt");
    unlink(file);
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "create new file");
    if (fd >= 0) {
        ssize_t written = write(fd, content, sizeof(content)-1);
        result = written;
        CHECK(written == (ssize_t)(sizeof(content)-1), "single write");
        result = close(fd);
        CHECK(result == 0, "close after write");
    }
    fd = open(file, O_RDONLY);
    result = fd;
    CHECK(fd >= 0, "reopen read-only");
    if (fd >= 0) {
        char buffer[32];
        ssize_t count = read(fd, buffer, sizeof(buffer));
        result = count;
        CHECK(count == (ssize_t)(sizeof(content)-1) && memcmp(buffer, content, count) == 0,
              "exact read back");
        close(fd);
    }
    struct stat info;
    result = stat(file, &info);
    CHECK(result == 0 && info.st_size == sizeof(content)-1, "stat size");
    result = unlink(file);
    CHECK(result == 0, "unlink");
    return failures ? 1 : 42;
}
static int mode_multi(void) {
    char file[128];
    under(file, "multi.txt");
    unlink(file);
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "create multi file");
    if (fd < 0) return 1;
    char chunk[7];
    for (int index = 0; index < 300; ++index) {
        chunk[0] = (char)('A' + index % 26);
        for (int i = 1; i < 6; ++i) chunk[i] = chunk[0];
        chunk[6] = '\n';
        ssize_t written = write(fd, chunk, sizeof(chunk));
        if (written != (ssize_t)sizeof(chunk)) { result = written; CHECK(0, "chunk write"); break; }
    }
    close(fd);
    struct stat info;
    result = stat(file, &info);
    CHECK(result == 0 && info.st_size == 300*7, "multi size");
    fd = open(file, O_RDONLY);
    result = fd;
    CHECK(fd >= 0, "reopen multi");
    if (fd >= 0) {
        for (int index = 0; index < 300; ++index) {
            ssize_t count = read(fd, chunk, sizeof(chunk));
            if (count != (ssize_t)sizeof(chunk) || chunk[0] != (char)('A' + index % 26) ||
                chunk[6] != '\n') { CHECK(0, "chunk read back"); break; }
        }
        close(fd);
    }
    result = unlink(file);
    CHECK(result == 0, "unlink multi");
    return failures ? 1 : 42;
}
static int mode_large(void) {
    enum { LARGE = 40960, WINDOW = 4096 };
    char file[128];
    under(file, "large.txt");
    unlink(file);
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "create large file");
    if (fd < 0) return 1;
    static char buffer[WINDOW];
    for (int window = 0; window < LARGE/WINDOW; ++window) {
        pattern(buffer, WINDOW, (unsigned long)window*31u);
        ssize_t written = write(fd, buffer, WINDOW);
        if (written != WINDOW) { result = written; CHECK(0, "large window write"); break; }
    }
    close(fd);
    struct stat info;
    result = stat(file, &info);
    CHECK(result == 0 && info.st_size == LARGE, "large size");
    fd = open(file, O_RDONLY);
    result = fd;
    CHECK(fd >= 0, "reopen large");
    if (fd >= 0) {
        static char expected[WINDOW];
        for (int window = 0; window < LARGE/WINDOW; ++window) {
            ssize_t count = read(fd, buffer, WINDOW);
            pattern(expected, WINDOW, (unsigned long)window*31u);
            if (count != WINDOW || memcmp(buffer, expected, WINDOW) != 0) {
                unsigned bad = 0;
                while (bad < WINDOW && buffer[bad] == expected[bad]) ++bad;
                printf("[C5] large mismatch window=%d offset=%u got=%d want=%d\n",
                       window, bad, (int)(unsigned char)buffer[bad],
                       (int)(unsigned char)expected[bad]);
                result = count;
                CHECK(0, "large window read back");
                break;
            }
        }
        close(fd);
    }
    result = unlink(file);
    CHECK(result == 0, "unlink large");
    return failures ? 1 : 42;
}
static int mode_append(void) {
    char file[128];
    under(file, "append.txt");
    unlink(file);
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "create append file");
    if (fd < 0) return 1;
    result = write(fd, "ABC", 3);
    CHECK(result == 3, "append prefix");
    close(fd);
    fd = open(file, O_WRONLY|O_APPEND);
    result = fd;
    CHECK(fd >= 0, "reopen append");
    if (fd >= 0) {
        result = write(fd, "DEF", 3);
        CHECK(result == 3, "append suffix");
        close(fd);
    }
    fd = open(file, O_RDONLY);
    char buffer[8];
    result = fd >= 0 ? read(fd, buffer, sizeof(buffer)) : -1;
    CHECK(result == 6 && memcmp(buffer, "ABCDEF", 6) == 0, "ABCDEF content");
    if (fd >= 0) close(fd);
    result = unlink(file);
    CHECK(result == 0, "unlink append");
    return failures ? 1 : 42;
}
static int mode_truncate(void) {
    char file[128];
    under(file, "trunc.txt");
    unlink(file);
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "create truncate file");
    if (fd < 0) return 1;
    static char buffer[5000];
    pattern(buffer, sizeof(buffer), 11);
    result = write(fd, buffer, sizeof(buffer));
    CHECK(result == (long)sizeof(buffer), "truncate seed write");
    close(fd);
    fd = open(file, O_WRONLY|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "reopen with O_TRUNC");
    if (fd >= 0) {
        struct stat info;
        result = fstat(fd, &info);
        CHECK(result == 0 && info.st_size == 0, "truncated size zero");
        result = write(fd, "0123456789", 10);
        CHECK(result == 10, "write after truncate");
        close(fd);
    }
    fd = open(file, O_RDONLY);
    char small[16];
    result = fd >= 0 ? read(fd, small, sizeof(small)) : -1;
    CHECK(result == 10 && memcmp(small, "0123456789", 10) == 0, "post-truncate content");
    if (fd >= 0) close(fd);
    result = unlink(file);
    CHECK(result == 0, "unlink truncate");
    return failures ? 1 : 42;
}
static int mode_dirs(void) {
    char directory[128], file[128], renamed[128];
    under(directory, "sub");
    under(file, "sub/f.txt");
    under(renamed, "sub/g.txt");
    rmdir(directory); /* tolerate leftovers from an interrupted run */
    errno = 0;
    result = mkdir(directory, 0755);
    CHECK(result == 0, "mkdir new directory");
    errno = 0;
    result = mkdir(directory, 0755);
    CHECK(result == -1 && errno == EEXIST, "mkdir existing directory EEXIST");
    errno = 0;
    result = open(directory, O_WRONLY);
    CHECK(result == -1 && errno == EISDIR, "write-open directory EISDIR");
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "create file in new directory");
    if (fd >= 0) { write(fd, "dir data", 8); close(fd); }
    errno = 0;
    result = unlink(directory);
    CHECK(result == -1 && errno == EISDIR, "unlink directory EISDIR");
    errno = 0;
    result = rmdir(directory);
    CHECK(result == -1 && errno == ENOTEMPTY, "rmdir non-empty ENOTEMPTY");
    errno = 0;
    result = rename(file, renamed);
    CHECK(result == 0, "rename inside directory");
    fd = open(renamed, O_RDONLY);
    char buffer[8];
    result = fd >= 0 ? read(fd, buffer, 8) : -1;
    CHECK(result == 8 && memcmp(buffer, "dir data", 8) == 0, "renamed content");
    if (fd >= 0) close(fd);
    errno = 0;
    result = rename(renamed, file);
    CHECK(result == 0, "rename back");
    /* Recreate the destination then verify rename refuses to overwrite. */
    int other = open(renamed, O_WRONLY|O_CREAT|O_TRUNC);
    if (other >= 0) { write(other, "old data", 8); close(other); }
    errno = 0;
    result = rename(file, renamed);
    CHECK(result == -1 && errno == EEXIST, "rename collision EEXIST");
    result = rename_replace(file, renamed);
    CHECK(result == 0, "rename_replace replaces regular file");
    errno = 0;
    result = stat(file, &(struct stat){0});
    CHECK(result == -1 && errno == ENOENT, "rename_replace removes source name");
    fd = open(renamed, O_RDONLY);
    char replace_buffer[8];
    result = fd >= 0 ? read(fd, replace_buffer, sizeof(replace_buffer)) : -1;
    CHECK(result == 8 && memcmp(replace_buffer, "dir data", 8) == 0,
          "rename_replace publishes source contents");
    if (fd >= 0) close(fd);
    char target_directory[128];
    under(target_directory, "sub/targetdir");
    result = mkdir(target_directory, 0755);
    CHECK(result == 0, "rename_replace directory fixture");
    errno = 0;
    result = rename_replace(renamed, target_directory);
    CHECK(result == -1 && errno == EISDIR, "rename_replace refuses directory target");
    struct stat target_info;
    result = stat(target_directory, &target_info);
    CHECK(result == 0 && S_ISDIR(target_info.st_mode), "rename_replace leaves directory intact");
    result = stat(renamed, &target_info);
    CHECK(result == 0 && target_info.st_size == 8, "rename_replace failure preserves source file");
    result = rmdir(target_directory);
    CHECK(result == 0, "rename_replace directory cleanup");
    errno = 0;
    result = unlink(renamed);
    CHECK(result == 0, "unlink renamed file");
    errno = 0;
    result = rmdir(directory);
    CHECK(result == 0, "rmdir empty directory");
    errno = 0;
    result = unlink(file);
    CHECK(result == -1 && errno == ENOENT, "unlink missing ENOENT");
    errno = 0;
    result = rmdir(file);
    CHECK(result == -1 && errno == ENOENT, "rmdir missing ENOENT");
    return failures ? 1 : 42;
}
static int mode_clock(void) {
    time_t seconds = 0;
    long raw = __pollikos_syscall0(0x504f003f);
    result = raw;
    CHECK(raw >= 1700000000L, "realtime syscall returns a contemporary Unix epoch");
    errno = 0;
    time_t from_time = time(&seconds);
    result = from_time;
    CHECK(from_time >= 1700000000L && seconds == from_time,
          "time returns RTC epoch and stores it through its argument");
    struct timeval tv = {0, 0};
    result = gettimeofday(&tv, 0);
    CHECK(result == 0 && tv.tv_sec >= 1700000000L &&
          tv.tv_usec >= 0 && tv.tv_usec < 1000000,
          "gettimeofday returns RTC epoch and valid microseconds");
    CHECK(tv.tv_sec - from_time >= -1 && tv.tv_sec - from_time <= 1,
          "time and gettimeofday share the same realtime clock");
    struct tm *utc = localtime(&from_time);
    CHECK(utc && utc->tm_year >= 123 && utc->tm_mon >= 0 && utc->tm_mon < 12 &&
          utc->tm_mday >= 1 && utc->tm_mday <= 31,
          "localtime converts RTC epoch as UTC calendar time");
    return failures ? 1 : 42;
}
static int mode_relative(void) {
    char cwd[64];
    if (!getcwd(cwd, sizeof(cwd))) return 1;
    errno = 0;
    result = chdir(DIRECTORY);
    CHECK(result == 0, "chdir mutation directory");
    if (result != 0) return 1;
    unlink("rel.txt");
    unlink("rel2.txt");
    int fd = open("rel.txt", O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "relative create");
    if (fd >= 0) { write(fd, "relative", 8); close(fd); }
    errno = 0;
    result = rename("rel.txt", "rel2.txt");
    CHECK(result == 0, "relative rename");
    fd = open("rel2.txt", O_RDONLY);
    char buffer[8];
    result = fd >= 0 ? read(fd, buffer, 8) : -1;
    CHECK(result == 8 && memcmp(buffer, "relative", 8) == 0, "relative read");
    if (fd >= 0) close(fd);
    errno = 0;
    result = unlink("rel2.txt");
    CHECK(result == 0, "relative unlink");
    errno = 0;
    result = chdir(cwd);
    CHECK(result == 0, "restore cwd");
    return failures ? 1 : 42;
}
static int mode_flags(void) {
    char file[128];
    under(file, "flags.txt");
    unlink(file);
    errno = 0;
    result = open("/mutation/missing.txt", O_RDONLY);
    CHECK(result == -1 && errno == ENOENT, "open missing ENOENT");
    errno = 0;
    result = open(file, 0x10);
    CHECK(result == -1 && errno == EINVAL, "unknown open bits EINVAL");
    /* POSIX-style SDK flags: O_RDONLY is 0, so O_CREAT alone means read-only
     * create and is valid; truncating without write access is not. */
    errno = 0;
    result = open(file, O_RDONLY|O_TRUNC);
    CHECK(result == -1 && errno == EACCES, "O_TRUNC without write EACCES");
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "flags fixture create");
    if (fd < 0) return 1;
    char buffer[8] = {0};
    errno = 0;
    result = read(fd, buffer, sizeof(buffer));
    CHECK(result == -1 && errno == EACCES, "read on O_WRONLY EACCES");
    /* The SDK write() splits requests at the kernel 64 KiB syscall cap, so an
     * oversized request is legal when its source range is mapped. The raw
     * wrapper still exposes the kernel's E2BIG bound. */
    result = pollikos_write(fd, "x", USER_WRITE_MAX+1);
    CHECK(result == -(long)USER_E2BIG, "oversized write E2BIG");
    errno = 0;
    static char large[USER_WRITE_MAX+4096];
    memset(large, 'q', sizeof(large));
    result = write(fd, large, sizeof(large));
    CHECK(result == (long)sizeof(large), "chunked write beyond syscall cap");
    errno = 0;
    result = write(fd, (const void *)(uintptr_t)0x100000, 8);
    CHECK(result == -1 && errno == EFAULT, "write unmapped pointer EFAULT");
    errno = 0;
    result = write(fd, (const void *)(uintptr_t)0xffffff8000000000ul, 8);
    CHECK(result == -1 && errno == EFAULT, "write kernel pointer EFAULT");
    errno = 0;
    result = write(fd, (const void *)(uintptr_t)0x0000800000000000ul, 8);
    CHECK(result == -1 && errno == EFAULT, "write noncanonical pointer EFAULT");
    close(fd);
    errno = 0;
    result = open((const char *)(uintptr_t)0x100000, O_RDONLY);
    CHECK(result == -1 && errno == EFAULT, "open unmapped path EFAULT");
    errno = 0;
    result = open((const char *)(uintptr_t)0xffffff8000000000ul, O_RDONLY);
    CHECK(result == -1 && errno == EFAULT, "open kernel path EFAULT");
    errno = 0;
    result = mkdir((const char *)(uintptr_t)0x0000800000000000ul, 0);
    CHECK(result == -1 && errno == EFAULT, "mkdir noncanonical path EFAULT");
    errno = 0;
    result = unlink((const char *)(uintptr_t)0x100000);
    CHECK(result == -1 && errno == EFAULT, "unlink unmapped path EFAULT");
    errno = 0;
    result = rename((const char *)(uintptr_t)0x100000, file);
    CHECK(result == -1 && errno == EFAULT, "rename unmapped source EFAULT");
    errno = 0;
    result = rename(file, (const char *)(uintptr_t)0x100000);
    CHECK(result == -1 && errno == EFAULT, "rename unmapped target EFAULT");
    static char unterminated[USER_PATH_MAX];
    memset(unterminated, 'x', sizeof(unterminated));
    errno = 0;
    result = open(unterminated, O_RDONLY);
    CHECK(result == -1 && (errno == ENAMETOOLONG || errno == EFAULT), "unterminated path rejected");
    errno = 0;
    result = mkdir(DIRECTORY, 0);
    CHECK(result == -1 && errno == EEXIST, "mkdir existing EEXIST");
    errno = 0;
    result = unlink(DIRECTORY);
    CHECK(result == -1 && errno == EISDIR, "unlink directory EISDIR");
    errno = 0;
    result = rmdir(file);
    CHECK(result == -1 && errno == ENOTDIR, "rmdir regular file ENOTDIR");
    result = unlink(file);
    CHECK(result == 0, "flags fixture cleanup");
    return failures ? 1 : 42;
}
static int mode_peer(void) {
    char file[128], content[32];
    int length = snprintf(content, sizeof(content), "peer-%d", getpid());
    char name[32];
    snprintf(name, sizeof(name), "peer-%d.txt", getpid());
    under(file, name);
    unlink(file);
    int fd = open(file, O_RDWR|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "peer create");
    if (fd < 0) return 1;
    result = write(fd, content, (size_t)length);
    CHECK(result == length, "peer write");
    /* Yield to the peer, then verify only our own data survived. */
    sleep_ms(30);
    char buffer[32];
    result = lseek(fd, 0, SEEK_SET);
    CHECK(result == 0, "peer rewind");
    result = read(fd, buffer, sizeof(buffer));
    CHECK(result == length && memcmp(buffer, content, (size_t)length) == 0, "peer data intact");
    close(fd);
    /* Closing fd 3 must not disturb the other process's descriptor. */
    sleep_ms(10);
    result = unlink(file);
    CHECK(result == 0, "peer unlink");
    return failures ? 1 : 42;
}
static int mode_lifecycle(void) {
    char first[128], second[128];
    char name[40];
    snprintf(name, sizeof(name), "life-%d.txt", getpid());
    under(first, name);
    snprintf(name, sizeof(name), "life-%d.dat", getpid());
    under(second, name);
    unlink(first);
    unlink(second);
    static char payload[777];
    pattern(payload, sizeof(payload), (unsigned long)getpid());
    int fd = open(first, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "lifecycle create");
    if (fd < 0) return 1;
    result = write(fd, payload, sizeof(payload));
    CHECK(result == (long)sizeof(payload), "lifecycle write");
    close(fd);
    fd = open(first, O_RDONLY);
    static char buffer[777];
    result = fd >= 0 ? read(fd, buffer, sizeof(buffer)) : -1;
    CHECK(result == (long)sizeof(payload) && memcmp(buffer, payload, sizeof(payload)) == 0,
          "lifecycle verify");
    if (fd >= 0) close(fd);
    errno = 0;
    result = rename(first, second);
    CHECK(result == 0, "lifecycle rename");
    fd = open(second, O_RDONLY);
    result = fd >= 0 ? read(fd, buffer, sizeof(buffer)) : -1;
    CHECK(result == (long)sizeof(payload) && memcmp(buffer, payload, sizeof(payload)) == 0,
          "lifecycle renamed verify");
    if (fd >= 0) close(fd);
    errno = 0;
    result = unlink(second);
    CHECK(result == 0, "lifecycle unlink");
    return failures ? 1 : 42;
}
/* Unlink-while-open: the old descriptor must never observe or modify a file
 * that later reuses the freed inode number. */
static int mode_unlink_reuse(void) {
    char file[128], other[128];
    under(file, "reuse_a.txt");
    unlink(file);
    int fd = open(file, O_RDWR|O_CREAT|O_TRUNC);
    result = fd;
    CHECK(fd >= 0, "reuse create A");
    if (fd < 0) return 1;
    result = write(fd, "AAAA", 4);
    CHECK(result == 4, "reuse write A");
    struct stat info;
    result = stat(file, &info);
    CHECK(result == 0, "reuse stat A");
    unsigned long old_inode = info.st_ino;
    result = unlink(file);
    CHECK(result == 0, "reuse unlink A");
    int reused = 0, newfd = -1;
    char name[32];
    for (int index = 0; index < 64 && !reused; ++index) {
        snprintf(name, sizeof(name), "reuse_b%d.txt", index);
        under(other, name);
        unlink(other);
        newfd = open(other, O_RDWR|O_CREAT|O_TRUNC);
        result = newfd;
        if (newfd < 0) break;
        struct stat st;
        if (stat(other, &st) == 0 && st.st_ino == old_inode) {
            reused = 1;
        } else {
            close(newfd);
            newfd = -1;
            unlink(other);
        }
    }
    CHECK(reused, "freed inode number reused");
    if (!reused) {
        if (newfd >= 0) close(newfd);
        close(fd);
        return 1;
    }
    result = write(newfd, "BBBB", 4);
    CHECK(result == 4, "reuse write B");
    char buffer[8];
    memset(buffer, 0, sizeof(buffer));
    errno = 0;
    result = lseek(fd, 0, SEEK_SET);
    CHECK(result == 0, "reuse rewind old descriptor");
    errno = 0;
    ssize_t count = read(fd, buffer, sizeof(buffer));
    result = count;
    CHECK(count == 0, "old descriptor reads EOF after inode reuse");
    CHECK(count == 0 || memcmp(buffer, "BBBB", 4) != 0, "old descriptor never sees new data");
    errno = 0;
    result = write(fd, "ZZZZ", 4);
    CHECK(result == -1 && errno == EACCES, "old descriptor write rejected after inode reuse");
    struct stat st2;
    result = fstat(newfd, &st2);
    CHECK(result == 0 && st2.st_size == 4, "new file size unchanged");
    result = lseek(newfd, 0, SEEK_SET);
    CHECK(result == 0, "new file rewind");
    result = read(newfd, buffer, sizeof(buffer));
    CHECK(result == 4 && memcmp(buffer, "BBBB", 4) == 0, "new file data intact");
    close(newfd);
    close(fd);
    result = unlink(other);
    CHECK(result == 0, "reuse cleanup B");
    return failures ? 1 : 42;
}
static int mode_diskfull(void) {
    char file[128];
    under(file, "full.bin");
    unlink(file);
    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC);
    result = fd;
    if (fd < 0) return 1;
    static char buffer[4096];
    memset(buffer, 0x6b, sizeof(buffer));
    long total = 0;
    int no_space = 0;
    for (int attempt = 0; attempt < 64; ++attempt) {
        errno = 0;
        ssize_t written = write(fd, buffer, sizeof(buffer));
        if (written < 0) {
            if (errno != ENOSPC) return 2;
            no_space = 1;
            break;
        }
        if (!written) return 3;
        total += written;
    }
    close(fd);
    struct stat info;
    if (stat(file, &info) != 0 || info.st_size != (unsigned long)total) return 4;
    fd = open(file, O_RDONLY);
    if (fd < 0) return 5;
    long checked = 0;
    while (checked < total) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count <= 0) break;
        for (ssize_t index = 0; index < count; ++index)
            if ((unsigned char)buffer[index] != 0x6b) { close(fd); return 6; }
        checked += count;
    }
    close(fd);
    if (!no_space) return 0;
    printf("[C5] diskfull committed=%ld bytes then ENOSPC\n", total);
    unlink(file);
    return 42;
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "basic";
    if (strcmp(mode, "basic") == 0) return mode_basic();
    if (strcmp(mode, "multi") == 0) return mode_multi();
    if (strcmp(mode, "large") == 0) return mode_large();
    if (strcmp(mode, "append") == 0) return mode_append();
    if (strcmp(mode, "truncate") == 0) return mode_truncate();
    if (strcmp(mode, "dirs") == 0) return mode_dirs();
    if (strcmp(mode, "clock") == 0) return mode_clock();
    if (strcmp(mode, "relative") == 0) return mode_relative();
    if (strcmp(mode, "flags") == 0) return mode_flags();
    if (strcmp(mode, "peer") == 0) return mode_peer();
    if (strcmp(mode, "lifecycle") == 0) return mode_lifecycle();
    if (strcmp(mode, "unlinkreuse") == 0) return mode_unlink_reuse();
    if (strcmp(mode, "diskfull") == 0) return mode_diskfull();
    printf("[C5] unknown mode %s\n", mode);
    return 1;
}
