#include "file.h"
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#include "stat_abi.h"
#include "../../vfs.h"
#define check memory_require
static Process64 *peers[2];
static unsigned completed, cleaned;
static int isolated, injected;
static uint64_t get(const Process64 *p, unsigned offset) {
    uint64_t value;
    check(copy_from_user64(&p->space, &value, USER_DATA+offset, 8) == USER_COPY_OK, "stat fixture status");
    return value;
}
static void put(Process64 *p, unsigned offset, uint64_t value) {
    check(copy_to_user64(&p->space, USER_DATA+offset, &value, 8) == USER_COPY_OK, "stat fixture control");
}
static Process64 *launch(unsigned mode) {
    Process64 *p;
    unsigned handles = vfs_debug_handles();
    check(process64_launch_path("/bin/stattest", 0, 0, 0, 0, &p) == LAUNCH_OK && p,
          "stat ELF launch by path");
    check(!file64_count(p) && vfs_debug_handles() == handles, "stat launch ownership");
    put(p, 0, mode);
    return p;
}
static void complete(const Process64 *p) {
    unsigned mode = (unsigned)get(p, 0);
    if (p->state == PROCESS_EXITED && p->exit_status != 42) {
        memory_log("[STAT64] user check mode="); memory_hex(mode);
        memory_log(" line="); memory_hex(p->thread.frame.r15);
        memory_log(" result="); memory_hex(get(p, 24)); memory_log("\n");
    }
    if (mode == 6) check(p->state == PROCESS_FAULTED && p->thread.frame.vector == 14 && p->thread.frame.error == 7,
                         "stat fault cleanup");
    else if (mode == 7) check(p->state == PROCESS_KILLED && p->exit_status == 124, "stat kill cleanup");
    else check(p->state == PROCESS_EXITED && p->exit_status == 42, "userspace metadata assertions");
    if (mode >= 5) { check(file64_count(p) == 1, "metadata handle reaches safe reaper"); ++cleaned; }
    else check(!file64_count(p), "metadata fixture closes its descriptor");
    for (unsigned i = 0; i < 2; ++i) if (peers[i] == p) peers[i] = 0;
    ++completed;
}
static void isolation_boundary(void) {
    Process64 *a = peers[0], *b = peers[1];
    if (a && b && !get(a, 16) && get(a, 8) == 1 && get(b, 8) == 1 && a->thread.ticks >= 3 && b->thread.ticks >= 3) {
        check(a->fds[3].file && b->fds[3].file && a->fds[3].file != b->fds[3].file &&
              a->fds[3].file->inode != b->fds[3].file->inode &&
              a->fds[3].file->offset == 5 && b->fds[3].file->offset == 5,
              "metadata peers own distinct fd 3 objects across preemption");
        put(a, 16, 1);
    }
    if (!isolated && a && b && get(a, 8) == 2 && get(b, 8) == 1) {
        check(!file64_count(a) && file64_count(b) == 1 && b->fds[3].file->offset == 5,
              "closed fd rejects peer ownership while stat remains independent");
        isolated = 1;
        put(a, 16, 2); put(b, 16, 2);
    }
}
static void io_boundary(void) {
    if (peers[0] && get(peers[0], 8) == 1 && !injected) {
        fs64_io_fail_after(0); injected = 1; put(peers[0], 16, 1);
    }
}
static void kill_boundary(void) {
    Process64 *p = peers[0];
    if (p && get(p, 8) == 1 && !p->thread.tick_limit)
        check(process64_tick_limit(p->pid, p->thread.ticks+3), "metadata kill after open and fstat");
}
static void run(Scheduler64Boundary boundary) {
    check(scheduler64_run(100, boundary, complete) && !scheduler64_count(), "stat scheduler completes");
}
static void balance(page_count_t baseline) {
    check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
          "stat PMM, descriptors and handles return to baseline");
}
void stat64_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    balance(baseline);
    peers[0] = launch(1); peers[1] = launch(2);
    run(isolation_boundary); balance(baseline);
    check(isolated && completed == 2, "concurrent metadata completion");
    memory_log("[STAT64] PASS: concurrent fd ownership, preemption and path independence\n");
    for (unsigned i = 0; i < 10; ++i) {
        launch(3); run(0); balance(baseline);
        peers[0] = launch(4); injected = 0; run(io_boundary); fs64_io_fail_after(-1);
        check(injected, "stat IO error coverage"); balance(baseline);
    }
    memory_log("[STAT64] PASS: bad paths, output pointers and descriptors; no partial output\n");
    memory_log("[STAT64] PASS: stat/fstat IO failures preserve output and file offset\n");
    for (unsigned i = 0; i < 5; ++i) {
        launch(5); launch(6); peers[0] = launch(7); run(kill_boundary); balance(baseline);
    }
    check(cleaned == 15, "stat exit fault kill repeated cleanup");
    memory_log("[STAT64] PASS: exit/fault/kill reclaim metadata process descriptors\n");
    unsigned before = completed;
    for (unsigned i = 0; i < 25; ++i) {
        for (unsigned j = 0; j < 4; ++j) launch(0);
        run(0); balance(baseline);
    }
    check(completed-before == 100, "100 stat lifecycles");
    memory_log("[STAT64] PASS: dedicated ABI, file/directory types, size, stored ticks and zero reserves\n");
    memory_log("[STAT64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log(" descriptors=0\n");
    memory_log("[STAT64] PASS: 100 stat/open/fstat/close/exit lifecycles without leaks\n");
}
