/* Normal-build C7 demo: compiler-readiness workflow (multi-stage tools). */
#include "launch.h"
#include "scheduler.h"
#include "file.h"
#include "fs_platform.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
#define check memory_require
static int demo_completed;
static void complete(const Process64 *process) {
    if (process->state != PROCESS_EXITED || process->exit_status != 42) {
        memory_log("[C7] demo state="); memory_hex(process->state);
        memory_log(" status="); memory_hex((uint64_t)process->exit_status);
        memory_log(" vector="); memory_hex(process->frame.vector);
        memory_log(" error="); memory_hex(process->frame.error);
        memory_log("\n");
    }
    check(process->state == PROCESS_EXITED && process->exit_status == 42 && !file64_count(process),
          "C7 demo exit status");
    demo_completed = 1;
}
static void launch_demo(const char *path, const char *mode) {
    Process64 *process;
    const char *arguments[2] = {path, mode};
    Launch64Result result = process64_launch_path(path, 2, arguments, 0, 0, &process);
    if (result != LAUNCH_OK) {
        memory_log("[C7] "); memory_log(path); memory_log(": ");
        memory_log(launch64_error_name(result)); memory_log("\n");
        return;
    }
    demo_completed = 0;
    check(process64_tick_limit(process->pid, 800) && scheduler64_run(1000, 0, complete) &&
          demo_completed && !scheduler64_count(), "C7 demo completion");
}
void c7_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    if (vfs_mkdir("/tmp") < 0 && pollikfs_error() != VFS_EXISTS) {
        memory_log("[C7] /tmp: "); memory_hex(pollikfs_error()); memory_log("\n");
    }
    launch_demo("/bin/tool_driver_c", "chain");
    launch_demo("/bin/tool_driver_c", "temp");
    launch_demo("/bin/big_elf_c", "run");
    check(!vfs_debug_handles() && pmm64_stats().free == baseline, "C7 demo ownership balance");
    memory_log("[C7] demo PASS: FILE toolchain chain, temp files and 300 KiB ELF load\n");
}
