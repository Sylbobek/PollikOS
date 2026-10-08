#ifndef POLLIKOS_DEVICES_H
#define POLLIKOS_DEVICES_H
#include <pollikos/syscall.h>
/* Scalar device operations; no existing syscall or structure is changed.
 * Output mask bit 0 = PC speaker, bit 1 = Intel ICH AC97 PCM. IDs 0 and 1.
 * Results are raw negative PollikOS errors, like other pollikos_* helpers. */
/* USER_DEVICE_POWER: value 0 shuts down, 1 restarts; requires DEVICE and SESSION. */
static inline int64_t pollikos_device(uint64_t operation,uint64_t value){
    return __pollikos_syscall2(USER_DEVICE_CONTROL,operation,value);
}
#endif
