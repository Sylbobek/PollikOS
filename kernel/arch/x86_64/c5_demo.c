/* Normal-build C5 demo: SDK-built C programs mutate regular files through the
 * VFS/PollikFS write path. The self-test build extends this with full checks. */
#include "launch.h"
#include "scheduler.h"
#include "file.h"
#include "fs_platform.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
#define check memory_require
static int demo_completed, expected_status;
static void complete(const Process64 *process) {
    if (process->state != PROCESS_EXITED || process->exit_status != expected_status) {
        memory_log("[C5] demo state="); memory_hex(process->state);
        memory_log(" status="); memory_hex((uint64_t)process->exit_status);
        memory_log(" vector="); memory_hex(process->thread.frame.vector);
        memory_log(" error="); memory_hex(process->thread.frame.error);
        memory_log("\n");
    }
    check(process->state == PROCESS_EXITED && process->exit_status == expected_status &&
          !file64_count(process), "C5 demo exit status");
    demo_completed = 1;
}
static void launch_demo(const char *path, const char *mode, int status) {
    Process64 *process;
    const char *arguments[2] = {path, mode};
    size_t argc = mode ? 2 : 1;
    check(process64_launch_path(path, argc, arguments, 0, 0, &process) == LAUNCH_OK && process,
          "C5 demo launch");
    expected_status = status;
    demo_completed = 0;
    check(process64_tick_limit(process->pid, 500) && scheduler64_run(600, 0, complete) &&
          demo_completed && !scheduler64_count(), "C5 demo completion");
}
void c5_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    if (vfs_mkdir("/tmp/mutation") < 0 && pollikfs_error() != VFS_EXISTS) {
        memory_log("[C5] /tmp/mutation: "); memory_hex(pollikfs_error()); memory_log("\n");
    }
    launch_demo("/bin/sdk_write", 0, 0);
    launch_demo("/bin/mutation_c", "demo", 42);
    check(!vfs_debug_handles() && pmm64_stats().free == baseline, "C5 demo ownership balance");
    memory_log("[C5] demo PASS: userspace create/write/read/rename/unlink through the SDK\n");
}
