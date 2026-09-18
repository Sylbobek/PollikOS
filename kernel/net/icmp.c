#include "icmp.h"
#include "ipv4.h"
#include "net_util.h"

static u16 ping_seq = 0;
static u32 ping_start_tick = 0;
static u32 ping_rtt = 0;
static int ping_pending = 0;
static const char *ping_status = "PING IDLE";

void icmp_init(void) {
    ping_seq = 0;
    ping_pending = 0;
    ping_status = "PING IDLE";
}

static NetworkInterface *pending_iface = 0;
static u8 pending_dst_ip[4];

static void icmp_send_echo(void) {
    if (!pending_iface)
        return;

    u8 packet[64];
    memset(packet, 0, sizeof(packet));

    packet[0] = 8; /* Echo request */
    packet[1] = 0; /* Code */
    net_put16(packet + 2, 0); /* Checksum placeholder */
    net_put16(packet + 4, 0x504f); /* Identifier: 'PO' */
    net_put16(packet + 6, ping_seq); /* Sequence number */
    memcpy(packet + 8, "PollikOS", 8);

    net_put16(packet + 2, net_checksum(packet, 16));

    if (ipv4_send(pending_iface, IPV4_PROTO_ICMP, pending_dst_ip, packet, 16)) {
        ping_status = "PING SENT...";
        serial("NET ICMP echo sent\n");
    } else {
        ping_status = "RESOLVING GATEWAY...";
    }
}

void icmp_ping(NetworkInterface *iface, const u8 *dst_ip) {
    if (!iface)
        return;

    ping_seq++;
    ping_start_tick = ticks;
    ping_pending = 1;
    pending_iface = iface;
    memcpy(pending_dst_ip, dst_ip, 4);

    icmp_send_echo();
}

void icmp_on_arp_resolved(NetworkInterface *iface, const u8 *ip) {
    if (ping_pending && pending_iface == iface && net_same(pending_dst_ip, ip, 4)) {
        icmp_send_echo();
    }
}

void icmp_on_packet(NetworkInterface *iface, const u8 *src_ip, const u8 *payload, int len) {
    if (len < 8)
        return;

    u8 type = payload[0];
    u8 code = payload[1];
    u16 csum = net_be16(payload + 2);
    (void)csum;

    if (net_checksum(payload, len) != 0)
        return; /* Corrupt ICMP */

    if (type == 8 && code == 0) {
        /* Echo Request -> Send Echo Reply */
        u8 reply[128];
        if (len > (int)sizeof(reply))
            len = (int)sizeof(reply);
        memcpy(reply, payload, (unsigned)len);
        reply[0] = 0; /* Echo Reply */
        net_put16(reply + 2, 0);
        net_put16(reply + 2, net_checksum(reply, len));

        ipv4_send(iface, IPV4_PROTO_ICMP, src_ip, reply, len);
    } else if (type == 0 && code == 0) {
        /* Echo Reply */
        u16 id = net_be16(payload + 4);
        u16 seq = net_be16(payload + 6);

        if (ping_pending && id == 0x504f && seq == ping_seq) {
            ping_pending = 0;
            ping_rtt = (ticks >= ping_start_tick) ? (ticks - ping_start_tick) * 1000 / 120 : 0; /* ms */
            ping_status = "PING OK - GATEWAY REPLIED";
            serial("NET PING reply OK\n");
        }
    }
}

int icmp_is_pending(void) {
    if (ping_pending && (ticks - ping_start_tick > 300)) {
        ping_pending = 0;
        ping_status = "PING TIMEOUT";
        serial("ICMP Ping timeout\n");
    }
    return ping_pending;
}

const char *icmp_get_status(void) {
    icmp_is_pending();
    return ping_status;
}

u32 icmp_get_rtt(void) {
    return ping_rtt;
}
