#ifndef POLLIK_HAL_H
#define POLLIK_HAL_H

/* Self-contained fixed-width port I/O API; privileged instructions live here. */
void hal_port_write8(unsigned short port, unsigned char value);
unsigned char hal_port_read8(unsigned short port);
void hal_port_write16(unsigned short port, unsigned short value);
unsigned short hal_port_read16(unsigned short port);
void hal_port_write32(unsigned short port, unsigned int value);
unsigned int hal_port_read32(unsigned short port);

/* CPU and interrupt primitives shared by the i386 kernel and x86_64 build. */
unsigned long hal_irq_save_disable(void);
void hal_irq_restore(unsigned long flags);
void hal_irq_enable(void);
void hal_irq_disable(void);
unsigned long hal_read_flags(void);
int hal_interrupts_enabled(void);
int hal_cpu_has_cpuid(void);
void hal_cpu_relax(void);
void hal_cpu_idle_once(void);
void hal_cpu_idle_once_disabled(void);
void hal_cpu_halt_forever(void) __attribute__((noreturn));
void hal_cpu_triple_fault(void) __attribute__((noreturn));
void hal_load_idt(const void *descriptor);
void hal_load_task_register(unsigned short selector);

unsigned long hal_read_cr0(void);
unsigned long hal_read_cr2(void);
unsigned long hal_read_cr3(void);
unsigned long hal_read_cr4(void);
void hal_write_cr0(unsigned long value);
void hal_write_cr3(unsigned long value);
void hal_write_cr4(unsigned long value);
void hal_invalidate_page(const void *address);
unsigned long long hal_read_msr(unsigned int index);
void hal_write_msr(unsigned int index, unsigned long long value);
void hal_cpuid(unsigned int leaf, unsigned int subleaf,
               unsigned int *eax, unsigned int *ebx,
               unsigned int *ecx, unsigned int *edx);
unsigned long long hal_read_tsc(void);
unsigned long long hal_read_tsc_serialized(void);
int hal_rdrand32(unsigned int *value);
void hal_x87_init(void);
void hal_x87_save_reset(void *state);
void hal_x87_restore(const void *state);

#endif
