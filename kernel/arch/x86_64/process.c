#include "user.h"
#include "elf64.h"
#include "paging.h"
#include "scheduler.h"
#include "scheduler_internal.h"
#include "process_internal.h"
#include "file.h"
#include "heap.h"
#include "window.h"
#include "launch.h"
#include "path.h"
#include "fs_platform.h"
#include "tty.h"
#include "pipe.h"
#include "network.h"
#include "devices.h"
#include "audio_stream.h"
#include "../../vfs.h"
#include "../../hal.h"
#include "../../../include/pollikos_abi.h"
extern const uint8_t user_payload_start[], user_payload_end[];
extern void process64_enter(UserFrame *frame, uintptr_t *resume_stack);
extern void kernel64_debug_bytes(const char *data, size_t length);
static Process64 *slots[PROCESS_MAX];
static uint64_t next_pid = 1;
static uint64_t next_tid = 1;
#define PROCESS_CONTROL_PAGES 2
/* Larger machines can keep more full process contexts resident. This is an
 * upper bound only; actual creation can still fail naturally under pressure. */
size_t process64_capacity(void) {
    page_count_t managed = pmm64_stats().managed;
    if (managed < (page_count_t)96 * 256) return 32;       /* below 96 MiB */
    if (managed < (page_count_t)1024 * 256) return 64;    /* below 1 GiB */
    if (managed < (page_count_t)8192 * 256) return 128;   /* below 8 GiB */
    if (managed < (page_count_t)16384 * 256) return 256;  /* below 16 GiB */
    if (managed < (page_count_t)28672 * 256) return 512;  /* below 28 GiB */
    return PROCESS_MAX;
}
int process64_internal_dead(const Process64 *p) {
    return p->state == PROCESS_EXITED || p->state == PROCESS_FAULTED || p->state == PROCESS_KILLED;
}
static Process64 *find_slot(uint64_t pid) {
    for (size_t i = 0; i < process64_capacity(); ++i)
        if (slots[i] && slots[i]->pid == pid) return slots[i];
    return 0;
}
Process64 *process64_internal_find(uint64_t pid);
int process64_internal_signal_prepare(Process64 *process, UserFrame *frame);
static int64_t signal_action_request(Process64 *process, UserFrame *frame);
static int64_t signal_mask_request(Process64 *process, UserFrame *frame);
static int64_t signal_pending_request(Process64 *process, UserFrame *frame);
static int64_t signal_return_request(Process64 *process, UserFrame *frame);
int process64_internal_parent_live(uint64_t pid) {
    for (size_t i = 0; i < process64_capacity(); ++i)
        if (slots[i] && slots[i]->pid == pid && !process64_internal_dead(slots[i])) return 1;
    return 0;
}
void process64_internal_transition(Process64 *p, Process64State next) {
    Process64State old = p->state;
    memory_require((old == PROCESS_BUILDING && next == PROCESS_READY) ||
        (old == PROCESS_READY && (next == PROCESS_RUNNING || next == PROCESS_BLOCKED ||
                                 next == PROCESS_KILLED || next == PROCESS_FAULTED)) ||
        (old == PROCESS_RUNNING && (next == PROCESS_READY || next == PROCESS_BLOCKED ||
                                   next == PROCESS_EXITED || next == PROCESS_FAULTED ||
                                   next == PROCESS_KILLED)) ||
        (old == PROCESS_BLOCKED && (next == PROCESS_READY || next == PROCESS_KILLED)),
        "legal process state transition");
    p->state = next;
    /* At this checkpoint a process owns exactly one embedded TCB, so the
     * scheduler queue and the process record must never disagree: the
     * dispatcher checks thread->state, syscalls check process->state. */
    p->thread.state = next;
}
static void wake_ready(Process64 *p) {
    process64_internal_transition(p, PROCESS_READY);
    if (!p->stopped) scheduler64_enqueue(&p->thread);
}
static UserWait64 wait_status(const Process64 *child) {
    UserWait64 status = {USER_WAIT_VERSION, 0, 0, 0, 0};
    if (child->state == PROCESS_FAULTED) {
        status.kind = USER_WAIT_KIND_FAULTED;
        status.code = (int32_t)child->frame.vector;
        status.address = child->fault_address;
    } else if (child->state == PROCESS_KILLED) {
        status.kind = USER_WAIT_KIND_KILLED;
        status.code = child->exit_status;
    } else {
        status.kind = USER_WAIT_KIND_EXITED;
        status.code = child->exit_status;
    }
    return status;
}
/* Finish a blocked wait from the terminating child's context: deliver the
 * versioned status, return the child PID through the saved frame and wake the
 * parent. The reaper then destroys the collected child immediately. */
static void complete_wait(Process64 *parent, Process64 *child) {
    UserWait64 status = wait_status(child);
    if (parent->wait_status_va)
        memory_require(copy_to_user64(&parent->space, parent->wait_status_va, &status, sizeof(status)) == USER_COPY_OK,
                       "wait status delivery");
    parent->frame.rax = child->pid;
    parent->waiting = 0;
    parent->wait_pid = 0;
    parent->wait_status_va = 0;
    child->wait_collected = 1;
    wake_ready(parent);
}
static int wait_matches(const Process64 *parent, const Process64 *child, int64_t selector);
void process64_internal_wake_waiter(Process64 *child) {
    if (!child->parent_pid || child->wait_collected) return;
    Process64 *parent = find_slot(child->parent_pid);
    if (!parent || process64_internal_dead(parent)) return;
    (void)process64_signal_send(parent->pid, USER_SIG_CHLD);
    if (!parent->waiting) return;
    if (!wait_matches(parent, child, (int64_t)parent->wait_pid)) return;
    complete_wait(parent, child);
}
_Static_assert(sizeof(Process64) <= PROCESS_CONTROL_PAGES*MM_PAGE_SIZE,
               "two-page process control record");
static void frame_copy(UserFrame *to, const UserFrame *from) {
    volatile uint8_t *destination = (volatile uint8_t *)to;
    const volatile uint8_t *source = (const volatile uint8_t *)from;
    for (size_t i = 0; i < sizeof(*to); ++i) destination[i] = source[i];
}
int process64_internal_known(const Process64 *process) {
    for (size_t i = 0; i < process64_capacity(); ++i) if (slots[i] == process && process) return 1;
    return 0;
}
Process64 *process64_internal_slot(size_t index) {
    return index < process64_capacity() ? slots[index] : 0;
}
int process64_slots_full(void) {
    memory_context_check();
    for (size_t i = 0; i < process64_capacity(); ++i) if (!slots[i]) return 0;
    return 1;
}
static int64_t sleep_request(Process64 *process, uint64_t milliseconds) {
    if (!scheduler64_running()) return -USER_ENOTSUP; /* synchronous regression runs have no timer */
    if (milliseconds > USER_SLEEP_MAX_MS) return -USER_E2BIG;
    if (!milliseconds) return 0;
    uint64_t quantum = 1000/TIMER64_HZ;
    process->wake_tick = scheduler64_ticks()+(milliseconds+quantum-1)/quantum;
    return 1;
}
/* When a process disappears, its children become kernel-owned (parent 0) and
 * auto-reap on termination; already-collected zombie children are destroyed. */
static void orphan_children(uint64_t pid) {
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *child = slots[i];
        if (!child || child->parent_pid != pid) continue;
        if (child->zombie) {
            child->managed = 0;
            child->queued = 0;
            memory_require(process64_destroy(child), "orphan zombie reclamation");
        } else {
            child->parent_pid = 0;
        }
    }
}
/* Prompt heavy-resource reclamation for a dead child that a parent may still
 * wait for: address space, user stack and kernel stack go away, only the
 * lightweight control record (PID, parent, terminal state, status)
 * remains until wait collects it. */
int process64_reclaim(Process64 *process) {
    memory_context_check();
    if (!process64_internal_known(process)) return 0;
    network64_http_owner_cleanup(process->pid);
    audio64_stream_cleanup(process->pid);
    window64_process_cleanup(process);
    file64_cleanup(process);
    if (process->space.root)
        memory_require(vmm64_destroy(&process->space) == VM_OK, "zombie address space teardown");
    if (process->kernel_stack.pages)
        memory_require(vmm64_stack_destroy(vmm64_kernel(), &process->kernel_stack) == VM_OK,
                       "zombie kernel stack teardown");
    process->user_stack = (GuardedStack){0};
    process->io_waiting = 0;
    return 1;
}
int process64_destroy(Process64 *process) {
    memory_context_check();
    if (!process64_internal_known(process) || process == scheduler64_current() || process->managed || process->queued) return 0;
    size_t slot = process->slot;
    process->io_waiting = 0;
    window64_process_cleanup(process);
    orphan_children(process->pid);
    file64_cleanup(process);
    pipe64_wake_blocked(); /* closed pipe ends may unblock other processes */
    if (process->space.root)
        memory_require(vmm64_destroy(&process->space) == VM_OK, "process address space teardown");
    if (process->kernel_stack.pages)
        memory_require(vmm64_stack_destroy(vmm64_kernel(), &process->kernel_stack) == VM_OK,
                       "process kernel stack teardown");
    slots[slot] = 0;
    for (size_t page = 0; page < PROCESS_CONTROL_PAGES; ++page)
        memory_require(vmm64_unmap(vmm64_kernel(), PROCESS_KERNEL_BASE+slot*PROCESS_SLOT_SIZE+
                                   page*MM_PAGE_SIZE, 1, 0) == VM_OK,
                       "process control record teardown");
    return 1;
}
static Process64 *process64_prepare(void) {
    memory_context_check();
    if (!next_pid || !next_tid) return 0;
    size_t slot = 0, capacity = process64_capacity();
    while (slot < capacity && slots[slot]) ++slot;
    if (slot == capacity) return 0;
    virt_addr_t control = PROCESS_KERNEL_BASE+slot*PROCESS_SLOT_SIZE;
    for (size_t page = 0; page < PROCESS_CONTROL_PAGES; ++page) {
        if (vmm64_alloc_page(vmm64_kernel(), control+page*MM_PAGE_SIZE, VM_WRITE) == VM_OK) continue;
        while (page)
            memory_require(vmm64_unmap(vmm64_kernel(), control+(--page)*MM_PAGE_SIZE, 1, 0) == VM_OK,
                           "partial process control allocation rollback");
        return 0;
    }
    Process64 *process = (Process64 *)control; /* explicitly allocated kernel mapping */
    slots[slot] = process;
    process->thread.tid = next_tid++;
    process->thread.owner = process;
    process->slot = slot;
    process->state = PROCESS_BUILDING;
    if (vmm64_create(&process->space) != VM_OK ||
        vmm64_stack_create(vmm64_kernel(), control+PROCESS_CONTROL_PAGES*MM_PAGE_SIZE, 4, 1, 0,
                           &process->kernel_stack) != VM_OK ||
        vmm64_stack_create(&process->space, USER_STACK_BASE, USER_STACK_PAGES, USER_STACK_GUARDS, 1,
                           &process->user_stack) != VM_OK) goto failure;
    process->pid = next_pid++;
    process->pgid = process->pid;
    process->parent_pid = 0;
    process->zombie = 0;
    process->waiting = 0;
    process->wait_collected = 0;
    process->wait_pid = 0;
    process->wait_status_va = 0;
    process->io_waiting = 0;
    process->io_kind = 0;
    process->io_fd = 0;
    process->io_va = 0;
    process->io_count = 0;
    if (!fpu64_state_init(process->fpu_state)) goto failure;
    file64_init(process);
    heap64_reset(process);
    process->frame.rsp = process->user_stack.top;
    process->frame.cs = USER_CS;
    process->frame.ss = USER_SS;
    process->frame.flags = 2;
    return process;
failure:
    memory_require(process64_destroy(process), "partial process teardown");
    return 0;
}
Process64 *process64_create(unsigned payload_mode, uint64_t private_value) {
    Process64 *process = process64_prepare();
    if (!process) return 0;
    if (vmm64_alloc_page(&process->space, USER_CODE, VM_USER|VM_EXEC) != VM_OK ||
        vmm64_alloc_page(&process->space, USER_DATA, VM_USER|VM_WRITE) != VM_OK) goto failure;
    size_t bytes = (uintptr_t)user_payload_end-(uintptr_t)user_payload_start;
    memory_require(bytes && bytes <= MM_PAGE_SIZE, "embedded payload bounds");
    Mapping code;
    memory_require(vmm64_lookup(&process->space, USER_CODE, &code) == VM_OK, "payload mapping");
    volatile uint8_t *target = (volatile uint8_t *)physical_view(code.physical);
    /* Trusted embedded payload installation, not a user copy or ELF loader.
     * Code has never been user-writable and stays RX in its user mapping. */
    for (size_t i = 0; i < bytes; ++i) target[i] = user_payload_start[i];
    memory_require(copy_to_user64(&process->space, USER_DATA, &private_value, sizeof(private_value)) == USER_COPY_OK,
                   "initial process data");
    process->frame.rip = USER_CODE;
    process->frame.rsp = process->user_stack.top;
    process->frame.cs = USER_CS;
    process->frame.ss = USER_SS;
    process->frame.flags = 2; /* IF/IOPL/TF/NT/VM/AC clear */
    process->frame.r12 = payload_mode;
    process64_internal_transition(process, PROCESS_READY);
    return process;
failure:
    memory_require(process64_destroy(process), "partial process teardown");
    return 0;
}
static Elf64Result startup_stack(Process64 *process, size_t argc, const char *const *argv,
                                 size_t envc, const char *const *envp) {
    if (argc > STARTUP_MAX_ARGS || envc > STARTUP_MAX_ENV || (argc && !argv) || (envc && !envp))
        return ELF64_ARGUMENTS;
    size_t lengths[STARTUP_MAX_ARGS+STARTUP_MAX_ENV], total = 0;
    for (size_t i = 0; i < argc+envc; ++i) {
        const char *s = i < argc ? argv[i] : envp[i-argc];
        if (!s) return ELF64_ARGUMENTS;
        size_t length = 0;
        while (length < STARTUP_MAX_STRING && s[length]) ++length;
        if (length == STARTUP_MAX_STRING || length+1 > STARTUP_MAX_STRINGS-total) return ELF64_ARGUMENTS;
        lengths[i] = length+1; total += length+1;
    }
    virt_addr_t strings = process->user_stack.top-total;
    size_t table_bytes = 5*8+(argc+1+envc+1)*8;
    virt_addr_t rsp = (strings-table_bytes) & ~UINT64_C(15);
    if (rsp < process->user_stack.base+process->user_stack.guards*4096) return ELF64_ARGUMENTS;
    uint64_t arguments[STARTUP_MAX_ARGS+1], environment[STARTUP_MAX_ENV+1];
    arguments[argc] = 0; environment[envc] = 0;
    virt_addr_t cursor = strings;
    for (size_t i = 0; i < argc+envc; ++i) {
        const char *s = i < argc ? argv[i] : envp[i-argc];
        if (i < argc) arguments[i] = cursor; else environment[i-argc] = cursor;
        if (copy_to_user64(&process->space, cursor, s, lengths[i]) != USER_COPY_OK) return ELF64_ARGUMENTS;
        cursor += lengths[i];
    }
    uint64_t argv_address = rsp+5*8, envp_address = argv_address+(argc+1)*8;
    uint64_t header[] = {STARTUP_VERSION, argc, argv_address, envc, envp_address};
    if (copy_to_user64(&process->space, rsp, header, sizeof(header)) != USER_COPY_OK ||
        copy_to_user64(&process->space, argv_address, arguments, (argc+1)*8) != USER_COPY_OK ||
        copy_to_user64(&process->space, envp_address, environment, (envc+1)*8) != USER_COPY_OK)
        return ELF64_ARGUMENTS;
    process->frame.rsp = rsp;
    process->frame.rdi = argc; process->frame.rsi = argv_address;
    process->frame.rdx = envp_address; process->frame.rcx = STARTUP_VERSION;
    return ELF64_OK;
}
Process64 *process64_create_elf(const void *image, size_t size, size_t argc, const char *const *argv,
                               size_t envc, const char *const *envp, Elf64Result *error) {
    if (!error) return 0;
    *error = elf64_validate(image, size);
    if (*error != ELF64_OK) return 0;
    Process64 *process = process64_prepare();
    if (!process) { *error = ELF64_NOMEM; return 0; }
    *error = elf64_load(&process->space, image, size, &process->frame.rip);
    if (*error == ELF64_OK) *error = startup_stack(process, argc, argv, envc, envp);
    if (*error != ELF64_OK) {
        memory_require(process64_destroy(process), "failed ELF process teardown");
        return 0;
    }
    process64_internal_transition(process, PROCESS_READY);
    return process;
}
int process64_internal_return_valid(Process64 *process, UserFrame *frame) {
    Mapping mapping;
    if (frame->cs != USER_CS || frame->ss != USER_SS ||
        vmm64_user_page(&process->space, frame->rip, VM_EXEC, &mapping) != VM_OK ||
        frame->rip < MM_USER_START || frame->rip >= MM_USER_END ||
        frame->rsp <= MM_USER_START || frame->rsp >= MM_USER_END ||
        user_range_check(&process->space, frame->rsp-1, 1, 1) != USER_COPY_OK) return 0;
    /* Preserve arithmetic flags and DF. Fixed bit 1 on, IOPL off, user IF on
     * only in the scheduler (legacy synchronous regression runs keep IF off).
     * The same checks protect SYSRET and legacy IRET; unsafe flags never return. */
    frame->flags = (frame->flags & UINT64_C(0xcd5)) | 2 | (scheduler64_running() ? 0x200 : 0);
    return 1;
}
#ifdef SELFTEST
int process64_test_return(Process64 *p, UserFrame *f) { return process64_internal_return_valid(p, f); }
int process64_run(Process64 *process, Process64Result *result) {
    memory_context_check();
    if (!process64_internal_known(process) || !result || scheduler64_current() || scheduler64_running() ||
        process->managed || process->state != PROCESS_READY) return 0;
    if (!process64_internal_signal_prepare(process, &process->frame) || !process64_internal_return_valid(process, &process->frame)) {
        process64_internal_transition(process, PROCESS_FAULTED);
        process->exit_status = 141;
        process->frame.vector = 13;
    } else {
        memory_require(scheduler64_context_begin(process), "synchronous process context");
        process64_internal_transition(process, PROCESS_RUNNING);
        memory_require(vmm64_switch(&process->space) == VM_OK, "process CR3 entry");
        fpu64_state_restore(process->fpu_state);
        process64_enter(&process->frame, scheduler64_resume_stack());
        /* Resumed on the original kernel stack, kernel CR3 and RSP0 restored.
         * Only now is it safe to release the former interrupt/kernel stack. */
        memory_require(!scheduler64_current(), "process return context");
    }
    result->pid = process->pid;
    result->state = process->state;
    result->exit_status = process->exit_status;
    result->fault_address = process->fault_address;
    frame_copy(&result->frame, &process->frame);
    return process64_destroy(process);
}
#endif
static void finish(Process64 *process, UserFrame *frame, int fault, int status, uint64_t address)
    __attribute__((noreturn));
static void finish(Process64 *process, UserFrame *frame, int fault, int status, uint64_t address) {
    network64_http_owner_cleanup(process->pid);
    audio64_stream_cleanup(process->pid);
    frame_copy(&process->frame, frame);
    process64_internal_transition(process, fault ? PROCESS_FAULTED : PROCESS_EXITED);
    process->exit_status = status;
    process->fault_address = address;
    process64_internal_wake_waiter(process);
    if (fault) {
        memory_log("[USER64] fault pid="); memory_hex(process->pid);
        memory_log(" vector="); memory_hex(frame->vector);
        memory_log(" error="); memory_hex(frame->error);
        memory_log(" rip="); memory_hex(frame->rip);
        memory_log(" address="); memory_hex(address);
        memory_log(" cpl=3\n");
    }
    scheduler64_leave_current(!scheduler64_running());
}
/* ------------------------------------------------------------------ C6 --
 * Spawn and waitpid. The spawn path reuses launch64_create (the single ELF
 * loader) under the kernel descriptor context; all userspace arrays are copied
 * into bounded kernel storage before any kernel pointer is used. */
#define WAIT_UNSPECIFIED UINT64_MAX
#define WAIT_SUSPEND INT64_MIN
/* One bounded kernel staging area for argv then envp; the single-CPU IF=0
 * syscall path makes reuse safe and avoids large per-call kernel stack use. */
static char spawn_storage[STARTUP_MAX_STRINGS];
static const char *spawn_argv[STARTUP_MAX_ARGS];
static const char *spawn_envp[STARTUP_MAX_ENV];
static int64_t copy_user_string_array(const AddressSpace *space, uint64_t array_address,
        const char **pointers, size_t maximum, size_t *storage_used, size_t *out_count) {
    *out_count = 0;
    if (!array_address) return 0;
    for (size_t index = 0; index < maximum; ++index) {
        uint64_t string_address = 0;
        if (copy_from_user64(space, &string_address, array_address + index*8, 8) != USER_COPY_OK)
            return -USER_EFAULT;
        if (!string_address) return 0;
        if (*storage_used > STARTUP_MAX_STRINGS - STARTUP_MAX_STRING)
            return -USER_E2BIG; /* explicit total-bytes bound */
        char *target = spawn_storage + *storage_used;
        UserCopyResult copied = copy_string_from_user64(space, target, string_address, STARTUP_MAX_STRING);
        if (copied == USER_COPY_TOO_LONG) return -USER_E2BIG;
        if (copied != USER_COPY_OK) return -USER_EFAULT;
        size_t length = 0;
        while (target[length]) ++length;
        *storage_used += length + 1;
        pointers[index] = target;
        *out_count = index+1;
    }
    return -USER_E2BIG; /* no NUL terminator within the explicit bound */
}
static int64_t launch64_user_error(Launch64Result result) {
    switch (result) {
    case LAUNCH_NOT_FOUND: case LAUNCH_NOT_FILE: return -USER_ENOENT;
    case LAUNCH_BAD_REQUEST: return -USER_EINVAL;
    case LAUNCH_DENIED: return -USER_EACCES;
    case LAUNCH_NOMEM: return -USER_ENOMEM;
    case LAUNCH_TABLE_FULL: case LAUNCH_QUEUE: return -USER_EAGAIN;
    case LAUNCH_BAD_SIZE: case LAUNCH_INVALID_ELF: case LAUNCH_UNSUPPORTED_ELF:
    case LAUNCH_TOO_LARGE: return -USER_ENOEXEC;
    default: return -USER_EIO;
    }
}
static int spawn_group_allowed(const Process64 *parent, uint64_t pgid) {
    if (pgid == parent->pgid) return 1;
    /* A fast first pipeline stage may already be a zombie when the shell
     * starts the next stage. Its unreaped process-table record keeps the
     * group alive long enough for the remaining stages to join atomically. */
    for (size_t i = 0; i < process64_capacity(); ++i) {
        const Process64 *leader = slots[i];
        if (leader && leader->pid == pgid && leader->pgid == pgid &&
            leader->parent_pid == parent->pid && !leader->wait_collected)
            return 1;
    }
    return 0;
}
static int64_t spawn_request(Process64 *parent, UserFrame *frame, int with_group) {
    uint64_t requested_pgid = parent->pgid;
    if (with_group) {
        int64_t pgid = (int64_t)frame->r10;
        if (pgid < 0 || pgid > 2147483647ll) return -USER_EINVAL;
        if (pgid && !spawn_group_allowed(parent, (uint64_t)pgid)) return -USER_EPERM;
        requested_pgid = (uint64_t)pgid;
    }
    char path[USER_PATH_MAX];
    int64_t error = path64_user(parent, frame->rdi, path);
    if (error) return error;
    size_t argc = 0, envc = 0, storage_used = 0;
    error = copy_user_string_array(&parent->space, frame->rsi, spawn_argv,
                                   STARTUP_MAX_ARGS, &storage_used, &argc);
    if (error) return error;
    error = copy_user_string_array(&parent->space, frame->rdx, spawn_envp,
                                   STARTUP_MAX_ENV, &storage_used, &envc);
    if (error) return error;
    Process64 *child = 0;
    vfs_file_t **previous = fs64_fd_context(0); /* kernel-owned descriptor context */
    Launch64Result result = launch64_create(path, argc, spawn_argv, envc, spawn_envp, &child);
    fs64_fd_context(previous);
    if (result != LAUNCH_OK) return launch64_user_error(result);
    child->parent_pid = parent->pid;
    child->pgid = requested_pgid ? requested_pgid : child->pid;
    for (unsigned i = 0; i < sizeof(child->cwd); ++i) {
        child->cwd[i] = parent->cwd[i];
        if (!parent->cwd[i]) break;
    }
    file64_clone_parent(child, parent);
    if (!scheduler64_submit(child)) {
        memory_require(process64_destroy(child), "spawn insertion rollback");
        return -USER_EAGAIN;
    }
    return (int64_t)child->pid;
}
static int wait_matches(const Process64 *parent, const Process64 *child, int64_t selector) {
    if (!parent || !child || child->parent_pid != parent->pid) return 0;
    if (selector > 0) return child->pid == (uint64_t)selector;
    if (selector < -1) return child->pgid == (uint64_t)-selector;
    return 1; /* legacy raw selectors 0 and -1 both mean any child */
}
static Process64 *find_wait_child(Process64 *parent, int64_t selector) {
    Process64 *alive = 0;
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *child = slots[i];
        if (!wait_matches(parent, child, selector)) continue;
        if (process64_internal_dead(child)) return child; /* terminal state available now */
        if (!alive) alive = child;
    }
    return alive;
}
static int64_t wait_request(Process64 *parent, UserFrame *frame) {
    if (frame->rdx & ~(uint64_t)USER_WAIT_NOHANG) return -USER_ENOTSUP;
    uint64_t raw = frame->rdi;
    int64_t selector;
    if (raw == UINT64_MAX) selector = -1; /* legacy wait-any selector */
    else if (raw <= 0x7fffffffull) selector = (int64_t)raw;
    else if (raw >= UINT64_C(0xffffffff80000000)) {
        selector = (int64_t)raw;
        if (selector < (-2147483647ll - 1)) return -USER_EINVAL;
    } else return -USER_ESRCH;
    Process64 *child = find_wait_child(parent, selector);
    if (!child) return -USER_ECHILD;
    if (process64_internal_dead(child)) {
        UserWait64 status = wait_status(child);
        if (frame->rsi && user_range_check(&parent->space, frame->rsi, sizeof(status), 1) != USER_COPY_OK)
            return -USER_EFAULT;
        uint64_t pid = child->pid;
        if (!child->zombie) {
            memory_require(!child->queued && child != scheduler64_current(),
                           "terminal child is detached before wait reaping");
            child->managed = 0;
            memory_require(process64_reclaim(child), "dead child reclaim");
        }
        child->zombie = 1;
        memory_require(process64_destroy(child), "waited child reclamation");
        if (frame->rsi)
            memory_require(copy_to_user64(&parent->space, frame->rsi, &status, sizeof(status)) == USER_COPY_OK,
                           "wait status copy");
        return (int64_t)pid;
    }
    if (frame->rdx & USER_WAIT_NOHANG) return 0;
    if (frame->rsi && user_range_check(&parent->space, frame->rsi, sizeof(UserWait64), 1) != USER_COPY_OK)
        return -USER_EFAULT;
    if (parent->waiting) return -USER_EAGAIN;
    parent->waiting = 1;
    parent->wait_pid = (uint64_t)selector;
    parent->wait_status_va = frame->rsi;
    return WAIT_SUSPEND;
}
/* Only a parent may kill its own direct children; no signals, one reason. */
static int64_t kill_request(Process64 *parent, UserFrame *frame) {
    uint64_t pid = frame->rdi;
    if (!pid || pid == parent->pid) return -USER_EINVAL;
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *child = slots[i];
        if (!child || child->pid != pid || child->parent_pid != parent->pid) continue;
        return process64_kill(pid, (int)(frame->rsi & 0xff)) ? 0 : -USER_ESRCH;
    }
    return -USER_ESRCH;
}
static int64_t signal_send_request(Process64 *process, UserFrame *frame) {
    int64_t target = (int64_t)frame->rdi;
    if (target < -2147483647ll-1 || target > 2147483647ll || frame->rsi > 0xffffffffull)
        return -USER_EINVAL;
    return process64_signal_target(process, target, (int)frame->rsi);
}
static int64_t getpgid_request(Process64 *process, UserFrame *frame) {
    int64_t pid = (int64_t)frame->rdi;
    if (pid < 0 || pid > 2147483647ll) return -USER_EINVAL;
    Process64 *target = process64_internal_find(pid ? (uint64_t)pid : process->pid);
    if (!target || process64_internal_dead(target) || !target->pgid) return -USER_ESRCH;
    if (target->pgid > 2147483647ull) return -USER_ERANGE;
    return (int64_t)target->pgid;
}
static int group_exists(uint64_t pgid) {
    for (size_t i = 0; i < process64_capacity(); ++i)
        if (slots[i] && slots[i]->pgid == pgid && !process64_internal_dead(slots[i])) return 1;
    return 0;
}
static int group_change_allowed(const Process64 *sender, uint64_t pgid) {
    if (sender->pgid == pgid) return 1;
    for (size_t i = 0; i < process64_capacity(); ++i) {
        const Process64 *member = slots[i];
        if (member && !process64_internal_dead(member) && member->pgid == pgid &&
            (member->pid == sender->pid || member->parent_pid == sender->pid)) return 1;
    }
    return 0;
}
static int64_t setpgid_request(Process64 *sender, UserFrame *frame) {
    int64_t pid = (int64_t)frame->rdi, requested = (int64_t)frame->rsi;
    if (pid < 0 || pid > 2147483647ll || requested < 0 || requested > 2147483647ll)
        return -USER_EINVAL;
    uint64_t target_pid = pid ? (uint64_t)pid : sender->pid;
    Process64 *target = process64_internal_find(target_pid);
    if (!target || process64_internal_dead(target)) return -USER_ESRCH;
    if (target != sender && target->parent_pid != sender->pid) return -USER_EPERM;
    uint64_t pgid = requested ? (uint64_t)requested : target_pid;
    if (!pgid) return -USER_EINVAL;
    if (pgid != target_pid && (!group_exists(pgid) || !group_change_allowed(sender, pgid)))
        return -USER_EPERM;
    target->pgid = pgid;
    return 0;
}
int proc64_dispatch(Process64 *process, UserFrame *frame) {
    if (frame->rax != USER_SPAWN && frame->rax != USER_SPAWN_GROUP && frame->rax != USER_WAITPID &&
        frame->rax != USER_KILL && frame->rax != USER_FOREGROUND &&
        frame->rax != USER_SIGNAL_SEND && frame->rax != USER_SIGACTION &&
        frame->rax != USER_SIGPROCMASK && frame->rax != USER_SIGPENDING &&
        frame->rax != USER_SIGRETURN && frame->rax != USER_GETPGID &&
        frame->rax != USER_SETPGID) return 0;
    memory_context_check();
    if (frame->rax == USER_FOREGROUND) {
        tty64_set_foreground(frame->rdi);
        frame->rax = 0;
        return 1;
    }
    if (frame->rax == USER_KILL) {
        frame->rax = (uint64_t)kill_request(process, frame);
        return 1;
    }
    if (frame->rax == USER_SIGNAL_SEND) {
        frame->rax = (uint64_t)signal_send_request(process, frame);
        return 1;
    }
    if (frame->rax == USER_GETPGID) {
        frame->rax = (uint64_t)getpgid_request(process, frame);
        return 1;
    }
    if (frame->rax == USER_SETPGID) {
        frame->rax = (uint64_t)setpgid_request(process, frame);
        return 1;
    }
    if (frame->rax == USER_SIGACTION) {
        frame->rax = (uint64_t)signal_action_request(process, frame);
        return 1;
    }
    if (frame->rax == USER_SIGPROCMASK) {
        frame->rax = (uint64_t)signal_mask_request(process, frame);
        return 1;
    }
    if (frame->rax == USER_SIGPENDING) {
        frame->rax = (uint64_t)signal_pending_request(process, frame);
        return 1;
    }
    if (frame->rax == USER_SIGRETURN) {
        int64_t result = signal_return_request(process, frame);
        if (result < 0) frame->rax = (uint64_t)result;
        return 1;
    }
    if (frame->rax == USER_WAITPID) {
        int64_t result = wait_request(process, frame);
        if (result == WAIT_SUSPEND) scheduler64_suspend(&process->thread, frame); /* never returns until woken */
        frame->rax = (uint64_t)result;
        return 1;
    }
    frame->rax = (uint64_t)spawn_request(process, frame, frame->rax == USER_SPAWN_GROUP);
    return 1;
}
unsigned process64_debug_slots(void) {
    unsigned count = 0;
    for (size_t i = 0; i < process64_capacity(); ++i) if (slots[i]) ++count;
    return count;
}
unsigned process64_debug_zombies(void) {
    unsigned count = 0;
    for (size_t i = 0; i < process64_capacity(); ++i) if (slots[i] && slots[i]->zombie) ++count;
    return count;
}
/* Blocking console and pipe transfers. Like blocked waitpid, a request saves
 * the user buffer in the process record and suspends; the waker completes the
 * copy and fills the saved frame's rax. */
static int64_t tty_read_request(Process64 *process, UserFrame *frame) {
    uint64_t count = frame->rdx;
    if (!count) return 0; /* valid fd; destination is unused, even for stdin */
    if (!tty64_enabled()) return -USER_ENOTSUP; /* synchronous regression contract */
    if (count > USER_READ_MAX) return -USER_E2BIG;
    if (user_range_check(&process->space, frame->rsi, (size_t)count, 1) != USER_COPY_OK)
        return -USER_EFAULT;
    uint8_t buffer[USER_READ_MAX];
    size_t ready = tty64_pop(buffer, (size_t)count);
    if (ready) {
        if (copy_to_user64(&process->space, frame->rsi, buffer, ready) != USER_COPY_OK)
            return -USER_EFAULT;
        return (int64_t)ready;
    }
    if (!scheduler64_running()) return -USER_ENOTSUP; /* synchronous regression runs */
    process->io_waiting = 1;
    process->io_kind = IO64_TTY_READ;
    process->io_fd = 0;
    process->io_va = frame->rsi;
    process->io_count = count;
    return WAIT_SUSPEND;
}
static int64_t pipe_read_request(Process64 *process, UserFrame *frame) {
    uint64_t fd = frame->rdi, count = frame->rdx;
    if (count > USER_READ_MAX) return -USER_E2BIG;
    if (!count) return 0;
    if (user_range_check(&process->space, frame->rsi, (size_t)count, 1) != USER_COPY_OK)
        return -USER_EFAULT;
    int index = process->pipe_index[fd];
    uint8_t buffer[USER_READ_MAX];
    size_t ready = pipe64_read_data(index, buffer, (size_t)count);
    if (ready) {
        pipe64_wake_blocked(); /* freed space may unblock a writer */
        if (copy_to_user64(&process->space, frame->rsi, buffer, ready) != USER_COPY_OK)
            return -USER_EFAULT;
        return (int64_t)ready;
    }
    if (!pipe64_has_writers(index)) return 0; /* EOF: last writer is gone */
    if (!scheduler64_running()) return -USER_EAGAIN;
    process->io_waiting = 1;
    process->io_kind = IO64_PIPE_READ;
    process->io_fd = (uint8_t)fd;
    process->io_va = frame->rsi;
    process->io_count = count;
    return WAIT_SUSPEND;
}
static int64_t pipe_read_available_request(Process64 *process, UserFrame *frame) {
    uint64_t fd=frame->rdi, count=frame->rdx;
    if (fd>=USER_FD_LIMIT || file64_kind(process,fd)!=FD64_PIPE || process->pipe_write[fd])
        return -USER_EBADF;
    if (count>USER_READ_MAX) return -USER_E2BIG;
    if (!count) return 0;
    if (user_range_check(&process->space,frame->rsi,(size_t)count,1)!=USER_COPY_OK)
        return -USER_EFAULT;
    int index=process->pipe_index[fd];
    uint8_t buffer[USER_READ_MAX];
    size_t ready=pipe64_read_data(index,buffer,(size_t)count);
    if (!ready) return pipe64_has_writers(index) ? -USER_EAGAIN : 0;
    if (copy_to_user64(&process->space,frame->rsi,buffer,ready)!=USER_COPY_OK)
        return -USER_EFAULT;
    pipe64_wake_blocked();
    return (int64_t)ready;
}
static int64_t pipe_write_request(Process64 *process, UserFrame *frame) {
    uint64_t fd = frame->rdi, count = frame->rdx;
    if (count > USER_WRITE_MAX) return -USER_E2BIG;
    if (!count) return 0;
    if (user_range_check(&process->space, frame->rsi, (size_t)count, 0) != USER_COPY_OK)
        return -USER_EFAULT;
    int index = process->pipe_index[fd];
    uint8_t buffer[USER_WRITE_CHUNK];
    size_t chunk = count > sizeof(buffer) ? sizeof(buffer) : (size_t)count;
    if (copy_from_user64(&process->space, buffer, frame->rsi, chunk) != USER_COPY_OK)
        return -USER_EFAULT;
    size_t written = pipe64_write_data(index, buffer, chunk);
    if (written) {
        pipe64_wake_blocked();
        return (int64_t)written;
    }
    if (!pipe64_has_readers(index)) return -USER_EIO; /* broken pipe */
    if (!scheduler64_running()) return -USER_EAGAIN;
    process->io_waiting = 1;
    process->io_kind = IO64_PIPE_WRITE;
    process->io_fd = (uint8_t)fd;
    process->io_va = frame->rsi;
    process->io_count = count;
    return WAIT_SUSPEND;
}
/* Wake at most one chunk per blocked transfer; remaining bytes stay queued for
 * the following call. Runs from syscall/PIT context and never switches spaces. */
void process64_internal_tty_wake_readers(void) {
    if (!tty64_enabled() || !tty64_available()) return;
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *p = slots[i];
        if (!p || !p->io_waiting || p->io_kind != IO64_TTY_READ ||
            p->state != PROCESS_BLOCKED) continue;
        uint8_t buffer[256];
        size_t want = p->io_count > sizeof(buffer) ? sizeof(buffer) : (size_t)p->io_count;
        size_t ready = tty64_pop(buffer, want);
        int64_t result = (int64_t)ready;
        if (ready && copy_to_user64(&p->space, p->io_va, buffer, ready) != USER_COPY_OK)
            result = -(int64_t)USER_EFAULT;
        p->frame.rax = (uint64_t)result;
        p->io_waiting = 0;
        wake_ready(p);
        if (!tty64_available()) break;
    }
}
void pipe64_wake_blocked(void) {
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *p = slots[i];
        if (!p || !p->io_waiting || p->state != PROCESS_BLOCKED) continue;
        if (p->io_kind != IO64_PIPE_READ && p->io_kind != IO64_PIPE_WRITE) continue;
        int fd = p->io_fd;
        int index = fd < USER_FD_LIMIT ? p->pipe_index[fd] : PIPE64_NONE;
        int64_t result = 0;
        if (!pipe64_used(index)) {
            result = -(int64_t)USER_EBADF; /* the descriptor vanished */
        } else if (p->io_kind == IO64_PIPE_READ) {
            if (pipe64_available(index)) {
                uint8_t buffer[256];
                size_t want = p->io_count > sizeof(buffer) ? sizeof(buffer) : (size_t)p->io_count;
                size_t ready = pipe64_read_data(index, buffer, want);
                result = (int64_t)ready;
                if (ready && copy_to_user64(&p->space, p->io_va, buffer, ready) != USER_COPY_OK)
                    result = -(int64_t)USER_EFAULT;
            } else if (!pipe64_has_writers(index)) {
                result = 0; /* EOF */
            } else {
                continue;
            }
        } else {
            if (pipe64_space(index)) {
                uint8_t buffer[256];
                size_t want = p->io_count > sizeof(buffer) ? sizeof(buffer) : (size_t)p->io_count;
                if (copy_from_user64(&p->space, buffer, p->io_va, want) != USER_COPY_OK) {
                    result = -(int64_t)USER_EFAULT;
                } else {
                    result = (int64_t)pipe64_write_data(index, buffer, want);
                }
            } else if (!pipe64_has_readers(index)) {
                result = -(int64_t)USER_EIO; /* broken pipe */
            } else {
                continue;
            }
        }
        p->frame.rax = (uint64_t)result;
        p->io_waiting = 0;
        wake_ready(p);
    }
}
int process64_trap(UserFrame *frame, uint64_t cr2) {
    if ((frame->cs & 3) != 3) return 0; /* kernel faults never become user errors */
    Process64 *process = scheduler64_current();
    memory_require(process && process->state == PROCESS_RUNNING, "CPL3 without active process");
    if (frame->vector == 2 || frame->vector == 8 || frame->vector == 18) return 0;
    memory_require(kernel64_get_rsp0() == process->kernel_stack.top &&
                   (uintptr_t)frame >= process->kernel_stack.base+4096 &&
                   (uintptr_t)(frame+1) <= process->kernel_stack.top, "per-process RSP0 entry");
    fpu64_state_save(process->fpu_state);
    if (process->kill_pending) scheduler64_park(scheduler64_current_thread(), frame);
    if (frame->vector != USER_GATE && frame->vector != USER_SYSCALL_VECTOR)
        finish(process, frame, 1, 128+(int)frame->vector, frame->vector == 14 ? cr2 : 0);
    if (frame->rax == USER_EXIT) finish(process, frame, 0, (int)(frame->rdi & 255), 0);
    if (frame->rax == USER_ABI_INFO) {
        uint32_t requested_size = 0;
        if (copy_from_user64(&process->space, &requested_size, frame->rdi,
                             sizeof(requested_size)) != USER_COPY_OK) {
            frame->rax = (uint64_t)-(int64_t)USER_EFAULT;
        } else if (requested_size < POLLIKOS_ABI_INFO_MIN_SIZE) {
            frame->rax = (uint64_t)-(int64_t)USER_EINVAL;
        } else {
            PollikAbiInfo info;
            info.size = sizeof(info);
            info.major = POLLIKOS_ABI_VERSION_MAJOR;
            info.minor = POLLIKOS_ABI_VERSION_MINOR;
            info.architecture = POLLIKOS_ABI_ARCH_X86_64;
            info.transport = POLLIKOS_ABI_TRANSPORT_SYSCALL64;
            info.operation_namespace = POLLIKOS_ABI_NAMESPACE_X86_64;
            info.features = POLLIKOS_ABI_FEATURE_PROCESS | POLLIKOS_ABI_FEATURE_FILES |
                            POLLIKOS_ABI_FEATURE_NETWORK | POLLIKOS_ABI_FEATURE_WINDOWS;
            size_t written = requested_size < sizeof(info) ? requested_size : sizeof(info);
            if (copy_to_user64(&process->space, frame->rdi, &info, written) != USER_COPY_OK)
                frame->rax = (uint64_t)-(int64_t)USER_EFAULT;
            else
                frame->rax = written;
        }
    } else if (frame->rax == USER_DEBUG_WRITE) {
        char buffer[256];
        if (frame->rsi > sizeof(buffer)) frame->rax = (uint64_t)-(int64_t)USER_E2BIG;
        else if (copy_from_user64(&process->space, buffer, frame->rdi, (size_t)frame->rsi) != USER_COPY_OK)
            frame->rax = (uint64_t)-(int64_t)USER_EFAULT;
        else {
            kernel64_debug_bytes(buffer, (size_t)frame->rsi);
            frame->rax = frame->rsi;
        }
    } else if (frame->rax == USER_SLEEP) {
        int64_t result = sleep_request(process, frame->rdi);
        if (result < 0) frame->rax = (uint64_t)result;
        else {
            frame->rax = 0; /* success is visible when the timer resumes the process */
            if (result) scheduler64_suspend(&process->thread, frame); /* never returns until woken */
        }
    } else if (frame->rax == USER_READ && frame->rdi == 0 &&
               file64_kind(process, 0) == FD64_STDIN) {
        int64_t result = tty_read_request(process, frame);
        if (result == WAIT_SUSPEND) scheduler64_suspend(&process->thread, frame); /* never returns until input arrives */
        frame->rax = (uint64_t)result;
    } else if (frame->rax == USER_PIPE_READ_NOWAIT) {
        frame->rax=(uint64_t)pipe_read_available_request(process,frame);
    } else if (frame->rax == USER_HTTP_OPEN) {
        char url[1024];
        UserCopyResult copied=copy_string_from_user64(&process->space,url,frame->rdi,sizeof(url));
        if (copied==USER_COPY_TOO_LONG) frame->rax=(uint64_t)-(int64_t)USER_ENAMETOOLONG;
        else if (copied!=USER_COPY_OK) frame->rax=(uint64_t)-(int64_t)USER_EFAULT;
        else frame->rax=(uint64_t)network64_http_open(process->pid,url);
    } else if (frame->rax == USER_HTTP_READ) {
        uint8_t buffer[4096];
        if (!frame->rdx || frame->rdx>sizeof(buffer)) frame->rax=(uint64_t)-(int64_t)USER_E2BIG;
        else if (user_range_check(&process->space,frame->rsi,(size_t)frame->rdx,1)!=USER_COPY_OK)
            frame->rax=(uint64_t)-(int64_t)USER_EFAULT;
        else {
            int64_t result=network64_http_read(process->pid,frame->rdi,buffer,(int)frame->rdx);
            if (result>0 && copy_to_user64(&process->space,frame->rsi,buffer,(size_t)result)!=USER_COPY_OK)
                frame->rax=(uint64_t)-(int64_t)USER_EFAULT;
            else frame->rax=(uint64_t)result;
        }
    } else if (frame->rax == USER_HTTP_CLOSE) {
        frame->rax=(uint64_t)network64_http_close(process->pid,frame->rdi);
    } else if ((frame->rax == USER_READ || frame->rax == USER_WRITE) &&
               frame->rdi < USER_FD_LIMIT && file64_kind(process, frame->rdi) == FD64_PIPE) {
        int64_t result = frame->rax == USER_READ ? pipe_read_request(process, frame)
                                                 : pipe_write_request(process, frame);
        if (result == WAIT_SUSPEND) scheduler64_suspend(&process->thread, frame); /* never returns until progressed */
        frame->rax = (uint64_t)result;
    } else if(frame->rax==USER_AUDIO_STREAM){
        frame->rax=(uint64_t)audio64_stream_control(process,frame->rdi,frame->rsi,frame->rdx);
    } else if (frame->rax==USER_DEVICE_CONTROL) {
        frame->rax=(uint64_t)devices64_control(frame->rdi,frame->rsi);
    } else if (!file64_dispatch(process, frame) && !heap64_dispatch(process, frame) &&
               !proc64_dispatch(process, frame) && !window64_dispatch(process, frame))
        frame->rax = (uint64_t)-(int64_t)USER_ENOSYS;
    if (process->kill_pending) scheduler64_park(scheduler64_current_thread(), frame);
    if (process->stop_pending) scheduler64_park(scheduler64_current_thread(), frame);
    if (!process64_internal_signal_prepare(process, frame)) finish(process, frame, 1, 142, frame->rsp);
    if (process->kill_pending) scheduler64_park(scheduler64_current_thread(), frame);
    if (!process64_internal_return_valid(process, frame)) finish(process, frame, 1, 141, 0);
    frame_copy(&process->frame, frame);
    fpu64_state_restore(process->fpu_state);
    return 1;
}
#ifdef SELFTEST
void process64_demo(void) {
    Process64 *process = process64_create(0, 42);
    Process64Result result;
    memory_require(process && process64_run(process, &result) &&
                   result.state == PROCESS_EXITED && result.exit_status == 42, "Ring 3 demo");
    memory_log("[USER64] demo exited with status 42\n");
}
#endif

Process64 *process64_internal_find(uint64_t pid) {
    for (size_t i = 0; i < process64_capacity(); ++i)
        if (slots[i] && slots[i]->pid == pid && slots[i]->managed) return slots[i];
    return 0;
}
static int signal_supported(int signal) {
    return (signal >= 1 && signal <= 15) || signal == USER_SIG_CHLD ||
        signal == USER_SIG_CONT || signal == USER_SIG_STOP;
}
static uint64_t signal_bit(int signal) { return UINT64_C(1) << signal; }
static int signal_action_valid(Process64 *process, const UserSignalAction64 *action, int signal) {
    Mapping mapping;
    if (action->flags) return 0;
    if (action->handler == USER_SIGNAL_DFL || action->handler == USER_SIGNAL_IGN) return 1;
    return action->handler >= MM_USER_START && action->handler < MM_USER_END &&
        action->restorer >= MM_USER_START && action->restorer < MM_USER_END &&
        vmm64_user_page(&process->space, action->handler, VM_EXEC, &mapping) == VM_OK &&
        vmm64_user_page(&process->space, action->restorer, VM_EXEC, &mapping) == VM_OK &&
        signal != USER_SIG_KILL && signal != USER_SIG_STOP;
}
static int64_t signal_action_request(Process64 *process, UserFrame *frame) {
    uint64_t number = frame->rdi, new_address = frame->rsi, old_address = frame->rdx;
    if (number >= 32 || !signal_supported((int)number) ||
        number == USER_SIG_KILL || number == USER_SIG_STOP)
        return -USER_EINVAL;
    if ((new_address && user_range_check(&process->space, new_address, sizeof(UserSignalAction64), 0) != USER_COPY_OK) ||
        (old_address && user_range_check(&process->space, old_address, sizeof(UserSignalAction64), 1) != USER_COPY_OK))
        return -USER_EFAULT;
    UserSignalAction64 replacement, previous = process->signal_actions[number];
    if (new_address) {
        if (copy_from_user64(&process->space, &replacement, new_address, sizeof(replacement)) != USER_COPY_OK)
            return -USER_EFAULT;
        if (!signal_action_valid(process, &replacement, (int)number)) return -USER_EINVAL;
    }
    if (old_address && copy_to_user64(&process->space, old_address, &previous, sizeof(previous)) != USER_COPY_OK)
        return -USER_EFAULT;
    if (new_address) {
        replacement.mask &= ~(signal_bit(USER_SIG_KILL)|signal_bit(USER_SIG_STOP));
        process->signal_actions[number] = replacement;
        if (replacement.handler == USER_SIGNAL_IGN)
            process->signal_pending &= ~signal_bit((int)number);
    }
    return 0;
}
static int64_t signal_mask_request(Process64 *process, UserFrame *frame) {
    uint64_t how = frame->rdi, set_address = frame->rsi, old_address = frame->rdx;
    if (how > 2) return -USER_EINVAL;
    if ((set_address && user_range_check(&process->space, set_address, sizeof(uint64_t), 0) != USER_COPY_OK) ||
        (old_address && user_range_check(&process->space, old_address, sizeof(uint64_t), 1) != USER_COPY_OK))
        return -USER_EFAULT;
    uint64_t set = 0;
    if (set_address && copy_from_user64(&process->space, &set, set_address, sizeof(set)) != USER_COPY_OK)
        return -USER_EFAULT;
    if (old_address && copy_to_user64(&process->space, old_address, &process->signal_blocked, sizeof(uint64_t)) != USER_COPY_OK)
        return -USER_EFAULT;
    if (!set_address) return 0;
    set &= ~(signal_bit(USER_SIG_KILL)|signal_bit(USER_SIG_STOP));
    if (how == 0) process->signal_blocked |= set;
    else if (how == 1) process->signal_blocked &= ~set;
    else process->signal_blocked = set;
    return 0;
}
static int64_t signal_pending_request(Process64 *process, UserFrame *frame) {
    if (user_range_check(&process->space, frame->rdi, sizeof(uint64_t), 1) != USER_COPY_OK)
        return -USER_EFAULT;
    uint64_t pending = process->signal_pending & process->signal_blocked;
    return copy_to_user64(&process->space, frame->rdi, &pending, sizeof(pending)) == USER_COPY_OK ? 0 : -USER_EFAULT;
}
typedef struct { uint64_t cookie, signal; } SignalToken64;
static int64_t signal_return_request(Process64 *process, UserFrame *frame) {
    SignalToken64 token;
    if (!process->signal_active || frame->rdi != process->signal_frame_va+sizeof(uint64_t) ||
        copy_from_user64(&process->space, &token, frame->rdi, sizeof(token)) != USER_COPY_OK ||
        token.cookie != process->signal_frame_cookie) return -USER_EINVAL;
    process->signal_blocked = process->signal_saved_mask;
    frame_copy(frame, &process->signal_saved_frame);
    for (size_t i = 0; i < FPU64_STATE_SIZE; ++i)
        process->fpu_state[i] = process->signal_saved_fpu[i];
    process->signal_active = 0;
    process->signal_frame_va = 0;
    process->signal_frame_cookie = 0;
    return 0;
}
int process64_internal_signal_prepare(Process64 *process, UserFrame *frame) {
    if (process->signal_active) return 1;
    uint64_t ready = process->signal_pending & ~process->signal_blocked;
    for (int signal = 1; signal < 32; ++signal) {
        uint64_t bit = signal_bit(signal);
        if (!(ready & bit)) continue;
        UserSignalAction64 action = process->signal_actions[signal];
        if (action.handler == USER_SIGNAL_IGN ||
            (action.handler == USER_SIGNAL_DFL && signal == USER_SIG_CHLD)) {
            process->signal_pending &= ~bit;
            continue;
        }
        if (action.handler == USER_SIGNAL_DFL) {
            process->signal_pending &= ~bit;
            (void)process64_kill(process->pid, signal);
            return 1;
        }
        const size_t token_bytes = sizeof(uint64_t)+sizeof(SignalToken64);
        if (frame->rsp < token_bytes+16) return 0;
        uint64_t address = ((frame->rsp-token_bytes-8) & ~UINT64_C(15))+8;
        if (user_range_check(&process->space, address, token_bytes, 1) != USER_COPY_OK)
            return 0;
        SignalToken64 token = {
            .cookie = USER_SIGNAL_FRAME_MAGIC ^ process->pid ^ address ^ ((uint64_t)signal<<32),
            .signal = (uint64_t)signal
        };
        if (copy_to_user64(&process->space, address, &action.restorer, sizeof(action.restorer)) != USER_COPY_OK ||
            copy_to_user64(&process->space, address+sizeof(uint64_t), &token, sizeof(token)) != USER_COPY_OK)
            return 0;
        frame_copy(&process->signal_saved_frame, frame);
        for (size_t i = 0; i < FPU64_STATE_SIZE; ++i)
            process->signal_saved_fpu[i] = process->fpu_state[i];
        process->signal_saved_mask = process->signal_blocked;
        process->signal_frame_va = address;
        process->signal_frame_cookie = token.cookie;
        process->signal_active = 1;
        process->signal_pending &= ~bit;
        process->signal_blocked |= (action.mask | bit);
        frame->rip = action.handler;
        frame->rsp = address;
        frame->rdi = (uint64_t)signal;
        return 1;
    }
    return 1;
}
int64_t process64_signal_send(uint64_t pid, int signal) {
    memory_context_check();
    Process64 *process = process64_internal_find(pid);
    if (!process || process64_internal_dead(process)) return -USER_ESRCH;
    if (signal == 0) return 0;
    if (signal == USER_SIG_KILL) {
        process->signal_pending &= ~signal_bit(signal);
        return process64_kill(pid, signal) ? 0 : -USER_ESRCH;
    }
    if (!signal_supported(signal)) return -USER_EINVAL;
    if (signal == USER_SIG_STOP) {
        process->stopped = 1;
        if (process->state == PROCESS_RUNNING) process->stop_pending = 1;
        else if (process->state == PROCESS_READY) scheduler64_dequeue(&process->thread);
        return 0;
    }
    if (signal == USER_SIG_CONT) {
        process->stopped = 0;
        if (process->state == PROCESS_READY && process->managed && !process->queued)
            scheduler64_enqueue(&process->thread);
    }
    uint64_t bit = signal_bit(signal);
    UserSignalAction64 action = process->signal_actions[signal];
    if (signal == USER_SIG_CONT && action.handler == USER_SIGNAL_DFL) return 0;
    if (action.handler == USER_SIGNAL_IGN ||
        (action.handler == USER_SIGNAL_DFL && signal == USER_SIG_CHLD)) return 0;
    if (!(process->signal_blocked & bit) && action.handler == USER_SIGNAL_DFL) {
        return process64_kill(pid, signal) ? 0 : -USER_ESRCH;
    }
    process->signal_pending |= bit;
    if (!process->stopped && !(process->signal_blocked & bit) && process->state == PROCESS_BLOCKED) {
        process->wake_tick = 0;
        process->waiting = 0;
        process->wait_pid = 0;
        process->wait_status_va = 0;
        process->io_waiting = 0;
        process->frame.rax = (uint64_t)-(int64_t)USER_EINTR;
        wake_ready(process);
    }
    return 0;
}
int64_t process64_signal_group(uint64_t pgid, int signal) {
    memory_context_check();
    if (!pgid) return -USER_EINVAL;
    int found = 0;
    for (size_t i = 0; i < process64_capacity(); ++i) {
        Process64 *member = slots[i];
        if (!member || member->pgid != pgid || process64_internal_dead(member)) continue;
        found = 1;
        int64_t result = process64_signal_send(member->pid, signal);
        if (result < 0) return result;
    }
    return found ? 0 : -USER_ESRCH;
}
int64_t process64_signal_target(Process64 *sender, int64_t target, int signal) {
    memory_context_check();
    if (!sender) return -USER_EINVAL;
    if (target > 0) return process64_signal_send((uint64_t)target, signal);
    if (target == 0) return process64_signal_group(sender->pgid, signal);
    if (target == (-2147483647ll-1)) return -USER_EINVAL;
    return process64_signal_group((uint64_t)-target, signal);
}
