/* Userspace errno and shared kernel-result translation. The kernel returns
 * negative USER_E* codes; POSIX-style wrappers map them to stable errno
 * numbers. Single-threaded per process until TLS-backed errno exists. */
#include <errno.h>
#include <pollikos/syscall.h>
int errno;
static int translate(int64_t result) {
    switch (result) {
    case -(int64_t)USER_EFAULT: return EFAULT;
    case -(int64_t)USER_ENOSYS: return ENOSYS;
    case -(int64_t)USER_E2BIG: return E2BIG;
    case -(int64_t)USER_EBADF: return EBADF;
    case -(int64_t)USER_ENOENT: return ENOENT;
    case -(int64_t)USER_EINVAL: return EINVAL;
    case -(int64_t)USER_EACCES: return EACCES;
    case -(int64_t)USER_EIO: return EIO;
    case -(int64_t)USER_EMFILE: return EMFILE;
    case -(int64_t)USER_ENOMEM: return ENOMEM;
    case -(int64_t)USER_ENAMETOOLONG: return ENAMETOOLONG;
    case -(int64_t)USER_EISDIR: return EISDIR;
    case -(int64_t)USER_ENOTDIR: return ENOTDIR;
    case -(int64_t)USER_ENOTSUP: return ENOTSUP;
    case -(int64_t)USER_ERANGE: return ERANGE;
    case -(int64_t)USER_ENOSPC: return ENOSPC;
    case -(int64_t)USER_ENOTEMPTY: return ENOTEMPTY;
    case -(int64_t)USER_EEXIST: return EEXIST;
    case -(int64_t)USER_ECHILD: return ECHILD;
    case -(int64_t)USER_ESRCH: return ESRCH;
    case -(int64_t)USER_EAGAIN: return EAGAIN;
    case -(int64_t)USER_ENOEXEC: return ENOEXEC;
    case -(int64_t)USER_EINTR: return EINTR;
    case -(int64_t)USER_EPERM: return EPERM;
    default: return EIO;
    }
}
long __pollikos_fail(int64_t result) {
    errno = result < 0 ? translate(result) : 0;
    return -1;
}
const char *strerror(int error) {
    switch (error) {
    case 0: return "success";
    case ENOENT: return "no such file or directory";
    case ESRCH: return "no such process";
    case EPERM: return "operation not permitted";
    case EIO: return "input/output error";
    case EBADF: return "bad file descriptor";
    case ENOMEM: return "out of memory";
    case EACCES: return "permission denied";
    case EFAULT: return "bad address";
    case EINVAL: return "invalid argument";
    case EMFILE: return "too many open files";
    case ENOTDIR: return "not a directory";
    case EISDIR: return "is a directory";
    case ERANGE: return "result out of range";
    case ENOSYS: return "function not implemented";
    case ENOTSUP: return "operation not supported";
    case ENAMETOOLONG: return "file name too long";
    case E2BIG: return "argument list too long";
    default: return "unknown error";
    }
}
