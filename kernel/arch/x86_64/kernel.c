/* Deliberately isolated from the i386 system.h ABI during migration. */
#include <stdint.h>
#include <stddef.h>
#include "memory.h"
#include "user.h"
#include "process_internal.h"
#include "elf64.h"
#include "scheduler.h"
#include "launch.h"
#include "auth64.h"
#include "window.h"
#include "fs_platform.h"
#include "file.h"
#include "heap.h"
#include "rtc64.h"
#include "syscall.h"
#include "tty.h"
#include "console_fb.h"
#include "network.h"
#include "../../hal.h"
void c3_demo(void);
void c4_demo(void);
void c5_demo(void);
void c6_demo(void);
void c7_demo(void);
#ifdef SELFTEST
void c3_selftest(void);
void c4_selftest(void);
void c5_selftest(void);
void c6_selftest(void);
void c7_selftest(void);
void selfhost_selftest(void);
#endif

_Static_assert(sizeof(uintptr_t) == 8, "x86_64 pointer ABI required");
typedef struct __attribute__((packed)) {
    uint16_t low, selector;
    uint8_t ist, attributes;
    uint16_t middle;
    uint32_t high, reserved;
} Gate;
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap;
} Tss;
typedef UserFrame Frame;
_Static_assert(sizeof(Gate) == 16, "IDT gate layout");
_Static_assert(sizeof(Tss) == 104, "TSS layout");
_Static_assert(offsetof(Frame, vector) == 120, "assembly frame layout");
static Gate idt[256] __attribute__((aligned(16)));
static Tss tss;
static GuardedStack main_stack, emergency_stack, interrupt_stack;
extern uintptr_t isr64_table[256];
extern uint64_t gdt64[7], boot_pml4[512], boot_pt[512];
extern char boot_stack_top[], boot_stack_guard[];
extern char df_stack_top[], df_stack_guard[], nmi_stack_top[];

static void out8(uint16_t port, uint8_t value) {
    hal_port_write8(port, value);
}
static uint8_t in8(uint16_t port) {
    return hal_port_read8(port);
}
static void serial64(const char *text) {
    while (*text) {
        while (!(in8(0x3fd) & 0x20)) {}
        out8(0x3f8, (uint8_t)*text++);
    }
}
static void hex64(uint64_t value) {
    char text[19] = "0x0000000000000000";
    for (unsigned i = 0; i < 16; ++i)
        text[17-i] = "0123456789abcdef"[(value >> (i*4)) & 15];
    serial64(text);
}
__attribute__((noreturn)) static void halt(void) {
    hal_cpu_halt_forever();
}
__attribute__((noreturn)) static void fail(const char *message) {
    serial64("[X64] FAIL: ");
    serial64(message);
    serial64("\n");
    halt();
}
static void require(int condition, const char *message) {
    if (!condition) fail(message);
}
void memory_log(const char *message) { serial64(message); }
void memory_hex(uint64_t value) { hex64(value); }
void memory_require(int condition, const char *message) { require(condition, message); }
void memory_panic(const char *message) { fail(message); }
uintptr_t kernel64_set_rsp0(uintptr_t top) {
    uintptr_t previous = tss.rsp[0];
    tss.rsp[0] = top;
    syscall64_stack(top);
    return previous;
}
uintptr_t kernel64_get_rsp0(void) { return tss.rsp[0]; }
static int gui_session_active;
void kernel64_debug_bytes(const char *data, size_t length) {
    if (!gui_session_active) console_fb_write(data, length);
    for (size_t i = 0; i < length; ++i) {
        while (!(in8(0x3fd) & 0x20)) {}
        out8(0x3f8, (uint8_t)data[i]);
    }
}
static void descriptors_init(void) {
    tss.rsp[0] = (uintptr_t)boot_stack_top;
    tss.ist[0] = (uintptr_t)df_stack_top;
    tss.ist[1] = (uintptr_t)nmi_stack_top;
    tss.iomap = sizeof(tss); /* no I/O bitmap: deny user port access */
    uint64_t base = (uintptr_t)&tss;
    gdt64[3] = (sizeof(tss)-1) | ((base & 0xffffff) << 16)
             | (UINT64_C(0x89) << 40) | (((base >> 24) & 0xff) << 56);
    gdt64[4] = base >> 32;
    uint16_t selector = 0x18;
    hal_load_task_register(selector);
    for (unsigned i = 0; i < 256; ++i) {
        uintptr_t address = isr64_table[i];
        idt[i] = (Gate){(uint16_t)address, 8, i == 8 ? 1 : i == 2 ? 2 : 0,
                        i == USER_GATE ? 0xee : 0x8e, (uint16_t)(address >> 16), (uint32_t)(address >> 32), 0};
    }
    struct __attribute__((packed)) { uint16_t limit; uintptr_t base; }
        pointer = {sizeof(idt)-1, (uintptr_t)idt};
    hal_load_idt(&pointer);
    /* Bootstrap and synchronous selftests keep IRQs masked. The scheduler
     * explicitly initializes/unmasks PIT IRQ0 only when ready to dispatch. */
    out8(0x21, 0xff);
    out8(0xa1, 0xff);
}

#ifdef SELFTEST
extern void probe_registers(void), probe_read(void *), probe_write(void *);
extern void probe_execute(void *), probe_return(void), probe_double_fault(void *);
extern char probe_read_instruction[], probe_write_instruction[];
static volatile unsigned breakpoints, fault_seen;
static uintptr_t expected_address, expected_rip;
static uint64_t expected_error;
static int expect_page_fault, expect_double_fault;
static uint8_t nx_code[1] = {0xc3};

void memory_fault_test(void (*probe)(void *), uintptr_t address,
                       uintptr_t rip, uint64_t error, const char *name) {
    expected_address = address;
    expected_rip = rip;
    expected_error = error;
    fault_seen = 0;
    expect_page_fault = 1;
    probe((void *)address);
    expect_page_fault = 0;
    require(fault_seen == 1, name);
    serial64("[X64] PASS: "); serial64(name); serial64("\n");
}
#endif

void exception64(Frame *frame) {
    if (frame->vector == TIMER64_VECTOR) { scheduler64_timer(frame); return; }
    uintptr_t cr2;
    cr2 = hal_read_cr2();
    if (process64_trap(frame, cr2)) return;
#ifdef SELFTEST
    if (frame->vector == 3 && breakpoints < 2) {
        const uint64_t *registers = &frame->r15;
        for (unsigned i = 0; i < 15; ++i)
            require(registers[i] == UINT64_C(0x1234567800000000) + 15-i,
                    "64-bit register preservation");
        require((frame->flags & (1u << 10)) != 0, "IRET preserves DF");
        unsigned long flags = hal_read_flags();
        require(!(flags & (1u << 10)), "C exception entry clears DF");
        ++breakpoints;
        return;
    }
    if (frame->vector == 14 && expect_page_fault &&
        cr2 == expected_address && frame->rip == expected_rip &&
        frame->error == expected_error) {
        ++fault_seen;
        expect_page_fault = 0;
        frame->rip = (uintptr_t)probe_return;
        return;
    }
    if (frame->vector == 8 && expect_double_fault) {
        require((uintptr_t)frame >= emergency_stack.base + emergency_stack.guards*MM_PAGE_SIZE &&
                (uintptr_t)(frame+1) <= emergency_stack.top && frame->error == 0,
                "double-fault IST stack");
        serial64("[X64] PASS: double-fault IST stack\n[X64] SELFTEST PASS\n");
        halt();
    }
#endif
    serial64("[X64] PANIC vector="); hex64(frame->vector);
    serial64(" error="); hex64(frame->error);
    serial64(" rip="); hex64(frame->rip);
    serial64(" cr2="); hex64(cr2);
    serial64(" rsp="); hex64(frame->rsp);
    serial64("\n");
    halt();
}

static void kernel64_continue(void);
extern void enter_dynamic_stack(uintptr_t top, void (*continuation)(void)) __attribute__((noreturn));
#ifndef SELFTEST
/* Interactive console: enable the TTY stdin path and keep a userspace shell
 * running as the foreground session. The host terminal emulator on COM1 is the
 * display; line editing, history and scrollback are the shell's and the
 * terminal's responsibility. */
static uint64_t console_shell_pid;
static int console_shell_exited;
static void session_completion(const Process64 *process) {
    if (process->pid == console_shell_pid) {
        console_shell_exited = 1;
        serial64(process->state == PROCESS_EXITED ? "[TTY] /bin/pollish exited\n"
                                                  : "[TTY] /bin/pollish terminated abnormally\n");
    }
}
__attribute__((noreturn)) static void console64_run(void) {
    serial64("[TTY] PollikOS console ready; starting /bin/pollish\n");
    tty64_init();
    tty64_enable();
    timer64_start(); /* The TTY is polled from PIT ticks while no process runs. */
    int authenticated=auth64_login();
    timer64_stop();
    if (!authenticated) halt();
    network64_init();
    extern int audio_init(void);
    (void)audio_init();
    const char *environment[] = {"PATH=/bin"};
    const char *desktop_arguments[]={"desktop",0};
    Process64 *desktop=0;
    Launch64Result desktop_result=process64_launch_path("/bin/desktop.pol",1,
        desktop_arguments,1,environment,&desktop);
    if (desktop_result==LAUNCH_OK) {
        desktop->credentials.capabilities|=CAP_DEVICE;
        gui_session_active=1;
        serial64("[GUI] authenticated session started /bin/desktop.pol\n");
    } else
        serial64("[GUI] desktop could not start; text shell remains available\n");
    for (;;) {
        tty64_init();
        Process64 *process = 0;
        const char *arguments[] = {"pollish"};
        Launch64Result result = process64_launch_path("/bin/pollish", 1, arguments, 1, environment, &process);
        if (result != LAUNCH_OK) {
            serial64("[TTY] cannot launch /bin/pollish\n");
            halt();
        }
        console_shell_pid = process->pid;
        console_shell_exited = 0;
        (void)process64_tick_limit(process->pid, 0); /* interactive: no CPU budget */
        while (!console_shell_exited) {
            if (!scheduler64_run(10, 0, session_completion)) halt();
            int state=security_session_state();
            if(state==SESSION_ACTIVE) continue;
            int graphical_elevation=state==SESSION_ELEVATE&&console_fb_width()>=400&&console_fb_height()>=440;
            if(graphical_elevation)console_fb_elevation_capture();
            window64_session_hide(1);
            if(graphical_elevation)console_fb_elevation_save_background();
            gui_session_active=graphical_elevation;
            if(!graphical_elevation)console_fb_write("\033[2J\033[H",7);
            tty64_init();
            if(state==SESSION_LOGOUT) {
                uint64_t old_session=security_session_id();
                process64_end_session(old_session);
                security_session_end();
                while(scheduler64_count()) if(!scheduler64_run(10,0,session_completion)) halt();
                serial64("[SESSION64] logged out; resources reclaimed\n");
                timer64_start();
                int accepted=auth64_login();
                timer64_stop();
                if(!accepted) halt();
                const char *desktop_args[]={"desktop",0};
                Process64 *fresh=0;
                if(process64_launch_path("/bin/desktop.pol",1,desktop_args,1,environment,&fresh)==LAUNCH_OK)
                    { fresh->credentials.capabilities|=CAP_DEVICE; gui_session_active=1; }
                window64_session_hide(0);
                break;
            }
            timer64_start();
            if(state==SESSION_PASSWORD) (void)auth64_change_password();
            else if(state==SESSION_ELEVATE) process64_complete_elevation(auth64_elevate());
            else if(!auth64_login()) halt();
            timer64_stop();
            tty64_init();
            if(graphical_elevation)console_fb_elevation_end();else console_fb_write("\033[2J\033[H",7);
            window64_session_hide(0);
            gui_session_active=1;
            serial64("[SESSION64] session resumed\n");
        }
        serial64("[TTY] shell exited; restarting\n");
    }
}
#endif
void kernel64_main(void) {
    descriptors_init();
    uint64_t cr0, cr3, cr4;
    uint64_t efer;
    uint16_t cs, tr;
    cr0 = hal_read_cr0();
    cr3 = hal_read_cr3();
    cr4 = hal_read_cr4();
    efer = hal_read_msr(0xc0000080);
    __asm__ volatile("mov %%cs,%0; str %1" : "=r"(cs), "=r"(tr));
    require((cr0 & ((1ull << 31) | (1ull << 16))) == ((1ull << 31) | (1ull << 16)), "PG/WP");
    require((cr4 & (1u << 5)) && cr3 == (uintptr_t)boot_pml4, "PAE/PML4");
    require((efer & 0xd00) == 0xd00 && !(efer & 1), "LME/LMA/NXE; syscall disabled");
    require(cs == 8 && tr == 0x18, "64-bit CS/TSS");
    serial64("[X64] long mode active; PML4, NX, WP, GDT, TSS, IDT ready\n");
    physical_window_init();
    /* Preserve the complete low bootstrap aperture, including BIOS buffers,
     * image/BSS, original stacks/tables and VGA/ROM. VBE offsets are the existing
     * stage-2 contract; pitch * height is the actual framebuffer extent. */
    const volatile uint8_t *vbe = (const volatile uint8_t *)0x7000;
    phys_addr_t framebuffer = *(const volatile uint32_t *)(vbe+40);
    uint64_t framebuffer_size = (uint64_t)*(const volatile uint16_t *)(vbe+16) *
                                *(const volatile uint16_t *)(vbe+20);
    PhysicalRange reserved[] = {{0, 0x200000}, {framebuffer, framebuffer+framebuffer_size},
                               {0xfec00000, 0xfec01000}, {0xfee00000, 0xfee01000}};
    require(pmm64_init((const MemoryMapEntry *)0x6004, *(const volatile uint32_t *)0x6000,
                       reserved, sizeof(reserved)/sizeof(reserved[0])), "initialize E820 PMM");
    PmmStats stats = pmm64_stats();
    serial64("[MM64] managed frames="); hex64(stats.managed);
    serial64(" above4g="); hex64(stats.above4g);
    serial64(" metadata="); hex64(stats.metadata); serial64("\n");
#ifdef SELFTEST
    for (int64_t budget = 0; budget < 5; ++budget) {
        pmm64_fail_after(budget);
        require(!vmm64_init(), "injected kernel table allocation failure");
        pmm64_fail_after(-1);
        require(pmm64_stats().free == stats.free, "kernel table rollback balance");
    }
    serial64("[MM64] PASS: kernel table allocation rollback\n");
#endif
    require(vmm64_init(), "dynamic kernel page tables");
    require(console_fb_init(vbe), "map graphical console framebuffer");
    require(vmm64_stack_create(vmm64_kernel(), MM_KERNEL_START, 4, 1, 0, &main_stack) == VM_OK,
            "dynamic kernel stack");
    require(vmm64_stack_create(vmm64_kernel(), MM_KERNEL_START+0x10000, 4, 1, 0, &emergency_stack) == VM_OK,
            "dynamic double-fault stack");
    require(vmm64_stack_create(vmm64_kernel(), MM_KERNEL_START+0x20000, 4, 1, 0, &interrupt_stack) == VM_OK,
            "dynamic NMI stack");
    tss.rsp[0] = main_stack.top;
    tss.ist[0] = emergency_stack.top;
    tss.ist[1] = interrupt_stack.top;
    enter_dynamic_stack(main_stack.top, kernel64_continue);
}
static void kernel64_continue(void) {
    require(fpu64_init(), "FPU/FXSR/SSE2 process context");
    uintptr_t rsp;
    __asm__ volatile("mov %%rsp,%0" : "=r"(rsp));
    require(rsp >= main_stack.base+MM_PAGE_SIZE && rsp < main_stack.top, "running on dynamic stack");
    serial64("[MM64] dynamic paging and guarded kernel/IST stacks ready\n");
    syscall64_init();
    if (!fs64_mount()) {
        serial64("[VFS64] mount refused; disk unchanged\n");
        halt();
    }
#ifndef PRODUCTION
    if (!elf64_demo()) halt();
    file64_demo();
    stat64_demo();
    dir64_demo();
    runtime64_demo();
    heap64_demo();
    c3_demo();
    c4_demo();
    c5_demo();
    c6_demo();
    c7_demo();
#endif
#ifdef SELFTEST
    rtc64_selftest();
    disk64_load_regression_fixtures();
    selfhost_selftest();
    process64_demo();
    memory_selftest();
    usercopy_selftest();
    process64_selftest();
    elf64_selftest();
    scheduler64_selftest();
    path64_selftest();
    disk64_release_regression_fixtures();
    file64_selftest();
    stat64_selftest();
    dir64_selftest();
    runtime64_selftest();
    heap64_selftest();
    c3_selftest();
    c4_selftest();
    c5_selftest();
    c6_selftest();
    c7_selftest();
    probe_registers();
    require(breakpoints == 2, "INT3/IRET roundtrip");
    serial64("[X64] PASS: 64-bit registers and IRET\n");
    memory_fault_test(probe_read, 0, (uintptr_t)probe_read_instruction, 0, "null page");
    memory_fault_test(probe_read, (uintptr_t)boot_stack_guard,
               (uintptr_t)probe_read_instruction, 0, "stack guard");
    memory_fault_test(probe_write, (uintptr_t)isr64_table,
               (uintptr_t)probe_write_instruction, 3, "read-only rodata");
    memory_fault_test(probe_write, (uintptr_t)probe_read_instruction,
               (uintptr_t)probe_write_instruction, 3, "read-only text");
    memory_fault_test(probe_execute, (uintptr_t)nx_code, (uintptr_t)nx_code, 17, "NX data");
    expect_double_fault = 1;
    probe_double_fault((void *)(main_stack.base + 2048));
    fail("double fault did not occur");
#else
#ifndef PRODUCTION
    scheduler64_demo();
#endif
    serial64("[X64] ready (Ring 3, native .pol windows, Desktop/Files Dock apps)\n");
    console64_run();
#endif
}
