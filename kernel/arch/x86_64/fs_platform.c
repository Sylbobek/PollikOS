#include "fs_platform.h"
#include "paging.h"
#include "../../vfs.h"
#include "../../pollikfs.h"
static vfs_file_t *kernel_fds[VFS_MAX_FDS];
static vfs_file_t **fd_context;
static uint32_t sectors;
static int64_t fail_after = -1;
u32 ticks;
void fs64_tick_update(u32 value) { ticks = value; }
void *memcpy(void *dest, const void *src, size_t size) {
    volatile uint8_t *d = dest;
    const volatile uint8_t *s = src;
    for (size_t i = 0; i < size; ++i) d[i] = s[i];
    return dest;
}
void *memmove(void *dest, const void *src, size_t size) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d < s) {
        for (size_t i = 0; i < size; ++i) d[i] = s[i];
    } else if (d > s) {
        while (size) { --size; d[size] = s[size]; }
    }
    return dest;
}
void *memset(void *dest, int value, size_t size) {
    volatile uint8_t *d = dest;
    for (size_t i = 0; i < size; ++i) d[i] = (uint8_t)value;
    return dest;
}
void klog(const char *category, const char *level, const char *message) {
    (void)category; (void)level;
    memory_log("[VFS64] "); memory_log(message); memory_log("\n");
}
vfs_file_t **process_get_current_fd_table(void) {
    memory_context_check();
    return fd_context ? fd_context : kernel_fds;
}
vfs_file_t **fs64_fd_context(vfs_file_t **table) {
    memory_context_check();
    vfs_file_t **previous = fd_context;
    fd_context = table;
    return previous;
}
void fs64_io_fail_after(int64_t count) { memory_context_check(); fail_after = count; }
int fs64_sector_allowed(u32 lba) {
    memory_context_check();
    if (!sectors || lba >= sectors || fail_after == 0) return 0;
    if (fail_after > 0) --fail_after;
    return 1;
}
int fs64_mount(void) {
    memory_context_check();
    sectors = 0;
    /* IDENTIFY the same primary slave used by shared storage.c. No writes,
     * format, migration or fallback to the boot disk exist in this target. */
    outb(0x3f6, 2); /* PIO polling; disk IRQs disabled. */
    outb(0x1f6, 0xb0);
    for (unsigned i = 0; i < 4; ++i) (void)inb(0x3f6);
    outb(0x1f2, 0); outb(0x1f3, 0); outb(0x1f4, 0); outb(0x1f5, 0);
    outb(0x1f7, 0xec);
    int ready = 0;
    for (unsigned i = 0; i < 1000000; ++i) {
        uint8_t s = inb(0x1f7);
        if (!s || s == 255) break;
        if (!(s & 0x80)) {
            if (s & 0x21) break;
            if (s & 8) { ready = 1; break; }
        }
    }
    if (ready) {
        uint16_t words[256];
        for (unsigned i = 0; i < 256; ++i) words[i] = inw(0x1f0);
        if (words[49] & (1u << 9)) sectors = words[60] | ((uint32_t)words[61]<<16);
    }
    if (sectors < POLLIK2_START_LBA+POLLIK2_TOTAL_BLOCKS*2u) sectors = 0;
    vfs_init();
    if (!vfs_is_ready()) return 0;
    return pollikfs_mounted();
}
