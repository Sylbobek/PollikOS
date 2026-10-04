#ifndef POLLIK_NET_IF_H
#define POLLIK_NET_IF_H

#ifdef POLLIK_X64
#include "../arch/x86_64/net_platform.h"
#else
#include "../system.h"
#include "../mem.h"
#endif

typedef enum {
    IF_TYPE_ETHERNET = 0,
    IF_TYPE_WIFI     = 1
} NetIfType;

typedef struct NetworkInterface {
    const char *name;
    NetIfType type;
    u8 mac[6];
    u8 ip[4];
    u8 netmask[4];
    u8 gateway[4];
    u8 dns[4];
    u16 mtu;
    int link_up;
    int dhcp_enabled;
    int dhcp_bound;
    u32 tx_packets;
    u32 rx_packets;
    u32 tx_bytes;
    u32 rx_bytes;

    int (*send_frame)(struct NetworkInterface *iface, const u8 *frame, int len);
    void (*poll)(struct NetworkInterface *iface);
    void *driver_data;
} NetworkInterface;

#endif
