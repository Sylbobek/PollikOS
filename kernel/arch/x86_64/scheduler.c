#include "scheduler.h"
#include "scheduler_internal.h"
#include "process_internal.h"
#include "paging.h"
#include "fs_platform.h"
#include "tty.h"
#include "network.h"
#include "../../hal.h"

extern void process64_enter(UserFrame *frame, uintptr_t *resume_stack);
extern void process64_leave(uintptr_t resume_stack) __attribute__((noreturn));

/* Runnable execution state is independent of process-slot allocation. */
static struct { Thread64 *active; uintptr_t resume_stack, previous_rsp0; } cpu;
static Thread64 *queue[PROCESS_MAX];
static size_t queue_count, queue_head;
static int scheduling;
static Scheduler64Stats accounting;
#ifdef SELFTEST
static int reject_submit;
void scheduler64_fail_submit(int fail) { memory_context_check(); reject_submit = fail; }
#endif

static void frame_copy(UserFrame *to, const UserFrame *from) {
    volatile uint8_t *destination = (volatile uint8_t *)to;
    const volatile uint8_t *source = (const volatile uint8_t *)from;
    for (size_t i = 0; i < sizeof(*to); ++i) destination[i] = source[i];
}

Process64 *scheduler64_current(void) { return cpu.active ? cpu.active->owner : 0; }
Thread64 *scheduler64_current_thread(void) { return cpu.active; }
int scheduler64_running(void) { return scheduling; }
uintptr_t *scheduler64_resume_stack(void) { return &cpu.resume_stack; }
size_t scheduler64_runnable_count(void) { return queue_count; }

void scheduler64_enqueue(Thread64 *t) {
    memory_require(t && t->owner && &t->owner->thread == t && t->managed && !t->stopped &&
                   !t->queued && t->state == THREAD_READY &&
                   queue_count < process64_capacity(), "runnable queue insertion");
    queue[(queue_head+queue_count)%PROCESS_MAX] = t;
    ++queue_count;
    t->queued = 1;
}
void scheduler64_dequeue(Thread64 *t) {
    if (!t || !t->queued) return;
    size_t i = 0;
    while (i < queue_count && queue[(queue_head+i)%PROCESS_MAX] != t) ++i;
    memory_require(i < queue_count, "queued process present");
    if (!i) {
        queue[queue_head] = 0;
        queue_head = (queue_head+1)%PROCESS_MAX;
    } else {
        for (size_t at = i; at+1 < queue_count; ++at)
            queue[(queue_head+at)%PROCESS_MAX] = queue[(queue_head+at+1)%PROCESS_MAX];
        queue[(queue_head+queue_count-1)%PROCESS_MAX] = 0;
    }
    --queue_count;
    t->queued = 0;
}
Thread64 *scheduler64_take_next(void) {
    if (!queue_count) return 0;
    Thread64 *t = queue[queue_head];
    scheduler64_dequeue(t);
    return t;
}
int scheduler64_context_begin(Process64 *p) {
    if (!p || p->thread.owner != p || cpu.active || scheduling) return 0;
    cpu.active = &p->thread;
    cpu.previous_rsp0 = kernel64_set_rsp0(p->thread.kernel_stack.top);
    return 1;
}
void scheduler64_leave_current(int switch_to_kernel_cr3) {
    if (switch_to_kernel_cr3)
        memory_require(vmm64_switch(vmm64_kernel()) == VM_OK, "process CR3 exit");
    kernel64_set_rsp0(cpu.previous_rsp0);
    cpu.active = 0;
    process64_leave(cpu.resume_stack);
}

static void wake_ready(Process64 *p) {
    process64_internal_transition(p, THREAD_READY);
    if (!p->thread.stopped) scheduler64_enqueue(&p->thread);
}
void scheduler64_park(Thread64 *thread, UserFrame *frame) {
    Process64 *process = thread ? thread->owner : 0;
    memory_require(process && &process->thread == thread, "park owned thread");
    frame_copy(&process->thread.frame, frame);
    if (process->thread.kill_pending) {
        process64_internal_transition(process, THREAD_KILLED);
        process64_internal_wake_waiter(process);
    } else if (process->thread.stop_pending) {
        process->thread.stop_pending = 0;
        process->thread.stopped = 1;
        process64_internal_transition(process, THREAD_READY);
    } else if (!process64_internal_signal_prepare(process, frame)) {
        process64_internal_transition(process, THREAD_FAULTED);
        process->exit_status = 142;
        process->fault_address = frame->rsp;
        process64_internal_wake_waiter(process);
    } else if (process->thread.kill_pending) {
        process64_internal_transition(process, THREAD_KILLED);
        process64_internal_wake_waiter(process);
    } else {
        frame_copy(&process->thread.frame, frame);
        wake_ready(process);
    }
    scheduler64_leave_current(0);
}
void scheduler64_suspend(Thread64 *thread, UserFrame *frame) {
    Process64 *process = thread ? thread->owner : 0;
    memory_require(process && &process->thread == thread, "suspend owned thread");
    frame_copy(&process->thread.frame, frame);
    process64_internal_transition(process, THREAD_BLOCKED);
    scheduler64_leave_current(0);
}

int scheduler64_submit(Process64 *p) {
    memory_context_check();
    if (!process64_internal_known(p) || p->thread.managed || p->thread.queued || p->thread.state != THREAD_READY ||
        queue_count == process64_capacity()) return 0;
#ifdef SELFTEST
    if (reject_submit) return 0;
#endif
    p->thread.managed = 1;
    scheduler64_enqueue(&p->thread);
    return 1;
}
int process64_kill(uint64_t pid, int reason) {
    memory_context_check();
    Process64 *p = process64_internal_find(pid);
    if (!p || process64_internal_dead(p)) return 0;
    p->exit_status = reason;
    if (p->thread.owner == p && &p->thread == cpu.active)
        p->thread.kill_pending = 1; /* consumed at a safe IRQ/syscall boundary */
    else {
        scheduler64_dequeue(&p->thread);
        process64_internal_transition(p, THREAD_KILLED);
        process64_internal_wake_waiter(p);
    }
    return 1;
}
int process64_block(uint64_t pid) {
    memory_context_check();
    Process64 *p = process64_internal_find(pid);
    if (!p || p->thread.stopped || p->thread.state != THREAD_READY) return 0;
    scheduler64_dequeue(&p->thread);
    process64_internal_transition(p, THREAD_BLOCKED);
    return 1;
}
int process64_wake(uint64_t pid) {
    memory_context_check();
    Process64 *p = process64_internal_find(pid);
    if (!p || p->thread.state != THREAD_BLOCKED) return 0;
    p->thread.wake_tick = 0;
    wake_ready(p);
    return 1;
}
void process64_wake_expired(void) {
    memory_context_check();
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *p = process64_internal_slot(i);
        if (!p || !p->thread.managed || p->thread.state != THREAD_BLOCKED || !p->thread.wake_tick ||
            accounting.ticks < p->thread.wake_tick) continue;
        p->thread.wake_tick = 0;
        wake_ready(p);
    }
}
uint64_t scheduler64_ticks(void) { memory_context_check(); return accounting.ticks; }
int process64_tick_limit(uint64_t pid, uint64_t ticks) {
    memory_context_check();
    Process64 *p = process64_internal_find(pid);
    if (!p || process64_internal_dead(p) || (ticks && ticks <= p->thread.ticks)) return 0;
    p->thread.tick_limit = ticks;
    return 1;
}
Scheduler64Stats scheduler64_stats(void) { memory_context_check(); return accounting; }
size_t scheduler64_count(void) {
    memory_context_check();
    size_t count = 0;
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *p = process64_internal_slot(i);
        if (p && p->thread.managed) ++count;
    }
    return count;
}
void scheduler64_timer(UserFrame *frame) {
    timer64_ack(); /* exactly once, before abandoning the interrupt stack */
    ++accounting.ticks;
    tty64_poll(); /* keep the kernel-owned login prompt live before scheduling */
    if (!scheduling) return;
    fs64_tick_update((u32)accounting.ticks);
    network64_poll();
    process64_internal_tty_wake_readers();
    process64_wake_expired();
    if ((frame->cs & 3) != 3) {
        memory_require(!cpu.active, "timer in non-preemptible kernel");
        ++accounting.idle_ticks;
        return;
    }
    Thread64 *thread = cpu.active;
    Process64 *p = thread ? thread->owner : 0;
    memory_require(p && &p->thread == thread && thread->managed && thread->state == THREAD_RUNNING &&
        kernel64_get_rsp0() == thread->kernel_stack.top &&
        (uintptr_t)frame >= p->thread.kernel_stack.base+4096 &&
        (uintptr_t)(frame+1) <= thread->kernel_stack.top, "timer uses current thread RSP0");
    fpu64_state_save(thread->fpu_state);
    ++thread->ticks; ++accounting.user_ticks;
    if (thread->tick_limit && thread->ticks >= thread->tick_limit)
        memory_require(process64_kill(p->pid, 124), "kill running process at CPU tick limit");
    scheduler64_park(thread, frame);
}

static void reap(Scheduler64Completion completion) {
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *p = process64_internal_slot(i);
        if (!p || !p->thread.managed || !process64_internal_dead(p)) continue;
        memory_require(!cpu.active && !p->thread.queued, "deferred reaper ownership");
        uintptr_t rsp;
        __asm__ volatile("mov %%rsp,%0" : "=r"(rsp));
        memory_require(rsp < p->thread.kernel_stack.base || rsp >= p->thread.kernel_stack.top,
                       "reaper outside victim stack");
        memory_require(vmm64_switch(vmm64_kernel()) == VM_OK, "reaper kernel CR3");
        if (completion) completion(p);
        p->thread.managed = 0;
        if (p->wait_collected || !p->parent_pid || !process64_internal_parent_live(p->parent_pid)) {
            memory_require(process64_destroy(p), "scheduler process reclamation");
        } else {
            memory_require(process64_reclaim(p), "scheduler zombie reclamation");
            p->zombie = 1;
        }
        ++accounting.reaped;
    }
}
int scheduler64_run(uint64_t maximum_ticks, Scheduler64Boundary boundary, Scheduler64Completion completion) {
    memory_context_check();
    if (scheduling || cpu.active || !maximum_ticks) return 0;
    scheduling = 1;
    uint64_t start = accounting.ticks, last_pid = 0;
    cpu.previous_rsp0 = kernel64_get_rsp0();
    timer64_start();
    for (;;) {
        reap(completion);
        if (boundary) boundary();
        reap(completion);
        if (accounting.ticks-start >= maximum_ticks || !scheduler64_count()) break;
        if(security_session_state()==SESSION_LOCKED || security_session_state()==SESSION_LOGOUT || security_session_state()==SESSION_PASSWORD || security_session_state()==SESSION_ELEVATE || security_session_state()==SESSION_FACTORY_RESET) break;
        if (!queue_count) {
            hal_cpu_idle_once_disabled(); /* atomic enable+halt avoids lost wakeups */
            continue;
        }
        Thread64 *thread = scheduler64_take_next();
        if (!thread || !thread->owner) continue;
        Process64 *p = thread->owner;
        if(!security_credentials_live(&p->credentials)) { (void)process64_kill(p->pid,0); continue; }
        if (!process64_internal_signal_prepare(p, &p->thread.frame)) {
            process64_internal_transition(p, THREAD_FAULTED);
            p->exit_status = 142; p->fault_address = p->thread.frame.rsp;
            continue;
        }
        if (p->thread.state != THREAD_READY) continue;
        if (!process64_internal_return_valid(p, &p->thread.frame)) {
            process64_internal_transition(p, THREAD_FAULTED);
            p->exit_status = 141; p->thread.frame.vector = 13;
            continue;
        }
        process64_internal_transition(p, THREAD_RUNNING);
        cpu.active = thread;
        kernel64_set_rsp0(thread->kernel_stack.top);
        memory_require(vmm64_switch(&p->space) == VM_OK, "scheduler CR3 switch");
        if (last_pid && last_pid != p->pid) ++accounting.switches;
        last_pid = p->pid;
        ++accounting.dispatches; ++p->thread.dispatches;
        fpu64_state_restore(thread->fpu_state);
        process64_enter(&thread->frame, &cpu.resume_stack);
        memory_require(!cpu.active, "scheduler resumed on dispatcher stack");
    }
    timer64_stop();
    scheduling = 0;
    memory_require(vmm64_switch(vmm64_kernel()) == VM_OK, "scheduler stopped kernel CR3");
    kernel64_set_rsp0(cpu.previous_rsp0);
    return 1;
}
