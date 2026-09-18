#ifndef POLLIK_DNS_H
#define POLLIK_DNS_H

#include "net_if.h"

void dns_init(void);
int dns_resolve(NetworkInterface *iface, const char *hostname, u8 *out_ip, int timeout_ticks);
void dns_cache_dump(void);

#endif
