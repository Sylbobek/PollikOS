#ifndef POLLIK_LIBPAYLOAD_ADAPTER_H
#define POLLIK_LIBPAYLOAD_ADAPTER_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include "libpayload-config.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __printf(a,b) __attribute__((format(__printf__,a,b)))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ALIGN_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define ALIGN(n,a) ALIGN_UP(n,a)
#define div_round_up(n,d) (((n)+(d)-1)/(d))
#define BIT(n) (1u<<(n))
void *usb_lp_malloc(size_t bytes);
void *usb_lp_memalign(size_t align,size_t bytes);
void *usb_lp_xzalloc(size_t bytes);
void *usb_lp_calloc(size_t count,size_t bytes);
void usb_lp_fatal(const char *format,...) __attribute__((noreturn)) __printf(1,2);
void usb_lp_free(void *pointer);
int usb_lp_printf(const char *format,...) __printf(1,2);
int usb_lp_vprintf(const char *format,va_list args);
void usb_lp_delay(unsigned usec);
#define malloc usb_lp_malloc
#define calloc usb_lp_calloc
#define memalign usb_lp_memalign
#define dma_malloc usb_lp_malloc
#define dma_memalign usb_lp_memalign
#define xzalloc usb_lp_xzalloc
#define xmalloc usb_lp_xzalloc
#define fatal usb_lp_fatal
#define free usb_lp_free
#define printf usb_lp_printf
#define vprintf usb_lp_vprintf
#define udelay usb_lp_delay
#define mdelay(ms) usb_lp_delay((ms)*1000u)
#define dma_initialized() 1
int usb_lp_dma_coherent(const volatile void *pointer);
#define dma_coherent(p) usb_lp_dma_coherent(p)
uintptr_t usb_lp_virt_to_phys(const volatile void *pointer);
void *usb_lp_phys_to_virt(uintptr_t address);
#define virt_to_phys(p) usb_lp_virt_to_phys(p)
#define phys_to_virt(p) usb_lp_phys_to_virt(p)
static inline u8 readb(const volatile void *p){return *(const volatile u8 *)p;}
static inline u16 readw(const volatile void *p){return *(const volatile u16 *)p;}
static inline u32 readl(const volatile void *p){return *(const volatile u32 *)p;}
static inline void writeb(u8 v,volatile void *p){*(volatile u8 *)p=v;}
static inline void writew(u16 v,volatile void *p){*(volatile u16 *)p=v;}
static inline void writel(u32 v,volatile void *p){*(volatile u32 *)p=v;}
#define read32(p) readl(p)
#define read16(p) readw(p)
#define read8(p) readb(p)
#define write32(p,v) writel(v,p)
#define write16(p,v) writew(v,p)
#define write8(p,v) writeb(v,p)
#include "arch/io.h"
#endif
