#include "system.h"
#include "process.h"
#include "pmm.h"
#include "hw.h"
#include "desktop.h"
#include "input_dispatch.h"
#include "gui/apps.h"

/* Freestanding runtime and machine boot remain separate from the GUI shell. */
void *memset(void *p, int v, unsigned n) {
    void *orig = p;
    u32 val = (u8)v;
    val |= val << 8;
    val |= val << 16;
    u32 dwords = n >> 2, bytes = n & 3;
    if (dwords) __asm__ volatile("cld; rep stosl" : "+D"(p), "+c"(dwords) : "a"(val) : "memory");
    if (bytes) __asm__ volatile("rep stosb" : "+D"(p), "+c"(bytes) : "a"(val) : "memory");
    return orig;
}
void *memcpy(void *d, const void *s, unsigned n) {
    void *orig = d;
    u32 dwords = n >> 2, bytes = n & 3;
    if (dwords) __asm__ volatile("cld; rep movsl" : "+D"(d), "+S"(s), "+c"(dwords) :: "memory");
    if (bytes) __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(bytes) :: "memory");
    return orig;
}
void serial(const char *s) {
    while (*s) {
        while (!(inb(0x3fd) & 32)) {}
        outb(0x3f8, *s++);
    }
}
void number(char *out, u32 value) {
    char reverse[12];
    int i = 0, j = 0;
    do {
        reverse[i++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (i) out[j++] = reverse[--i];
    out[j] = 0;
}
void kernel_main(void) {
    outb(0x3f8 + 1, 0);
    outb(0x3fb, 0x80);
    outb(0x3f8, 3);
    outb(0x3f9, 0);
    outb(0x3fb, 3);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
    serial("PollikOS: own kernel entered\n");
    pmm_init();
    vmm_init();
    pmm_self_test();
    vmm_self_test();
    if (!framebuffer_init((const u8 *)0x7000)) {
        serial("Unsupported VBE framebuffer\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    desktop_init();
    mem_init();
    fs_init();
    extern void vfs_init(void);
    vfs_init();
    input_dispatch_init();
    rtc_init();
    net_init();
    gui_apps_init();
    process_init();
    desktop_start();
    serial("VBE ready; PS/2 ready; desktop ready\n");
    for (;;) {
        const char *old_net = net_status;
        net_poll();
        phase2_poll();
        if (desktop_poll(old_net != net_status) && !(inb(0x64) & 1))
            __asm__ volatile("sti; hlt");
    }
}
