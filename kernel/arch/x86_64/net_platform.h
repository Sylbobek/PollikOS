#ifndef POLLIK_X64_NET_PLATFORM_H
#define POLLIK_X64_NET_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
#include "fs_platform.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
extern u32 ticks;

void *memcpy(void *dest, const void *src, size_t size);
void *memmove(void *dest, const void *src, size_t size);
void *memset(void *dest, int value, size_t size);
int memcmp(const void *left, const void *right, size_t size);
size_t strlen(const char *text);
void serial(const char *text);
void number(char *out, u32 value);
void memory_log(const char *message);
void *tcp64_buffer_alloc(unsigned slot,unsigned bytes);
void tcp64_buffer_free(void *buffer);

static inline void outl(u16 port, u32 value) {
    hal_port_write32(port, value);
}
static inline u32 inl(u16 port) {
    return hal_port_read32(port);
}
#endif
