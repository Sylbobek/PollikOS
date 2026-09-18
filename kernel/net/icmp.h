#ifndef POLLIK_ICMP_H
#define POLLIK_ICMP_H

#include "net_if.h"

void icmp_init(void);
void icmp_ping(NetworkInterface *iface, const u8 *dst_ip);
void icmp_on_arp_resolved(NetworkInterface *iface, const u8 *ip);
void icmp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *payload, int len);
int icmp_is_pending(void);
const char *icmp_get_status(void);
u32 icmp_get_rtt(void);

#endif
