#include "scheduler.h"
#include "elf64.h"
#include "launch.h"
static Process64 *demo_processes[3];
static unsigned completed;
static void complete(const Process64 *p) {
    memory_require(p->state == PROCESS_KILLED && p->ticks == 6 && p->exit_status == 124,
                   "demo CPU budget termination");
    ++completed;
    for (size_t i = 0; i < 3; ++i) if (demo_processes[i] == p) demo_processes[i] = 0;
}
static void boundary(void) {
    /* Request another visible letter after each preemption. Not a yield. */
    uint64_t print = 1;
    for (size_t i = 0; i < 3; ++i) if (demo_processes[i])
        memory_require(copy_to_user64(&demo_processes[i]->space, USER_DATA+32, &print, 8) == USER_COPY_OK,
                       "request demo output");
}
void scheduler64_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    for (size_t i = 0; i < 3; ++i) {
        Process64 *p;
        Launch64Result error = process64_launch_path("/bin/spin", 0, 0, 0, 0, &p);
        if (error != LAUNCH_OK) {
            memory_log("[LAUNCH64] /bin/spin: "); memory_log(launch64_error_name(error)); memory_log("\n");
            for (size_t j = 0; j < i; ++j) process64_kill(demo_processes[j]->pid, 9);
            scheduler64_run(1, 0, 0);
            return;
        }
        uint64_t identity = 'A'+i;
        memory_require(p && error == LAUNCH_OK &&
            copy_to_user64(&p->space, USER_DATA+8, &identity, 8) == USER_COPY_OK &&
            process64_tick_limit(p->pid, 6), "schedule ELF demo");
        demo_processes[i] = p;
    }
    memory_log("[SCHED64] timer-driven ELF demo: ");
    memory_require(scheduler64_run(30, boundary, complete) && completed == 3 &&
        pmm64_stats().free == baseline, "scheduled demo cleanup");
    memory_log("\n[SCHED64] demo PASS: three infinite-loop ELFs preempted and killed\n");
}
