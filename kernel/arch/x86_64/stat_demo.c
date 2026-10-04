#include "file.h"
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
static int completed;
static void complete(const Process64 *p) {
    memory_require(p->state == PROCESS_EXITED && p->exit_status == 42 && !file64_count(p),
                   "userspace metadata demo");
    completed = 1;
}
void stat64_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    Process64 *p;
    Launch64Result result = process64_launch_path("/bin/stattest", 0, 0, 0, 0, &p);
    if (result != LAUNCH_OK) {
        memory_log("[STAT64] /bin/stattest: "); memory_log(launch64_error_name(result)); memory_log("\n");
        return;
    }
    completed = 0;
    memory_require(!file64_count(p) && process64_tick_limit(p->pid, 20) &&
        scheduler64_run(30, 0, complete) && completed && !scheduler64_count() &&
        !vfs_debug_handles() && pmm64_stats().free == baseline, "file demo ownership balance");
    memory_log("[STAT64] demo PASS: VFS-launched ELF stat/fstat exits 42\n");
}
