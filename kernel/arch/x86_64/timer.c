#include "scheduler.h"
#include "paging.h"
#include "../../hal.h"
static int initialized;
static void out8(uint16_t port, uint8_t value) {
    hal_port_write8(port, value);
    hal_port_write8(0x80, 0);
}
void timer64_start(void) {
    memory_context_check();
    if (!initialized) {
        /* Same legacy PIC/PIT hardware as i386 process.c; separate IDT ABI. */
        out8(0x20, 0x11); out8(0xa0, 0x11);
        out8(0x21, 32); out8(0xa1, 40);
        out8(0x21, 4); out8(0xa1, 2);
        out8(0x21, 1); out8(0xa1, 1);
        initialized = 1;
    }
    out8(0x21, 0xff); out8(0xa1, 0xff);
    unsigned divisor = 1193182 / TIMER64_HZ;
    out8(0x43, 0x36);
    out8(0x40, (uint8_t)divisor); out8(0x40, (uint8_t)(divisor >> 8));
    out8(0x21, 0xfe); /* Only IRQ0. No APIC/SMP or other device IRQs. */
}
void timer64_stop(void) {
    memory_context_check();
    out8(0x21, 0xff); out8(0xa1, 0xff);
}
void timer64_ack(void) { out8(0x20, 0x20); }
