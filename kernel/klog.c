#include "klog.h"

typedef struct {
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 gs, fs, es, ds, vector, error, eip, cs, eflags, useresp, ss;
} RegFrame;

static void hex_to_str(char *buf, u32 val) {
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        u8 nibble = (val >> (i * 4)) & 0xFu;
        buf[2 + (7 - i)] = nibble < 10 ? ('0' + nibble) : ('a' + (nibble - 10));
    }
    buf[10] = 0;
}

void klog(const char *cat, const char *level, const char *msg) {
    serial("[");
    serial(cat);
    serial(":");
    serial(level);
    serial("] ");
    serial(msg);
    serial("\n");
}

void klog_hex(const char *cat, const char *prefix, u32 val) {
    char h[12];
    hex_to_str(h, val);
    serial("[");
    serial(cat);
    serial("] ");
    serial(prefix);
    serial(h);
    serial("\n");
}

void klog_dec(const char *cat, const char *prefix, u32 val) {
    char d[16];
    number(d, val);
    serial("[");
    serial(cat);
    serial("] ");
    serial(prefix);
    serial(d);
    serial("\n");
}

void dump_registers(const void *frame_ptr, u32 cr2) {
    const RegFrame *f = (const RegFrame *)frame_ptr;
    u32 cr0, cr3, cr4 = 0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    /* CR4 is only available on Pentium+, test if readable */
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4) : : "memory");

    char h[12];

    serial("\n--- CPU REGISTER DUMP ---\n");
    if (f) {
        hex_to_str(h, f->eip); serial("EIP: "); serial(h);
        hex_to_str(h, f->cs);  serial("  CS: "); serial(h);
        hex_to_str(h, f->eflags); serial("  EFLAGS: "); serial(h); serial("\n");

        u32 esp_val = (f->cs & 3) == 3 ? f->useresp : (u32)&f->esp_dummy;
        u32 ss_val  = (f->cs & 3) == 3 ? f->ss : f->ds;
        hex_to_str(h, esp_val); serial("ESP: "); serial(h);
        hex_to_str(h, f->ebp);  serial("  EBP: "); serial(h);
        hex_to_str(h, ss_val);  serial("  SS: "); serial(h); serial("\n");

        hex_to_str(h, f->eax); serial("EAX: "); serial(h);
        hex_to_str(h, f->ebx); serial("  EBX: "); serial(h);
        hex_to_str(h, f->ecx); serial("  ECX: "); serial(h);
        hex_to_str(h, f->edx); serial("  EDX: "); serial(h); serial("\n");

        hex_to_str(h, f->esi); serial("ESI: "); serial(h);
        hex_to_str(h, f->edi); serial("  EDI: "); serial(h);
        hex_to_str(h, f->ds);  serial("  DS: "); serial(h);
        hex_to_str(h, f->es);  serial("  ES: "); serial(h); serial("\n");

        hex_to_str(h, f->vector); serial("VEC: "); serial(h);
        hex_to_str(h, f->error);  serial("  ERR: "); serial(h); serial("\n");
    }

    hex_to_str(h, cr0); serial("CR0: "); serial(h);
    hex_to_str(h, cr2); serial("  CR2: "); serial(h);
    hex_to_str(h, cr3); serial("  CR3: "); serial(h);
    hex_to_str(h, cr4); serial("  CR4: "); serial(h); serial("\n");
    serial("-------------------------\n");
}

void panic(const char *msg, const void *frame_ptr) {
    serial("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    serial("           POLLIKOS KERNEL PANIC                \n");
    serial("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    serial("REASON: ");
    serial(msg);
    serial("\n");

    u32 cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    dump_registers(frame_ptr, cr2);

    serial("KERNEL SYSTEM HALTED.\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
