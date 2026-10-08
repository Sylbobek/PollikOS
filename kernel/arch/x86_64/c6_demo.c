/* Normal-build C6 demo: an SDK-built C parent spawns, waits and reports. */
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
        memory_log("[C6] demo state="); memory_hex(process->state);
        memory_log(" status="); memory_hex((uint64_t)process->exit_status);
        memory_log(" vector="); memory_hex(process->thread.frame.vector);
        memory_log(" error="); memory_hex(process->thread.frame.error);
        memory_log("\n");
    }
    check(process->state == PROCESS_EXITED && process->exit_status == 42 && !file64_count(process),
          "C6 demo exit status");
    demo_completed = 1;
}
void c6_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    if (vfs_mkdir("/mutation") < 0 && pollikfs_error() != VFS_EXISTS) {
        memory_log("[C6] /mutation: "); memory_hex(pollikfs_error()); memory_log("\n");
    }
    Process64 *process;
    const char *arguments[2] = {"/bin/spawn_parent_c", "basic"};
    Launch64Result result = process64_launch_path("/bin/spawn_parent_c", 2, arguments, 0, 0, &process);
    if (result != LAUNCH_OK) {
        memory_log("[C6] /bin/spawn_parent_c: "); memory_log(launch64_error_name(result)); memory_log("\n");
        return;
    }
    demo_completed = 0;
    check(process64_tick_limit(process->pid, 600) && scheduler64_run(800, 0, complete) &&
          demo_completed && !scheduler64_count() && !vfs_debug_handles() &&
          pmm64_stats().free == baseline, "C6 demo ownership balance");
    memory_log("[C6] demo PASS: C parent spawns, waits and receives child exit 42\n");
}
