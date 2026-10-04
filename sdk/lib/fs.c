/* Raw PollikOS and POSIX-style read-only filesystem wrappers. The kernel ABI
 * is called exactly as documented; errno translation happens only in the
 * POSIX-style layer. */
#include <pollikos/fs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
long pollikos_open(const char *path, unsigned long flags) {
    return __pollikos_syscall2(USER_OPEN, (uint64_t)(uintptr_t)path, flags);
}
long pollikos_read(int fd, void *buffer, size_t count) {
    return __pollikos_syscall3(USER_READ, (uint64_t)(uint32_t)fd,
                               (uint64_t)(uintptr_t)buffer, count);
}
long pollikos_seek(int fd, off_t offset, int whence) {
    return __pollikos_syscall3(USER_SEEK, (uint64_t)(uint32_t)fd,
                               (uint64_t)offset, (uint64_t)(uint32_t)whence);
}
long pollikos_close(int fd) { return __pollikos_syscall1(USER_CLOSE, (uint64_t)(uint32_t)fd); }
long pollikos_stat(const char *path, pollikos_stat_t *result) {
    return __pollikos_syscall2(USER_STAT, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)result);
}
long pollikos_fstat(int fd, pollikos_stat_t *result) {
    return __pollikos_syscall2(USER_FSTAT, (uint64_t)(uint32_t)fd, (uint64_t)(uintptr_t)result);
}
long pollikos_dir_open(const char *path) {
    return __pollikos_syscall2(USER_OPENDIR, (uint64_t)(uintptr_t)path, USER_O_RDONLY);
}
long pollikos_dir_read(int fd, pollikos_dirent_t *result) {
    return __pollikos_syscall2(USER_READDIR, (uint64_t)(uint32_t)fd, (uint64_t)(uintptr_t)result);
}
long pollikos_dir_rewind(int fd) {
    return __pollikos_syscall3(USER_SEEK, (uint64_t)(uint32_t)fd, 0, USER_SEEK_SET);
}
long pollikos_dir_close(int fd) { return __pollikos_syscall1(USER_CLOSE, (uint64_t)(uint32_t)fd); }
long pollikos_pipe(int *fds) { return __pollikos_syscall1(USER_PIPE, (uint64_t)(uintptr_t)fds); }
long pollikos_pipe_read_available(int fd, void *buffer, size_t count) {
    return __pollikos_syscall3(USER_PIPE_READ_NOWAIT, (uint64_t)(uint32_t)fd,
                               (uint64_t)(uintptr_t)buffer, count);
}
long pollikos_dup(int fd) { return __pollikos_syscall1(USER_DUP, (uint64_t)(uint32_t)fd); }
long pollikos_dup2(int oldfd, int newfd) {
    return __pollikos_syscall2(USER_DUP2, (uint64_t)(uint32_t)oldfd, (uint64_t)(uint32_t)newfd);
}
long pollikos_set_cloexec(int fd, int set) {
    return __pollikos_syscall2(USER_CLOEXEC, (uint64_t)(uint32_t)fd, (uint64_t)(uint32_t)(set ? 1 : 0));
}
int pipe(int fds[2]) {
    long result = pollikos_pipe(fds);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int dup(int fd) {
    long result = pollikos_dup(fd);
    return result < 0 ? (int)__pollikos_fail(result) : (int)result;
}
int dup2(int oldfd, int newfd) {
    long result = pollikos_dup2(oldfd, newfd);
    return result < 0 ? (int)__pollikos_fail(result) : (int)result;
}
long pollikos_write(int fd, const void *buffer, size_t count) {
    return __pollikos_syscall3(USER_WRITE, (uint64_t)(uint32_t)fd,
                               (uint64_t)(uintptr_t)buffer, count);
}
long pollikos_mkdir(const char *path) { return __pollikos_syscall1(USER_MKDIR, (uint64_t)(uintptr_t)path); }
long pollikos_unlink(const char *path) { return __pollikos_syscall1(USER_UNLINK, (uint64_t)(uintptr_t)path); }
long pollikos_rmdir(const char *path) { return __pollikos_syscall1(USER_RMDIR, (uint64_t)(uintptr_t)path); }
long pollikos_rename(const char *oldpath, const char *newpath) {
    return __pollikos_syscall2(USER_RENAME, (uint64_t)(uintptr_t)oldpath, (uint64_t)(uintptr_t)newpath);
}
long pollikos_rename_replace(const char *oldpath, const char *newpath) {
    return __pollikos_syscall2(USER_RENAME_REPLACE,
        (uint64_t)(uintptr_t)oldpath, (uint64_t)(uintptr_t)newpath);
}
/* Translate SDK open flags to the kernel USER_O_* ABI. Unknown bits and a
 * missing access mode are rejected here without a syscall. */
static long translate_flags(int flags) {
    if (flags & ~O_FLAGS_MASK) { errno = EINVAL; return -1; }
    long kernel;
    switch (flags & (O_RDONLY|O_WRONLY|O_RDWR)) {
    case O_RDONLY: kernel = USER_O_RDONLY; break;
    case O_WRONLY: kernel = USER_O_WRONLY; break;
    case O_RDWR: kernel = USER_O_RDWR; break;
    default: errno = EINVAL; return -1;
    }
    if (flags & O_CREAT) kernel |= USER_O_CREAT;
    if (flags & O_TRUNC) kernel |= USER_O_TRUNC;
    if (flags & O_APPEND) kernel |= USER_O_APPEND;
    if (flags & O_EXCL) kernel |= USER_O_EXCL;
    if (flags & O_CLOEXEC) kernel |= USER_O_CLOEXEC;
    return kernel;
}
int open(const char *path, int flags, ...) {
    long kernel = translate_flags(flags);
    if (kernel < 0) return -1;
    long result = pollikos_open(path, (unsigned long)kernel);
    return result < 0 ? (int)__pollikos_fail(result) : (int)result;
}
ssize_t read(int fd, void *buffer, size_t count) {
    size_t total = 0;
    while (total < count) {
        size_t chunk = count - total;
        if (chunk > USER_READ_MAX) chunk = USER_READ_MAX;
        long result = pollikos_read(fd, (unsigned char *)buffer + total, chunk);
        if (result < 0) return total ? (ssize_t)total : __pollikos_fail(result);
        total += (size_t)result;
        if ((size_t)result < chunk) break;
    }
    return (ssize_t)total;
}
ssize_t write(int fd, const void *buffer, size_t count) {
    size_t total = 0;
    while (total < count) {
        size_t chunk = count - total;
        if (chunk > USER_WRITE_MAX) chunk = USER_WRITE_MAX;
        long result = pollikos_write(fd, (const unsigned char *)buffer + total, chunk);
        if (result < 0) return total ? (ssize_t)total : __pollikos_fail(result);
        total += (size_t)result;
        if ((size_t)result < chunk) break;
    }
    return (ssize_t)total;
}
int mkdir(const char *path, mode_t mode) {
    (void)mode;
    long result = pollikos_mkdir(path);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int unlink(const char *path) {
    long result = pollikos_unlink(path);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int rmdir(const char *path) {
    long result = pollikos_rmdir(path);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int rename(const char *oldpath, const char *newpath) {
    long result = pollikos_rename(oldpath, newpath);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
int rename_replace(const char *oldpath, const char *newpath) {
    long result = pollikos_rename_replace(oldpath, newpath);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
off_t lseek(int fd, off_t offset, int whence) {
    long result = pollikos_seek(fd, offset, whence);
    return result < 0 ? __pollikos_fail(result) : result;
}
int close(int fd) {
    long result = pollikos_close(fd);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
static void stat_convert(const pollikos_stat_t *info, struct stat *result) {
    result->st_ino = info->inode;
    result->st_size = info->size_bytes;
    result->st_type = info->type;
    result->st_mode = info->type == POLLIKOS_TYPE_DIRECTORY ? S_IFDIR : S_IFREG;
    result->st_mtime = info->modified_ticks;
    result->st_ctime = info->created_ticks;
}
int stat(const char *path, struct stat *result) {
    pollikos_stat_t info;
    long outcome = pollikos_stat(path, &info);
    if (outcome < 0) return (int)__pollikos_fail(outcome);
    stat_convert(&info, result);
    return 0;
}
int fstat(int fd, struct stat *result) {
    pollikos_stat_t info;
    long outcome = pollikos_fstat(fd, &info);
    if (outcome < 0) return (int)__pollikos_fail(outcome);
    stat_convert(&info, result);
    return 0;
}
int access(const char *path, int mode) {
    if (!path || (mode & ~(R_OK|W_OK|X_OK))) { errno = EINVAL; return -1; }
    pollikos_stat_t info;
    long outcome = pollikos_stat(path, &info);
    if (outcome < 0) {
        if (mode == F_OK) return (int)__pollikos_fail(outcome);
        (void)__pollikos_fail(outcome);
        return -1;
    }
    if ((mode & (W_OK|X_OK)) && info.type != POLLIKOS_TYPE_REGULAR) {
        errno = (mode & X_OK) ? EACCES : EISDIR;
        return -1;
    }
    return 0;
}
