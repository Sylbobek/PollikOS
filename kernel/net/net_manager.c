#include "net_manager.h"
#include "rtl8139.h"
#include "wifi_if.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "dhcp.h"
#include "dns.h"
#include "net_util.h"

static NetworkInterface eth_iface;
static NetworkInterface wifi_iface;
static NetworkInterface *active_iface = 0;
static int eth_available = 0;
static int wifi_available = 0;
static int last_eth_link = -1;

void net_manager_init(void) {
    arp_init();
    icmp_init();
    udp_init();
    tcp_init();
    dhcp_init();
    dns_init();

    eth_available = rtl8139_init(&eth_iface);
    wifi_available = wifi_init(&wifi_iface);

    if (eth_available && eth_iface.link_up) {
        active_iface = &eth_iface;
        dhcp_start(&eth_iface);
    } else if (wifi_available && wifi_iface.link_up) {
        active_iface = &wifi_iface;
    } else if (eth_available) {
        active_iface = &eth_iface;
    } else {
        active_iface = 0;
    }

    last_eth_link = eth_available ? eth_iface.link_up : -1;
    serial("NetworkManager: Initialized. Active interface: ");
    serial(active_iface ? active_iface->name : "none");
    serial("\n");
}

void net_manager_poll(void) {
    if (eth_available && eth_iface.poll)
        eth_iface.poll(&eth_iface);
    if (wifi_available && wifi_iface.poll)
        wifi_iface.poll(&wifi_iface);

    /* Automatic interface switching: Ethernet preferred over Wi-Fi */
    if (eth_available) {
        int eth_link = eth_iface.link_up;
        if (eth_link != last_eth_link) {
            last_eth_link = eth_link;
            if (eth_link) {
                serial("NetworkManager: Ethernet link UP, selecting eth0\n");
                active_iface = &eth_iface;
                if (!eth_iface.dhcp_bound)
                    dhcp_start(&eth_iface);
            } else {
                eth_iface.dhcp_bound = 0;
                memset(eth_iface.ip, 0, 4);
                arp_init();
                dns_init();
                serial("NetworkManager: Ethernet link DOWN, falling back to Wi-Fi\n");
                if (wifi_available && wifi_iface.link_up) {
                    active_iface = &wifi_iface;
                } else {
                    active_iface = 0;
                }
            }
        }
    }

    if (active_iface && active_iface->type == IF_TYPE_ETHERNET) {
        dhcp_poll(active_iface);
    }

    tcp_poll_all();
    icmp_is_pending();
}

int net_manager_is_connected(void) {
    return active_iface && active_iface->link_up && active_iface->dhcp_bound;
}

NetworkInterface *net_manager_get_active_iface(void) {
    return active_iface;
}

NetworkInterface *net_manager_get_ethernet_iface(void) {
    return eth_available ? &eth_iface : 0;
}

NetworkInterface *net_manager_get_wifi_iface(void) {
    return wifi_available ? &wifi_iface : 0;
}

int net_manager_get_local_ip(u8 *out_ip) {
    if (!active_iface)
        return 0;
    memcpy(out_ip, active_iface->ip, 4);
    return 1;
}

int net_manager_get_gateway(u8 *out_gw) {
    if (!active_iface)
        return 0;
    memcpy(out_gw, active_iface->gateway, 4);
    return 1;
}

int net_manager_get_dns(u8 *out_dns) {
    if (!active_iface)
        return 0;
    memcpy(out_dns, active_iface->dns, 4);
    return 1;
}

int net_manager_get_mac(u8 *out_mac) {
    if (!active_iface)
        return 0;
    memcpy(out_mac, active_iface->mac, 6);
    return 1;
}

void net_manager_get_status_text(char *out, int max_len) {
    if (!out || max_len <= 0)
        return;

    char *p = out;
    const char *status_line = "OFFLINE";
    if (net_manager_is_connected()) {
        status_line = active_iface->type == IF_TYPE_ETHERNET ? "ETHERNET CONNECTED" : "WI-FI CONNECTED";
    }

    while (*status_line && (p - out < max_len - 1))
        *p++ = *status_line++;
    *p = 0;
}

int net_manager_resolve_host(const char *hostname, u8 *out_ip) {
    if (!active_iface)
        return 0;
    u32 start = ticks;
    while (!net_manager_is_connected() && ticks - start < 1000u) {
        net_manager_poll();
        net_service_wait();
    }
    if (!net_manager_is_connected()) return 0;
    return dns_resolve(active_iface, hostname, out_ip, 500);
}

TcpSocket *net_manager_connect_tcp(const u8 *ip, u16 port) {
    if (!active_iface)
        return 0;
    return tcp_connect(active_iface, ip, port, 300); /* 3 seconds */
}

void net_on_frame_received(NetworkInterface *iface, const u8 *frame, int len) {
    if (len < 14)
        return;

    u16 ether_type = net_be16(frame + 12);
    if (ether_type == 0x0806) {
        /* ARP */
        arp_on_packet(iface, frame, len);
    } else if (ether_type == 0x0800) {
        /* IPv4 */
        ipv4_on_packet(iface, frame, len);
    }
}
