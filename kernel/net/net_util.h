#ifndef POLLIK_NET_UTIL_H
#define POLLIK_NET_UTIL_H

#ifdef POLLIK_X64
#include "../arch/x86_64/net_platform.h"
#else
#include "../system.h"
#endif

static inline u16 net_be16(const u8 *p) {
    return (u16)(p[0] << 8 | p[1]);
}

static inline void net_put16(u8 *p, u16 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

static inline u32 net_be32(const u8 *p) {
    return (u32)(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]);
}

static inline void net_put32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static inline int net_same(const u8 *a, const u8 *b, int n) {
    while (n--) {
        if (*a++ != *b++)
            return 0;
    }
    return 1;
}

/* Main-thread only. Opt-in for the browser load scope, never packet/IRQ dispatch.
 * The callback must not poll apps, start network work or execute client input. */
typedef void (*NetWaitService)(void);
NetWaitService net_set_wait_service(NetWaitService service);
void net_service_wait(void);

u16 net_checksum(const u8 *p, int n);
u16 net_tcp_udp_checksum(const u8 *src_ip, const u8 *dst_ip, u8 proto, const u8 *payload, int len);
int net_parse_ip(const char *str, u8 *out_ip);
void net_format_ip(const u8 *ip, char *out_str);
void net_format_mac(const u8 *mac, char *out_str);

#endif
