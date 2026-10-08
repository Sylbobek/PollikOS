/* C4 SDK verification: official examples through the pollikcc workflow, C ABI
 * at -O2/-O0, fault containment, optimized preemption stress and 100 lifecycles. */
#include "launch.h"
#include "scheduler.h"
#include "file.h"
#include "fs_platform.h"
#include "paging.h"
#define check memory_require
static unsigned completions;
static int expected_status;
static void pass(const char *text) { memory_log("[C4] PASS: "); memory_log(text); memory_log("\n"); }
static void balance_and_check(page_count_t baseline) {
    check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
          "C4 PMM and handles return to baseline");
}
static void complete(const Process64 *p) {
    if (p->state != PROCESS_EXITED || p->exit_status != expected_status) {
        memory_log("[C4] user state="); memory_hex(p->state);
        memory_log(" status="); memory_hex((uint64_t)p->exit_status);
        memory_log(" vector="); memory_hex(p->thread.frame.vector);
        memory_log(" error="); memory_hex(p->thread.frame.error);
        memory_log("\n");
    }
    check(p->state == PROCESS_EXITED && p->exit_status == expected_status, "C4 exit status");
    check(!file64_count(p), "C4 descriptors closed");
    ++completions;
}
static Process64 *launch_process(const char *path, size_t argc, const char *const *argv,
                                 size_t envc, const char *const *envp) {
    Process64 *process;
    check(process64_launch_path(path, argc, argv, envc, envp, &process) == LAUNCH_OK && process,
          "C4 ELF launch by path");
    check(!file64_count(process) && file64_stream_count(process) == 3, "C4 launch ownership");
    check(process64_tick_limit(process->pid, 500), "C4 tick budget");
    return process;
}
static void run_expected(int status, Scheduler64Boundary boundary, Scheduler64Completion completion) {
    expected_status = status;
    check(scheduler64_run(600, boundary, completion) && !scheduler64_count(), "C4 scheduler completes");
    check(!vfs_debug_handles(), "C4 file handle balance");
}
static void launch_expected(const char *path, size_t argc, const char *const *argv,
                            size_t envc, const char *const *envp, int status) {
    launch_process(path, argc, argv, envc, envp);
    run_expected(status, 0, complete);
}
static unsigned fault_count, peer_count;
static uint64_t fault_low, fault_high;
static void fault_complete(const Process64 *p) {
    if (p->state == PROCESS_FAULTED) {
        if (p->thread.frame.vector != 14 || p->thread.frame.error != 6 ||
            p->fault_address < fault_low || p->fault_address >= fault_high) {
            memory_log("[C4] fault vector="); memory_hex(p->thread.frame.vector);
            memory_log(" error="); memory_hex(p->thread.frame.error);
            memory_log(" address="); memory_hex(p->fault_address); memory_log("\n");
        }
        check(p->thread.frame.vector == 14 && p->thread.frame.error == 6 &&
              p->fault_address >= fault_low && p->fault_address < fault_high,
              "C4 fault diagnostics");
        ++fault_count;
    } else {
        check(p->state == PROCESS_EXITED && p->exit_status == 42 && !file64_count(p),
              "C4 healthy peer exit 42");
        ++peer_count;
    }
    ++completions;
}
static unsigned stress_kills, stress_exits;
static void stress_complete(const Process64 *p) {
    if (p->state == PROCESS_KILLED) {
        check(p->exit_status == 124, "C4 spin kill reason");
        ++stress_kills;
    } else {
        check(p->state == PROCESS_EXITED && p->exit_status == 42 && !file64_count(p),
              "C4 stress peer exit");
        ++stress_exits;
    }
    ++completions;
}
void c4_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    const char *args_hello[] = {"sdk_hello"};
    const char *args_abi[] = {"abi_test"};
    completions = 0;
    launch_expected("/bin/sdk_hello", 1, args_hello, 0, 0, 0);
    pass("pollikcc-built hello runs from PollikFS, prints and exits 0");
    balance_and_check(baseline);
    launch_expected("/bin/sdk_files", 1, args_hello, 0, 0, 0);
    pass("SDK filesystem example: stat/open/read/fstat/seek/close");
    launch_expected("/bin/sdk_dirs", 1, args_hello, 0, 0, 0);
    pass("SDK directory example enumerates entries and closes the stream");
    launch_expected("/bin/sdk_memory", 1, args_hello, 0, 0, 0);
    pass("SDK memory example uses malloc/calloc/realloc/free");
    launch_expected("/bin/sdk_cwd", 1, args_hello, 0, 0, 0);
    pass("SDK cwd example uses getcwd/chdir and a relative path");
    launch_expected("/bin/sdk_time", 1, args_hello, 0, 0, 0);
    pass("SDK time example uses the monotonic clock and blocking sleep");
    launch_expected("/bin/sdk_errno", 1, args_hello, 0, 0, 0);
    pass("SDK errno example reports PollikOS libc errors");
    launch_expected("/bin/sdk_multifile", 1, args_hello, 0, 0, 0);
    pass("multi-file C application compiles and links through pollikcc");
    launch_expected("/bin/sdk_library", 1, args_hello, 0, 0, 0);
    pass("static user library builds and links through pollikcc -L/-l");
    balance_and_check(baseline);
    launch_expected("/bin/abi_test_o2", 1, args_abi, 0, 0, 42);
    launch_expected("/bin/abi_test_o0", 1, args_abi, 0, 0, 42);
    pass("C ABI, calling convention, stack alignment, globals, pointers at -O2 and -O0");
    balance_and_check(baseline);
    completions = 0; fault_count = 0; peer_count = 0;
    fault_low = USER_STACK_BASE; fault_high = USER_STACK_BASE+MM_PAGE_SIZE;
    launch_process("/bin/stack_guard_c", 1, args_hello, 0, 0);
    launch_process("/bin/abi_test_o2", 1, args_abi, 0, 0);
    check(scheduler64_run(200, 0, fault_complete) && !scheduler64_count() &&
          completions == 2 && fault_count == 1 && peer_count == 1, "C4 stack guard containment");
    balance_and_check(baseline);
    completions = 0; fault_count = 0; peer_count = 0;
    fault_low = 0; fault_high = MM_PAGE_SIZE;
    launch_process("/bin/null_fault_c", 1, args_hello, 0, 0);
    launch_process("/bin/abi_test_o2", 1, args_abi, 0, 0);
    check(scheduler64_run(200, 0, fault_complete) && !scheduler64_count() &&
          completions == 2 && fault_count == 1 && peer_count == 1, "C4 null fault containment");
    balance_and_check(baseline);
    pass("stack-guard and invalid-pointer faults contained; healthy C peer continues");
    completions = 0; stress_kills = 0; stress_exits = 0;
    Scheduler64Stats stats_before = scheduler64_stats();
    Process64 *spin_a = launch_process("/bin/spin_c", 1, args_hello, 0, 0);
    Process64 *spin_b = launch_process("/bin/spin_c", 1, args_hello, 0, 0);
    check(process64_tick_limit(spin_a->pid, 4) && process64_tick_limit(spin_b->pid, 4),
          "C4 spin tick budgets");
    launch_process("/bin/abi_test_o2", 1, args_abi, 0, 0);
    launch_process("/bin/runtime_test_c", 1, args_hello, 0, 0);
    check(scheduler64_run(300, 0, stress_complete) && !scheduler64_count() &&
          completions == 4 && stress_kills == 2 && stress_exits == 2, "C4 optimized stress");
    Scheduler64Stats stats_after = scheduler64_stats();
    check(stats_after.switches-stats_before.switches >= 4 &&
          stats_after.user_ticks-stats_before.user_ticks >= 2, "C4 optimized stress switches");
    balance_and_check(baseline);
    pass("optimized C binaries preempted; spin killed at tick limit, peers exit 42");
    unsigned before = completions;
    for (unsigned batch = 0; batch < 25; ++batch) {
        for (unsigned i = 0; i < 4; ++i) launch_process("/bin/runtime_test_c", 1, args_hello, 0, 0);
        run_expected(42, 0, complete);
        balance_and_check(baseline);
    }
    check(completions-before == 100, "100 SDK lifecycles");
    memory_log("[C4] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log(" descriptors=0\n");
    pass("100 SDK-built C process lifecycles return PMM exactly to baseline");
}
