/* Working directory example: getcwd, chdir and a relative file open. */
#include <stdio.h>
#include <string.h>
#include <pollikos/process.h>
#include <pollikos/fs.h>
int main(void) {
    char path[128];
    if (!getcwd(path, sizeof(path))) return 1;
    printf("[sdk] cwd: %s\n", path);
    if (chdir("/etc") != 0) return 2;
    if (!getcwd(path, sizeof(path)) || strcmp(path, "/etc") != 0) return 3;
    int fd = open("read_test.txt", O_RDONLY);
    if (fd < 0) return 4;
    char buffer[4];
    ssize_t count = read(fd, buffer, 4);
    close(fd);
    if (count != 4 || memcmp(buffer, "Alph", 4) != 0) return 5;
    if (chdir("/") != 0) return 6;
    printf("[sdk] cwd: relative read OK\n");
    return 0;
}
