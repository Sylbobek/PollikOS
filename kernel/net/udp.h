#ifndef POLLIK_UDP_H
#define POLLIK_UDP_H

#include "net_if.h"

typedef void (*udp_callback_t)(NetworkInterface *iface, const u8 *src_ip, u16 src_port, const u8 *data, int len);

void udp_init(void);
int udp_bind(u16 local_port, udp_callback_t cb);
void udp_unbind(u16 local_port);
int udp_send(NetworkInterface *iface, const u8 *dst_ip, u16 src_port, u16 dst_port, const u8 *data, int len);
void udp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *dst_ip, const u8 *payload, int len);

#endif
