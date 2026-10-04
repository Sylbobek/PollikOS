#include <pollikos/fs.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    int fds[2];
    if (pollikos_pipe(fds)<0) return 1;
    char buffer[32]={0};
    long empty=pollikos_pipe_read_available(fds[0],buffer,sizeof(buffer));
    if (empty>=0) return 2;
    static const char expected[]="pipe-nowait";
    if (write(fds[1],expected,sizeof(expected))!=(ssize_t)sizeof(expected)) return 3;
    long received=pollikos_pipe_read_available(fds[0],buffer,sizeof(buffer));
    if (received!=(long)sizeof(expected) || memcmp(buffer,expected,sizeof(expected))) return 4;
    close(fds[0]); close(fds[1]);
    puts("PIPE_NOWAIT_PASS");
    return 0;
}
