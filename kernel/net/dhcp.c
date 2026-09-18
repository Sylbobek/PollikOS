#include "dhcp.h"
#include "udp.h"
#include "net_util.h"

typedef enum {
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_DISCOVER,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
    DHCP_STATE_RENEWING,
    DHCP_STATE_REBINDING,
    DHCP_STATE_FAILED
} DhcpState;

static DhcpState dhcp_state = DHCP_STATE_IDLE;
static u32 dhcp_xid = 0x504f4c4c; /* 'POLL' */
static u32 dhcp_timer = 0;
static int dhcp_retries = 0;
static u32 lease_seconds=3600,lease_start=0;
static u8 offered_ip[4];
static u8 server_ip[4];
static u8 offered_netmask[4];
static u8 offered_gateway[4];
static u8 offered_dns[4];
static const char *dhcp_status = "DHCP IDLE";

static const u8 broadcast_ip[4] = {255, 255, 255, 255};

static void dhcp_send_discover(NetworkInterface *iface) {
    u8 packet[350];
    memset(packet, 0, sizeof(packet));

    packet[0] = 1; /* BOOTREQUEST */
    packet[1] = 1; /* Ethernet */
    packet[2] = 6; /* MAC length */
    packet[3] = 0; /* Hops */
    net_put32(packet + 4, dhcp_xid);
    net_put16(packet + 8, 0); /* Secs */
    net_put16(packet + 10, 0x8000); /* Broadcast flag */

    /* Client HW address */
    memcpy(packet + 28, iface->mac, 6);

    /* Magic Cookie */
    packet[236] = 0x63;
    packet[237] = 0x82;
    packet[238] = 0x53;
    packet[239] = 0x63;

    int opt = 240;
    /* Option 53: DHCP Message Type = DHCPDISCOVER (1) */
    packet[opt++] = 53;
    packet[opt++] = 1;
    packet[opt++] = 1;

    /* Option 55: Parameter Request List */
    packet[opt++] = 55;
    packet[opt++] = 3;
    packet[opt++] = 1; /* Subnet Mask */
    packet[opt++] = 3; /* Router */
    packet[opt++] = 6; /* Domain Name Server */

    /* Option 255: End */
    packet[opt++] = 255;

    udp_send(iface, broadcast_ip, 68, 67, packet, opt);
    dhcp_state = DHCP_STATE_SELECTING;
    dhcp_timer = ticks;
    dhcp_status = "DHCP DISCOVER SENT...";
    serial("DHCP: Discover sent\n");
}

static void dhcp_send_request(NetworkInterface *iface) {
    int renewing=dhcp_state==DHCP_STATE_RENEWING||dhcp_state==DHCP_STATE_REBINDING;
    u8 packet[350];
    memset(packet, 0, sizeof(packet));

    packet[0] = 1; /* BOOTREQUEST */
    packet[1] = 1; /* Ethernet */
    packet[2] = 6; /* MAC length */
    packet[3] = 0; /* Hops */
    net_put32(packet + 4, dhcp_xid);
    net_put16(packet + 8, 0);
    net_put16(packet + 10, 0x8000); /* Broadcast flag */

    /* Client HW address */
    memcpy(packet + 28, iface->mac, 6);

    /* Magic Cookie */
    packet[236] = 0x63;
    packet[237] = 0x82;
    packet[238] = 0x53;
    packet[239] = 0x63;

    int opt = 240;
    /* Option 53: DHCP Message Type = DHCPREQUEST (3) */
    packet[opt++] = 53;
    packet[opt++] = 1;
    packet[opt++] = 3;

    if(renewing)memcpy(packet+12,iface->ip,4);
    /* Option 50: Requested IP */
    packet[opt++] = 50;
    packet[opt++] = 4;
    memcpy(packet + opt, offered_ip, 4);
    opt += 4;

    /* Option 54: Server Identifier */
    packet[opt++] = 54;
    packet[opt++] = 4;
    memcpy(packet + opt, server_ip, 4);
    opt += 4;

    if(renewing)opt=243; /* ciaddr replaces requested IP and server ID. */
    /* Option 55: Parameter Request List */
    packet[opt++] = 55;
    packet[opt++] = 3;
    packet[opt++] = 1;
    packet[opt++] = 3;
    packet[opt++] = 6;

    /* Option 255: End */
    packet[opt++] = 255;

    udp_send(iface, dhcp_state==DHCP_STATE_RENEWING?server_ip:broadcast_ip, 68, 67, packet, opt);
    if(!renewing)dhcp_state = DHCP_STATE_REQUESTING;
    dhcp_timer = ticks;
    dhcp_status = "DHCP REQUEST SENT...";
    serial("DHCP: Request sent\n");
}

static void dhcp_on_udp(NetworkInterface *iface, const u8 *src_ip, u16 src_port, const u8 *data, int len) {
    (void)src_ip;
    if (src_port != 67 || data == 0) return;

    if (len < 244)
        return;

    if (data[1] != 1 || data[2] != 6 || !net_same(data + 28, iface->mac, 6)) return;
    if (data[0] != 2) /* BOOTREPLY */
        return;

    u32 xid = net_be32(data + 4);
    if (xid != dhcp_xid)
        return;

    /* Check magic cookie */
    if (data[236] != 0x63 || data[237] != 0x82 || data[238] != 0x53 || data[239] != 0x63)
        return;

    /* Parse options */
    u8 msg_type = 0;
    int opt = 240;
    while (opt < len && data[opt] != 255) {
        u8 code = data[opt++];
        if (code == 0) /* Pad */
            continue;
        if (opt >= len)
            break;
        u8 opt_len = data[opt++];
        if (opt + opt_len > len)
            break;

        if (code == 53 && opt_len >= 1) {
            msg_type = data[opt];
        } else if (code == 1 && opt_len == 4) {
            memcpy(offered_netmask, data + opt, 4);
        } else if (code == 3 && opt_len >= 4) {
            memcpy(offered_gateway, data + opt, 4);
        } else if (code == 6 && opt_len >= 4) {
            memcpy(offered_dns, data + opt, 4);
        } else if(code==51 && opt_len==4) {
            lease_seconds=net_be32(data+opt);if(lease_seconds>86400)lease_seconds=86400;if(!lease_seconds)lease_seconds=1;
        } else if (code == 54 && opt_len == 4) {
            memcpy(server_ip, data + opt, 4);
        }
        opt += opt_len;
    }

    if (dhcp_state == DHCP_STATE_SELECTING && msg_type == 2) {
        /* DHCPOFFER received */
        memcpy(offered_ip, data + 16, 4); /* yiaddr */
        serial("DHCP: Offer received\n");
        dhcp_send_request(iface);
    } else if ((dhcp_state == DHCP_STATE_REQUESTING || dhcp_state==DHCP_STATE_RENEWING || dhcp_state==DHCP_STATE_REBINDING) && msg_type == 5) {
        /* DHCPACK received */
        memcpy(iface->ip, offered_ip, 4);
        if (offered_netmask[0] != 0)
            memcpy(iface->netmask, offered_netmask, 4);
        if (offered_gateway[0] != 0)
            memcpy(iface->gateway, offered_gateway, 4);
        if (offered_dns[0] != 0)
            memcpy(iface->dns, offered_dns, 4);

        iface->dhcp_bound = 1;
        lease_start=ticks;
        dhcp_state = DHCP_STATE_BOUND;
        dhcp_status = "DHCP BOUND - IP ASSIGNED";
        serial("DHCP: Bound successfully, IP=");
        char ip_str[20];
        net_format_ip(iface->ip, ip_str);
        serial(ip_str);
        serial("\n");
    }
}

void dhcp_init(void) {
    dhcp_state = DHCP_STATE_IDLE;
    dhcp_retries = 0;
    dhcp_status = "DHCP IDLE";
    memset(offered_ip, 0, 4);
    memset(server_ip, 0, 4);
    memset(offered_netmask, 0, 4);
    memset(offered_gateway, 0, 4);
    memset(offered_dns, 0, 4);

    udp_bind(68, dhcp_on_udp);
}

void dhcp_start(NetworkInterface *iface) {
    if (!iface)
        return;
    iface->dhcp_bound = 0;
    memset(iface->ip, 0, 4);
    memset(iface->netmask, 0, 4);
    memset(iface->gateway, 0, 4);
    memset(iface->dns, 0, 4);
    memset(offered_netmask, 0, 4);
    memset(offered_gateway, 0, 4);
    memset(offered_dns, 0, 4);
    dhcp_retries = 0;
    dhcp_xid++;
    dhcp_send_discover(iface);
}

void dhcp_poll(NetworkInterface *iface) {
    if (!iface || !iface->link_up || dhcp_state == DHCP_STATE_IDLE)return;
    if(iface->dhcp_bound) {
        u32 elapsed=ticks-lease_start,total=lease_seconds*100;
        if(elapsed>=total) {dhcp_start(iface);return;}
        if(elapsed>=total*7/8 && dhcp_state!=DHCP_STATE_REBINDING){dhcp_state=DHCP_STATE_REBINDING;dhcp_send_request(iface);}
        else if(elapsed>=total/2 && dhcp_state==DHCP_STATE_BOUND){dhcp_state=DHCP_STATE_RENEWING;dhcp_send_request(iface);}
        if(dhcp_state==DHCP_STATE_BOUND)return;
        if(ticks-dhcp_timer>100)dhcp_send_request(iface);
        return;
    }

    if (dhcp_state == DHCP_STATE_FAILED) {
        if (ticks - dhcp_timer > 3000) dhcp_start(iface);
        return;
    }
    if (ticks - dhcp_timer > 150) { /* 1.5 seconds timeout */
        if (dhcp_retries < 3) {
            dhcp_retries++;
            serial("DHCP: Retrying...\n");
            dhcp_send_discover(iface);
        } else {
            iface->dhcp_bound = 0;
            memset(iface->ip, 0, 4);
            dhcp_state = DHCP_STATE_FAILED;
            dhcp_timer = ticks;
            dhcp_status = "DHCP FAILED - OFFLINE";
            serial("DHCP: no lease; remaining offline\n");
        }
    }
}

int dhcp_is_bound(NetworkInterface *iface) {
    return iface && iface->dhcp_bound;
}

const char *dhcp_get_status(void) {
    return dhcp_status;
}
