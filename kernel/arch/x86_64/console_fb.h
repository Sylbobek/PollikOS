#ifndef POLLIK_X64_CONSOLE_FB_H
#define POLLIK_X64_CONSOLE_FB_H
#include <stddef.h>
#include <stdint.h>
int console_fb_init(const volatile uint8_t *vbe);
void console_fb_write(const char *data, size_t length);
void console_fb_mouse_enable(void);
void console_fb_mouse_move(int x, int y);
unsigned console_fb_width(void);
unsigned console_fb_height(void);
unsigned console_fb_pitch(void);
unsigned console_fb_bpp(void);
void console_fb_overlay_begin(void);
void console_fb_overlay_end(void);
int console_fb_read_pixels(unsigned x, unsigned y, unsigned count, uint32_t *pixels_out);
int console_fb_write_pixels(unsigned x, unsigned y, unsigned count, const uint32_t *pixels_in);
void console_fb_draw_window_frame(unsigned x, unsigned y, unsigned width, unsigned height,
                                  const char *title);
#endif
