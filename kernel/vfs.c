#include "vfs.h"
#include "pollikfs.h"
#include "klog.h"
#include "security.h"

static int path_access(const char *path,unsigned access) {
    if (security_path_allowed(security_current(),path,access)) return 1;
    pollikfs_access_denied(); return 0;
}
static int file_access(const vfs_file_t *file,unsigned access) {
    const Credentials *c=security_current();
    unsigned rights=(access&ACCESS_READ?CAP_FILE_READ:0)|(access&ACCESS_WRITE?CAP_FILE_WRITE:0);
    if(security_has(c,rights) && (!c->uid || (file->user_access&access)==access)) return 1;
    pollikfs_access_denied(); return 0;
}

#ifdef POLLIK_X64
#include "arch/x86_64/user_abi.h"
#include "arch/x86_64/memory.h"
/* Every process slot can own all non-standard descriptors at once. */
#define VFS_FILE_BASE (MM_KERNEL_START + UINT64_C(0x20000000))
extern size_t process64_capacity(void);
static vfs_file_t *g_system_files;
static uint32_t *g_free_file_indices;
static unsigned g_system_file_count;
static unsigned g_free_file_count;
_Static_assert((uint64_t)PROCESS_MAX * (USER_FD_LIMIT - 3) <= UINT32_MAX,
               "VFS free index width");
#else
#define MAX_SYSTEM_FILES 64
static vfs_file_t g_system_files[MAX_SYSTEM_FILES];
static unsigned g_system_file_count = MAX_SYSTEM_FILES;
#endif
static int g_vfs_initialized = 0;
#if defined(POLLIK_X64) && defined(SELFTEST)
static int fail_file_alloc;
void vfs_test_fail_alloc(int fail) { fail_file_alloc = fail; }
#endif

#ifdef POLLIK_X64
static int vfs_allocate_file_pool(void) {
    if (g_system_files) return 1;
    size_t count = process64_capacity() * (USER_FD_LIMIT - 3);
    size_t files_bytes = count * sizeof(vfs_file_t);
    size_t indices_offset = (files_bytes + _Alignof(uint32_t) - 1) & ~((size_t)_Alignof(uint32_t) - 1);
    size_t bytes = indices_offset + count * sizeof(uint32_t);
    size_t pages = (bytes + MM_PAGE_SIZE - 1) / MM_PAGE_SIZE;
    size_t mapped = 0;
    while (mapped < pages &&
           vmm64_alloc_page(vmm64_kernel(), VFS_FILE_BASE + mapped*MM_PAGE_SIZE, VM_WRITE) == VM_OK)
        ++mapped;
    if (mapped != pages) {
        while (mapped) {
            --mapped;
            memory_require(vmm64_unmap(vmm64_kernel(), VFS_FILE_BASE + mapped*MM_PAGE_SIZE, 1, 0) == VM_OK,
                           "VFS pool allocation rollback");
        }
        return 0;
    }
    g_system_files = (vfs_file_t *)(uintptr_t)VFS_FILE_BASE;
    g_free_file_indices = (uint32_t *)(uintptr_t)(VFS_FILE_BASE + indices_offset);
    g_system_file_count = count;
    return 1;
}
#endif

/* External process hooks */
extern int process_get_current_pid(void);
extern vfs_file_t **process_get_current_file_slot(unsigned fd);

static vfs_file_t *alloc_file_desc(void) {
#if defined(POLLIK_X64) && defined(SELFTEST)
    if (fail_file_alloc) return 0;
#endif
#ifdef POLLIK_X64
    if (!g_system_files || !g_free_file_count) return 0;
    size_t index = g_free_file_indices[--g_free_file_count];
    vfs_file_t *file = &g_system_files[index];
    memset(file, 0, sizeof(*file));
    file->ref_count = 1;
    return file;
#else
    for (int i = 0; i < MAX_SYSTEM_FILES; i++) {
        if (g_system_files[i].ref_count == 0) {
            memset(&g_system_files[i], 0, sizeof(vfs_file_t));
            g_system_files[i].ref_count = 1;
            return &g_system_files[i];
        }
    }
    return 0;
#endif
}

static void free_file_desc(vfs_file_t *f) {
    if (!f || f->ref_count <= 0) return;
    if (--f->ref_count == 0) {
        memset(f, 0, sizeof(vfs_file_t));
#ifdef POLLIK_X64
        size_t index = (size_t)(f - g_system_files);
        if (index < g_system_file_count && g_free_file_count < g_system_file_count)
            g_free_file_indices[g_free_file_count++] = (uint32_t)index;
#endif
    }
}

void vfs_init(void) {
#ifdef POLLIK_X64
    g_vfs_initialized = 0;
    if (!vfs_allocate_file_pool()) {
        KLOG_ERROR(KLOG_CAT_BOOT, "VFS descriptor pool allocation failed");
        return;
    }
#endif
    memset(g_system_files, 0, g_system_file_count * sizeof(vfs_file_t));
#ifdef POLLIK_X64
    g_free_file_count = g_system_file_count;
    for (size_t i = 0; i < g_system_file_count; ++i)
        g_free_file_indices[i] = (uint32_t)(g_system_file_count-1-i);
#endif
    pollikfs_init();
    g_vfs_initialized = 1;
    KLOG_INFO(KLOG_CAT_BOOT, "Virtual File System (VFS) initialized");
}

int vfs_is_ready(void) { return g_vfs_initialized; }

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

/* Spawn inheritance: share open file descriptions with explicit references so
 * closing one descriptor never destroys another process's handle. Offsets are
 * shared (dup-style) because the vfs_file_t is the open file description. */
void vfs_clone_process_fds(vfs_file_t **dest, vfs_file_t **src) {
    if (!dest || !src) return;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        dest[i] = src[i];
        if (dest[i]) dest[i]->ref_count++;
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

    unsigned access = (flags & O_WRONLY) ? ACCESS_WRITE : ACCESS_READ;
    if(flags & O_RDONLY) access |= ACCESS_READ;
    if(flags & (O_CREAT|O_TRUNC|O_APPEND)) access |= ACCESS_WRITE;
    if(!path_access(path,access)) return -1;


    /* Find lowest free file descriptor */
    int fd = -1;
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (process_get_current_file_slot(i) && !*process_get_current_file_slot(i)) {
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

    *process_get_current_file_slot(fd) = file;
    file->user_access=security_path_user_access(path);
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS)
        return -1;

    vfs_file_t **slot = process_get_current_file_slot((unsigned)fd);
    if (!slot || !*slot)
        return -1;

    vfs_file_t *f = *slot;
    *slot = 0;

    if (f->type == VFS_FILE || f->type == VFS_DIR) {
        pollikfs_close(f);
    }
    free_file_desc(f);
    return 0;
}

int vfs_read(int fd, void *buf, u32 count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !buf || count == 0)
        return -1;

    vfs_file_t **slot = process_get_current_file_slot((unsigned)fd);
    if (!slot || !*slot)
        return -1;

    vfs_file_t *f = *slot;
    if (f->type == VFS_DEVICE) {
        /* Stdin device read */
        if (fd == 0) {
            /* Non-blocking console read */
            return 0;
        }
        return -1;
    }

    if(!(f->flags&O_RDONLY) || !file_access(f,ACCESS_READ)) return -1;
    return pollikfs_read(f, buf, count);
}

int vfs_write(int fd, const void *buf, u32 count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !buf || count == 0)
        return -1;

    vfs_file_t **slot = process_get_current_file_slot((unsigned)fd);
    if (!slot || !*slot)
        return -1;

    vfs_file_t *f = *slot;
    if (f->type == VFS_DEVICE) {
        /* The x86_64 process layer owns stdout/stderr before VFS is reached. */
#ifdef POLLIK_X64
        return -1;
#else
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
#endif
    }

    if(!(f->flags&O_WRONLY) || !file_access(f,ACCESS_WRITE)) return -1;
    return pollikfs_write(f, buf, count);
}
int vfs_write_preflight(int fd,u32 count) {
    vfs_file_t **slot=process_get_current_file_slot((unsigned)fd);
    if(!slot || fd<0 || fd>=VFS_MAX_FDS || !*slot) return -1;
    vfs_file_t *f=*slot;
    if(f->type==VFS_DEVICE) return 0;
    if(!(f->flags&O_WRONLY) || !file_access(f,ACCESS_WRITE)) return -1;
    vfs_stat_t st;
    if(pollikfs_fstat(f,&st)<0) return -1;
    u32 at=(f->flags&O_APPEND)?st.size:f->offset;
    if(at>POLLIK2_MAX_FILE || count>POLLIK2_MAX_FILE-at) { pollikfs_range_denied(); return -1; }
    return 0;
}
int vfs_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FDS)
        return -1;

    vfs_file_t **slot = process_get_current_file_slot((unsigned)fd);
    if (!slot || !*slot)
        return -1;

    vfs_file_t *f = *slot;
    if (f->type == VFS_DEVICE)
        return -1;

    if(!file_access(f,(f->flags&O_RDONLY)?ACCESS_READ:ACCESS_WRITE)) return -1;

    return pollikfs_seek(f, offset, whence);
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if(!path_access(path,ACCESS_READ)) return -1;
    return pollikfs_stat(path, st);
}

int vfs_mkdir(const char *path) {
    if(!path_access(path,ACCESS_WRITE)) return -1;
    return pollikfs_mkdir(path);
}

int vfs_unlink(const char *path) {
    if(!path_access(path,ACCESS_WRITE)) return -1;
    return pollikfs_unlink(path);
}

int vfs_rmdir(const char *path) {
    if(!path_access(path,ACCESS_WRITE)) return -1;
    return pollikfs_rmdir(path);
}

int vfs_readdir(int fd, vfs_dirent_t *dirent) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !dirent)
        return -1;

    vfs_file_t **slot = process_get_current_file_slot((unsigned)fd);
    if (!slot || !*slot)
        return -1;

    vfs_file_t *f = *slot;
    if (f->type != VFS_DIR)
        return -1;

    if(!file_access(f,ACCESS_READ)) return -1;

    return pollikfs_readdir(f, dirent);
}

int vfs_rename(const char *oldpath, const char *newpath) {
    if(!path_access(oldpath,ACCESS_WRITE) || !path_access(newpath,ACCESS_WRITE)) return -1;
    return pollikfs_rename(oldpath, newpath);
}

int vfs_rename_replace(const char *oldpath, const char *newpath) {
    if(!path_access(oldpath,ACCESS_WRITE) || !path_access(newpath,ACCESS_WRITE)) return -1;
    return pollikfs_rename_replace(oldpath, newpath);
}

unsigned vfs_debug_handles(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < g_system_file_count; ++i) if (g_system_files[i].ref_count) ++count;
    return count;
}

int vfs_fstat(int fd, vfs_stat_t *st) {
    vfs_file_t **slot = process_get_current_file_slot((unsigned)fd);
    if (!slot || fd < 0 || fd >= VFS_MAX_FDS || !*slot || !st) return -1;
    if(!file_access(*slot,ACCESS_READ)) return -1;
    return pollikfs_fstat(*slot, st);
}
