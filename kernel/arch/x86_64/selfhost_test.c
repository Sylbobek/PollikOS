#include "launch.h"
#include "scheduler.h"
#include "file.h"
#include "paging.h"
#include "../../vfs.h"

#define check memory_require

static uint64_t selfhost_pid;
static int completed;

static void completion(const Process64 *process) {
    if (process->pid != selfhost_pid) return;
    if (process->state != PROCESS_EXITED || process->exit_status != 42) {
        memory_log("[SELFHOST] driver state="); memory_hex(process->state);
        memory_log(" status="); memory_hex((uint64_t)process->exit_status);
        memory_log(" vector="); memory_hex(process->frame.vector); memory_log("\n");
    }
    check(process->state == PROCESS_EXITED && process->exit_status == 42,
          "native TinyCC driver exits 42");
    completed = 1;
}

void selfhost_selftest(void) {
    vfs_stat_t info;
    if (vfs_stat("/etc/diskfull", &info) == 0) {
        /* Dedicated almost-full image: only the C5 ENOSPC path is meaningful. */
        memory_log("[SELFHOST] skipped: dedicated full image\n");
        return;
    }
    page_count_t baseline = pmm64_stats().free;
    Process64 *process = 0;
    const char *arguments[] = {"selfhost_driver_c"};
    check(process64_launch_path("/bin/selfhost_driver_c", 1, arguments, 0, 0, &process) == LAUNCH_OK,
          "native TinyCC driver launch");
    selfhost_pid = process->pid;
    check(process64_tick_limit(process->pid, 100000), "native TinyCC tick budget");
    check(scheduler64_run(110000, 0, completion) && completed && !scheduler64_count(),
          "native TinyCC workflow completes");
    check(!vfs_debug_handles(), "native TinyCC VFS handles balance");
    check(process64_debug_slots() == 0 && process64_debug_zombies() == 0,
          "native TinyCC process balance");
    check(pmm64_stats().free == baseline, "native TinyCC PMM balance");
    memory_log("[SELFHOST] balance pmm="); memory_hex(baseline);
    memory_log(" handles="); memory_hex(vfs_debug_handles());
    memory_log(" slots="); memory_hex(process64_debug_slots());
    memory_log(" zombies="); memory_hex(process64_debug_zombies()); memory_log("\n");
}
