#ifndef POLLIK_X64_USER_H
#define POLLIK_X64_USER_H
#include "memory.h"
#include "user_abi.h"
#include "fpu.h"
#include "../../security.h"
#include "fd.h"
_Static_assert(USER_CODE == MM_USER_START && USER_LIMIT == MM_USER_END, "shared user layout");
_Static_assert(USER_MMAP_BASE < USER_MMAP_LIMIT && USER_MMAP_LIMIT <= USER_HEAP_BASE &&
               USER_HEAP_BASE < USER_HEAP_LIMIT && USER_HEAP_LIMIT < USER_STACK_BASE,
               "C2 runtime region separation");
/* In stack order, shared by interrupt entry and initial IRETQ. All GPRs plus
 * hardware return state. Vector/error are normalized by interrupt assembly. */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector, error, rip, cs, flags, rsp, ss;
} UserFrame;
#define FRAME_GPR(field, index) _Static_assert(offsetof(UserFrame, field) == (index)*8, "frame " #field)
FRAME_GPR(r15, 0); FRAME_GPR(r14, 1); FRAME_GPR(r13, 2); FRAME_GPR(r12, 3);
FRAME_GPR(r11, 4); FRAME_GPR(r10, 5); FRAME_GPR(r9, 6); FRAME_GPR(r8, 7);
FRAME_GPR(rdi, 8); FRAME_GPR(rsi, 9); FRAME_GPR(rbp, 10); FRAME_GPR(rbx, 11);
FRAME_GPR(rdx, 12); FRAME_GPR(rcx, 13); FRAME_GPR(rax, 14);
#undef FRAME_GPR
_Static_assert(offsetof(UserFrame, vector) == USER_FRAME_VECTOR, "frame vector");
_Static_assert(offsetof(UserFrame, rip) == USER_FRAME_RIP, "frame RIP");
_Static_assert(offsetof(UserFrame, cs) == USER_FRAME_CS, "frame CS");
_Static_assert(offsetof(UserFrame, flags) == USER_FRAME_FLAGS, "frame flags");
_Static_assert(offsetof(UserFrame, rsp) == USER_FRAME_RSP, "frame RSP");
_Static_assert(offsetof(UserFrame, ss) == USER_FRAME_SS, "frame SS");
_Static_assert(sizeof(UserFrame) == USER_FRAME_SIZE, "frame size");
typedef enum { USER_COPY_OK, USER_COPY_FAULT, USER_COPY_TOO_LONG } UserCopyResult;
UserCopyResult user_range_check(const AddressSpace *space, virt_addr_t address, size_t length, int write);
UserCopyResult copy_from_user64(const AddressSpace *space, void *destination, virt_addr_t source, size_t length);
UserCopyResult copy_to_user64(const AddressSpace *space, virt_addr_t destination, const void *source, size_t length);
UserCopyResult copy_string_from_user64(const AddressSpace *space, char *destination,
                                      virt_addr_t source, size_t maximum);
/* Tests effective permissions across every level, not just the leaf PTE. */
VmResult vmm64_user_page(const AddressSpace *space, virt_addr_t address, unsigned required, Mapping *out);
/* Process lifetime and runnable thread state are distinct contracts. */
typedef enum { PROCESS_BUILDING, PROCESS_ACTIVE, PROCESS_EXITED, PROCESS_FAULTED,
               PROCESS_KILLED } Process64State;
typedef enum { THREAD_BUILDING=32, THREAD_READY, THREAD_RUNNING, THREAD_BLOCKED,
               THREAD_EXITED, THREAD_FAULTED, THREAD_KILLED } Thread64State;
struct Process64;
/* Scheduling state belongs to a thread. At this checkpoint each process owns
 * one embedded primary TCB; scheduler queues and CPU context store Thread64*. */
typedef struct {
    uint64_t tid;
    struct Process64 *owner;
    GuardedStack kernel_stack, user_stack;
    _Alignas(16) uint8_t fpu_state[FPU64_STATE_SIZE];
    UserFrame frame;
    Thread64State state;
    int exit_status;
    uint64_t ticks, dispatches, tick_limit;
    unsigned managed, queued, kill_pending;
    unsigned stopped, stop_pending;
    uint64_t wake_tick;
} Thread64;
/* Descriptor backing overrides: an entry in files[]/pipe_index[]/console_alias[]
 * takes precedence over the standard stream kind, which is how dup/dup2 and
 * shell redirection move stdin/stdout/stderr onto files or pipes. */
#define PIPE64_NONE 0xFF
#define IO64_TTY_READ 0
#define IO64_PIPE_READ 1
#define IO64_PIPE_WRITE 2
/* One explicit kernel-owned record per live anonymous mapping; no VM-area tree. */
typedef struct { virt_addr_t base; uint64_t pages; } Mmap64Record;
/* Versioned wait-status copy written to a waiting parent (C6). */
typedef struct {
    uint32_t version, kind;
    int32_t code;      /* exit status, fault vector or kill reason */
    uint32_t reserved;
    uint64_t address;  /* fault address for kind FAULTED, else zero */
} UserWait64;
_Static_assert(sizeof(UserWait64) == 24, "wait ABI size");
typedef struct {
    uint64_t handler, mask, flags, restorer;
} UserSignalAction64;
_Static_assert(sizeof(UserSignalAction64) == 32, "signal action ABI size");
#define USER_SIGNAL_DFL 0
#define USER_SIGNAL_IGN 1
#define USER_SIGNAL_FRAME_MAGIC UINT64_C(0x504f4c4c53494746)
typedef struct Process64 {
    uint64_t pid;
    AddressSpace space;
    Thread64 thread;
    Credentials credentials;
    uint64_t admin_spawn_session; /* One authenticated spawn; never a user-set credential. */
    char admin_spawn_path[USER_PATH_MAX];
    Process64State state;
    int exit_status;
    size_t slot;
    uint64_t fault_address;
    uint64_t termination_vector;
    uint64_t parent_pid;            /* 0 = kernel-owned, auto-reaped */
    uint64_t pgid;                  /* process group leader PID */
    unsigned zombie;                /* heavy state reclaimed, awaiting wait */
    unsigned waiting, wait_collected;
    uint64_t wait_pid;              /* 0 = wait for any child */
    uint64_t wait_status_va;        /* parent's saved status pointer */
    UserSignalAction64 signal_actions[32];
    uint64_t signal_pending, signal_blocked;
    uint64_t signal_frame_va, signal_frame_cookie, signal_saved_mask;
    UserFrame signal_saved_frame;
    _Alignas(16) uint8_t signal_saved_fpu[FPU64_STATE_SIZE];
    unsigned signal_active;
    Descriptor64 fds[USER_FD_LIMIT];
    unsigned io_waiting;            /* suspended in a console or pipe transfer */
    uint8_t io_kind;                /* IO64_* transfer kind */
    uint8_t io_fd;                  /* descriptor of the blocked transfer */
    virt_addr_t io_va;              /* saved user buffer */
    uint64_t io_count;              /* saved byte count */
    virt_addr_t heap_base, heap_break, heap_limit; /* owned RW/NX brk region */
    virt_addr_t mmap_next; /* bump cursor inside the reserved mmap region */
    Mmap64Record mmap[USER_MMAP_MAX]; /* exact anonymous mapping ownership */
    char name[56]; /* owned diagnostic basename, PID remains identity */
    char cwd[USER_PATH_MAX]; /* owned canonical absolute path; initially / */
} Process64;
_Static_assert(offsetof(Process64, thread.fpu_state) % 16 == 0, "aligned FPU context");
typedef struct {
    uint64_t pid;
    Process64State state;
    int exit_status;
    UserFrame frame;
    uint64_t fault_address;
} Process64Result;
Process64 *process64_create(unsigned payload_mode, uint64_t private_value);
int process64_destroy(Process64 *process);
void process64_end_session(uint64_t session);
int process64_reclaim(Process64 *process); /* heavy state -> waitable zombie */
int process64_slots_full(void);
size_t process64_capacity(void);
int process64_trap(UserFrame *frame, uint64_t cr2);
int64_t process64_signal_send(uint64_t pid, int signal);
int64_t process64_signal_group(uint64_t pgid, int signal);
int64_t process64_signal_target(Process64 *sender, int64_t target, int signal);
int proc64_dispatch(Process64 *process, UserFrame *frame); /* spawn, wait, signals and groups */
#ifdef SELFTEST
unsigned process64_debug_slots(void);
unsigned process64_debug_zombies(void);
#endif
uintptr_t kernel64_set_rsp0(uintptr_t top);
uintptr_t kernel64_get_rsp0(void);
#ifdef SELFTEST
int process64_run(Process64 *process, Process64Result *result); /* consumes; synchronous regression only */
void process64_demo(void);
void process64_selftest(void);
void usercopy_selftest(void);
#endif
#endif
