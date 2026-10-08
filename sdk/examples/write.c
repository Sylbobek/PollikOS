/* SDK writable-files example: create, write, close, reopen, read, print. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pollikos/fs.h>
int main(void) {
    const char *text = "PollikOS writable files\n";
    if (mkdir("/tmp/mutation", 0755) != 0 && errno != EEXIST) {
        printf("[sdk] write: mkdir failed: %s\n", strerror(errno));
        return 1;
    }
    int fd = open("/tmp/mutation/notes.txt", O_WRONLY|O_CREAT|O_TRUNC);
    if (fd < 0) {
        printf("[sdk] write: open failed: %s\n", strerror(errno));
        return 2;
    }
    if (write(fd, text, strlen(text)) != (ssize_t)strlen(text)) {
        printf("[sdk] write: write failed: %s\n", strerror(errno));
        close(fd);
        return 3;
    }
    if (close(fd) != 0) return 4;
    fd = open("/tmp/mutation/notes.txt", O_RDONLY);
    if (fd < 0) return 5;
    char buffer[64];
    ssize_t count = read(fd, buffer, sizeof(buffer)-1);
    close(fd);
    if (count < 0) return 6;
    buffer[count] = 0;
    printf("[sdk] write: notes.txt -> \"%s\"", buffer);
    if (unlink("/tmp/mutation/notes.txt") != 0) return 7;
    return 0;
}
