#ifndef POLLIKOS_WINDOW_H
#define POLLIKOS_WINDOW_H
#include <stdint.h>
#include <pollikos/syscall.h>

/* Window pixels are writable XRGB8888 words owned by the calling process.
 * Width and height are the content size; the kernel supplies frame/titlebar. */
typedef struct {
    uint32_t version, size;
    int32_t content_x, content_y;
    uint32_t width, height;
} pollikos_window_info_t;
_Static_assert(sizeof(pollikos_window_info_t)==24,"PollikOS window info ABI size");
typedef struct {
    uint32_t version, size, kind, buttons, changed;
    uint32_t key, modifiers;
    int32_t x, y, wheel;
    uint64_t sequence;
} pollikos_input_event_t;
_Static_assert(sizeof(pollikos_input_event_t)==USER_INPUT_EVENT_SIZE,
               "PollikOS input event ABI size");

#define POLLIKOS_INPUT_MOUSE_MOVE USER_INPUT_MOUSE_MOVE
#define POLLIKOS_INPUT_MOUSE_BUTTON USER_INPUT_MOUSE_BUTTON
#define POLLIKOS_INPUT_MOUSE_WHEEL USER_INPUT_MOUSE_WHEEL
#define POLLIKOS_INPUT_WINDOW_CLOSE USER_INPUT_WINDOW_CLOSE
#define POLLIKOS_INPUT_KEY_DOWN USER_INPUT_KEY_DOWN
#define POLLIKOS_INPUT_KEY_UP USER_INPUT_KEY_UP
#define POLLIKOS_INPUT_MOD_SHIFT USER_INPUT_MOD_SHIFT
#define POLLIKOS_INPUT_MOD_CONTROL USER_INPUT_MOD_CONTROL
#define POLLIKOS_INPUT_MOD_ALT USER_INPUT_MOD_ALT
#define POLLIKOS_KEY_UP USER_KEY_UP
#define POLLIKOS_KEY_DOWN USER_KEY_DOWN
#define POLLIKOS_KEY_LEFT USER_KEY_LEFT
#define POLLIKOS_KEY_RIGHT USER_KEY_RIGHT
#define POLLIKOS_KEY_HOME USER_KEY_HOME
#define POLLIKOS_KEY_END USER_KEY_END
#define POLLIKOS_KEY_DELETE USER_KEY_DELETE
#define POLLIKOS_KEY_LEFT_SHIFT USER_KEY_LEFT_SHIFT
#define POLLIKOS_KEY_RIGHT_SHIFT USER_KEY_RIGHT_SHIFT
#define POLLIKOS_KEY_CONTROL USER_KEY_CONTROL
#define POLLIKOS_KEY_ALT USER_KEY_ALT
#define POLLIKOS_MOUSE_LEFT USER_MOUSE_BUTTON_LEFT
#define POLLIKOS_MOUSE_RIGHT USER_MOUSE_BUTTON_RIGHT
#define POLLIKOS_MOUSE_MIDDLE USER_MOUSE_BUTTON_MIDDLE

/* Returns a user-space pixel pointer or a negative PollikOS error. One window
 * is allowed per process; window count follows the process/RAM capacity. */
static __inline__ int64_t pollikos_window_create(uint32_t width, uint32_t height,
                                                const char *title) {
    return __pollikos_syscall3(USER_WINDOW_CREATE,width,height,(uint64_t)(uintptr_t)title);
}
static __inline__ int64_t pollikos_window_present(void) {
    return __pollikos_syscall0(USER_WINDOW_PRESENT);
}
static __inline__ int64_t pollikos_window_destroy(void) {
    return __pollikos_syscall0(USER_WINDOW_DESTROY);
}
/* Returns current client-area screen coordinates and content dimensions. */
static __inline__ int64_t pollikos_window_info(pollikos_window_info_t *info) {
    return __pollikos_syscall1(USER_WINDOW_INFO,(uint64_t)(uintptr_t)info);
}
/* Nonblocking: sizeof(event) on success, negative USER_EAGAIN when empty. */
static __inline__ int64_t pollikos_input_read(pollikos_input_event_t *event) {
    return __pollikos_syscall1(USER_INPUT_READ,(uint64_t)(uintptr_t)event);
}
#endif
