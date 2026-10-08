#include "file.h"
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
static int completed;
static void complete(const Process64 *p) {
    if (p->state != PROCESS_EXITED || p->exit_status != 42 || file64_count(p)) {
        memory_log("[DIR64] demo state="); memory_hex(p->state);
        memory_log(" status="); memory_hex((uint64_t)p->exit_status);
        memory_log(" vector="); memory_hex(p->thread.frame.vector);
        memory_log(" error="); memory_hex(p->thread.frame.error);
        memory_log(" line="); memory_hex(p->thread.frame.r15);
        memory_log(" descriptors="); memory_hex(file64_count(p));
        memory_log("\n");
    }
    memory_require(p->state == PROCESS_EXITED && p->exit_status == 42 && !file64_count(p),
                   "userspace directory demo");
    completed = 1;
}
void dir64_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    Process64 *p;
    Launch64Result result = process64_launch_path("/bin/dirtest", 0, 0, 0, 0, &p);
    if (result != LAUNCH_OK) {
        memory_log("[DIR64] /bin/dirtest: "); memory_log(launch64_error_name(result)); memory_log("\n");
        return;
    }
    completed = 0;
    memory_require(!file64_count(p) && process64_tick_limit(p->pid, 20) &&
        scheduler64_run(30, 0, complete) && completed && !scheduler64_count() &&
        !vfs_debug_handles() && pmm64_stats().free == baseline, "file demo ownership balance");
    memory_log("[DIR64] demo PASS: VFS-launched ELF opendir/readdir/close exits 42\n");
}
