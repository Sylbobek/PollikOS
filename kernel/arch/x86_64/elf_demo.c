#include "elf64.h"
#include "scheduler.h"
#include "launch.h"
#include "fs_platform.h"
static int demo_complete;
static void complete(const Process64 *process) {
    memory_require(process->state == PROCESS_EXITED && process->exit_status == 42,
                   "scheduled ELF demo exit");
    demo_complete = 1;
}
int elf64_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    const char *argv[] = {"hello.elf", "first", "second"};
    const char *envp[] = {"TEST=pollikos"};
    Process64 *process;
    Launch64Result error = process64_launch_path("/bin/hello", 3, argv, 1, envp, &process);
    if (error != LAUNCH_OK) {
        memory_log("[LAUNCH64] /bin/hello: "); memory_log(launch64_error_name(error)); memory_log("\n");
        memory_require(pmm64_stats().free == baseline && !vfs_debug_handles() && !scheduler64_count(),
                       "demo launch failure cleanup");
        memory_log("[LAUNCH64] rejected image: PMM/handles/queue balanced\n");
        return 0;
    }
    demo_complete = 0;
    memory_require(process64_tick_limit(process->pid, 10) &&
                   scheduler64_run(20, 0, complete) && demo_complete && !scheduler64_count(),
                   "ELF demo execution");
    memory_log("[ELF64] real ELF demo exited 42; argv/envp/data/BSS verified in CPL3\n");
    memory_log("[LAUNCH64] /bin/hello VFS exit 42\n");
    return 1;
}
