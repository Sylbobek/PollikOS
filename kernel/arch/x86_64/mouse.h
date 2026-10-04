#ifndef POLLIK_X64_MOUSE_H
#define POLLIK_X64_MOUSE_H
#include <stdint.h>
#include "user_abi.h"
typedef struct {
    uint32_t version, size, kind, buttons, changed;
    uint32_t key, modifiers;
    int32_t x, y, wheel;
    uint64_t sequence;
} MouseEvent64;
_Static_assert(sizeof(MouseEvent64)==USER_INPUT_EVENT_SIZE, "mouse event ABI size");
int mouse64_init(void);
void mouse64_byte(uint8_t value);
int mouse64_pop(MouseEvent64 *event);
void mouse64_flush(void);
int mouse64_wheel_enabled(void);
#endif
