#ifndef POLLIK_VFS_H
#define POLLIK_VFS_H

#ifdef POLLIK_X64
#include "arch/x86_64/fs_platform.h"
#else
#include "system.h"
#endif

#define VFS_MAX_PATH     128
#define VFS_MAX_NAME     64
#ifdef POLLIK_X64
#define VFS_MAX_FDS      128
#else
#define VFS_MAX_FDS      64
#endif

#define VFS_FILE         1
#define VFS_DIR          2
#define VFS_DEVICE       3
enum { VFS_OK, VFS_NOT_FOUND, VFS_IO, VFS_CORRUPT, VFS_BAD_PATH, VFS_DENIED, VFS_NOT_MOUNTED,
       VFS_NOSPACE, VFS_EXISTS, VFS_NOT_EMPTY, VFS_ISDIR, VFS_NOTDIR };

#define O_RDONLY         0x0001
#define O_WRONLY         0x0002
#define O_RDWR           0x0003
#define O_CREAT          0x0100
#define O_TRUNC          0x0200
#define O_APPEND         0x0400
#define O_EXCL           0x0800

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
    unsigned user_access; /* snapshotted policy, checked on inherited fds */
#ifdef POLLIK_X64
    /* In-memory identity token, never part of an on-disk or syscall ABI.
     * Snapshots the inode allocation epoch at open so a descriptor whose
     * inode was unlinked and later reused cannot touch the new occupant. */
    u32 inode_generation;
#endif
    void *fs_private;
} vfs_file_t;

void vfs_init(void);
int vfs_is_ready(void);
int vfs_open(const char *path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, u32 count);
int vfs_write(int fd, const void *buf, u32 count);
int vfs_write_preflight(int fd, u32 count);
int vfs_seek(int fd, int offset, int whence);
int vfs_fstat(int fd, vfs_stat_t *st);
int vfs_stat(const char *path, vfs_stat_t *st);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_readdir(int fd, vfs_dirent_t *dirent);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_rename_replace(const char *oldpath, const char *newpath);
unsigned vfs_debug_handles(void);

/* Per-process FD table management */
void vfs_init_process_fds(vfs_file_t **table);
void vfs_clone_process_fds(vfs_file_t **dest_table, vfs_file_t **src_table);
void vfs_close_process_fds(vfs_file_t **table);

#endif
