#include "file.h"
#include "launch.h"
#include "scheduler.h"
#include "fs_platform.h"
#include "syscall.h"
#include "../../vfs.h"
#define check memory_require
static Process64 *peers[2];
static unsigned completed, cleaned;
static int isolated;
static uint64_t get(const Process64 *p, unsigned offset) {
    uint64_t value;
    check(copy_from_user64(&p->space, &value, USER_DATA+offset, 8) == USER_COPY_OK, "runtime fixture status");
    return value;
}
static void put(Process64 *p, unsigned offset, uint64_t value) {
    check(copy_to_user64(&p->space, USER_DATA+offset, &value, 8) == USER_COPY_OK, "runtime fixture control");
}
static Process64 *launch(unsigned mode) {
    Process64 *p;
    unsigned handles = vfs_debug_handles();
    check(process64_launch_path("/bin/runtime", 0, 0, 0, 0, &p) == LAUNCH_OK && p,
          "runtime ELF launch by path");
    check(!file64_count(p) && vfs_debug_handles() == handles, "runtime launch ownership");
    check(file64_stream_count(p) == 3 && p->cwd[0] == '/' && !p->cwd[1], "initial streams and cwd");
    put(p, 0, mode);
    return p;
}
static void complete(const Process64 *p) {
    unsigned mode = (unsigned)get(p, 0);
    if (p->state == PROCESS_EXITED && p->exit_status != 42) {
        memory_log("[C1] user check mode="); memory_hex(mode);
        memory_log(" line="); memory_hex(p->thread.frame.r15);
        memory_log(" result="); memory_hex(get(p, 24)); memory_log("\n");
    }
    if (mode == 5) check(p->state == PROCESS_FAULTED && p->thread.frame.vector == 14 && p->thread.frame.error == 7,
                         "SYSCALL process fault cleanup");
    else if (mode == 6) check(p->state == PROCESS_KILLED && p->exit_status == 124, "SYSCALL process kill cleanup");
    else if (mode >= 7) check(p->state == PROCESS_FAULTED && p->exit_status == 141 &&
                              p->thread.frame.vector == USER_SYSCALL_VECTOR, "unsafe actual SYSRET stack rejected");
    else check(p->state == PROCESS_EXITED && p->exit_status == 42, "userspace runtime assertions");
    if (mode >= 4 && mode <= 6) {
        check(file64_count(p) == USER_FD_LIMIT-3, "runtime handles reach safe reaper"); ++cleaned;
    } else check(!file64_count(p), "runtime fixture closes its files");
    if (mode <= 2) check(get(p, 32) >= 256, "many successful SYSCALL register checks");
    for (unsigned i = 0; i < 2; ++i) if (peers[i] == p) peers[i] = 0;
    ++completed;
}
static void isolation_boundary(void) {
    Process64 *a = peers[0], *b = peers[1];
    if (a && b && !get(a, 16) && get(a, 8) == 1 && get(b, 8) == 1 && a->thread.ticks >= 3 && b->thread.ticks >= 3) {
        check(a->fds[3].file && b->fds[3].file && a->fds[3].file != b->fds[3].file &&
              a->fds[3].file->inode != b->fds[3].file->inode &&
              a->cwd[1] == 'b' && b->cwd[1] == 'e' &&
              get(a, 32) >= 256 && get(b, 32) >= 256,
              "independent cwd and descriptors across repeated preempted SYSCALLs");
        put(a, 16, 1); put(b, 16, 1);
    }
    if (!isolated && a && b && get(a, 8) == 2 && get(b, 8) == 2) {
        check(!file64_count(a) && file64_count(b) == 1 &&
              file64_kind(a, 1) == FD64_CLOSED && file64_kind(b, 1) == FD64_STDOUT,
              "closing stdout and file cannot affect peer descriptors");
        isolated = 1; put(a, 16, 2); put(b, 16, 2);
    }
}
static void kill_boundary(void) {
    Process64 *p = peers[0];
    if (p && get(p, 8) == 1 && !p->thread.tick_limit)
        check(process64_tick_limit(p->pid, p->thread.ticks+3), "runtime kill after open and chdir");
}
static void return_checks(Process64 *p) {
    UserFrame f = p->thread.frame;
    const uint64_t invalid[] = {UINT64_C(0x800000000000), MM_KERNEL_START, USER_PRIVATE};
    for (unsigned i = 0; i < 3; ++i) {
        f = p->thread.frame; f.rip = invalid[i];
        check(!process64_test_return(p, &f), "reject noncanonical/kernel/unmapped SYSRET RIP");
        f = p->thread.frame; f.rsp = invalid[i];
        check(!process64_test_return(p, &f), "reject noncanonical/kernel/unmapped SYSRET RSP");
    }
    f = p->thread.frame; f.cs = 8;
    check(!process64_test_return(p, &f), "reject kernel return selector");
    f = p->thread.frame; f.flags = UINT64_MAX;
    check(process64_test_return(p, &f) && !(f.flags & ~UINT64_C(0xed7)) && (f.flags & 2),
          "clear unsafe SYSRET flags including TF IOPL NT RF VM AC");
}
static void run(Scheduler64Boundary boundary) {
    check(scheduler64_run(100, boundary, complete) && !scheduler64_count(), "runtime scheduler completes");
}
static void balance(page_count_t baseline) {
    check(!vfs_debug_handles() && !scheduler64_count() && pmm64_stats().free == baseline,
          "runtime PMM, descriptors and handles return to baseline");
}
void runtime64_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    balance(baseline);
    peers[0] = launch(1); peers[1] = launch(2);
    return_checks(peers[0]);
    run(isolation_boundary); balance(baseline);
    check(isolated && completed == 2, "concurrent runtime completion");
    memory_log("[C1] PASS: repeated SYSCALL registers, stacks, preemption, cwd and descriptor isolation\n");
    for (unsigned mode = 7; mode <= 9; ++mode) { launch(mode); run(0); balance(baseline); }
    memory_log("[C1] PASS: unsafe return RIP/RSP rejected and privileged flags sanitized\n");
    launch(3); run(0); balance(baseline);
    memory_log("[C1] PASS: stdio, chunked write, bad buffers/fds and closed streams\n");
    memory_log("[C1] PASS: bounded getcwd/chdir, relative paths, dot/dot-dot and failure preservation\n");
    for (unsigned i = 0; i < 5; ++i) {
        /* Keep terminal cleanup isolated from the descriptor-capacity stress. */
        launch(4); run(0);
    launch(5); run(0);
    peers[0] = launch(6); run(kill_boundary);
    balance(baseline);
    }
    check(cleaned == 15, "runtime exit fault kill cleanup");
    memory_log("[C1] PASS: exit/fault/kill reclaim files, streams and cwd state\n");
    unsigned before = completed;
    for (unsigned i = 0; i < 25; ++i) {
        for (unsigned j = 0; j < 4; ++j) launch(0);
        run(0); balance(baseline);
    }
    check(completed-before == 100, "100 runtime lifecycles");
    memory_log("[C1] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free);
    memory_log(" handles="); memory_hex(vfs_debug_handles()); memory_log(" descriptors=0\n");
    memory_log("[C1] PASS: 100 combined runtime lifecycles without leaks\n");
}
