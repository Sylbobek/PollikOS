#include "vfs.h"
#include "pollikfs.h"
#include "klog.h"

#define MAX_SYSTEM_FILES 64
static vfs_file_t g_system_files[MAX_SYSTEM_FILES];
static int g_vfs_initialized = 0;

/* External process hooks */
extern int process_get_current_pid(void);
extern vfs_file_t **process_get_current_fd_table(void);

static vfs_file_t *alloc_file_desc(void) {
    for (int i = 0; i < MAX_SYSTEM_FILES; i++) {
        if (g_system_files[i].ref_count == 0) {
            memset(&g_system_files[i], 0, sizeof(vfs_file_t));
            g_system_files[i].ref_count = 1;
            return &g_system_files[i];
        }
    }
    return 0;
}

static void free_file_desc(vfs_file_t *f) {
    if (!f) return;
    f->ref_count--;
    if (f->ref_count <= 0) {
        memset(f, 0, sizeof(vfs_file_t));
    }
}

void vfs_init(void) {
    memset(g_system_files, 0, sizeof(g_system_files));
    pollikfs_init();
    g_vfs_initialized = 1;
    KLOG_INFO(KLOG_CAT_BOOT, "Virtual File System (VFS) initialized");
}

void vfs_init_process_fds(vfs_file_t **table) {
    if (!table) return;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        table[i] = 0;
    }

    /* Stdin (0) */
    vfs_file_t *f_in = alloc_file_desc();
    if (f_in) {
        f_in->type = VFS_DEVICE;
        f_in->flags = O_RDONLY;
        table[0] = f_in;
    }

    /* Stdout (1) */
    vfs_file_t *f_out = alloc_file_desc();
    if (f_out) {
        f_out->type = VFS_DEVICE;
        f_out->flags = O_WRONLY;
        table[1] = f_out;
    }

    /* Stderr (2) */
    vfs_file_t *f_err = alloc_file_desc();
    if (f_err) {
        f_err->type = VFS_DEVICE;
        f_err->flags = O_WRONLY;
        table[2] = f_err;
    }
}

void vfs_close_process_fds(vfs_file_t **table) {
    if (!table) return;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (table[i]) {
            if (table[i]->type == VFS_FILE || table[i]->type == VFS_DIR) {
                pollikfs_close(table[i]);
            }
            free_file_desc(table[i]);
            table[i] = 0;
        }
    }
}

int vfs_open(const char *path, int flags) {
    if (!g_vfs_initialized || !path)
        return -1;

    vfs_file_t **table = process_get_current_fd_table();
    if (!table)
        return -1;

    /* Find lowest free file descriptor */
    int fd = -1;
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (!table[i]) {
            fd = i;
            break;
        }
    }
    if (fd < 0)
        return -1;

    vfs_file_t *file = alloc_file_desc();
    if (!file)
        return -1;

    if (pollikfs_open(path, flags, file) < 0) {
        free_file_desc(file);
        return -1;
    }

    table[fd] = file;
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS)
        return -1;

    vfs_file_t **table = process_get_current_fd_table();
    if (!table || !table[fd])
        return -1;

    vfs_file_t *f = table[fd];
    table[fd] = 0;

    if (f->type == VFS_FILE || f->type == VFS_DIR) {
        pollikfs_close(f);
    }
    free_file_desc(f);
    return 0;
}

int vfs_read(int fd, void *buf, u32 count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !buf || count == 0)
        return -1;

    vfs_file_t **table = process_get_current_fd_table();
    if (!table || !table[fd])
        return -1;

    vfs_file_t *f = table[fd];
    if (f->type == VFS_DEVICE) {
        /* Stdin device read */
        if (fd == 0) {
            /* Non-blocking console read */
            return 0;
        }
        return -1;
    }

    return pollikfs_read(f, buf, count);
}

int vfs_write(int fd, const void *buf, u32 count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !buf || count == 0)
        return -1;

    vfs_file_t **table = process_get_current_fd_table();
    if (!table || !table[fd])
        return -1;

    vfs_file_t *f = table[fd];
    if (f->type == VFS_DEVICE) {
        /* Stdout/Stderr output */
        if (fd == 1 || fd == 2) {
            char kbuf[128];
            u32 total = 0;
            const u8 *src = (const u8 *)buf;
            while (total < count) {
                u32 chunk = count - total;
                if (chunk > sizeof(kbuf) - 1)
                    chunk = sizeof(kbuf) - 1;
                memcpy(kbuf, src + total, chunk);
                kbuf[chunk] = 0;
                serial(kbuf);
                total += chunk;
            }
            return (int)total;
        }
        return -1;
    }

    return pollikfs_write(f, buf, count);
}

int vfs_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FDS)
        return -1;

    vfs_file_t **table = process_get_current_fd_table();
    if (!table || !table[fd])
        return -1;

    vfs_file_t *f = table[fd];
    if (f->type == VFS_DEVICE)
        return -1;

    return pollikfs_seek(f, offset, whence);
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    return pollikfs_stat(path, st);
}

int vfs_mkdir(const char *path) {
    return pollikfs_mkdir(path);
}

int vfs_unlink(const char *path) {
    return pollikfs_unlink(path);
}
