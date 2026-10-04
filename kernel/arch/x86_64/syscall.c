#include "syscall.h"
#include "paging.h"
#include "../../hal.h"
/* One record per CPU. GS supplies entry-only scratch before a stack exists.
 * Bind a different record/MSR on each CPU before enabling future SMP. */
static struct { uint64_t kernel_rsp, user_rsp; } bsp_entry;
_Static_assert(offsetof(__typeof__(bsp_entry), kernel_rsp) == CPU_ENTRY_KERNEL_RSP, "entry stack offset");
_Static_assert(offsetof(__typeof__(bsp_entry), user_rsp) == CPU_ENTRY_USER_RSP, "entry scratch offset");
_Static_assert(USER_CS == USER_SS+8, "SYSRET GDT order");
extern void syscall64_entry(void);
static void write_msr_checked(uint32_t number, uint64_t value) {
    hal_write_msr(number, (unsigned long long)value);
    memory_require((uint64_t)hal_read_msr(number) == value, "syscall MSR readback");
}
void syscall64_stack(uintptr_t top) { bsp_entry.kernel_rsp = top; }
void syscall64_init(void) {
    memory_context_check();
    syscall64_stack(kernel64_get_rsp0());
    /* No userspace GS/TLS or FSGSBASE API exists. Interrupt/NMI handlers never
     * use GS, including the short entry interval between the two SWAPGSes. */
    uintptr_t cr4;
    cr4 = hal_read_cr4();
    memory_require(!(cr4 & (1u<<16)), "unprivileged FSGSBASE disabled");
    write_msr_checked(0xc0000101, 0); /* user/ordinary kernel GS base */
    write_msr_checked(0xc0000102, (uintptr_t)&bsp_entry);
    write_msr_checked(0xc0000081, ((uint64_t)(USER_CS-16)<<48)|((uint64_t)8<<32));
    write_msr_checked(0xc0000082, (uintptr_t)syscall64_entry);
    write_msr_checked(0xc0000084, 0xffffffff); /* mask IF/DF/TF/AC/NT/IOPL and all user flags */
    write_msr_checked(0xc0000080, (uint64_t)hal_read_msr(0xc0000080)|1);
    memory_log("[C1] SYSCALL MSRs verified\n");
}
