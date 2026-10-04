#ifndef POLLIKOS_FS_H
#define POLLIKOS_FS_H
#include <stddef.h>
#include <stdint.h>
#include <pollikos/syscall.h>
#include <pollikos/types.h>
/* PollikOS filesystem API. Version-1 stat and dirent layouts mirror the
 * kernel's fixed 64/96-byte ABI; applications normally use <sys/stat.h>
 * (struct stat) and <dirent.h> instead of these raw structures. */
typedef struct pollikos_stat {
    uint32_t version, struct_size, type, timestamp_kind;
    uint64_t size_bytes, inode, created_ticks, modified_ticks;
    uint64_t reserved[2];
} pollikos_stat_t;
typedef struct pollikos_dirent {
    uint32_t version, struct_size, type, name_length;
    uint64_t inode, reserved;
    char name[USER_DIRENT_NAME_CAPACITY];
} pollikos_dirent_t;
#define POLLIKOS_TYPE_UNKNOWN 0
#define POLLIKOS_TYPE_REGULAR 1
#define POLLIKOS_TYPE_DIRECTORY 2
#define POLLIKOS_TIME_POLLIK_TICKS 1
#define POLLIKOS_DIRENT_VERSION USER_DIRENT_VERSION
#define POLLIKOS_STAT_VERSION USER_STAT_VERSION
/* PollikOS SDK open flags. Values are PollikOS-defined (POSIX-style layout);
 * the libc translates them to the kernel's USER_O_* ABI values. */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x0040
#define O_TRUNC 0x0200
#define O_APPEND 0x0400
#define O_EXCL 0x0800
/* Do not inherit this descriptor through spawn/spawn_group. */
#define O_CLOEXEC 0x1000
#define O_FLAGS_MASK (O_RDONLY|O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND|O_EXCL|O_CLOEXEC)
#ifndef SEEK_SET
#define SEEK_SET USER_SEEK_SET
#define SEEK_CUR USER_SEEK_CUR
#define SEEK_END USER_SEEK_END
#endif
long pollikos_open(const char *path, unsigned long flags);
long pollikos_read(int fd, void *buffer, size_t count);
long pollikos_write(int fd, const void *buffer, size_t count);
long pollikos_seek(int fd, off_t offset, int whence);
long pollikos_close(int fd);
long pollikos_stat(const char *path, pollikos_stat_t *result);
long pollikos_fstat(int fd, pollikos_stat_t *result);
long pollikos_mkdir(const char *path);
long pollikos_unlink(const char *path);
long pollikos_rmdir(const char *path);
long pollikos_rename(const char *oldpath, const char *newpath);
long pollikos_rename_replace(const char *oldpath, const char *newpath);
long pollikos_dir_open(const char *path);
long pollikos_dir_read(int fd, pollikos_dirent_t *result);
long pollikos_dir_rewind(int fd);
long pollikos_dir_close(int fd);
/* Descriptor plumbing: kernel pipes and descriptor duplication. */
long pollikos_pipe(int *fds);
/* Nonblocking read from a pipe. Returns bytes, 0 at EOF, or a raw -USER_E* result. */
long pollikos_pipe_read_available(int fd, void *buffer, size_t count);
long pollikos_dup(int fd);
long pollikos_dup2(int oldfd, int newfd);
/* Per-descriptor close-on-spawn flag. pollikos_set_cloexec sets (set!=0) or
 * clears (set==0) the flag and returns the previous value, or a negative
 * USER_E* code. Descriptors marked close-on-spawn are not inherited by
 * spawn/spawn_group; a dup/dup2 target never inherits the flag. */
long pollikos_set_cloexec(int fd, int set);
int open(const char *path, int flags, ...);
ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int close(int fd);
int mkdir(const char *path, mode_t mode);
int unlink(const char *path);
int rmdir(const char *path);
int rename(const char *oldpath, const char *newpath);
int rename_replace(const char *oldpath, const char *newpath);
/* Minimal directory stream. No telldir/seekdir/rewinddir semantics beyond a
 * full rewind, and entries are returned in storage order. */
typedef struct pollikos_dir_stream {
    int fd, done, error;
    pollikos_dirent_t entry;
} DIR;
DIR *opendir(const char *path);
pollikos_dirent_t *readdir(DIR *directory);
int rewinddir(DIR *directory);
int dirfd(DIR *directory);
int closedir(DIR *directory);
#endif
