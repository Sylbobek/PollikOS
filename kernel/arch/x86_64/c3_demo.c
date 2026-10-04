/* Normal-build C3 demo: a real C program with crt0 + libpollikc launched by
 * VFS path. The selftest suite extends this with the full C runtime checks. */
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#define check memory_require
static int demo_completed;
static void complete(const Process64 *process) {
    if (process->state != PROCESS_EXITED || process->exit_status != 42) {
        memory_log("[C3] demo state="); memory_hex(process->state);
        memory_log(" status="); memory_hex((uint64_t)process->exit_status);
        memory_log(" vector="); memory_hex(process->frame.vector);
        memory_log(" error="); memory_hex(process->frame.error);
        memory_log("\n");
    }
    check(process->state == PROCESS_EXITED && process->exit_status == 42, "C runtime demo exit 42");
    demo_completed = 1;
}
void c3_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    Process64 *process;
    Launch64Result result = process64_launch_path("/bin/hello_c", 0, 0, 0, 0, &process);
    if (result != LAUNCH_OK) {
        memory_log("[C3] /bin/hello_c: "); memory_log(launch64_error_name(result)); memory_log("\n");
        return;
    }
    demo_completed = 0;
    check(process64_tick_limit(process->pid, 300) && scheduler64_run(400, 0, complete) &&
          demo_completed && !scheduler64_count() && !vfs_debug_handles() &&
          pmm64_stats().free == baseline, "C runtime demo ownership balance");
    memory_log("[C3] demo PASS: C main(argc,argv) via crt0+libpollikc prints and exits 42\n");
}
