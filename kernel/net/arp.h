#ifndef POLLIK_ARP_H
#define POLLIK_ARP_H

#include "net_if.h"

void arp_init(void);
int arp_lookup(const u8 *ip, u8 *out_mac);
void arp_request(NetworkInterface *iface, const u8 *target_ip);
void arp_reply(NetworkInterface *iface, const u8 *dest_mac, const u8 *target_ip);
void arp_on_packet(NetworkInterface *iface, const u8 *frame, int len);

#endif
