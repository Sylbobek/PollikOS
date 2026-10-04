#include "file.h"
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#include "../../vfs.h"
#define check memory_require
static Process64 *peers[2];
static Process64 *bulk_peers[8];
static unsigned completed, cleanup_cases;
static int bulk_released;
static int isolation_verified, io_injected;
static void pass(const char *text) { memory_log("[FD64] PASS: "); memory_log(text); memory_log("\n"); }
static uint64_t get(const Process64 *p, unsigned offset) {
    uint64_t value;
    check(copy_from_user64(&p->space, &value, USER_DATA+offset, 8) == USER_COPY_OK, "file fixture status read");
    return value;
}
static void put(Process64 *p, unsigned offset, uint64_t value) {
    check(copy_to_user64(&p->space, USER_DATA+offset, &value, 8) == USER_COPY_OK, "file fixture control write");
}
static Process64 *launch(unsigned mode) {
    unsigned handles = vfs_debug_handles();
    Process64 *p;
    check(process64_launch_path("/bin/readtest", 0, 0, 0, 0, &p) == LAUNCH_OK && p,
          "file test launched from PollikFS path");
    check(!file64_count(p) && vfs_debug_handles() == handles, "executable handle stays outside user table");
    put(p, 0, mode);
    return p;
}
static void complete(const Process64 *p) {
    unsigned mode = (unsigned)get(p, 0);
    if (p->state == PROCESS_EXITED && p->exit_status != 42) {
        memory_log("[FD64] user check mode="); memory_hex(mode);
        memory_log(" line="); memory_hex(p->frame.r15);
        memory_log(" result="); memory_hex(get(p, 32)); memory_log("\n");
    }
    if (mode == 4) check(p->state == PROCESS_FAULTED && p->frame.vector == 14 && p->frame.error == 7,
                         "fault with open descriptors");
    else if (mode == 5) check(p->state == PROCESS_KILLED && p->exit_status == 124,
                              "kill with open descriptors");
    else check(p->state == PROCESS_EXITED && p->exit_status == 42, "userspace file assertions");
    if (mode >= 3 && mode <= 5) {
        check(file64_count(p) == USER_FD_LIMIT-3, "all owned files reach deferred reaper");
        ++cleanup_cases;
    } else if (mode == 9) {
        check(file64_count(p) == USER_FD_LIMIT-3, "concurrent full table reaches deferred reaper");
    } else check(!file64_count(p), "userspace explicitly closed all files");
    for (unsigned i = 0; i < 2; ++i) if (peers[i] == p) peers[i] = 0;
    ++completed;
}
static void bulk_boundary(void) {
    if (bulk_released) return;
    for (unsigned i = 0; i < sizeof(bulk_peers)/sizeof(bulk_peers[0]); ++i) {
        if (!bulk_peers[i] || get(bulk_peers[i], 8) != 1) return;
    }
    check(vfs_debug_handles() == (sizeof(bulk_peers)/sizeof(bulk_peers[0])) * (USER_FD_LIMIT-3),
          "global VFS pool holds eight simultaneous full descriptor tables");
    bulk_released = 1;
    for (unsigned i = 0; i < sizeof(bulk_peers)/sizeof(bulk_peers[0]); ++i)
        put(bulk_peers[i], 16, 1);
}
static void isolation_boundary(void) {
    Process64 *a = peers[0], *b = peers[1];
    if (a && b && !get(a, 16) && get(a, 8) == 1 && get(b, 8) == 1 && a->ticks >= 3 && b->ticks >= 3) {
        check(a->files[3] && b->files[3] && a->files[3] != b->files[3] &&
            a->files[3]->inode != b->files[3]->inode && a->files[3]->offset == 1 && b->files[3]->offset == 1,
            "fd 3 maps distinct files and offsets after preemption");
        check(get(a, 24) == 3 && get(b, 24) == 3, "same userspace descriptor number");
        put(a, 16, 1); /* A closes while B keeps its own fd 3 open. */
    }
    if (!isolation_verified && a && get(a, 8) == 2 && b && get(b, 8) == 1) {
        check(!file64_count(a) && file64_count(b) == 1 && b->files[3]->offset == 1,
              "peer close cannot close or seek another process descriptor");
        isolation_verified = 1;
        put(b, 16, 1);
        /* Launch while B's user handle remains open: this exercises the
         * independent kernel executable descriptor context. */
        if (get(a, 16) == 1) {
            launch(0);
            put(a, 16, 2);
        }
    }
}
static void io_boundary(void) {
    if (peers[0] && get(peers[0], 8) == 1 && !io_injected) {
        fs64_io_fail_after(0);
        io_injected = 1;
        put(peers[0], 16, 1);
    }
}
static void kill_boundary(void) {
    Process64 *p = peers[0];
    if (p && get(p, 8) == 1 && !p->tick_limit)
        check(process64_tick_limit(p->pid, p->ticks+3), "kill after all files are open");
}
static void run(Scheduler64Boundary boundary) {
    check(scheduler64_run(100, boundary, complete) && !scheduler64_count(), "file scheduler completes");
}
void file64_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    check(!vfs_debug_handles() && !scheduler64_count(), "file suite baseline");
    peers[0] = launch(1); peers[1] = launch(2);
    run(isolation_boundary);
    check(isolation_verified && completed == 3 && !vfs_debug_handles() && pmm64_stats().free == baseline,
          "concurrent descriptor isolation balance");
    pass("process-local fd 3, independent offsets, peer close and preemption");
    pass("kernel executable handles separated from userspace descriptors");

    for (unsigned repeat = 0; repeat < 20; ++repeat) {
        launch(6); run(0);
        check(!vfs_debug_handles() && pmm64_stats().free == baseline, "bad-argument and full-table churn balance");
    }
    pass("bad pointers/fds/flags, seek bounds, full table and descriptor reuse");
    for (unsigned repeat = 0; repeat < 5; ++repeat) {
        launch(7);
        vfs_test_fail_alloc(1);
        run(0);
        vfs_test_fail_alloc(0);
        check(!vfs_debug_handles() && pmm64_stats().free == baseline, "VFS object allocation rejection cleanup");
        peers[0] = launch(8);
        io_injected = 0;
        run(io_boundary);
        fs64_io_fail_after(-1);
        check(io_injected && !vfs_debug_handles() && pmm64_stats().free == baseline,
              "read I/O errors leave destination/offset and ownership intact");
    }
    pass("repeated VFS allocation/read failures preserve handles, offsets and buffers");
    /* Exercise terminal cleanup independently before the aggregate-pool case. */
    for (unsigned repeat = 0; repeat < 5; ++repeat) {
        launch(3); run(0);
        launch(4); run(0);
        peers[0] = launch(5);
        run(kill_boundary);
        check(!vfs_debug_handles() && pmm64_stats().free == baseline, "reaper closes exit/fault/kill files");
    }
    check(cleanup_cases == 15, "three terminal paths exercised repeatedly");
    pass("exit/fault/kill reclaim all owned descriptors in the safe reaper");
    for (unsigned i = 0; i < sizeof(bulk_peers)/sizeof(bulk_peers[0]); ++i)
        bulk_peers[i] = launch(9);
    check(scheduler64_run(1200, bulk_boundary, complete) && !scheduler64_count(),
          "aggregate VFS scheduler completes");
    check(!vfs_debug_handles() && pmm64_stats().free == baseline,
          "aggregate VFS capacity returns to baseline after concurrent teardown");
    pass("eight processes hold 1000 VFS objects concurrently and reclaim them cleanly");
    unsigned before = completed;
    for (unsigned cycle = 0; cycle < 25; ++cycle) {
        for (unsigned i = 0; i < 4; ++i) launch(0);
        run(0);
        check(!vfs_debug_handles() && pmm64_stats().free == baseline, "100 file lifecycles balance each batch");
    }
    check(completed-before == 100, "100 full file syscall lifecycles");
    memory_log("[FD64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log(" descriptors=0\n");
    pass("100 open/read/seek/close/exit lifecycles without PMM or descriptor leaks");
}
