#ifndef POLLIK_SYSTEM_H
#define POLLIK_SYSTEM_H
#define OS_NAME "Pollik OS"
#define OS_VERSION "v0.1 Alpha"
#define OS_LABEL OS_NAME " " OS_VERSION
/* Shared corner-radius scale (px): controls, inputs/cards, large panels, pills.
 * Every rounded element should pick one of these instead of a magic number. */
#define UI_RADIUS_SMALL  8
#define UI_RADIUS_MEDIUM 12
#define UI_RADIUS_LARGE  16
#define UI_RADIUS_PILL   9999
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned int uintptr_t;
#include "hal.h"

/* Legacy port-I/O names remain as source-compatible adapters. Device code can
 * migrate to the explicit HAL names without duplicating privileged asm. */
static inline void outb(u16 p, u8 v) {
    hal_port_write8(p, v);
}
static inline u8 inb(u16 p) {
    return hal_port_read8(p);
}
static inline void outw(u16 p, u16 v) {
    hal_port_write16(p, v);
}
static inline u16 inw(u16 p) {
    return hal_port_read16(p);
}
static inline void outl(u16 p, u32 v) {
    hal_port_write32(p, v);
}
static inline u32 inl(u16 p) {
    return hal_port_read32(p);
}
void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);
void *memmove(void *, const void *, unsigned);
int memcmp(const void *, const void *, unsigned);
unsigned strlen(const char *);
void serial(const char *);
void number(char *, u32);
int framebuffer_init(const u8 *);
void framebuffer_present(const u32 *, int, int, int, int);
void framebuffer_present_cursor_pair(const u32 *, int, int, int, int,
                                     const u32 *, int, int, int, int);
void framebuffer_info(char *);
int framebuffer_brightness(void);
void framebuffer_set_brightness(int percent);
int sys_text_width(const char *s, int scale);
int sys_get_glyph_advance(u8 c, int scale);
void sys_draw_rect_clipped(int x, int y, int w, int h, u32 c, int cx1, int cy1, int cx2, int cy2);
void sys_draw_rounded_clipped(int x, int y, int w, int h, int r, u32 c, int cx1, int cy1, int cx2, int cy2);
void sys_draw_roundrect_border_clipped(int x, int y, int w, int h, int r, int t, u32 c, int cx1, int cy1, int cx2, int cy2);
void sys_draw_roundrect_stroke_clipped(int x, int y, int w, int h, int r, int t, u32 stroke, u32 fill, int cx1, int cy1, int cx2, int cy2);
void sys_draw_rgba_clipped(int x, int y, int w, int h, const u8 *rgba, int sw, int sh, int cx1, int cy1, int cx2, int cy2);
void sys_draw_canvas_clipped(int x, int y, int w, int h, const u32 *src, int sw, int sh, int cx1, int cy1, int cx2, int cy2);
void sys_draw_window_bottom(int x, int y, int w, int h, int r, u32 c, int start_y);
void sys_draw_letter_clipped(int x, int y, u8 c, u32 color, int scale, int cx1, int cy1, int cx2, int cy2);
void sys_text_to_buffer(u32 *buffer, int width, int height, int stride, int x, int y, const char *s, u32 color, int scale);
void pmm_init(void);
void vmm_init(void);
void vmm_page_fault_handler(void *frame_ptr);
int pmm_self_test(void);
int vmm_self_test(void);
void mem_init(void);
void *kmalloc(u32 size);
void kfree(void *ptr);
void *kcalloc(u32 n, u32 size);
void *krealloc(void *ptr, u32 new_size);
u32 mem_get_used(void);
u32 mem_get_free(void);

#define FS_FILES 8
#define FS_CAPACITY 1023
typedef struct {
    char name[24];
    u32 length;
    char data[1024];
} File;
extern File files[FS_FILES];
extern int fs_ready;
extern const char *fs_status;
void fs_init(void);
int fs_save(int index, const char *name, const char *data, u32 length);
int fs_remove(int index);
int fs_find(const char *name);

extern volatile u32 ticks;
void process_init(void);
void process_list(char *out);
int process_action(int pid, int action);
void process_fault_test(void);
extern const char *net_status;
extern int net_ready;
void net_init(void);
void net_poll(void);
void net_ping(void);
void net_info(char *out);
#endif
