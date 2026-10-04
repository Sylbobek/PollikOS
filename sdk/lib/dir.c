/* Minimal directory stream over the kernel's versioned 96-byte dirent ABI. */
#include <pollikos/fs.h>
#include <stdlib.h>
#include <errno.h>
DIR *opendir(const char *path) {
    long fd = pollikos_dir_open(path);
    if (fd < 0) { __pollikos_fail(fd); return NULL; }
    DIR *directory = malloc(sizeof(DIR));
    if (!directory) { (void)pollikos_dir_close((int)fd); errno = ENOMEM; return NULL; }
    directory->fd = (int)fd;
    directory->done = 0;
    directory->error = 0;
    return directory;
}
pollikos_dirent_t *readdir(DIR *directory) {
    if (!directory) { errno = EBADF; return NULL; }
    if (directory->done) return NULL;
    long result = pollikos_dir_read(directory->fd, &directory->entry);
    if (result == 0) { directory->done = 1; return NULL; }
    if (result < 0) { __pollikos_fail(result); directory->error = 1; directory->done = 1; return NULL; }
    return &directory->entry;
}
int rewinddir(DIR *directory) {
    if (!directory) { errno = EBADF; return -1; }
    long result = pollikos_dir_rewind(directory->fd);
    if (result < 0) return (int)__pollikos_fail(result);
    directory->done = 0;
    directory->error = 0;
    return 0;
}
int dirfd(DIR *directory) { return directory ? directory->fd : -1; }
int closedir(DIR *directory) {
    if (!directory) { errno = EBADF; return -1; }
    int fd = directory->fd;
    free(directory);
    long result = pollikos_dir_close(fd);
    return result < 0 ? (int)__pollikos_fail(result) : 0;
}
