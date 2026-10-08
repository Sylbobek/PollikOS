#include "scheduler.h"
#include "elf64.h"
#include "test_memory.h"
#define check memory_require
#include "launch.h"
typedef struct {
    Process64 *p;
    uint64_t identity, minimum_ticks, vector, error;
    Process64State expected;
} Record;
static Record *records;
static unsigned completions, exits, faults, kills, created;
static uint64_t idle_start;
static void pass(const char *text) { memory_log("[SCHED64] PASS: "); memory_log(text); memory_log("\n"); }
static size_t bytes(void) { return schedule_elf_size; }
static Process64 *new_image(Elf64Result *error) {
    return process64_create_elf(schedule_elf_start, bytes(), 0, 0, 0, 0, error);
}
static void put(Process64 *p, unsigned offset, uint64_t value) {
    check(copy_to_user64(&p->space, USER_DATA+offset, &value, 8) == USER_COPY_OK, "scheduler test data write");
}
static void create(unsigned slot, uint64_t command, uint64_t limit) {
    check(!records[slot].p, "test slot empty");
    Elf64Result error;
    Process64 *p = new_image(&error);
    check(p && error == ELF64_OK, "scheduler ELF construction");
    check(p->thread.owner == p && p->thread.tid, "primary TCB owner and nonzero TID");
    check(p->state==PROCESS_ACTIVE && p->thread.state==THREAD_READY,
          "process lifetime independent of ready thread state");
    for (size_t i = 0; i < PROCESS_MAX; ++i)
        if (records[i].p) check(records[i].p->thread.tid != p->thread.tid, "live TID uniqueness");
    uint64_t identity = 'A'+slot;
    *(volatile uint64_t *)(p->thread.kernel_stack.base+4096) = UINT64_C(0xfeed000000000000)|identity;
    put(p, 0, command); put(p, 8, identity); put(p, 32, 0);
    records[slot] = (Record){p, identity, limit, 0, 0, PROCESS_KILLED};
    if (command == 1) records[slot].expected = PROCESS_EXITED;
    else if (command == 7) records[slot].expected = PROCESS_EXITED;
    else if (command >= 2) {
        records[slot].expected = PROCESS_FAULTED;
        records[slot].vector = command <= 4 ? 14 : command == 5 ? 13 : 6;
        records[slot].error = command == 2 ? 7 : command == 3 ? 21 : command == 4 ? 5 : 0;
    }
    check(scheduler64_submit(p) && !scheduler64_submit(p), "submit exactly once");
    check(!process64_destroy(p), "scheduler owns submitted process");
    if (limit) check(process64_tick_limit(p->pid, limit), "set CPU tick budget");
}
static void complete(const Process64 *p) {
    unsigned i = 0;
    while (i < PROCESS_MAX && records[i].p != p) ++i;
    check(i < PROCESS_MAX, "completion identity");
    Record *r = &records[i];
    check(*(volatile uint64_t *)(p->thread.kernel_stack.base+4096) == (UINT64_C(0xfeed000000000000)|r->identity),
          "per-process kernel stack canary");
    check(p->state == r->expected && !p->thread.queued && p->thread.ticks >= r->minimum_ticks,
          "scheduler terminal state and accounting");
    check(!process64_kill(p->pid, 9), "already terminal kill returns error");
    if (p->state == PROCESS_EXITED) { check(p->exit_status == 42, "resumed register/stack checks exit 42"); ++exits; }
    if (p->state == PROCESS_FAULTED) {
        check(p->thread.frame.vector == r->vector && p->thread.frame.error == r->error, "scheduled fault diagnostic"); ++faults;
    }
    if (p->state == PROCESS_KILLED) ++kills;
    uint64_t data[6];
    check(copy_from_user64(&p->space, data, USER_DATA, sizeof(data)) == USER_COPY_OK && data[1] == r->identity,
          "same-VA private data isolation");
    if (r->minimum_ticks > 3) {
        uint64_t cookie[2];
        check(data[2] > 100 && data[3] > 100 &&
            copy_from_user64(&p->space, cookie, data[5], sizeof(cookie)) == USER_COPY_OK &&
            cookie[0] == r->identity && cookie[1] == ~r->identity,
            "repeated register and stack verification");
    }
    records[i].p = 0;
    ++completions;
}
static void stress_boundary(void) {
    for (unsigned i = 0; i < 3; ++i) {
        Process64 *p = records[i].p;
        if (p && p->thread.ticks && p->thread.ticks % 100 == 0) put(p, 32, 1);
    }
}
static void churn_boundary(void) {
    for (unsigned i = 0; i < 3 && created < 100; ++i) if (!records[i].p) {
        unsigned command = created % 3;
        create(i, command, command ? 0 : 1);
        ++created;
    }
}
static void idle_boundary(void) {
    uint64_t elapsed = scheduler64_stats().ticks-idle_start;
    if (elapsed >= 3 && records[0].p && records[0].p->thread.state == THREAD_BLOCKED) {
        check(records[0].p->state==PROCESS_ACTIVE,"blocked thread preserves active process lifetime");
        check(records[0].p->thread.ticks == 0 && process64_wake(records[0].p->pid) &&
              !process64_wake(records[0].p->pid), "blocked wake exactly once");
    }
    if (elapsed >= 4 && records[1].p) check(process64_kill(records[1].p->pid, 9), "kill blocked process");
}
void scheduler64_selftest(void) {
    records = kernel_test_buffer_alloc(sizeof(*records)*PROCESS_MAX);
    check(records != 0, "scheduler records allocated after boot");
    page_count_t baseline = pmm64_stats().free;
    Scheduler64Stats before = scheduler64_stats();
    check(!process64_kill(UINT64_MAX, 9) && !process64_wake(UINT64_MAX) && !scheduler64_run(0, 0, 0),
          "invalid scheduler requests");
    for (unsigned i = 0; i < 3; ++i) create(i, 0, 401);
    for (unsigned i = 1; i < 3; ++i) {
        Mapping a, b;
        check(records[0].p->space.root != records[i].p->space.root &&
            records[0].p->thread.kernel_stack.top != records[i].p->thread.kernel_stack.top &&
            records[0].p->thread.user_stack.top == records[i].p->thread.user_stack.top &&
            vmm64_lookup(&records[0].p->space, USER_DATA, &a) == VM_OK &&
            vmm64_lookup(&records[i].p->space, USER_DATA, &b) == VM_OK && a.physical != b.physical,
            "separate CR3 data and kernel stacks");
        check(vmm64_lookup(&records[0].p->space, USER_STACK_BASE+4096, &a) == VM_OK &&
            vmm64_lookup(&records[i].p->space, USER_STACK_BASE+4096, &b) == VM_OK && a.physical != b.physical,
            "separate user stack frames at identical VA");
    }
    check(scheduler64_run(3, 0, complete) && scheduler64_count() == 3 && completions == 0,
          "bounded stop retains runnable processes");
    uint64_t stopped = scheduler64_stats().ticks;
    timer64_stop();
    check(scheduler64_stats().ticks == stopped, "timer masked deterministic inspection");
    check(scheduler64_run(1210, stress_boundary, complete) && scheduler64_count() == 0 && completions == 3 &&
          kills == 3 && pmm64_stats().free == baseline, "infinite loop termination and stress balance");
    Scheduler64Stats after = scheduler64_stats();
    check(after.switches-before.switches >= 1200 && after.user_ticks-before.user_ticks == 1203,
          "1200 process switches driven by CPL3 PIT interrupts");
    memory_log("\n[SCHED64] stress switches="); memory_hex(after.switches-before.switches);
    memory_log(" user ticks="); memory_hex(after.user_ticks-before.user_ticks); memory_log("\n");
    pass("1200 switches: GPR/RFLAGS, user/kernel stacks and CR3 isolation");
    pass("infinite-loop ELFs preempted; current-process kill and safe reaping");

    /* Every supported user fault is followed by a healthy ELF in the same run. */
    for (unsigned command = 2; command <= 7; ++command) {
        create(0, command, 0); create(1, 1, 0);
        check(scheduler64_run(10, 0, complete) && scheduler64_count() == 0 &&
            pmm64_stats().free == baseline, "fault and exit continue peers");
    }
    check(faults == 5 && exits == 7, "scheduled faults, FPU task and healthy peers");
    pass("exit, page/GP/UD faults and isolated x87/SSE2 state across sleep");

    create(0, 0, 0);
    uint64_t killed_pid = records[0].p->pid;
    check(process64_kill(killed_pid, 9) && !process64_kill(killed_pid, 9), "remove runnable kill once");
    check(scheduler64_run(1, 0, complete) && !process64_kill(killed_pid, 9) && !scheduler64_count(),
          "reaped PID never reused");
    create(0, 1, 0); create(1, 0, 0);
    check(process64_block(records[0].p->pid) && process64_block(records[1].p->pid) &&
        !process64_block(records[1].p->pid), "block queue removal");
    idle_start = scheduler64_stats().ticks;
    before = scheduler64_stats();
    check(scheduler64_run(12, idle_boundary, complete) && !scheduler64_count() &&
        scheduler64_stats().idle_ticks-before.idle_ticks >= 3, "HLT timer wake with no runnable user");
    pass("runnable/blocked kill, duplicate rejection, idle wake and timer stop");

    create(0, 1, 0); /* Existing runnable entry survives all failed constructions. */
    page_count_t with_peer = pmm64_stats().free;
    int success = 0;
    for (int64_t budget = 0; budget < 96; ++budget) {
        Elf64Result error;
        pmm64_fail_after(budget);
        Process64 *p = new_image(&error);
        pmm64_fail_after(-1);
        if (p) {
            scheduler64_fail_submit(1);
            check(!scheduler64_submit(p) && !p->thread.managed && !p->thread.queued && p->thread.state == THREAD_READY,
                  "injected insertion failure does not transfer ownership");
            scheduler64_fail_submit(0);
            check(process64_destroy(p), "caller cleans rejected process");
            success = 1;
        } else check(error == ELF64_NOMEM, "allocation prefix fails cleanly");
        check(scheduler64_count() == 1 && pmm64_stats().free == with_peer, "failed construction queue/PMM integrity");
        if (success) break;
    }
    check(success && scheduler64_run(10, 0, complete) && !scheduler64_count() &&
          pmm64_stats().free == baseline, "surviving queue after injection");
    /* Capacity is bounded by the control slots, not dynamic queue allocations. */
    size_t capacity = process64_capacity();
    for (size_t i = 0; i < capacity; ++i) create((unsigned)i, 1, 0);
    Elf64Result error;
    with_peer = pmm64_stats().free;
    check(!new_image(&error) && error == ELF64_NOMEM && pmm64_stats().free == with_peer &&
          scheduler64_count() == capacity, "full process capacity failure");
    /* Draining the full queue tears down large stacks; the tick budget must
     * cover the teardown work, not just process dispatch. */
    check(scheduler64_run(capacity+32, 0, complete) && !scheduler64_count(), "full queue drains");
    memory_log("[SCHED64] capacity="); memory_hex(capacity); memory_log("\n");
    pass("creation/stack/ELF allocation and insertion failure rollback");

    unsigned completed_before = completions;
    check(scheduler64_run(200, churn_boundary, complete) && created == 100 &&
        completions-completed_before == 100 && !scheduler64_count() && pmm64_stats().free == baseline,
        "100 mixed processes churn while scheduler is active");
    memory_log("[SCHED64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free); memory_log("\n");
    pass("100 mixed exit/fault/kill replacements without PMM leak");
    kernel_test_buffer_free(records);
    records = 0;
}
