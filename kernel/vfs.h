#ifndef POLLIK_VFS_H
#define POLLIK_VFS_H

#include "system.h"

#define VFS_MAX_PATH     128
#define VFS_MAX_NAME     64
#define VFS_MAX_FDS      16

#define VFS_FILE         1
#define VFS_DIR          2
#define VFS_DEVICE       3

#define O_RDONLY         0x0001
#define O_WRONLY         0x0002
#define O_RDWR           0x0003
#define O_CREAT          0x0100
#define O_TRUNC          0x0200
#define O_APPEND         0x0400

#define SEEK_SET         0
#define SEEK_CUR         1
#define SEEK_END         2

typedef struct {
    u32 inode;
    u32 size;
    u32 type;
    u32 created;
    u32 modified;
} vfs_stat_t;

typedef struct {
    u32 inode;
    u32 type;
    char name[VFS_MAX_NAME];
} vfs_dirent_t;

typedef struct vfs_file {
    u32 type;
    u32 flags;
    u32 offset;
    u32 size;
    u32 inode;
    int ref_count;
    void *fs_private;
} vfs_file_t;

void vfs_init(void);
int vfs_open(const char *path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, u32 count);
int vfs_write(int fd, const void *buf, u32 count);
int vfs_seek(int fd, int offset, int whence);
int vfs_stat(const char *path, vfs_stat_t *st);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);

/* Per-process FD table management */
void vfs_init_process_fds(vfs_file_t **table);
void vfs_clone_process_fds(vfs_file_t **dest_table, vfs_file_t **src_table);
void vfs_close_process_fds(vfs_file_t **table);

#endif
