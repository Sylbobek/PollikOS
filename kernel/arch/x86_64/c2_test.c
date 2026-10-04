#include "heap.h"
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#define check memory_require
static Process64 *peers[2];
static unsigned completions;
static int isolated_checked, preempted, exec_faulted;
static uint64_t get(const Process64 *p, unsigned offset) {
    uint64_t value;
    check(copy_from_user64(&p->space, &value, USER_DATA+offset, 8) == USER_COPY_OK, "memtest status");
    return value;
}
static void put(Process64 *p, unsigned offset, uint64_t value) {
    check(copy_to_user64(&p->space, USER_DATA+offset, &value, 8) == USER_COPY_OK, "memtest control");
}
static Process64 *launch(unsigned mode) {
    Process64 *p;
    check(process64_launch_path("/bin/memtest", 0, 0, 0, 0, &p) == LAUNCH_OK && p,
          "memtest ELF launch by path");
    put(p, 0, mode);
    return p;
}
static void complete(const Process64 *p) {
    unsigned mode = (unsigned)get(p, 0);
    if (p->state != PROCESS_EXITED || p->exit_status != 42) {
        memory_log("[C2] user check mode="); memory_hex(mode);
        memory_log(" line="); memory_hex(p->frame.r15);
        memory_log(" result="); memory_hex(get(p, 24));
        memory_log(" state="); memory_hex(p->state);
        memory_log(" vector="); memory_hex(p->frame.vector);
        memory_log(" error="); memory_hex(p->frame.error);
        memory_log(" break="); memory_hex(p->heap_break); memory_log("\n");
    }
    check(p->state == PROCESS_EXITED && p->exit_status == 42, "memtest userspace assertions");
    for (unsigned i = 0; i < 2; ++i) if (peers[i] == p) peers[i] = 0;
    ++completions;
}
static void run(Scheduler64Boundary boundary) {
    check(scheduler64_run(800, boundary, complete) && !scheduler64_count(), "memory scheduler completes");
}
static void balance(page_count_t baseline) {
    check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
          "memory PMM and handles return to baseline");
}
static void pass(const char *text) { memory_log("[C2] PASS: "); memory_log(text); memory_log("\n"); }

static void fault_complete(const Process64 *p) {
    check(p->state == PROCESS_FAULTED && p->frame.vector == 14 && p->frame.error == 0x15 &&
          p->fault_address == USER_HEAP_BASE && p->exit_status == 128+14,
          "heap instruction fetch fault diagnostics");
    exec_faulted = 1;
}
/* Mode 9 asks the kernel to inject a global allocation failure at a defined
 * point, then restores normal allocation before the process tries again. */
static void inject_boundary(void) {
    Process64 *p = peers[0];
    if (!p) return;
    uint64_t phase = get(p, 8);
    if (phase == 1 && !get(p, 16)) { pmm64_fail_after(0); put(p, 16, 1); }
    else if (phase == 2 && get(p, 16) == 1) { pmm64_fail_after(-1); put(p, 16, 2); }
}
static void injection_checks(page_count_t baseline) {
    Process64 *p = launch(0);
    check(process64_block(p->pid), "quiesce process for injection");
    page_count_t before = pmm64_stats().free;
    /* The first four-page growth needs three tables and four frames: every
     * smaller budget must fail cleanly and roll back all allocations. */
    for (int64_t budget = 0; budget < 7; ++budget) {
        pmm64_fail_after(budget);
        int64_t result = heap64_brk(p, USER_HEAP_BASE+4*MM_PAGE_SIZE);
        pmm64_fail_after(-1);
        check(result == -USER_ENOMEM, "injected heap growth failure");
        check(heap64_brk(p, 0) == (int64_t)USER_HEAP_BASE, "failed growth preserves break");
        check(pmm64_stats().free == before, "heap growth rollback balance");
    }
    check(heap64_brk(p, USER_HEAP_BASE+4*MM_PAGE_SIZE) == (int64_t)(USER_HEAP_BASE+4*MM_PAGE_SIZE) &&
          pmm64_stats().free < before, "uninjected heap growth");
    check(heap64_brk(p, USER_HEAP_BASE) == (int64_t)USER_HEAP_BASE &&
          pmm64_stats().free == before, "heap shrink after injection balance");
    /* First anonymous mapping needs three tables and two frames. */
    for (int64_t budget = 0; budget < 4; ++budget) {
        pmm64_fail_after(budget);
        int64_t result = heap64_mmap(p, 2*MM_PAGE_SIZE, 0);
        pmm64_fail_after(-1);
        check(result == -USER_ENOMEM, "injected anonymous mapping failure");
        check(pmm64_stats().free == before, "anonymous mapping rollback balance");
    }
    int64_t mapping = heap64_mmap(p, 2*MM_PAGE_SIZE, 0);
    check(mapping == (int64_t)USER_MMAP_BASE && pmm64_stats().free < before,
          "uninjected anonymous mapping");
    check(heap64_munmap(p, (uint64_t)mapping, 2*MM_PAGE_SIZE) == 0 &&
          pmm64_stats().free == before, "anonymous mapping release balance");
    check(process64_wake(p->pid), "resume injected process");
    run(0);
    balance(baseline);
    pass("injected heap, page-table and anonymous mapping failures roll back without leaks");
    peers[0] = launch(9);
    check(process64_tick_limit(peers[0]->pid, 400), "injected syscall tick budget");
    check(scheduler64_run(500, inject_boundary, complete) && !scheduler64_count(),
          "injected userspace run");
    pmm64_fail_after(-1);
    balance(baseline);
}
static void isolation_boundary(void) {
    Process64 *a = peers[0], *b = peers[1];
    if (!a || !b) return;
    uint64_t pa = get(a, 8), pb = get(b, 8);
    if (pa >= 1 && pb >= 1 && !isolated_checked) {
        Mapping ma, mb;
        check(vmm64_lookup(&a->space, USER_HEAP_BASE, &ma) == VM_OK &&
              vmm64_lookup(&b->space, USER_HEAP_BASE, &mb) == VM_OK &&
              ma.physical != mb.physical && ma.owned && mb.owned &&
              (ma.flags & VM_USER) && (ma.flags & VM_WRITE) && !(ma.flags & VM_EXEC),
              "identical-VA heaps map distinct RW/NX frames");
        uint64_t va = 0, vb = 0;
        check(copy_from_user64(&a->space, &va, USER_HEAP_BASE, 8) == USER_COPY_OK &&
              copy_from_user64(&b->space, &vb, USER_HEAP_BASE, 8) == USER_COPY_OK &&
              va != vb && (uint32_t)(va>>32) == (uint32_t)a->pid &&
              (uint32_t)(vb>>32) == (uint32_t)b->pid, "identical-VA heap data isolation");
        isolated_checked = 1;
    }
    if (pa == 2 && pb == 2) { put(a, 16, 1); put(b, 16, 1); }
    if (get(a, 8) == 3 && get(b, 8) == 3) { put(a, 16, 2); put(b, 16, 2); }
    if (a->ticks >= 2 && b->ticks >= 2) preempted = 1;
}
void heap64_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    balance(baseline);
    launch(0);
    run(0);
    balance(baseline);
    pass("userspace brk grow/shrink/regrow, zeroed pages, mmap/munmap, getpid, clock, sleep");
    launch(1);
    run(0);
    balance(baseline);
    pass("test-only bump allocator: growth, unique patterns, no overlap, shrink and regrow");
    launch(2); launch(3);
    run(0);
    balance(baseline);
    pass("invalid brk/mmap/munmap requests fail safely and preserve break state");
    launch(4);
    check(scheduler64_run(100, 0, fault_complete) && exec_faulted && !scheduler64_count(),
          "heap execute fault contained");
    balance(baseline);
    pass("heap pages are RW/NX: executing from heap faults only the offending process");
    injection_checks(baseline);
    peers[0] = launch(6);
    peers[1] = launch(7);
    check(process64_tick_limit(peers[0]->pid, 400) && process64_tick_limit(peers[1]->pid, 400),
          "peer tick budgets");
    run(isolation_boundary);
    check(isolated_checked && preempted, "concurrent heap isolation across preemption");
    balance(baseline);
    pass("multi-process heap isolation at identical virtual addresses across preemption");
    unsigned before = completions;
    for (unsigned batch = 0; batch < 25; ++batch) {
        for (unsigned i = 0; i < 4; ++i) launch(0);
        run(0);
        balance(baseline);
    }
    check(completions-before == 100, "100 memory lifecycles");
    memory_log("[C2] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log(" descriptors=0\n");
    pass("100 memory lifecycles return PMM exactly to baseline");
}
