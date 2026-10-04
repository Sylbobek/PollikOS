#ifndef POLLIKOS_SYS_WAIT_H
#define POLLIKOS_SYS_WAIT_H

#include <sys/types.h>

/* The supported waitpid option. Other POSIX options return EINVAL. */
#define WNOHANG 0x1

#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && (((status) & 0x7f) != 0x7f))
#define WTERMSIG(status)    ((status) & 0x7f)
#define WIFSTOPPED(status)  (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)    (((status) >> 8) & 0xff)

pid_t waitpid(pid_t pid, int *status, int options);

#endif
