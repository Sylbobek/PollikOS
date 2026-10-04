/* Normal-build C4 demo: a PollikOS SDK-built application (built by pollikcc,
 * installed through pollikinstall) launched from PollikFS. */
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#define check memory_require
static int demo_completed;
static void complete(const Process64 *process) {
    if (process->state != PROCESS_EXITED || process->exit_status != 0) {
        memory_log("[C4] demo state="); memory_hex(process->state);
        memory_log(" status="); memory_hex((uint64_t)process->exit_status);
        memory_log(" vector="); memory_hex(process->frame.vector);
        memory_log(" error="); memory_hex(process->frame.error);
        memory_log("\n");
    }
    check(process->state == PROCESS_EXITED && process->exit_status == 0, "SDK demo exit 0");
    demo_completed = 1;
}
void c4_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    Process64 *process;
    Launch64Result result = process64_launch_path("/bin/sdk_hello", 0, 0, 0, 0, &process);
    if (result != LAUNCH_OK) {
        memory_log("[C4] /bin/sdk_hello: "); memory_log(launch64_error_name(result)); memory_log("\n");
        return;
    }
    demo_completed = 0;
    check(process64_tick_limit(process->pid, 300) && scheduler64_run(400, 0, complete) &&
          demo_completed && !scheduler64_count() && !vfs_debug_handles() &&
          pmm64_stats().free == baseline, "SDK demo ownership balance");
    memory_log("[C4] demo PASS: pollikcc-built C app runs from PollikFS and exits 0\n");
}
