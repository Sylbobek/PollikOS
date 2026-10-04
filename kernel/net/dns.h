#ifndef POLLIK_DNS_H
#define POLLIK_DNS_H

#include "net_if.h"

void dns_init(void);
int dns_resolve(NetworkInterface *iface, const char *hostname, u8 *out_ip, int timeout_ticks);
/* Nonblocking resolver: start returns 1 for an immediate result, 0 pending,
 * and -1 on failure. poll uses the same result convention. */
int dns_resolve_start(NetworkInterface *iface, const char *hostname, u8 *out_ip);
int dns_resolve_poll(u8 *out_ip);
void dns_cache_dump(void);

#endif
