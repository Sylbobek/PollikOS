#include "launch.h"
#include "scheduler.h"
#include "file.h"
#include "fs_platform.h"
#include "paging.h"
#define check memory_require
static unsigned completions;
static int expected_status;
static Process64 *peers[4];
static int peers_checked, preempted;
static void pass(const char *text) { memory_log("[C3] PASS: "); memory_log(text); memory_log("\n"); }
static void balance_and_check(page_count_t baseline) {
    check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
          "C PMM and handles return to baseline");
}
static uint64_t get(const Process64 *p, virt_addr_t address) {
    uint64_t value = 0;
    check(copy_from_user64(&p->space, &value, address, 8) == USER_COPY_OK, "C test read");
    return value;
}
static void put(Process64 *p, virt_addr_t address, uint64_t value) {
    check(copy_to_user64(&p->space, address, &value, 8) == USER_COPY_OK, "C test write");
}
static void complete(const Process64 *p) {
    if (p->state != PROCESS_EXITED || p->exit_status != expected_status) {
        memory_log("[C3] user state="); memory_hex(p->state);
        memory_log(" status="); memory_hex((uint64_t)p->exit_status);
        memory_log(" vector="); memory_hex(p->frame.vector);
        memory_log(" error="); memory_hex(p->frame.error);
        memory_log("\n");
    }
    check(p->state == PROCESS_EXITED && p->exit_status == expected_status, "C exit status");
    check(!file64_count(p), "C program descriptors closed");
    for (unsigned i = 0; i < 4; ++i) if (peers[i] == p) peers[i] = 0;
    ++completions;
}
static unsigned crash_faults, crash_health;
static void mixed_complete(const Process64 *p) {
    if (p->state == PROCESS_FAULTED) {
        check(p->frame.vector == 14 && p->frame.error == 6, "C crash fault diagnostics");
        ++crash_faults;
    } else {
        check(p->state == PROCESS_EXITED && p->exit_status == 42 && !file64_count(p),
              "C healthy peer unaffected");
        ++crash_health;
    }
    ++completions;
}
static Process64 *launch_process(const char *path, size_t argc, const char *const *argv,
                                 size_t envc, const char *const *envp) {
    Process64 *p;
    check(process64_launch_path(path, argc, argv, envc, envp, &p) == LAUNCH_OK && p,
          "C ELF launch by path");
    check(!file64_count(p) && file64_stream_count(p) == 3, "C launch ownership");
    check(process64_tick_limit(p->pid, 500), "C tick budget");
    return p;
}
static void run_expected(int status, Scheduler64Boundary boundary, Scheduler64Completion completion) {
    expected_status = status;
    check(scheduler64_run(600, boundary, completion) && !scheduler64_count(), "C scheduler completes");
    check(!vfs_debug_handles(), "C file handle balance");
}
static void launch_expected(const char *path, size_t argc, const char *const *argv,
                            size_t envc, const char *const *envp, int status) {
    launch_process(path, argc, argv, envc, envp);
    run_expected(status, 0, complete);
}
static uint64_t injected_failures;
static void allocfail_boundary(void) {
    /* The fixture publishes handshake values in its first heap block payload at
     * USER_HEAP_BASE+64; the kernel toggles global allocation failure there. */
    for (unsigned i = 0; i < 4; ++i) {
        Process64 *p = peers[i];
        if (!p || p->heap_break <= USER_HEAP_BASE) continue;
        uint64_t cell = get(p, USER_HEAP_BASE+64);
        if (cell == 0x1111) { pmm64_fail_after(0); put(p, USER_HEAP_BASE+64, 0x2222); ++injected_failures; }
        else if (cell == 0x3333) { pmm64_fail_after(-1); put(p, USER_HEAP_BASE+64, 0x4444); }
    }
}
static void isolation_boundary(void) {
    Process64 *a = peers[0], *b = peers[1];
    if (!a || !b) return;
    if (!peers_checked && a->heap_break > USER_HEAP_BASE && b->heap_break > USER_HEAP_BASE) {
        Mapping ma, mb;
        check(vmm64_lookup(&a->space, USER_HEAP_BASE, &ma) == VM_OK &&
              vmm64_lookup(&b->space, USER_HEAP_BASE, &mb) == VM_OK &&
              ma.physical != mb.physical && ma.owned && mb.owned &&
              (ma.flags & VM_USER) && (ma.flags & VM_WRITE) && !(ma.flags & VM_EXEC),
              "C heap frames distinct RW/NX");
        uint64_t va = get(a, USER_HEAP_BASE+64), vb = get(b, USER_HEAP_BASE+64);
        if ((uint32_t)(va>>32) == (uint32_t)a->pid && (uint32_t)(vb>>32) == (uint32_t)b->pid &&
            (uint32_t)va < 4 && (uint32_t)vb < 4) peers_checked = 1;
    }
}
void c3_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    const char *hello_args[] = {"hello_c"};
    const char *alloc_args[] = {"allocator_test_c"};
    const char *argv_args[] = {"argv_c", "first", "second"};
    const char *test_env[] = {"TEST=pollikos", "OTHER=second"};
    const char *mode0[] = {"allocator_test_c", "return0"};
    const char *mode1[] = {"allocator_test_c", "exit0"};
    const char *mode2[] = {"allocator_test_c", "exit1"};
    const char *mode3[] = {"allocator_test_c", "exit255"};
    const char *modefail[] = {"allocator_test_c", "allocfail"};
    const char *modecrash[] = {"allocator_test_c", "crash"};
    completions = 0;
    launch_expected("/bin/hello_c", 1, hello_args, 0, 0, 42);
    launch_expected("/bin/allocator_test_c", 2, mode0, 0, 0, 0);
    launch_expected("/bin/allocator_test_c", 2, mode1, 0, 0, 0);
    launch_expected("/bin/allocator_test_c", 2, mode2, 0, 0, 1);
    launch_expected("/bin/allocator_test_c", 2, mode3, 0, 0, 255);
    check(completions == 5, "crt0 lifecycle count");
    pass("crt0 main(argc,argv), exit-status propagation and stdout");
    balance_and_check(baseline);
    launch_expected("/bin/argv_c", 3, argv_args, 2, test_env, 42);
    pass("argv/envp and environ from C");
    balance_and_check(baseline);
    launch_expected("/bin/allocator_test_c", 1, alloc_args, 0, 0, 42);
    pass("libc memory, string and formatter unit tests");
    pass("malloc/calloc/realloc/free reuse, coalescing, large and overflow paths");
    balance_and_check(baseline);
    peers[0] = launch_process("/bin/allocator_test_c", 2, modefail, 0, 0);
    run_expected(42, allocfail_boundary, complete);
    pmm64_fail_after(-1);
    check(injected_failures == 1, "injected allocation failure handshake");
    balance_and_check(baseline);
    pass("injected allocation failure returns NULL and recovers");
    launch_expected("/bin/filesystem_test_c", 1, hello_args, 0, 0, 42);
    pass("read-only filesystem, directory and cwd wrappers with errno paths");
    balance_and_check(baseline);
    unsigned before_crash = completions;
    peers[0] = launch_process("/bin/allocator_test_c", 2, modecrash, 0, 0);
    peers[1] = launch_process("/bin/hello_c", 1, hello_args, 0, 0);
    check(scheduler64_run(200, 0, mixed_complete) && !scheduler64_count() &&
          completions-before_crash == 2 && crash_faults == 1 && crash_health == 1,
          "C fault containment");
    balance_and_check(baseline);
    pass("faulting C process cannot damage a healthy peer");
    completions = 0;
    preempted = 0;
    for (unsigned i = 0; i < 4; ++i) peers[i] = launch_process("/bin/runtime_test_c", 1, hello_args, 0, 0);
    Scheduler64Stats stats_before = scheduler64_stats();
    run_expected(42, isolation_boundary, complete);
    Scheduler64Stats stats_after = scheduler64_stats();
    if (stats_after.switches-stats_before.switches >= 8 &&
        stats_after.user_ticks-stats_before.user_ticks >= 2) preempted = 1;
    if (completions != 4 || !peers_checked || !preempted) {
        memory_log("[C3] peers completions="); memory_hex(completions);
        memory_log(" isolated="); memory_hex(peers_checked);
        memory_log(" switches="); memory_hex(stats_after.switches-stats_before.switches);
        memory_log(" ticks="); memory_hex(stats_after.user_ticks-stats_before.user_ticks); memory_log("\n");
    }
    check(completions == 4 && peers_checked && preempted, "concurrent C memory isolation");
    balance_and_check(baseline);
    pass("four concurrent C processes: heap and errno isolation across preemption");
    unsigned before = completions;
    for (unsigned batch = 0; batch < 25; ++batch) {
        for (unsigned i = 0; i < 4; ++i) launch_process("/bin/runtime_test_c", 1, hello_args, 0, 0);
        run_expected(42, 0, complete);
        balance_and_check(baseline);
    }
    check(completions-before == 100, "100 C lifecycles");
    memory_log("[C3] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log(" descriptors=0\n");
    pass("100 C process lifecycles return PMM exactly to baseline");
}
