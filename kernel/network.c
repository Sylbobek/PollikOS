#include "system.h"
#include "net/net_manager.h"
#include "net/icmp.h"
#include "net/dhcp.h"
#include "net/net_util.h"

int net_ready = 0;
const char *net_status = "NO NETWORK CARD";

void net_init(void) {
    net_manager_init();
    net_ready = net_manager_is_connected();
    if (net_ready) {
        NetworkInterface *iface = net_manager_get_active_iface();
        net_status = iface ? (iface->type == IF_TYPE_ETHERNET ? "CONNECTED - ETHERNET" : "CONNECTED - WI-FI") : "OFFLINE";
    } else {
        net_status = "CONNECTING (DHCP)...";
    }
}

void net_poll(void) {
    net_manager_poll();
    net_ready = net_manager_is_connected();

    NetworkInterface *iface = net_manager_get_active_iface();
    if (iface) {
        const char *istat = icmp_get_status();
        if (icmp_is_pending()) {
            net_status = istat;
        } else if (istat && istat[0] == 'P' && istat[5] == 'O') {
            net_status = istat; /* PING OK - GATEWAY REPLIED */
        } else if (iface->dhcp_bound) {
            net_status = iface->type == IF_TYPE_ETHERNET ? "CONNECTED - ETHERNET" : "CONNECTED - WI-FI";
        } else {
            net_status = dhcp_get_status();
        }
    } else {
        net_status = "OFFLINE - NO LINK";
    }
}

void net_ping(void) {
    NetworkInterface *iface = net_manager_get_active_iface();
    if (!iface || !net_ready)
        return;
    icmp_ping(iface, iface->gateway);
}

void net_info(char *out) {
    NetworkInterface *iface = net_manager_get_active_iface();
    char *p = out;

    #define APPEND(s) do { const char *as = (s); while (*as) *p++ = *as++; } while(0)

    if (!iface) {
        APPEND("NO ACTIVE NETWORK INTERFACE\nOFFLINE\n");
        *p = 0;
        return;
    }

    APPEND(iface->name);
    APPEND(iface->type == IF_TYPE_ETHERNET ? " (ETHERNET) / " : " (WI-FI) / ");
    APPEND(iface->link_up ? "LINK UP\n" : "LINK DOWN\n");

    APPEND("IP ");
    char buf[32];
    net_format_ip(iface->ip, buf);
    APPEND(buf);

    APPEND("  MASK ");
    net_format_ip(iface->netmask, buf);
    APPEND(buf);

    APPEND("\nGATEWAY ");
    net_format_ip(iface->gateway, buf);
    APPEND(buf);

    APPEND("  DNS ");
    net_format_ip(iface->dns, buf);
    APPEND(buf);

    APPEND("\nMAC ");
    net_format_mac(iface->mac, buf);
    APPEND(buf);

    APPEND("\nSTATUS: ");
    APPEND(net_status);

    APPEND("\nTX ");
    number(buf, iface->tx_packets);
    APPEND(buf);

    APPEND("  RX ");
    number(buf, iface->rx_packets);
    APPEND(buf);

    *p = 0;
}
