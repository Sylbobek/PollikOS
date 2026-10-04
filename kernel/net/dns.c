#include "dns.h"
#include "udp.h"
#include "net_util.h"

#define DNS_CACHE_SIZE 16

typedef struct {
    char name[64];
    u8 ip[4];
    u32 expire_tick;
    int valid;
} DnsCacheEntry;

static DnsCacheEntry dns_cache[DNS_CACHE_SIZE];
static u16 dns_query_id = 0x444e; /* 'DN' */
static u16 dns_local_port = 53053;
static volatile int dns_pending = 0;
static u8 dns_resolved_ip[4];
static char dns_pending_name[64];
static NetworkInterface *dns_pending_iface;
static u8 dns_query_packet[256];
static int dns_query_length;
static u32 dns_deadline_tick, dns_retry_tick;

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void dns_init(void) {
    memset(dns_cache, 0, sizeof(dns_cache));
    dns_pending = 0;
    dns_pending_iface = 0;
}

static int dns_cache_lookup(const char *hostname, u8 *out_ip) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && str_eq(dns_cache[i].name, hostname)) {
            if ((int)(dns_cache[i].expire_tick - ticks) > 0) {
                memcpy(out_ip, dns_cache[i].ip, 4);
                return 1;
            } else {
                dns_cache[i].valid = 0; /* Expired */
            }
        }
    }
    return 0;
}

static void dns_cache_insert(const char *hostname, const u8 *ip, u32 ttl_seconds) {
    if (!ttl_seconds) return;
    if (ttl_seconds > 86400)
        ttl_seconds = 86400; /* Max 24h */

    int slot = -1;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* Evict oldest or first */
        slot = 0;
    }

    str_copy(dns_cache[slot].name, hostname, sizeof(dns_cache[slot].name));
    memcpy(dns_cache[slot].ip, ip, 4);
    dns_cache[slot].expire_tick = ticks + ttl_seconds * 100u;
    dns_cache[slot].valid = 1;
}

static void dns_on_udp(NetworkInterface *iface, const u8 *src_ip, u16 src_port, const u8 *data, int len) {
    if (!iface || iface != dns_pending_iface || src_port != 53 || !net_same(src_ip, iface->dns, 4)) return;

    if (dns_pending != 1 || len < 12)
        return;

    u16 id = net_be16(data);
    if (id != dns_query_id)
        return;

    u16 flags = net_be16(data + 2);
    if (!(flags & 0x8000) || (flags & 0x0200))
        return; /* Not a response */
    if ((flags & 0x000f) != 0) {
        /* RCODE error (e.g. NXDOMAIN) */
        dns_pending = -1;
        return;
    }

    u16 qdcount = net_be16(data + 4);
    u16 ancount = net_be16(data + 6);
    if (ancount == 0) {
        dns_pending = -1;
        return;
    }

    int idx = 12;

    /* Skip Question records */
    for (int q = 0; q < qdcount && idx < len; q++) {
        while (idx < len && data[idx] != 0) {
            if ((data[idx] & 0xc0) == 0xc0) {
                idx += 2;
                goto question_type;
            }
            idx += 1 + data[idx];
        }
        if (idx < len && data[idx] == 0)
            idx++;
question_type:
        idx += 4; /* QTYPE (2) + QCLASS (2) */
    }

    /* Parse Answers */
    for (int a = 0; a < ancount && idx < len; a++) {
        /* Skip name */
        if ((data[idx] & 0xc0) == 0xc0) {
            idx += 2;
        } else {
            while (idx < len && data[idx] != 0)
                idx += 1 + data[idx];
            if (idx < len && data[idx] == 0)
                idx++;
        }

        if (idx + 10 > len)
            break;

        u16 type = net_be16(data + idx);
        u16 class_val = net_be16(data + idx + 2);
        u32 ttl = net_be32(data + idx + 4);
        u16 rdlength = net_be16(data + idx + 8);
        idx += 10;

        if (type == 1 && class_val == 1 && rdlength == 4 && idx + 4 <= len) {
            /* Found A record */
            memcpy(dns_resolved_ip, data + idx, 4);
            dns_cache_insert(dns_pending_name, dns_resolved_ip, ttl);
            dns_pending = 2; /* Success */
            serial("DNS: Resolved domain ");
            serial(dns_pending_name);
            serial(" -> ");
            char ip_str[20];
            net_format_ip(dns_resolved_ip, ip_str);
            serial(ip_str);
            serial("\n");
            return;
        }
        idx += rdlength;
    }

    dns_pending = -1; /* No A record found */
}

int dns_resolve_start(NetworkInterface *iface, const char *hostname, u8 *out_ip) {
    if (!iface || !hostname || !out_ip || strlen(hostname) >= sizeof(dns_pending_name) || dns_pending == 1)
        return -1;
    if (net_parse_ip(hostname, out_ip)) return 1;
    if (dns_cache_lookup(hostname, out_ip)) {
        serial("DNS: Cache hit for "); serial(hostname); serial("\n");
        return 1;
    }

    u8 *packet = dns_query_packet;
    memset(packet, 0, sizeof(dns_query_packet));
    dns_query_id++;
    net_put16(packet + 0, dns_query_id);
    net_put16(packet + 2, 0x0100);
    net_put16(packet + 4, 1);
    int idx = 12;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int label_len = (int)(dot - p);
        if (label_len <= 0 || label_len > 63 || idx + label_len + 6 >= (int)sizeof(dns_query_packet))
            return -1;
        packet[idx++] = (u8)label_len;
        for (int i = 0; i < label_len; ++i) packet[idx++] = (u8)p[i];
        p = dot;
        if (*p == '.') p++;
    }
    packet[idx++] = 0;
    net_put16(packet + idx, 1);
    net_put16(packet + idx + 2, 1);
    idx += 4;
    dns_query_length = idx;

    dns_local_port++;
    if (dns_local_port < 50000 || dns_local_port > 60000) dns_local_port = 50001;
    udp_bind(dns_local_port, dns_on_udp);
    str_copy(dns_pending_name, hostname, sizeof(dns_pending_name));
    dns_pending_iface = iface;
    dns_pending = 1;
    dns_deadline_tick = ticks + 500;
    dns_retry_tick = ticks;

    serial("DNS: Querying "); serial(hostname); serial(" at ");
    char dns_ip_str[20];
    net_format_ip(iface->dns, dns_ip_str); serial(dns_ip_str); serial("\n");
    udp_send(iface, iface->dns, dns_local_port, 53, packet, dns_query_length);
    dns_retry_tick = ticks;
    return 0;
}

int dns_resolve_poll(u8 *out_ip) {
    if (!out_ip || dns_pending == 0) return -1;
    if (dns_pending == 1 && (u32)(ticks - dns_deadline_tick) < 0x80000000u) dns_pending = -1;
    if (dns_pending == 1 && (u32)(ticks - dns_retry_tick) >= 30) {
        if (dns_pending_iface)
            udp_send(dns_pending_iface, dns_pending_iface->dns, dns_local_port, 53,
                     dns_query_packet, dns_query_length);
        dns_retry_tick = ticks;
    }
    if (dns_pending == 1) return 0;
    udp_unbind(dns_local_port);
    dns_pending_iface = 0;
    if (dns_pending == 2) {
        memcpy(out_ip, dns_resolved_ip, 4);
        dns_pending = 0;
        return 1;
    }
    serial("DNS: Resolution failed/timeout for "); serial(dns_pending_name); serial("\n");
    dns_pending = 0;
    return -1;
}

int dns_resolve(NetworkInterface *iface, const char *hostname, u8 *out_ip, int timeout_ticks) {
    int result = dns_resolve_start(iface, hostname, out_ip);
    if (result != 0) return result > 0;
    u32 start = ticks;
    while ((result = dns_resolve_poll(out_ip)) == 0 && ticks - start < (u32)timeout_ticks) {
        if (iface->poll) iface->poll(iface);
        net_service_wait();
    }
    if (result == 0) {
        /* Expire and unbind through the common completion path. */
        dns_deadline_tick = ticks;
        (void)dns_resolve_poll(out_ip);
        return 0;
    }
    return result > 0;
}
