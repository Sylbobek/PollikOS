#ifndef POLLIKOS_ERRNO_H
#define POLLIKOS_ERRNO_H
/* Single-threaded per-process errno. TLS-backed errno arrives with userspace
 * threads; the kernel never knows about this storage. POSIX-style wrappers set
 * it, raw pollikos_* calls return negative PollikOS error codes instead. */
extern int errno;
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EPIPE 32
#define ERANGE 34
#define ENAMETOOLONG 36
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define EOVERFLOW 75
#define EMSGSIZE 90
#define ENOTSUP 95
#define EWOULDBLOCK EAGAIN
#define ETIMEDOUT 110
const char *strerror(int error);
#endif
