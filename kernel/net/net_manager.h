#ifndef POLLIK_NET_MANAGER_H
#define POLLIK_NET_MANAGER_H

#include "net_if.h"
#include "tcp.h"

void net_manager_init(void);
void net_manager_poll(void);
/* Administrative block applies to all interfaces, including cached sockets. */
int net_manager_enabled(void);
void net_manager_set_enabled(int enabled);
int net_manager_is_connected(void);
NetworkInterface *net_manager_get_active_iface(void);
NetworkInterface *net_manager_get_ethernet_iface(void);
NetworkInterface *net_manager_get_wifi_iface(void);
int net_manager_get_local_ip(u8 *out_ip);
int net_manager_get_gateway(u8 *out_gw);
int net_manager_get_dns(u8 *out_dns);
int net_manager_get_mac(u8 *out_mac);
void net_manager_get_status_text(char *out, int max_len);
int net_manager_resolve_host(const char *hostname, u8 *out_ip);
TcpSocket *net_manager_connect_tcp(const u8 *ip, u16 port);
void net_on_frame_received(NetworkInterface *iface, const u8 *frame, int len);

#endif
