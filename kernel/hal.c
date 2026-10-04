#include "system.h"

void hal_port_write8(u16 port, u8 value) {
    __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}

u8 hal_port_read8(u16 port) {
    u8 value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

void hal_port_write16(u16 port, u16 value) {
    __asm__ volatile("outw %0,%1" :: "a"(value), "Nd"(port));
}

u16 hal_port_read16(u16 port) {
    u16 value;
    __asm__ volatile("inw %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

void hal_port_write32(u16 port, u32 value) {
    __asm__ volatile("outl %0,%1" :: "a"(value), "Nd"(port));
}

u32 hal_port_read32(u16 port) {
    u32 value;
    __asm__ volatile("inl %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

unsigned long hal_irq_save_disable(void) {
    unsigned long flags;
#if defined(__x86_64__)
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
#else
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
#endif
    return flags;
}

void hal_irq_restore(unsigned long flags) {
    if (flags & (1ul << 9))
        __asm__ volatile("sti" ::: "memory");
    else
        __asm__ volatile("cli" ::: "memory");
}

void hal_irq_enable(void) {
    __asm__ volatile("sti" ::: "memory");
}

void hal_irq_disable(void) {
    __asm__ volatile("cli" ::: "memory");
}

unsigned long hal_read_flags(void) {
    unsigned long flags;
#if defined(__x86_64__)
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
#else
    __asm__ volatile("pushfl; popl %0" : "=r"(flags) :: "memory");
#endif
    return flags;
}

int hal_interrupts_enabled(void) {
    return (hal_read_flags() & (1ul << 9)) != 0;
}

int hal_cpu_has_cpuid(void) {
    unsigned long before, after;
#if defined(__x86_64__)
    __asm__ volatile("pushfq; popq %0; movq %0, %1; xorq $0x200000, %1;"
                     " pushq %1; popfq; pushfq; popq %1; pushq %0; popfq"
                     : "=&r"(before), "=&r"(after) :: "cc", "memory");
#else
    __asm__ volatile("pushfl; popl %0; movl %0, %1; xorl $0x200000, %1;"
                     " pushl %1; popfl; pushfl; popl %1; pushl %0; popfl"
                     : "=&r"(before), "=&r"(after) :: "cc", "memory");
#endif
    return ((before ^ after) & (1ul << 21)) != 0;
}

void hal_cpu_relax(void) {
    __asm__ volatile("pause");
}

void hal_cpu_idle_once(void) {
    __asm__ volatile("sti; hlt" ::: "memory");
}

void hal_cpu_idle_once_disabled(void) {
    __asm__ volatile("sti; hlt; cli" ::: "memory");
}

void hal_cpu_halt_forever(void) {
    for (;;) __asm__ volatile("cli; hlt" ::: "memory");
}

void hal_cpu_triple_fault(void) {
    struct __attribute__((packed)) { unsigned short limit; unsigned long base; }
        null_idt = {0, 0};
    __asm__ volatile("lidt %0; int3" :: "m"(null_idt) : "memory");
    hal_cpu_halt_forever();
}

void hal_load_idt(const void *descriptor) {
    __asm__ volatile("lidt (%0)" :: "r"(descriptor) : "memory");
}

void hal_load_task_register(unsigned short selector) {
    __asm__ volatile("ltr %0" :: "r"(selector) : "memory");
}

unsigned long hal_read_cr0(void) {
    unsigned long value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

unsigned long hal_read_cr2(void) {
    unsigned long value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

unsigned long hal_read_cr3(void) {
    unsigned long value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

unsigned long hal_read_cr4(void) {
    unsigned long value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value) :: "memory");
    return value;
}

void hal_write_cr0(unsigned long value) {
    __asm__ volatile("mov %0, %%cr0" :: "r"(value) : "memory");
}

void hal_write_cr3(unsigned long value) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(value) : "memory");
}

void hal_write_cr4(unsigned long value) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(value) : "memory");
}

void hal_invalidate_page(const void *address) {
    __asm__ volatile("invlpg (%0)" :: "r"(address) : "memory");
}

unsigned long long hal_read_msr(unsigned int index) {
    unsigned int low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(index));
    return ((unsigned long long)high << 32) | low;
}

void hal_write_msr(unsigned int index, unsigned long long value) {
    unsigned int low = (unsigned int)value;
    unsigned int high = (unsigned int)(value >> 32);
    __asm__ volatile("wrmsr" :: "c"(index), "a"(low), "d"(high) : "memory");
}

void hal_cpuid(unsigned int leaf, unsigned int subleaf,
               unsigned int *eax, unsigned int *ebx,
               unsigned int *ecx, unsigned int *edx) {
    unsigned int a = leaf, b, c = subleaf, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d) :: "memory");
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

unsigned long long hal_read_tsc(void) {
    unsigned int low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high) :: "memory");
    return ((unsigned long long)high << 32) | low;
}

unsigned long long hal_read_tsc_serialized(void) {
    unsigned int low, high;
#if defined(__x86_64__)
    __asm__ volatile("cpuid; rdtsc" : "=a"(low), "=d"(high) : "a"(0)
                     : "rbx", "rcx", "memory");
#else
    __asm__ volatile("cpuid; rdtsc" : "=a"(low), "=d"(high) : "a"(0)
                     : "ebx", "ecx", "memory");
#endif
    return ((unsigned long long)high << 32) | low;
}

int hal_rdrand32(unsigned int *value) {
    unsigned int random_value;
    unsigned char success;
    if (!value) return 0;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(random_value), "=qm"(success));
    if (!success) return 0;
    *value = random_value;
    return 1;
}

void hal_x87_init(void) {
    __asm__ volatile("fninit" ::: "memory");
}

void hal_x87_save_reset(void *state) {
    __asm__ volatile("fnsave (%0); fwait" :: "r"(state) : "memory");
}

void hal_x87_restore(const void *state) {
    __asm__ volatile("frstor (%0)" :: "r"(state) : "memory");
}
