/* errno example: ordinary C failure reporting without raw kernel codes. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pollikos/fs.h>
#include <pollikos/process.h>
int main(void) {
    errno = 0;
    if (open("/missing", O_RDONLY) != -1 || errno != ENOENT) return 1;
    printf("[sdk] errno: open -> %s\n", strerror(errno));
    errno = 0;
    char byte = 0;
    if (read(99, &byte, 1) != -1 || errno != EBADF) return 2;
    printf("[sdk] errno: read -> %s\n", strerror(errno));
    errno = 0;
    char small[1]; /* any path needs at least two bytes with the NUL */
    if (getcwd(small, sizeof(small)) != NULL || errno != ERANGE) return 3;
    printf("[sdk] errno: getcwd -> %s\n", strerror(errno));
    errno = 0;
    if (read(0, &byte, 1) != -1 || errno != ENOTSUP) return 4;
    printf("[sdk] errno: stdin -> %s\n", strerror(errno));
    errno = 0;
    if (open("/etc", O_WRONLY) != -1 || errno != EISDIR) return 5;
    printf("[sdk] errno: write-open directory -> %s\n", strerror(errno));
    if (read(0, &byte, 0) != 0) return 6;
    printf("[sdk] errno: all error paths OK\n");
    return 0;
}
