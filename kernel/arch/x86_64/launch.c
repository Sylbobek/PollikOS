#include "launch.h"
#include "scheduler.h"
#include "paging.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
#define IMAGE_BUFFER (MM_KERNEL_START+0x400000)
static int busy;
#ifdef SELFTEST
static unsigned read_chunk;
static int64_t eof_after = -1;
void launch64_read_inject(unsigned chunk, int64_t eof) { read_chunk = chunk; eof_after = eof; }
#endif
static Launch64Result fs_error(void) {
    switch (pollikfs_error()) {
    case VFS_NOT_FOUND: return LAUNCH_NOT_FOUND;
    case VFS_BAD_PATH: return LAUNCH_BAD_REQUEST;
    case VFS_DENIED: return LAUNCH_DENIED;
    case VFS_CORRUPT: return LAUNCH_CORRUPT_FS;
    default: return LAUNCH_IO;
    }
}
const char *launch64_error_name(Launch64Result r) {
    static const char *names[] = {"ok", "bad request", "not found", "not regular file",
        "denied", "I/O error", "corrupt filesystem", "file too short", "file too large",
        "invalid ELF", "unsupported ELF", "out of memory", "process table full",
        "scheduler insertion rejected", "filesystem not mounted"};
    return (unsigned)r < sizeof(names)/sizeof(names[0]) ? names[r] : "unknown error";
}
/* Load an ELF from PollikFS into a fully built, READY process. The caller owns
 * the result until scheduler64_submit succeeds; every failure path reclaims all
 * buffers, handles and the partially built process. Shared by kernel launches
 * and the userspace spawn syscall so the loader exists exactly once. */
Launch64Result launch64_create(const char *path, size_t argc, const char *const *argv,
    size_t envc, const char *const *envp, Process64 **out) {
    memory_context_check();
    if (out) *out = 0;
    if (!path || busy || argc > STARTUP_MAX_ARGS || envc > STARTUP_MAX_ENV ||
        (argc && !argv) || (envc && !envp)) return LAUNCH_BAD_REQUEST;
    size_t length = 0;
    while (length < VFS_MAX_PATH && path[length]) ++length;
    if (!length || length == VFS_MAX_PATH || path[0] != '/') return LAUNCH_BAD_REQUEST;
    if (!pollikfs_mounted()) return LAUNCH_NOT_MOUNTED;
    if (process64_slots_full()) return LAUNCH_TABLE_FULL;
    busy = 1;
    Launch64Result result = LAUNCH_IO;
    size_t pages = 0;
    Process64 *process = 0;
    int fd = -1;
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) { result = fs_error(); goto done; }
    if (st.type != VFS_FILE) { result = LAUNCH_NOT_FILE; goto done; }
    if (st.size < sizeof(Elf64Header)) { result = LAUNCH_BAD_SIZE; goto done; }
    if (st.size > LAUNCH64_MAX_IMAGE) { result = LAUNCH_TOO_LARGE; goto done; }
    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { result = fs_error(); goto done; }
    size_t required = ((size_t)st.size+4095)/4096;
    for (; pages < required; ++pages) {
        if (vmm64_alloc_page(vmm64_kernel(), IMAGE_BUFFER+pages*4096, VM_WRITE) != VM_OK) {
            result = LAUNCH_NOMEM; goto done;
        }
    }
    size_t received = 0;
    while (received < st.size) {
        u32 count = st.size-(u32)received;
#ifdef SELFTEST
        if (read_chunk && count > read_chunk) count = read_chunk;
        if (eof_after >= 0 && (uint64_t)eof_after <= received) { result = LAUNCH_IO; goto done; }
        if (eof_after >= 0 && count > (uint64_t)eof_after-received) count = (u32)((uint64_t)eof_after-received);
#endif
        int n = vfs_read(fd, (void *)(IMAGE_BUFFER+received), count);
        if (n <= 0 || (u32)n > count) { result = n < 0 ? fs_error() : LAUNCH_IO; goto done; }
        received += (unsigned)n;
    }
    Elf64Result error;
    process = process64_create_elf((void *)IMAGE_BUFFER, st.size, argc, argv, envc, envp, &error);
    if (!process) {
        result = error == ELF64_NOMEM ? LAUNCH_NOMEM : error == ELF64_ARGUMENTS ? LAUNCH_BAD_REQUEST :
                 error == ELF64_UNSUPPORTED ? LAUNCH_UNSUPPORTED_ELF : LAUNCH_INVALID_ELF;
        goto done;
    }
    size_t base = length;
    while (base && path[base-1] != '/') --base;
    size_t name_length = length-base;
    if (name_length >= sizeof(process->name)) name_length = sizeof(process->name)-1;
    for (size_t i = 0; i < name_length; ++i) process->name[i] = path[base+i];
    process->name[name_length] = 0;
    result = LAUNCH_OK;
    if (out) *out = process;
done:
    if (result != LAUNCH_OK && process) memory_require(process64_destroy(process), "launch rollback process ownership");
    while (pages) {
        --pages;
        memory_require(vmm64_unmap(vmm64_kernel(), IMAGE_BUFFER+pages*4096, 1, 0) == VM_OK,
                       "launch temporary buffer release");
    }
    if (fd >= 0) memory_require(vfs_close(fd) == 0, "launch descriptor close");
    busy = 0;
    return result;
}
Launch64Result process64_launch_path(const char *path, size_t argc, const char *const *argv,
    size_t envc, const char *const *envp, Process64 **out) {
    if (out) *out = 0;
    Process64 *process = 0;
    Launch64Result result = launch64_create(path, argc, argv, envc, envp, &process);
    if (result != LAUNCH_OK) return result;
    if (!scheduler64_submit(process)) {
        memory_require(process64_destroy(process), "launch insertion rollback");
        return LAUNCH_QUEUE;
    }
    if (out) *out = process;
    return LAUNCH_OK;
}
