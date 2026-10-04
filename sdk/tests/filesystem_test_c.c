/* Read-only filesystem, directory, cwd and error-path tests from ordinary C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pollikos/process.h>
#include <pollikos/fs.h>
#include <sys/stat.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C3] FAIL: %s\n", name); ++failures; } } while (0)
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    struct stat info;
    CHECK(stat("/etc/read_test.txt", &info) == 0, "stat file");
    CHECK(info.st_type == POLLIKOS_TYPE_REGULAR && info.st_size == 16, "stat type and size");
    int fd = open("/etc/read_test.txt", O_RDONLY);
    CHECK(fd >= 0, "open file");
    char buffer[32];
    long count = fd >= 0 ? read(fd, buffer, 5) : -1;
    CHECK(count == 5 && memcmp(buffer, "Alpha", 5) == 0, "read prefix");
    struct stat fstat_info;
    CHECK(fstat(fd, &fstat_info) == 0 && fstat_info.st_size == 16 &&
          fstat_info.st_ino == info.st_ino, "fstat matches stat");
    CHECK(lseek(fd, 0, SEEK_SET) == 0, "seek rewind");
    count = read(fd, buffer, sizeof(buffer));
    CHECK(count == 16 && memcmp(buffer, "Alpha file data\n", 16) == 0, "read whole file");
    CHECK(read(fd, buffer, sizeof(buffer)) == 0, "read at EOF");
    CHECK(close(fd) == 0, "close file");
    CHECK(chdir("/etc") == 0, "chdir");
    char cwd[128];
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL && strcmp(cwd, "/etc") == 0, "getcwd");
    fd = open("read_test.txt", O_RDONLY);
    CHECK(fd >= 0, "relative open after chdir");
    if (fd >= 0 || 1) { if (fd >= 0) CHECK(close(fd) == 0, "relative close"); }
    DIR *directory = opendir("/bin");
    CHECK(directory != NULL, "opendir /bin");
    if (directory) {
        int entries = 0, found = 0;
        pollikos_dirent_t *entry;
        while ((entry = readdir(directory)) != NULL) {
            ++entries;
            if (strcmp(entry->name, "hello_c") == 0) {
                found = 1;
                CHECK(entry->type == POLLIKOS_TYPE_REGULAR && entry->name_length == 7,
                      "dirent hello_c fields");
            }
        }
        CHECK(entries >= 20, "directory entry count");
        CHECK(found, "hello_c enumerated");
        CHECK(closedir(directory) == 0, "closedir");
    }
    errno = 0;
    CHECK(open("/missing", O_RDONLY) == -1 && errno == ENOENT, "open missing errno");
    errno = 0;
    CHECK(read(99, buffer, 1) == -1 && errno == EBADF, "read bad descriptor");
    errno = 0;
    CHECK(close(99) == -1 && errno == EBADF, "close bad descriptor");
    errno = 0;
    char small[2];
    CHECK(getcwd(small, sizeof(small)) == NULL && errno == ERANGE, "small getcwd buffer");
    /* C5: write modes are valid for regular files; unknown bits stay EINVAL. */
    errno = 0;
    CHECK(open("/etc/read_test.txt", 0x10) == -1 && errno == EINVAL, "unknown open flags rejected");
    int writable = open("/etc/read_test.txt", O_WRONLY);
    CHECK(writable >= 0, "write-mode open");
    if (writable >= 0) CHECK(close(writable) == 0, "write-mode close");
    errno = 0;
    char byte = 0;
    CHECK(read(0, &byte, 1) == -1 && errno == ENOTSUP, "unsupported stdin");
    CHECK(read(0, &byte, 0) == 0, "zero-length stdin read");
    errno = 0;
    CHECK(chdir("/missing") == -1 && errno == ENOENT, "chdir missing errno");
    errno = 0;
    CHECK(opendir("/etc/read_test.txt") == NULL && errno == ENOTDIR, "opendir regular file");
    CHECK(stat("/bin", &info) == 0 && info.st_type == POLLIKOS_TYPE_DIRECTORY, "stat directory");
    CHECK(stat("/bin/hello_c", &info) == 0 && info.st_size > 0, "stat C executable");
    errno = 0;
    CHECK(stat("/missing", &info) == -1 && errno == ENOENT, "stat missing errno");
    printf("[C3] filesystem tests %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 42;
}
