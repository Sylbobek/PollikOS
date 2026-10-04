#include "file.h"
#include "stat_abi.h"
#include "dir_abi.h"
#include "path.h"
#include "paging.h"
#include "fs_platform.h"
#include "pipe.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
_Static_assert(USER_FD_LIMIT == VFS_MAX_FDS && USER_PATH_MAX == VFS_MAX_PATH,
               "bounded file ABI agrees with VFS");
_Static_assert(USER_O_RDONLY == O_RDONLY && USER_O_WRONLY == O_WRONLY && USER_O_RDWR == O_RDWR &&
               USER_O_CREAT == O_CREAT && USER_O_TRUNC == O_TRUNC && USER_O_APPEND == O_APPEND,
               "userspace open flags agree with the VFS ABI");
static int64_t fs_error(void) {
    switch (pollikfs_error()) {
    case VFS_NOT_FOUND: return -USER_ENOENT;
    case VFS_BAD_PATH: return -USER_EINVAL;
    case VFS_DENIED: return -USER_EACCES;
    case VFS_NOSPACE: return -USER_ENOSPC;
    case VFS_EXISTS: return -USER_EEXIST;
    case VFS_NOT_EMPTY: return -USER_ENOTEMPTY;
    case VFS_ISDIR: return -USER_EISDIR;
    case VFS_NOTDIR: return -USER_ENOTDIR;
    default: return -USER_EIO;
    }
}
/* Access bits: O_RDONLY=1, O_WRONLY=2, O_RDWR=3 (bit OR semantics). */
static int readable(unsigned flags) { return (flags & USER_O_RDONLY) != 0; }
static int writable(unsigned flags) { return (flags & USER_O_WRONLY) != 0; }
extern void kernel64_debug_bytes(const char *data, size_t length);
void file64_init(Process64 *p) {
    p->streams[0] = FD64_STDIN;
    p->streams[1] = FD64_STDOUT;
    p->streams[2] = FD64_STDERR;
    for (unsigned i = 0; i < USER_FD_LIMIT; ++i) {
        p->files[i] = 0;
        p->pipe_index[i] = PIPE64_NONE;
        p->pipe_write[i] = 0;
        p->console_alias[i] = 0;
        p->cloexec[i] = 0;
    }
    p->cwd[0] = '/'; p->cwd[1] = 0;
}
/* A VFS object, pipe end or explicit console alias overrides the standard
 * stream kind; this is what makes dup2/redirection work on fds 0..2. */
Fd64Kind file64_kind(const Process64 *p, uint64_t fd) {
    if (fd >= USER_FD_LIMIT) return FD64_CLOSED;
    if (p->files[fd] && p->files[fd]->ref_count > 0) return FD64_VFS;
    if (p->pipe_index[fd] != PIPE64_NONE) return FD64_PIPE;
    if (p->console_alias[fd])
        return (Fd64Kind)(FD64_STDIN + (p->console_alias[fd] - 1));
    if (fd < 3) return (Fd64Kind)p->streams[fd];
    return FD64_CLOSED;
}
unsigned file64_stream_count(const Process64 *p) {
    return !!p->streams[0]+!!p->streams[1]+!!p->streams[2];
}
unsigned file64_count(const Process64 *p) {
    unsigned count = 0;
    for (unsigned i = 0; i < USER_FD_LIMIT; ++i) if (p->files[i]) ++count;
    return count;
}
void file64_cleanup(Process64 *p) {
    memory_context_check();
    vfs_close_process_fds(p->files);
    for (unsigned i = 0; i < USER_FD_LIMIT; ++i) {
        if (p->pipe_index[i] != PIPE64_NONE) {
            pipe64_release(p->pipe_index[i], p->pipe_write[i]);
            p->pipe_index[i] = PIPE64_NONE;
            p->pipe_write[i] = 0;
        }
        p->console_alias[i] = 0;
    }
    for (unsigned i = 0; i < 3; ++i) p->streams[i] = FD64_CLOSED;
    p->cwd[0] = 0;
    memory_require(!file64_stream_count(p), "standard stream cleanup");
    memory_require(file64_count(p) == 0, "process descriptor cleanup");
}
static int valid_fd(const Process64 *p, uint64_t fd) {
    return fd >= 3 && fd < USER_FD_LIMIT && p->files[fd] && p->files[fd]->ref_count > 0;
}
static int64_t open_file(Process64 *p, uint64_t address, uint64_t flags, int directory) {
    if (flags & ~(uint64_t)USER_O_FLAGS_MASK) return -USER_EINVAL;
    unsigned access = (unsigned)flags & (USER_O_RDONLY|USER_O_WRONLY|USER_O_RDWR);
    if (access == 0 || access > USER_O_RDWR) return -USER_EINVAL;
    if (directory && flags != USER_O_RDONLY) return -USER_EISDIR;
    if ((flags & (USER_O_TRUNC|USER_O_APPEND)) && !writable((unsigned)flags)) return -USER_EACCES;
    char path[USER_PATH_MAX];
    int64_t path_error = path64_user(p, address, path);
    if (path_error) return path_error;
    unsigned slot = 3;
    while (slot < USER_FD_LIMIT && p->files[slot]) ++slot;
    if (slot == USER_FD_LIMIT) return -USER_EMFILE;
    if (!pollikfs_mounted()) return -USER_EIO;
    if ((flags & USER_O_EXCL) && !(flags & USER_O_CREAT)) return -USER_EINVAL;
    vfs_stat_t st;
    if (!(flags & USER_O_CREAT)) {
        if (vfs_stat(path, &st) < 0) return fs_error();
        u32 required_type = directory ? VFS_DIR : VFS_FILE;
        if (st.type != required_type) return directory ? -USER_ENOTDIR : -USER_EISDIR;
    } else if (flags & USER_O_EXCL) {
        /* Exclusive create: single-CPU IF=0 makes stat+create atomic. */
        if (vfs_stat(path, &st) == 0) return -USER_EEXIST;
    }
    /* USER_O_CLOEXEC is a per-descriptor process attribute, not a VFS flag. */
    int fd = vfs_open(path, (int)(flags & ~(uint64_t)USER_O_CLOEXEC));
    if (fd < 0) return pollikfs_error() == VFS_OK ? -USER_ENOMEM : fs_error();
    /* IF=0 keeps stat/open stable; retain a defensive type check so future
     * backends cannot expose raw directory blocks. */
    u32 required_type = directory ? VFS_DIR : VFS_FILE;
    if (p->files[fd]->type != required_type) {
        vfs_close(fd);
        return directory ? -USER_ENOTDIR : -USER_EISDIR;
    }
    p->cloexec[fd] = (flags & USER_O_CLOEXEC) ? 1 : 0;
    return fd;
}
static int64_t read_file(Process64 *p, uint64_t fd, uint64_t destination, uint64_t count) {
    Fd64Kind kind = file64_kind(p, fd);
    if (kind == FD64_CLOSED) return -USER_EBADF;
    if (kind == FD64_PIPE) return -USER_EIO; /* handled by the blocking pipe path */
    if (kind == FD64_STDIN) return count ? -USER_ENOTSUP : 0;
    if (kind != FD64_VFS) return -USER_EACCES;
    vfs_file_t *file = p->files[fd];
    if (file->type != VFS_FILE || !readable(file->flags)) return -USER_EACCES;
    if (count > USER_READ_MAX) return -USER_E2BIG;
    if (!count) return 0; /* valid fd required; destination is unused */
    if (user_range_check(&p->space, destination, (size_t)count, 1) != USER_COPY_OK) return -USER_EFAULT;
    uint8_t bounce[USER_READ_MAX];
    uint32_t before = file->offset;
    int n = vfs_read((int)fd, bounce, (u32)count);
    if (n < 0 || (uint64_t)n > count) { file->offset = before; return -USER_EIO; }
    if (n && copy_to_user64(&p->space, destination, bounce, (size_t)n) != USER_COPY_OK) {
        file->offset = before;
        return -USER_EFAULT;
    }
    return n;
}
static int64_t seek_file(Process64 *p, uint64_t fd, int64_t offset, uint64_t whence) {
    if (fd < 3 && file64_kind(p, fd) != FD64_CLOSED) return -USER_ENOTSUP;
    if (!valid_fd(p, fd)) return -USER_EBADF;
    if (p->files[fd]->type == VFS_DIR) {
        if (offset || whence != USER_SEEK_SET) return -USER_EINVAL;
        p->files[fd]->offset = 0;
        return 0;
    }
    /* Current VFS offsets are bounded signed 32-bit. Check before narrowing. */
    if (offset < INT32_MIN || offset > INT32_MAX || whence > USER_SEEK_END) return -USER_EINVAL;
    int result = vfs_seek((int)fd, (int)offset, (int)whence);
    return result < 0 ? -USER_EINVAL : result;
}
static int64_t metadata(Process64 *p, uint64_t source, uint64_t destination, int by_fd) {
    if (by_fd && source < 3 && file64_kind(p, source) != FD64_CLOSED) return -USER_ENOTSUP;
    if (by_fd && !valid_fd(p, source)) return -USER_EBADF;
    if (user_range_check(&p->space, destination, sizeof(UserStat64), 1) != USER_COPY_OK)
        return -USER_EFAULT;
    vfs_stat_t st = {0};
    if (!pollikfs_mounted()) return -USER_EIO;
    if (by_fd) {
        if (vfs_fstat((int)source, &st) < 0) return fs_error();
    } else {
        char path[USER_PATH_MAX];
        int64_t path_error = path64_user(p, source, path);
        if (path_error) return path_error;
        if (vfs_stat(path, &st) < 0) return fs_error();
    }
    UserStat64 result = {0};
    result.version = USER_STAT_VERSION;
    result.struct_size = sizeof(result);
    result.type = st.type == VFS_FILE ? USER_TYPE_REGULAR :
                  st.type == VFS_DIR ? USER_TYPE_DIRECTORY : USER_TYPE_UNKNOWN;
    result.timestamp_kind = USER_TIME_POLLIK_TICKS;
    /* Unsigned 32-bit on-disk values widen exactly; no signed intermediate. */
    result.size_bytes = st.size;
    result.inode = st.inode;
    result.created_ticks = st.created;
    result.modified_ticks = st.modified;
    return copy_to_user64(&p->space, destination, &result, sizeof(result)) == USER_COPY_OK ? 0 : -USER_EFAULT;
}
static int64_t read_directory(Process64 *p, uint64_t fd, uint64_t destination) {
    if (fd < 3 && file64_kind(p, fd) != FD64_CLOSED) return -USER_ENOTDIR;
    if (!valid_fd(p, fd)) return -USER_EBADF;
    vfs_file_t *file = p->files[fd];
    if (file->type != VFS_DIR) return -USER_ENOTDIR;
    if (user_range_check(&p->space, destination, sizeof(UserDirent64), 1) != USER_COPY_OK)
        return -USER_EFAULT;
    vfs_dirent_t entry = {0};
    uint32_t before = file->offset;
    int n = vfs_readdir((int)fd, &entry);
    if (n != 1) {
        file->offset = before;
        return n == 0 ? 0 : -USER_EIO;
    }
    unsigned length = 0;
    while (length < sizeof(entry.name) && entry.name[length]) ++length;
    if (!length || length > USER_DIRENT_NAME_MAX) {
        file->offset = before;
        return -USER_EIO;
    }
    UserDirent64 result = {0};
    result.version = USER_DIRENT_VERSION;
    result.struct_size = sizeof(result);
    result.type = entry.type == VFS_FILE ? USER_TYPE_REGULAR :
                  entry.type == VFS_DIR ? USER_TYPE_DIRECTORY : USER_TYPE_UNKNOWN;
    result.name_length = length;
    result.inode = entry.inode;
    for (unsigned i = 0; i < length; ++i) {
        if (entry.name[i] == '/') { file->offset = before; return -USER_EIO; }
        result.name[i] = entry.name[i];
    }
    if (copy_to_user64(&p->space, destination, &result, sizeof(result)) != USER_COPY_OK) {
        file->offset = before;
        return -USER_EFAULT;
    }
    return 1;
}
static int64_t close_file(Process64 *p, uint64_t fd) {
    if (fd >= USER_FD_LIMIT || file64_kind(p, fd) == FD64_CLOSED) return -USER_EBADF;
    Fd64Kind kind = file64_kind(p, fd);
    if (kind == FD64_VFS) {
        if (vfs_close((int)fd) < 0) return -USER_EIO;
        p->files[fd] = 0;
    } else if (kind == FD64_PIPE) {
        pipe64_release(p->pipe_index[fd], p->pipe_write[fd]);
        p->pipe_index[fd] = PIPE64_NONE;
        p->pipe_write[fd] = 0;
        pipe64_wake_blocked();
    } else if (fd < 3) {
        p->streams[fd] = FD64_CLOSED;
    }
    p->console_alias[fd] = 0;
    p->cloexec[fd] = 0;
    return 0;
}
/* Copy one descriptor's backing to another slot (dup/dup2 core). */
static void fd_copy_backing(Process64 *p, unsigned dest, unsigned source) {
    if (p->files[source]) {
        p->files[dest] = p->files[source];
        ++p->files[source]->ref_count;
    } else if (p->pipe_index[source] != PIPE64_NONE) {
        p->pipe_index[dest] = p->pipe_index[source];
        p->pipe_write[dest] = p->pipe_write[source];
        pipe64_retain(p->pipe_index[source], p->pipe_write[source]);
    }
    p->console_alias[dest] = p->console_alias[source];
    /* POSIX: a duplicated descriptor never inherits close-on-exec. */
    p->cloexec[dest] = 0;
}
/* Set (or clear) a descriptor's close-on-spawn flag; returns the previous
 * value. This is the fcntl-like entry point behind pollikos_set_cloexec. */
static int64_t cloexec_request(Process64 *p, uint64_t fd, uint64_t set) {
    if (fd >= USER_FD_LIMIT || file64_kind(p, fd) == FD64_CLOSED) return -USER_EBADF;
    if (set > 1) return -USER_EINVAL;
    uint8_t previous = p->cloexec[fd];
    p->cloexec[fd] = (uint8_t)set;
    return previous;
}
static int64_t dup_request(Process64 *p, uint64_t oldfd, uint64_t newfd, int explicit_new) {
    if (oldfd >= USER_FD_LIMIT) return -USER_EBADF;
    Fd64Kind kind = file64_kind(p, oldfd);
    if (kind == FD64_CLOSED) return -USER_EBADF;
    unsigned dest;
    if (explicit_new) {
        if (newfd >= USER_FD_LIMIT) return -USER_EBADF;
        dest = (unsigned)newfd;
        if (dest == oldfd) return 0;
        (void)close_file(p, dest); /* an occupied target is replaced */
    } else {
        for (dest = 3; dest < USER_FD_LIMIT && file64_kind(p, dest) != FD64_CLOSED; ++dest) {}
        if (dest == USER_FD_LIMIT) return -USER_EMFILE;
    }
    /* Make an unbacked standard stream explicit so it can be restored. */
    if (!p->files[oldfd] && p->pipe_index[oldfd] == PIPE64_NONE && !p->console_alias[oldfd]) {
        if (oldfd >= 3 || !p->streams[oldfd]) return -USER_EBADF;
        p->console_alias[oldfd] = (uint8_t)(p->streams[oldfd] - FD64_STDIN + 1);
    }
    fd_copy_backing(p, dest, (unsigned)oldfd);
    return (int64_t)dest;
}
static int64_t pipe_request(Process64 *p, uint64_t address) {
    if (user_range_check(&p->space, address, 2 * sizeof(int32_t), 1) != USER_COPY_OK)
        return -USER_EFAULT;
    unsigned first = 3, second = 3;
    while (first < USER_FD_LIMIT && file64_kind(p, first) != FD64_CLOSED) ++first;
    for (second = first + 1; second < USER_FD_LIMIT && file64_kind(p, second) != FD64_CLOSED; ++second) {}
    if (second >= USER_FD_LIMIT) return -USER_EMFILE;
    uint8_t index = 0;
    if (pipe64_create(&index) < 0) return -USER_ENOMEM;
    p->pipe_index[first] = index;
    p->pipe_write[first] = 0;
    p->pipe_index[second] = index;
    p->pipe_write[second] = 1;
    int32_t fds[2] = {(int32_t)first, (int32_t)second};
    if (copy_to_user64(&p->space, address, fds, sizeof(fds)) != USER_COPY_OK) {
        pipe64_release(index, 0);
        pipe64_release(index, 1);
        p->pipe_index[first] = PIPE64_NONE;
        p->pipe_index[second] = PIPE64_NONE;
        return -USER_EFAULT;
    }
    return 0;
}
/* Spawn inheritance: VFS and pipe ends are refcounted, console aliases copied,
 * and a close-on-spawn descriptor is dropped instead of shared. The child's
 * table starts empty (file64_init), so a skipped descriptor simply stays
 * closed there. */
void file64_clone_parent(Process64 *child, Process64 *parent) {
    for (unsigned i = 0; i < USER_FD_LIMIT; ++i) {
        if (parent->cloexec[i]) continue;
        if (parent->files[i]) {
            child->files[i] = parent->files[i];
            ++child->files[i]->ref_count;
        }
        child->pipe_index[i] = parent->pipe_index[i];
        child->pipe_write[i] = parent->pipe_write[i];
        child->console_alias[i] = parent->console_alias[i];
        child->cloexec[i] = 0;
        if (parent->pipe_index[i] != PIPE64_NONE)
            pipe64_retain(parent->pipe_index[i], parent->pipe_write[i]);
    }
    for (unsigned i = 0; i < 3; ++i)
        child->streams[i] = parent->cloexec[i] ? FD64_CLOSED : parent->streams[i];
}
static int64_t write_file(Process64 *p, uint64_t fd, uint64_t source, uint64_t count) {
    Fd64Kind kind = file64_kind(p, fd);
    if (kind == FD64_CLOSED) return -USER_EBADF;
    if (kind == FD64_PIPE) return -USER_EIO; /* handled by the blocking pipe path */
    if (kind == FD64_STDIN) return -USER_EACCES;
    if (count > USER_WRITE_MAX) return -USER_E2BIG;
    if (!count) return 0;
    if (user_range_check(&p->space, source, count, 0) != USER_COPY_OK) return -USER_EFAULT;
    char buffer[USER_WRITE_CHUNK];
    uint64_t written = 0;
    int console = kind == FD64_STDOUT || kind == FD64_STDERR;
    if (!console) {
        vfs_file_t *file = p->files[fd];
        if (kind != FD64_VFS || file->type != VFS_FILE || !writable(file->flags)) return -USER_EACCES;
    }
    while (written < count) {
        size_t chunk = count-written > sizeof(buffer) ? sizeof(buffer) : (size_t)(count-written);
        if (copy_from_user64(&p->space, buffer, source+written, chunk) != USER_COPY_OK)
            return written ? (int64_t)written : -USER_EFAULT;
        if (console) {
            /* Polled COM1 consumes every byte; no short-write contract. */
            kernel64_debug_bytes(buffer, chunk);
            written += chunk;
            continue;
        }
        int result = vfs_write((int)fd, buffer, (u32)chunk);
        /* A filesystem short write is never silent: it would truncate user
         * output (this is how a truncated native TinyCC executable was once
         * mistaken for a bad ELF), so report the bound and backend error. */
        if (result < 0 || (size_t)result < chunk) {
            memory_log("[VFS64] short write chunk="); memory_hex(chunk);
            memory_log(" result="); memory_hex((uint64_t)(int64_t)result);
            memory_log(" error="); memory_hex((uint64_t)pollikfs_error());
            memory_log("\n");
        }
        if (result < 0) return written ? (int64_t)written : fs_error();
        written += (uint64_t)result;
        if ((size_t)result < chunk) break; /* bounded filesystem short write */
    }
    return (int64_t)written;
}
static int64_t change_directory(Process64 *p, uint64_t address) {
    char path[USER_PATH_MAX];
    int64_t error = path64_user(p, address, path);
    if (error) return error;
    vfs_stat_t st = {0};
    if (vfs_stat(path, &st) < 0) return fs_error();
    if (st.type != VFS_DIR) return -USER_ENOTDIR;
    for (unsigned i = 0; i < USER_PATH_MAX; ++i) { p->cwd[i] = path[i]; if (!path[i]) break; }
    return 0;
}
static int64_t get_directory(Process64 *p, uint64_t destination, uint64_t capacity) {
    size_t length = 0;
    while (length < sizeof(p->cwd) && p->cwd[length]) ++length;
    if (length == sizeof(p->cwd)) return -USER_EIO;
    if (capacity < length+1) return -USER_ERANGE;
    return copy_to_user64(&p->space, destination, p->cwd, length+1) == USER_COPY_OK ? (int64_t)length : -USER_EFAULT;
}
static int64_t make_directory(Process64 *p, uint64_t address) {
    char path[USER_PATH_MAX];
    int64_t error = path64_user(p, address, path);
    if (error) return error;
    if (!pollikfs_mounted()) return -USER_EIO;
    return vfs_mkdir(path) < 0 ? fs_error() : 0;
}
static int64_t remove_file(Process64 *p, uint64_t address) {
    char path[USER_PATH_MAX];
    int64_t error = path64_user(p, address, path);
    if (error) return error;
    if (!pollikfs_mounted()) return -USER_EIO;
    return vfs_unlink(path) < 0 ? fs_error() : 0;
}
static int64_t remove_directory(Process64 *p, uint64_t address) {
    char path[USER_PATH_MAX];
    int64_t error = path64_user(p, address, path);
    if (error) return error;
    if (!pollikfs_mounted()) return -USER_EIO;
    return vfs_rmdir(path) < 0 ? fs_error() : 0;
}
static int64_t rename_entry(Process64 *p, uint64_t old_address, uint64_t new_address,
                            int replace) {
    char old_path[USER_PATH_MAX], new_path[USER_PATH_MAX];
    int64_t error = path64_user(p, old_address, old_path);
    if (error) return error;
    error = path64_user(p, new_address, new_path);
    if (error) return error;
    if (!pollikfs_mounted()) return -USER_EIO;
    int result = replace ? vfs_rename_replace(old_path, new_path)
                         : vfs_rename(old_path, new_path);
    return result < 0 ? fs_error() : 0;
}
int file64_dispatch(Process64 *p, UserFrame *f) {
    switch (f->rax) {
    case USER_WRITE: case USER_CHDIR: case USER_GETCWD:
    case USER_OPEN: case USER_OPENDIR: case USER_READDIR:
    case USER_READ: case USER_SEEK: case USER_STAT: case USER_FSTAT:
    case USER_CLOSE: case USER_MKDIR: case USER_UNLINK: case USER_RMDIR:
    case USER_RENAME: case USER_RENAME_REPLACE:
    case USER_PIPE: case USER_DUP: case USER_DUP2: case USER_CLOEXEC:
        break;
    default:
        return 0;
    }
    memory_context_check();
    /* Bind only while handling this syscall. Kernel executable loading keeps
     * its separate descriptor context, even while user files remain open. */
    vfs_file_t **previous = fs64_fd_context(p->files);
    int64_t result;
    switch (f->rax) {
    case USER_WRITE: result = write_file(p, f->rdi, f->rsi, f->rdx); break;
    case USER_CHDIR: result = change_directory(p, f->rdi); break;
    case USER_GETCWD: result = get_directory(p, f->rdi, f->rsi); break;
    case USER_OPEN: result = open_file(p, f->rdi, f->rsi, 0); break;
    case USER_OPENDIR: result = open_file(p, f->rdi, f->rsi, 1); break;
    case USER_READDIR: result = read_directory(p, f->rdi, f->rsi); break;
    case USER_READ: result = read_file(p, f->rdi, f->rsi, f->rdx); break;
    case USER_SEEK: result = seek_file(p, f->rdi, (int64_t)f->rsi, f->rdx); break;
    case USER_STAT: result = metadata(p, f->rdi, f->rsi, 0); break;
    case USER_FSTAT: result = metadata(p, f->rdi, f->rsi, 1); break;
    case USER_MKDIR: result = make_directory(p, f->rdi); break;
    case USER_UNLINK: result = remove_file(p, f->rdi); break;
    case USER_RMDIR: result = remove_directory(p, f->rdi); break;
    case USER_RENAME: result = rename_entry(p, f->rdi, f->rsi, 0); break;
    case USER_RENAME_REPLACE: result = rename_entry(p, f->rdi, f->rsi, 1); break;
    case USER_PIPE: result = pipe_request(p, f->rdi); break;
    case USER_DUP: result = dup_request(p, f->rdi, 0, 0); break;
    case USER_DUP2: result = dup_request(p, f->rdi, f->rsi, 1); break;
    case USER_CLOEXEC: result = cloexec_request(p, f->rdi, f->rsi); break;
    default: result = close_file(p, f->rdi); break;
    }
    fs64_fd_context(previous);
    f->rax = (uint64_t)result;
    return 1;
}
