#include "net_util.h"

static NetWaitService wait_service;
static int servicing_wait;
static u32 last_service_tick;
NetWaitService net_set_wait_service(NetWaitService service) {
    NetWaitService previous = wait_service;
    wait_service = service;
    last_service_tick = ticks - 1u;
    return previous;
}
void net_service_wait(void) {
    if (!wait_service || servicing_wait || ticks == last_service_tick) return;
    last_service_tick = ticks;
    servicing_wait = 1;
    wait_service();
    servicing_wait = 0;
}

u16 net_checksum(const u8 *p, int n) {
    u32 s = 0;
    while (n > 1) {
        s += net_be16(p);
        p += 2;
        n -= 2;
    }
    if (n)
        s += (u16)(*p << 8);
    while (s >> 16)
        s = (s & 0xffff) + (s >> 16);
    return (u16)~s;
}

u16 net_tcp_udp_checksum(const u8 *src_ip, const u8 *dst_ip, u8 proto, const u8 *payload, int len) {
    u32 s = 0;
    /* Pseudo header */
    s += (src_ip[0] << 8) | src_ip[1];
    s += (src_ip[2] << 8) | src_ip[3];
    s += (dst_ip[0] << 8) | dst_ip[1];
    s += (dst_ip[2] << 8) | dst_ip[3];
    s += proto;
    s += (u16)len;

    const u8 *p = payload;
    int n = len;
    while (n > 1) {
        s += (p[0] << 8) | p[1];
        p += 2;
        n -= 2;
    }
    if (n)
        s += (p[0] << 8);

    while (s >> 16)
        s = (s & 0xffff) + (s >> 16);
    return (u16)~s;
}

int net_parse_ip(const char *str, u8 *out_ip) {
    if (!str || !out_ip)
        return 0;
    int part = 0;
    u32 val = 0;
    int has_digits = 0;
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            val = val * 10 + (*str - '0');
            if (val > 255)
                return 0;
            has_digits = 1;
        } else if (*str == '.') {
            if (!has_digits || part >= 3)
                return 0;
            out_ip[part++] = (u8)val;
            val = 0;
            has_digits = 0;
        } else {
            return 0;
        }
        str++;
    }
    if (!has_digits || part != 3)
        return 0;
    out_ip[3] = (u8)val;
    return 1;
}

void net_format_ip(const u8 *ip, char *out_str) {
    char *p = out_str;
    for (int i = 0; i < 4; i++) {
        char buf[12];
        number(buf, ip[i]);
        char *b = buf;
        while (*b)
            *p++ = *b++;
        if (i < 3)
            *p++ = '.';
    }
    *p = 0;
}

void net_format_mac(const u8 *mac, char *out_str) {
    static const char hex[] = "0123456789abcdef";
    char *p = out_str;
    for (int i = 0; i < 6; i++) {
        *p++ = hex[(mac[i] >> 4) & 15];
        *p++ = hex[mac[i] & 15];
        if (i < 5)
            *p++ = ':';
    }
    *p = 0;
}
