/* Read-only filesystem example: stat, open, read, fstat, seek and close. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pollikos/fs.h>
#include <sys/stat.h>
int main(void) {
    struct stat info;
    if (stat("/etc/read_test.txt", &info) != 0) {
        printf("[sdk] files: stat failed: %s\n", strerror(errno));
        return 1;
    }
    int fd = open("/etc/read_test.txt", O_RDONLY);
    if (fd < 0) {
        printf("[sdk] files: open failed: %s\n", strerror(errno));
        return 2;
    }
    char buffer[32];
    ssize_t count = read(fd, buffer, 16);
    if (count != 16) return 3;
    buffer[count] = 0;
    if (lseek(fd, 0, SEEK_SET) != 0 || fstat(fd, &info) != 0 ||
        info.st_type != POLLIKOS_TYPE_REGULAR) return 4;
    if (close(fd) != 0) return 5;
    printf("[sdk] files: %lu bytes inode=%lu \"%s\"\n",
           (unsigned long)info.st_size, (unsigned long)info.st_ino, buffer);
    return 0;
}
