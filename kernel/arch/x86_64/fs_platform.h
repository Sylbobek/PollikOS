#ifndef POLLIK_FS_PLATFORM64_H
#define POLLIK_FS_PLATFORM64_H
#include <stdint.h>
#include <stddef.h>
#include "../../hal.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
void *memcpy(void *dest, const void *src, size_t size);
void *memset(void *dest, int value, size_t size);
static inline void outb(u16 port, u8 value) { hal_port_write8(port, value); }
static inline void outw(u16 port, u16 value) { hal_port_write16(port, value); }
static inline u8 inb(u16 port) { return hal_port_read8(port); }
static inline u16 inw(u16 port) { return hal_port_read16(port); }
/* Legacy PollikFS inode tick stamps; the scheduler feeds this counter. */
extern u32 ticks;
void fs64_tick_update(u32 value);
int fs64_sector_allowed(u32 lba);
int fs64_mount(void);
void fs64_io_fail_after(int64_t sectors);
unsigned vfs_debug_handles(void);
struct vfs_file;
struct vfs_file **fs64_fd_context(struct vfs_file **table);
#ifdef SELFTEST
void vfs_test_fail_alloc(int fail);
#endif
#endif
