#ifndef POLLIK_IPV4_H
#define POLLIK_IPV4_H

#include "net_if.h"

#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_TCP  6
#define IPV4_PROTO_UDP  17

int ipv4_send(NetworkInterface *iface, u8 protocol, const u8 *dst_ip, const u8 *payload, int payload_len);
void ipv4_on_packet(NetworkInterface *iface, const u8 *frame, int len);

#endif
