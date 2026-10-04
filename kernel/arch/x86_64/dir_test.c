#include "file.h"
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#include "dir_abi.h"
#include "../../vfs.h"
#define check memory_require
static Process64 *peers[2];
static unsigned completed, cleaned;
static int isolated, injected;
static uint64_t get(const Process64 *p, unsigned offset) {
    uint64_t value;
    check(copy_from_user64(&p->space, &value, USER_DATA+offset, 8) == USER_COPY_OK, "directory fixture status");
    return value;
}
static void put(Process64 *p, unsigned offset, uint64_t value) {
    check(copy_to_user64(&p->space, USER_DATA+offset, &value, 8) == USER_COPY_OK, "directory fixture control");
}
static Process64 *launch(unsigned mode) {
    Process64 *p;
    unsigned handles = vfs_debug_handles();
    check(process64_launch_path("/bin/dirtest", 0, 0, 0, 0, &p) == LAUNCH_OK && p,
          "directory ELF launch by path");
    check(!file64_count(p) && vfs_debug_handles() == handles, "directory launch ownership");
    put(p, 0, mode);
    return p;
}
static void complete(const Process64 *p) {
    unsigned mode = (unsigned)get(p, 0);
    if (p->state == PROCESS_EXITED && p->exit_status != 42) {
        memory_log("[DIR64] user check mode="); memory_hex(mode);
        memory_log(" line="); memory_hex(p->frame.r15);
        memory_log(" result="); memory_hex(get(p, 24)); memory_log("\n");
    }
    if (mode == 6) check(p->state == PROCESS_FAULTED && p->frame.vector == 14 && p->frame.error == 7,
                         "stat fault cleanup");
    else if (mode == 7) check(p->state == PROCESS_KILLED && p->exit_status == 124, "stat kill cleanup");
    else check(p->state == PROCESS_EXITED && p->exit_status == 42, "userspace directory assertions");
    if (mode >= 5 && mode <= 7) { check(file64_count(p) == USER_FD_LIMIT-3, "directory handle reaches safe reaper"); ++cleaned; }
    else check(!file64_count(p), "directory fixture closes its descriptor");
    for (unsigned i = 0; i < 2; ++i) if (peers[i] == p) peers[i] = 0;
    ++completed;
}
static void isolation_boundary(void) {
    Process64 *a = peers[0], *b = peers[1];
    if (a && b && !get(a, 16) && get(a, 8) == 1 && get(b, 8) == 1 && a->ticks >= 3 && b->ticks >= 3) {
        check(a->files[3] && b->files[3] && a->files[3] != b->files[3] &&
              a->files[3]->inode == b->files[3]->inode &&
              a->files[3]->offset == 1 && b->files[3]->offset == 0,
              "same directory with independent enumeration positions after preemption");
        put(a, 16, 1); put(b, 16, 1);
    }
    if (!isolated && a && b && get(a, 8) == 2 && get(b, 8) == 2) {
        check(!file64_count(a) && file64_count(b) == 1 && b->files[3]->offset == 1,
              "closed directory fd cannot select peer handle");
        isolated = 1;
        put(a, 16, 2); put(b, 16, 2);
    }
}
static void io_boundary(void) {
    Process64 *p = peers[0];
    if (p && get(p, 8) == 1 && !injected) {
        fs64_io_fail_after(0); injected = 1; put(p, 16, 1);
    }
    if (p && get(p, 8) == 2 && injected == 1) {
        check(file64_count(p) == 1 && p->files[3]->offset == 0,
              "failed readdir does not advance iterator or leak handles");
        fs64_io_fail_after(-1); injected = 2; put(p, 16, 2);
    }
}
static void kill_boundary(void) {
    Process64 *p = peers[0];
    if (p && get(p, 8) == 1 && !p->tick_limit)
        check(process64_tick_limit(p->pid, p->ticks+3), "directory kill after open and fstat");
}
static void run(Scheduler64Boundary boundary) {
    check(scheduler64_run(100, boundary, complete) && !scheduler64_count(), "directory scheduler completes");
}
static void balance(page_count_t baseline) {
    check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
          "directory PMM, descriptors and handles return to baseline");
}
void dir64_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    balance(baseline);
    peers[0] = launch(1); peers[1] = launch(2);
    run(isolation_boundary); balance(baseline);
    check(isolated && completed == 2, "concurrent directory completion");
    memory_log("[DIR64] PASS: independent same-directory fd positions across preemption and peer close\n");
    for (unsigned i = 0; i < 10; ++i) {
        launch(3); run(0); balance(baseline);
        peers[0] = launch(4); injected = 0; run(io_boundary); fs64_io_fail_after(-1);
        check(injected == 2, "directory IO failure and recovery coverage"); balance(baseline);
    }
    for (unsigned i = 0; i < 5; ++i) {
        launch(8); vfs_test_fail_alloc(1); run(0); vfs_test_fail_alloc(0); balance(baseline);
    }
    memory_log("[DIR64] PASS: bad paths, pointers, descriptors, full table and slot reuse\n");
    memory_log("[DIR64] PASS: repeated allocation and IO failures preserve output, position and handles\n");
    /* Keep terminal cleanup isolated from the descriptor-capacity stress. */
    for (unsigned i = 0; i < 5; ++i) {
        launch(5); run(0);
        launch(6); run(0);
        peers[0] = launch(7); run(kill_boundary);
        balance(baseline);
    }
    check(cleaned == 15, "directory exit fault kill repeated cleanup");
    memory_log("[DIR64] PASS: exit/fault/kill reclaim all owned directory descriptors\n");
    unsigned before = completed;
    for (unsigned i = 0; i < 25; ++i) {
        for (unsigned j = 0; j < 4; ++j) launch(0);
        run(0); balance(baseline);
    }
    check(completed-before == 100, "100 directory lifecycles");
    memory_log("[DIR64] PASS: names, types, 55-byte limit, zero padding, empty EOF and rewind\n");
    memory_log("[DIR64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log(" descriptors=0\n");
    memory_log("[DIR64] PASS: 100 open/enumerate/close/exit lifecycles without leaks\n");
}
