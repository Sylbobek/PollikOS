#include "process.h"
#include "syscall.h"
#include "elf.h"
#include "vfs.h"
#include "klog.h"
#include "pmm.h"

typedef struct __attribute__((packed)) {
    u16 limit;
    u32 base;
} TablePtr;

typedef struct __attribute__((packed)) {
    u16 low, selector;
    u8 zero, flags;
    u16 high;
} Gate;

typedef struct __attribute__((packed)) {
    u16 limit, low;
    u8 mid, access, flags, high;
} Segment;

static Gate idt[256];
static Segment gdt[6];
static u32 tss[26];
static ProcessControlBlock processes[MAX_PROCESSES];
static u8 stacks[2][8192] __attribute__((aligned(16)));
static int current = 0, fault_request = 0;
static int g_spawn_verbose = 1; /* stress loops silence per-spawn diagnostics */
volatile u32 ticks = 0;

extern void *isr_table[];
extern void load_gdt(TablePtr *);
extern u8 user_program_start[], user_program_end[];

static void hex_to_str(char *buf, u32 val) {
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        u8 nib = (val >> (i * 4)) & 0xF;
        buf[2 + (7 - i)] = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    }
    buf[10] = '\0';
}

static void segment(int i, u32 base, u32 limit, u8 access, u8 flags) {
    gdt[i] = (Segment){
        (u16)(limit & 65535), (u16)(base & 65535), (u8)((base >> 16) & 255),
        access, (u8)((limit >> 16) | (flags & 0xf0)),
        (u8)(base >> 24)
    };
}

int process_get_current_pid(void) {
    return current;
}

const char *process_get_current_name(void) {
    if (current >= 0 && current < MAX_PROCESSES && processes[current].alive)
        return processes[current].name;
    return "unknown";
}

void **process_get_current_fd_table(void) {
    return (void **)processes[current].fd_table;
}

int process_is_current_flat(void) {
    return processes[current].is_flat;
}

void process_report_legacy(u32 count) {
    processes[current].reports = count;
}

int process_get_fault_request(void) {
    return fault_request;
}

void process_exit_current(int exit_code) {
    if (current <= 0)
        return;

    processes[current].exit_code = exit_code;
    processes[current].alive = 0;
    processes[current].state = PROC_STATE_DEAD;

    char nb[12], ec[12];
    number(nb, current);
    number(ec, exit_code);
    serial("[PROC] exit pid="); serial(nb); serial(" code="); serial(ec); serial("\n");
    serial("[PROC] PID "); serial(nb); serial(" exited code="); serial(ec); serial("\n");
    klog_dec(KLOG_CAT_PROC, "Process terminated: PID ", current);
}

uintptr_t process_sbrk(u32 bytes) {
    if (current <= 0 || !processes[current].is_flat)
        return 0;

    uintptr_t old_brk = processes[current].heap_end;
    if (bytes == 0)
        return old_brk;

    uintptr_t new_brk = old_brk + bytes;
    if (new_brk > 0xB0000000u || new_brk < old_brk)
        return 0;

    uintptr_t curr_page = (old_brk + 4095u) & ~0xFFFu;
    uintptr_t target_page = (new_brk + 4095u) & ~0xFFFu;

    for (uintptr_t v = curr_page; v < target_page; v += PAGE_SIZE) {
        allocate_page(processes[current].page_directory, v, PAGE_PRESENT | PAGE_USER | PAGE_RW);
    }

    processes[current].heap_end = new_brk;
    return old_brk;
}

void process_sleep_current(u32 ticks_count) {
    if (current <= 0)
        return;
    processes[current].sleep_until_tick = ticks + ticks_count;
    processes[current].state = PROC_STATE_SLEEPING;
}

void process_block_current(void) {
    if (current <= 0)
        return;
    processes[current].state = PROC_STATE_BLOCKED;
}

void process_unblock(int pid) {
    if (pid >= 0 && pid < MAX_PROCESSES && processes[pid].alive) {
        if (processes[pid].state == PROC_STATE_BLOCKED) {
            processes[pid].state = PROC_STATE_READY;
        }
    }
}

int process_send_event(int pid, const SystemEvent *ev) {
    if (pid < 0 || pid >= MAX_PROCESSES || !processes[pid].alive || !ev)
        return 0;
    ProcessControlBlock *pcb = &processes[pid];
    if (pcb->event_count >= MAX_PROCESS_EVENTS)
        return 0;

    pcb->event_queue[pcb->event_tail] = *ev;
    pcb->event_tail = (pcb->event_tail + 1) % MAX_PROCESS_EVENTS;
    pcb->event_count++;

    if (pcb->state == PROC_STATE_BLOCKED) {
        pcb->state = PROC_STATE_READY;
    }
    return 1;
}

int process_poll_event(int pid, SystemEvent *out_ev) {
    if (pid < 0 || pid >= MAX_PROCESSES || !processes[pid].alive || !out_ev)
        return 0;
    ProcessControlBlock *pcb = &processes[pid];
    if (pcb->event_count <= 0)
        return 0;

    *out_ev = pcb->event_queue[pcb->event_head];
    pcb->event_head = (pcb->event_head + 1) % MAX_PROCESS_EVENTS;
    pcb->event_count--;
    return 1;
}

int process_wait_event(int pid, SystemEvent *out_ev) {
    if (pid < 0 || pid >= MAX_PROCESSES || !processes[pid].alive || !out_ev)
        return 0;
    if (process_poll_event(pid, out_ev))
        return 1;
    process_block_current();
    return 0;
}

int process_send_ipc(int src_pid, int dest_pid, const void *data, u32 len) {
    if (dest_pid < 0 || dest_pid >= MAX_PROCESSES || !processes[dest_pid].alive)
        return -1;
    ProcessControlBlock *dest = &processes[dest_pid];
    if (dest->ipc_count >= MAX_PROCESS_IPC)
        return -2;

    u32 copy_len = len;
    if (copy_len > MAX_IPC_MSG_LEN)
        copy_len = MAX_IPC_MSG_LEN;

    IpcMessage *msg = &dest->ipc_queue[dest->ipc_tail];
    msg->src_pid = src_pid;
    msg->dest_pid = dest_pid;
    msg->len = copy_len;
    if (data && copy_len > 0)
        memcpy(msg->data, data, copy_len);

    dest->ipc_tail = (dest->ipc_tail + 1) % MAX_PROCESS_IPC;
    dest->ipc_count++;

    if (dest->state == PROC_STATE_BLOCKED) {
        dest->state = PROC_STATE_READY;
    }
    return (int)copy_len;
}

int process_recv_ipc(int pid, int *out_src, void *out_buf, u32 max_len) {
    if (pid < 0 || pid >= MAX_PROCESSES || !processes[pid].alive)
        return -1;
    ProcessControlBlock *pcb = &processes[pid];
    if (pcb->ipc_count <= 0)
        return -1;

    IpcMessage *msg = &pcb->ipc_queue[pcb->ipc_head];
    if (out_src)
        *out_src = msg->src_pid;

    u32 to_copy = msg->len;
    if (to_copy > max_len)
        to_copy = max_len;

    if (out_buf && to_copy > 0)
        memcpy(out_buf, msg->data, to_copy);

    pcb->ipc_head = (pcb->ipc_head + 1) % MAX_PROCESS_IPC;
    pcb->ipc_count--;
    return (int)to_copy;
}

int process_spawn_elf(const char *name, const u8 *elf_data, u32 elf_size) {
    int slot = -1;
    for (int i = 3; i < MAX_PROCESSES; i++) {
        if (!processes[i].alive && processes[i].state == PROC_STATE_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 3; i < MAX_PROCESSES; i++) {
            if (!processes[i].alive) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        KLOG_ERROR(KLOG_CAT_PROC, "process_spawn_elf: process table full");
        return -1;
    }

    page_directory_t *pd = vmm_create_address_space();
    if (!pd) {
        KLOG_ERROR(KLOG_CAT_PROC, "process_spawn_elf: vmm_create_address_space failed");
        return -1;
    }

    uintptr_t entry = 0, heap_start = 0;
    if (!elf_load(pd, elf_data, elf_size, &entry, &heap_start)) {
        KLOG_ERROR(KLOG_CAT_PROC, "process_spawn_elf: elf_load failed");
        vmm_destroy_address_space(pd);
        return -1;
    }

    /* Allocate user stack: USER_STACK_SIZE below USER_STACK_TOP, guard page beneath */
    for (uintptr_t v = USER_STACK_BOTTOM; v < USER_STACK_TOP; v += PAGE_SIZE) {
        uintptr_t p = pmm_alloc_page();
        if (!p) {
            vmm_destroy_address_space(pd);
            return -1;
        }
        memset((void *)p, 0, PAGE_SIZE);
        if (!map_page(pd, v, p, PAGE_PRESENT | PAGE_USER | PAGE_RW)) {
            pmm_free_page(p);
            vmm_destroy_address_space(pd);
            KLOG_ERROR(KLOG_CAT_PROC, "process_spawn_elf: user stack mapping refused");
            return -1;
        }
    }
    vmm_set_guard_page(pd, USER_GUARD_PAGE);

    /* Allocate kernel stack: 8 KiB */
    uintptr_t kstack_phys = pmm_alloc_pages(2);
    if (!kstack_phys) {
        vmm_destroy_address_space(pd);
        return -1;
    }
    memset((void *)kstack_phys, 0, 8192);
    uintptr_t kstack_top = kstack_phys + 8192;

    ProcessFrame *f = (ProcessFrame *)(kstack_top - sizeof(ProcessFrame));
    memset(f, 0, sizeof(ProcessFrame));
    f->ds = f->es = f->fs = f->gs = f->ss = 0x23;
    f->cs = 0x1B;
    f->eip = entry;
    f->eflags = 0x202; /* IF=1 */
    f->useresp = USER_STACK_TOP - 16;

    processes[slot].frame = f;
    processes[slot].pid = slot;
    processes[slot].state = PROC_STATE_READY;
    processes[slot].alive = 1;
    processes[slot].paused = 0;
    processes[slot].is_flat = 1;
    processes[slot].page_directory = pd;
    processes[slot].kernel_stack_bottom = kstack_phys;
    processes[slot].kernel_stack_top = kstack_top;
    processes[slot].heap_start = heap_start;
    processes[slot].heap_end = heap_start;
    processes[slot].switches = 0;
    processes[slot].reports = 0;
    processes[slot].sleep_until_tick = 0;
    processes[slot].event_head = 0;
    processes[slot].event_tail = 0;
    processes[slot].event_count = 0;
    processes[slot].ipc_head = 0;
    processes[slot].ipc_tail = 0;
    processes[slot].ipc_count = 0;

    extern void vfs_init_process_fds(vfs_file_t **table);
    vfs_init_process_fds((vfs_file_t **)processes[slot].fd_table);

    int idx = 0;
    while (name[idx] && idx < 23) {
        processes[slot].name[idx] = name[idx];
        idx++;
    }
    processes[slot].name[idx] = 0;

    if (g_spawn_verbose) {
        char nb[12], h_entry[12], h_esp[12], h_cr3[12], h_lo[12], h_hi[12];
        number(nb, slot);
        hex_to_str(h_entry, entry);
        hex_to_str(h_esp, USER_STACK_TOP - 16);
        hex_to_str(h_cr3, (uintptr_t)pd);
        hex_to_str(h_lo, USER_STACK_BOTTOM);
        hex_to_str(h_hi, USER_GUARD_PAGE);

        serial("[PROC] create pid="); serial(nb); serial("\n");
        serial("[PROC] page directory="); serial(h_cr3); serial("\n");
        serial("[PROC] PID "); serial(nb); serial(" address space created\n");
        serial("[ELF] entry="); serial(h_entry); serial("\n");
        serial("[PROC] stack bottom="); serial(h_lo); serial(" top=0xc0000000 guard="); serial(h_hi); serial("\n");
        serial("[PROC] starting pid="); serial(nb); serial("\n");
        serial("[PROC] entry="); serial(h_entry); serial("\n");
        serial("[PROC] user esp="); serial(h_esp); serial("\n");
        serial("[PROC] cr3="); serial(h_cr3); serial("\n");
        klog_dec(KLOG_CAT_PROC, "Spawned ELF process PID: ", slot);
    }
    return slot;
}

int process_spawn_elf_path(const char *path) {
    serial("[PROC] Loading ELF ");
    serial(path);
    serial("\n");
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0 || st.size == 0) {
        KLOG_WARN(KLOG_CAT_PROC, "process_spawn_elf_path: file not found or empty");
        return -1;
    }

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        KLOG_WARN(KLOG_CAT_PROC, "process_spawn_elf_path: failed to open file");
        return -1;
    }

    u8 *buf = (u8 *)kmalloc(st.size);
    if (!buf) {
        vfs_close(fd);
        KLOG_ERROR(KLOG_CAT_PROC, "process_spawn_elf_path: out of kernel memory for binary");
        return -1;
    }

    int bytes = vfs_read(fd, buf, st.size);
    vfs_close(fd);

    if (bytes != (int)st.size) {
        kfree(buf);
        KLOG_ERROR(KLOG_CAT_PROC, "process_spawn_elf_path: short read");
        return -1;
    }

    const char *name = path;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/')
            name = &path[i + 1];
    }

    int pid = process_spawn_elf(name, buf, st.size);
    kfree(buf);
    return pid;
}

extern u8 _binary_build_fault_test_elf_start[];
extern u8 _binary_build_fault_test_elf_end[];
extern u8 _binary_build_fault_kernel_elf_start[];
extern u8 _binary_build_fault_kernel_elf_end[];
extern u8 _binary_build_fault_stack_elf_start[];
extern u8 _binary_build_fault_stack_elf_end[];

extern u8 _binary_build_hello_elf_start[];
extern u8 _binary_build_hello_elf_end[];

#define STRESS_CYCLES 100
static int g_phase2_step = 0;
static int g_test_pid = 3;
static u32 g_stress_free_before = 0;
static int g_stress_cycles = 0;
static int g_stress_failures = 0;

void phase2_poll(void) {
    if (ticks < 50)
        return;

    if (g_phase2_step == 0) {
        if (!processes[g_test_pid].alive) {
            g_phase2_step = 1;
            u32 sz = (u32)(_binary_build_fault_test_elf_end - _binary_build_fault_test_elf_start);
            serial("[PROC] Loading ELF fault_test.elf\n");
            g_test_pid = process_spawn_elf("fault_test", _binary_build_fault_test_elf_start, sz);
        }
    } else if (g_phase2_step == 1) {
        if (!processes[g_test_pid].alive) {
            g_phase2_step = 2;
            u32 sz = (u32)(_binary_build_fault_kernel_elf_end - _binary_build_fault_kernel_elf_start);
            serial("[PROC] Loading ELF fault_kernel.elf\n");
            g_test_pid = process_spawn_elf("fault_kernel", _binary_build_fault_kernel_elf_start, sz);
        }
    } else if (g_phase2_step == 2) {
        if (!processes[g_test_pid].alive) {
            g_phase2_step = 3;
            u32 sz = (u32)(_binary_build_fault_stack_elf_end - _binary_build_fault_stack_elf_start);
            serial("[PROC] Loading ELF fault_stack.elf\n");
            g_test_pid = process_spawn_elf("fault_stack", _binary_build_fault_stack_elf_start, sz);
        }
    } else if (g_phase2_step == 3) {
        if (!processes[g_test_pid].alive) {
            /* Real spawn/exit stress: each cycle loads the embedded hello ELF
             * into a fresh address space, runs it in Ring 3 until it exits, and
             * lets the scheduler reap it. The PMM balance is checked only after
             * every process has been reaped, so leaked ELF pages, stacks, page
             * tables or directories show up as a mismatch. */
            g_phase2_step = 4;
            g_stress_free_before = pmm_get_free_pages_count();
            g_stress_cycles = 0;
            g_stress_failures = 0;
            g_spawn_verbose = 0;
            serial("[TEST] Running 100 real spawn/exit cycles (hello ELF, Ring 3) for PMM leak verification...\n");
        }
    } else if (g_phase2_step == 4) {
        if (g_test_pid >= 0 && processes[g_test_pid].state != PROC_STATE_UNUSED)
            return; /* previous cycle still running or awaiting reap */
        if (g_stress_cycles < STRESS_CYCLES) {
            u32 sz = (u32)(_binary_build_hello_elf_end - _binary_build_hello_elf_start);
            g_test_pid = process_spawn_elf("hello", _binary_build_hello_elf_start, sz);
            if (g_test_pid < 0)
                g_stress_failures++;
            g_stress_cycles++;
            return;
        }
        g_phase2_step = 5;
        g_spawn_verbose = 1;
        u32 free_after = pmm_get_free_pages_count();
        klog_dec(KLOG_CAT_PROC, "[TEST] spawn cycles completed: ", g_stress_cycles);
        klog_dec(KLOG_CAT_PROC, "[TEST] spawn failures: ", g_stress_failures);
        klog_dec(KLOG_CAT_PROC, "[TEST] PMM free pages before: ", g_stress_free_before);
        klog_dec(KLOG_CAT_PROC, "[TEST] PMM free pages after:  ", free_after);
        if (g_stress_free_before == free_after && g_stress_failures == 0) {
            serial("[TEST] desktop alive\n");
            serial("[TEST] scheduler alive\n");
            serial("[TEST] PMM no leak\n");
            serial("[TEST] PHASE 2 PASS\n");
        } else {
            serial("[TEST] PHASE 2 FAIL: PMM leak or spawn failure detected!\n");
        }
    }
}

static void reap_dead_processes(int keep_running);

static ProcessFrame *schedule(ProcessFrame *frame) {
    processes[current].frame = frame;

    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int next = (current + i) % MAX_PROCESSES;
        if (processes[next].alive && !processes[next].paused) {
            if (processes[next].state == PROC_STATE_BLOCKED) {
                if (processes[next].event_count > 0 || processes[next].ipc_count > 0) {
                    processes[next].state = PROC_STATE_READY;
                } else {
                    continue;
                }
            }
            if (processes[next].state == PROC_STATE_SLEEPING) {
                if (ticks >= processes[next].sleep_until_tick) {
                    processes[next].state = PROC_STATE_READY;
                } else {
                    continue;
                }
            }
            __asm__ volatile("fnsave %0; fwait" : "=m"(processes[current].fpu_state));
            __asm__ volatile("frstor %0" :: "m"(processes[next].fpu_state));

            current = next;
            processes[next].switches++;

            if (processes[next].is_flat && processes[next].switches == 1) {
                serial("[PROC] entering Ring 3\n");
            }

            if (processes[next].is_flat) {
                /* Flat 4 GiB Ring 3 segments */
                segment(3, 0, 0xFFFFF, 0xFA, 0xC0);
                segment(4, 0, 0xFFFFF, 0xF2, 0xC0);
                tss[1] = (u32)processes[next].kernel_stack_top;
                vmm_switch_address_space(processes[next].page_directory);
            } else if (next > 0) {
                /* Legacy 64 KiB segmented worker */
                u32 base = 0x800000 + (next - 1) * 0x10000;
                segment(3, base, 0xFFFF, 0xFA, 0x40);
                segment(4, base, 0xFFFF, 0xF2, 0x40);
                tss[1] = (u32)&stacks[next - 1][8192];
                vmm_switch_address_space(vmm_get_kernel_directory());
            } else {
                /* Kernel / Desktop (PID 0) */
                tss[1] = 0x9FC00;
                vmm_switch_address_space(vmm_get_kernel_directory());
            }

            /* Release every dead process except the one about to run. This
             * also covers processes killed while they were not scheduled. */
            reap_dead_processes(next);

            return processes[next].frame;
        }
    }

    return frame;
}

/* Free the resources of dead processes. Only flat ELF processes own PMM
 * frames (address space, kernel stack); legacy workers use static kernel
 * memory that must never be handed back to the PMM. Runs with interrupts
 * disabled from the scheduler; the dying process's own kernel stack may still
 * be in use until interrupt_common switches ESP, which is why this is called
 * only after the next frame has been selected. */
static void reap_dead_processes(int keep_running) {
    extern void vfs_close_process_fds(vfs_file_t **table);
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (i == keep_running || processes[i].state != PROC_STATE_DEAD)
            continue;
        vfs_close_process_fds((vfs_file_t **)processes[i].fd_table);
        if (processes[i].is_flat) {
            if (processes[i].page_directory) {
                vmm_destroy_address_space(processes[i].page_directory);
                processes[i].page_directory = 0;
            }
            if (processes[i].kernel_stack_bottom) {
                pmm_free_pages(processes[i].kernel_stack_bottom, 2);
                processes[i].kernel_stack_bottom = 0;
                processes[i].kernel_stack_top = 0;
            }
        }
        processes[i].alive = 0;
        processes[i].state = PROC_STATE_UNUSED;
    }
}

ProcessFrame *process_schedule(ProcessFrame *frame) {
    return schedule(frame);
}

ProcessFrame *interrupt_dispatch(ProcessFrame *f) {
    if (f->vector == 32) {
        ticks++;
        outb(0x20, 0x20);
        return schedule(f);
    }

    if (f->vector == 128) {
        return (ProcessFrame *)syscall_dispatch(f);
    }

    if (f->vector < 32) {
        if (f->vector == 14) {
            vmm_page_fault_handler(f);
            if (current && (f->cs & 3) == 3) {
                process_exit_current(-1);
                return schedule(f);
            }
        }
        if (current && (f->cs & 3) == 3) {
            char n[12];
            number(n, f->vector);
            serial("PROCESS isolated fault vector=");
            serial(n);
            serial("\n");
            serial("[PROC] Isolated fault vector=");
            serial(n);
            serial("\n");
            process_exit_current(-1);
            return schedule(f);
        }
        panic("Unhandled Kernel Exception", f);
    }

    if (f->vector >= 40 && f->vector < 48)
        outb(0xa0, 0x20);
    if (f->vector >= 32 && f->vector < 48)
        outb(0x20, 0x20);

    return f;
}

static void create_legacy_worker(int pid) {
    u8 *base = (u8 *)(0x800000 + (pid - 1) * 0x10000);
    memset(base, 0, 65536);
    memcpy(base, user_program_start, user_program_end - user_program_start);

    ProcessFrame *f = (ProcessFrame *)(&stacks[pid - 1][8192] - sizeof(ProcessFrame));
    memset(f, 0, sizeof(ProcessFrame));
    f->ds = f->es = f->fs = f->gs = f->ss = 0x23;
    f->cs = 0x1b;
    f->eflags = 0x202;
    f->useresp = 0xfff0;

    processes[pid].frame = f;
    processes[pid].pid = pid;
    processes[pid].state = PROC_STATE_READY;
    processes[pid].alive = 1;
    processes[pid].paused = 0;
    processes[pid].is_flat = 0;
    processes[pid].reports = 0;
    processes[pid].switches = 0;
    processes[pid].page_directory = vmm_get_kernel_directory();
    processes[pid].kernel_stack_top = (uintptr_t)&stacks[pid - 1][8192];
    processes[pid].kernel_stack_bottom = (uintptr_t)&stacks[pid - 1][0];

    processes[pid].name[0] = 'W';
    processes[pid].name[1] = 'O';
    processes[pid].name[2] = 'R';
    processes[pid].name[3] = 'K';
    processes[pid].name[4] = 'E';
    processes[pid].name[5] = 'R';
    processes[pid].name[6] = 0;
}

void process_init(void) {
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~12u;
    __asm__ volatile("mov %0, %%cr0; fninit" :: "r"(cr0));

    for (int i = 0; i < MAX_PROCESSES; i++) {
        __asm__ volatile("fnsave %0; fwait" : "=m"(processes[i].fpu_state));
    }

    segment(1, 0, 0xFFFFF, 0x9A, 0xC0); /* 0x08: Kernel Code */
    segment(2, 0, 0xFFFFF, 0x92, 0xC0); /* 0x10: Kernel Data */
    segment(3, 0x800000, 0xFFFF, 0xFA, 0x40); /* 0x1B: User Code (legacy start) */
    segment(4, 0x800000, 0xFFFF, 0xF2, 0x40); /* 0x23: User Data (legacy start) */

    tss[2] = 0x10;
    tss[25] = sizeof(tss) << 16;
    segment(5, (u32)tss, sizeof(tss) - 1, 0x89, 0); /* 0x28: TSS */

    TablePtr gp = {sizeof(gdt) - 1, (u32)gdt};
    load_gdt(&gp);
    __asm__ volatile("ltr %0" :: "r"((u16)0x28));

    for (int i = 0; i < 256; i++) {
        u32 addr = (u32)isr_table[i < 48 ? i : 13];
        if (i == 128)
            addr = (u32)isr_table[48];
        idt[i] = (Gate){(u16)(addr & 65535), 8, 0, (u8)(i == 128 ? 0xEE : 0x8E), (u16)(addr >> 16)};
    }
    TablePtr ip = {sizeof(idt) - 1, (u32)idt};
    __asm__ volatile("lidt %0" :: "m"(ip));

    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, 32);
    outb(0xa1, 40);
    outb(0x21, 4);
    outb(0xa1, 2);
    outb(0x21, 1);
    outb(0xa1, 1);
    outb(0x21, 0xfe);
    outb(0xa1, 0xff);

    outb(0x43, 0x36);
    outb(0x40, 9943 & 255);
    outb(0x40, 9943 >> 8);

    processes[0].alive = 1;
    processes[0].state = PROC_STATE_RUNNING;
    processes[0].pid = 0;
    processes[0].is_flat = 0;
    processes[0].page_directory = vmm_get_kernel_directory();
    processes[0].name[0] = 'D'; processes[0].name[1] = 'E'; processes[0].name[2] = 'S';
    processes[0].name[3] = 'K'; processes[0].name[4] = 'T'; processes[0].name[5] = 'O';
    processes[0].name[6] = 'P'; processes[0].name[7] = 0;

    create_legacy_worker(1);
    create_legacy_worker(2);

    extern void vfs_init_process_fds(vfs_file_t **table);
    vfs_init_process_fds((vfs_file_t **)processes[0].fd_table);

    syscall_init();

    /* Spawn /bin/hello from disk */
    process_spawn_elf_path("/bin/hello");

    KLOG_INFO(KLOG_CAT_PROC, "Process manager and scheduler v2 ready");
    __asm__ volatile("sti");
}

static void append(char **p, const char *s) {
    while (*s)
        *(*p)++ = *s++;
    **p = 0;
}

void process_list(char *out) {
    char *p = out, n[12];
    append(&p, "PID NAME       STATE     WORK / SLICES\n0   DESKTOP    KERNEL\n");
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (!processes[i].alive && !processes[i].switches)
            continue;
        number(n, i);
        append(&p, n);
        append(&p, "   ");
        append(&p, processes[i].name[0] ? processes[i].name : "WORKER");
        append(&p, "     ");
        append(&p, !processes[i].alive   ? "STOPPED "
                   : processes[i].paused ? "PAUSED  "
                                         : "RUNNING ");
        number(n, processes[i].reports);
        append(&p, n);
        append(&p, " / ");
        number(n, processes[i].switches);
        append(&p, n);
        append(&p, "\n");
    }
}

int process_action(int pid, int action) {
    if (pid < 1 || pid >= MAX_PROCESSES)
        return 0;
    __asm__ volatile("cli");
    if (action == 0)
        processes[pid].paused = 1;
    if (action == 1)
        processes[pid].paused = 0;
    if (action == 2 && processes[pid].alive) {
        /* Killed processes are reaped by the scheduler like exited ones. */
        processes[pid].alive = 0;
        processes[pid].exit_code = -9;
        processes[pid].state = PROC_STATE_DEAD;
    }
    if (action == 3 && pid <= 2) {
        if (fault_request == pid)
            fault_request = 0;
        create_legacy_worker(pid);
    }
    __asm__ volatile("sti");
    return 1;
}

void process_fault_test(void) {
    fault_request = 1;
    processes[1].paused = 0;
}
