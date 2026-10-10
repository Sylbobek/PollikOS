#include "system.h"
#include "process.h"
#include "pmm.h"
#include "hw.h"
#include "desktop.h"
#include "input_dispatch.h"
#include "gui/apps.h"
#include "auth.h"
#include "audio.h"

/* Freestanding runtime and machine boot remain separate from the GUI shell. */
unsigned long long __udivdi3(unsigned long long value,unsigned long long divisor) {
    if(!divisor) __builtin_trap();
    unsigned long long remainder=0,quotient=0;
    for(int bit=63;bit>=0;--bit) {
        unsigned carry=(unsigned)(remainder>>63);
        remainder=(remainder<<1)|((value>>bit)&1);
        if(carry || remainder>=divisor) { remainder-=divisor;quotient|=1ULL<<bit; }
    }
    return quotient;
}
/* Scalar 64-bit remainder required by the vendored Argon2 implementation on
 * i386. Binary division avoids a recursive compiler-runtime dependency. */
unsigned long long __umoddi3(unsigned long long value,unsigned long long divisor) {
    if(!divisor) __builtin_trap();
    unsigned long long remainder=0;
    for(int bit=63;bit>=0;--bit) {
        unsigned carry=(unsigned)(remainder>>63);
        remainder=(remainder<<1)|((value>>bit)&1);
        if(carry || remainder>=divisor) remainder-=divisor;
    }
    return remainder;
}
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
        hal_cpu_halt_forever();
    }
    desktop_init();
    compositor_splash("Bringing up the system", 8);
    mem_init();
    compositor_splash("Initializing memory", 22);
    fs_init();
    compositor_splash("Mounting the file system", 38);
    extern void vfs_init(void);
    vfs_init();
    compositor_splash("Preparing your files", 52);
    input_dispatch_init();
    rtc_init();
    compositor_splash("Starting devices", 66);
    audio_init();
    net_init();
    compositor_splash("Connecting to the network", 78);
    gui_apps_init();
    process_init();
    compositor_splash("Loading applications", 90);
    auth_init();
    desktop_start();
    audio_play_sound(SOUND_STARTUP);
    serial("VBE ready; PS/2 ready; desktop ready\n");
    for (;;) {
        const char *old_net = net_status;
        net_poll();
        phase2_poll();
        if (desktop_poll(old_net != net_status) && !(inb(0x64) & 1))
            hal_cpu_idle_once();
    }
}
