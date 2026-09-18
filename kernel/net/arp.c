#include "arp.h"
#include "icmp.h"
#include "net_util.h"

#define ARP_CACHE_SIZE 16

typedef struct {
    u8 ip[4];
    u8 mac[6];
    u32 timestamp;
    int valid;
} ArpEntry;

static ArpEntry arp_table[ARP_CACHE_SIZE];

void arp_init(void) {
    memset(arp_table, 0, sizeof(arp_table));
}

int arp_lookup(const u8 *ip, u8 *out_mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_table[i].valid && net_same(arp_table[i].ip, ip, 4)) {
            memcpy(out_mac, arp_table[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

static void arp_cache_insert(const u8 *ip, const u8 *mac) {
    /* Update existing */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_table[i].valid && net_same(arp_table[i].ip, ip, 4)) {
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].timestamp = ticks;
            return;
        }
    }

    /* Find empty or oldest slot */
    int slot = 0;
    u32 oldest = 0xffffffff;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_table[i].valid) {
            slot = i;
            break;
        }
        if (arp_table[i].timestamp < oldest) {
            oldest = arp_table[i].timestamp;
            slot = i;
        }
    }

    memcpy(arp_table[slot].ip, ip, 4);
    memcpy(arp_table[slot].mac, mac, 6);
    arp_table[slot].timestamp = ticks;
    arp_table[slot].valid = 1;
}

void arp_request(NetworkInterface *iface, const u8 *target_ip) {
    if (!iface || !iface->send_frame)
        return;

    u8 packet[60];
    memset(packet, 0, sizeof(packet));

    /* Ethernet header */
    memset(packet, 0xff, 6);               /* Broadcast destination */
    memcpy(packet + 6, iface->mac, 6);     /* Source MAC */
    net_put16(packet + 12, 0x0806);        /* EtherType: ARP */

    /* ARP Payload */
    net_put16(packet + 14, 1);             /* Hardware type: Ethernet */
    net_put16(packet + 16, 0x0800);        /* Protocol type: IPv4 */
    packet[18] = 6;                        /* HW address length */
    packet[19] = 4;                        /* Protocol address length */
    net_put16(packet + 20, 1);             /* Opcode: Request (1) */
    memcpy(packet + 22, iface->mac, 6);    /* Sender MAC */
    memcpy(packet + 28, iface->ip, 4);     /* Sender IP */
    memset(packet + 32, 0, 6);             /* Target MAC (unknown) */
    memcpy(packet + 38, target_ip, 4);     /* Target IP */

    iface->send_frame(iface, packet, 42);
}

void arp_reply(NetworkInterface *iface, const u8 *dest_mac, const u8 *target_ip) {
    if (!iface || !iface->send_frame)
        return;

    u8 packet[60];
    memset(packet, 0, sizeof(packet));

    /* Ethernet header */
    memcpy(packet, dest_mac, 6);           /* Destination MAC */
    memcpy(packet + 6, iface->mac, 6);     /* Source MAC */
    net_put16(packet + 12, 0x0806);        /* EtherType: ARP */

    /* ARP Payload */
    net_put16(packet + 14, 1);             /* Hardware type: Ethernet */
    net_put16(packet + 16, 0x0800);        /* Protocol type: IPv4 */
    packet[18] = 6;                        /* HW address length */
    packet[19] = 4;                        /* Protocol address length */
    net_put16(packet + 20, 2);             /* Opcode: Reply (2) */
    memcpy(packet + 22, iface->mac, 6);    /* Sender MAC */
    memcpy(packet + 28, iface->ip, 4);     /* Sender IP */
    memcpy(packet + 32, dest_mac, 6);      /* Target MAC */
    memcpy(packet + 38, target_ip, 4);     /* Target IP */

    iface->send_frame(iface, packet, 42);
}

void arp_on_packet(NetworkInterface *iface, const u8 *frame, int len) {
    if (len < 42)
        return;

    u16 hw_type = net_be16(frame + 14);
    u16 proto_type = net_be16(frame + 16);
    u8 hw_len = frame[18];
    u8 proto_len = frame[19];
    u16 opcode = net_be16(frame + 20);

    if (hw_type != 1 || proto_type != 0x0800 || hw_len != 6 || proto_len != 4)
        return;

    const u8 *sender_mac = frame + 22;
    const u8 *sender_ip = frame + 28;
    const u8 *target_ip = frame + 38;

    /* Always cache sender */
    arp_cache_insert(sender_ip, sender_mac);

    if (opcode == 1) {
        /* ARP Request */
        if (net_same(target_ip, iface->ip, 4)) {
            arp_reply(iface, sender_mac, sender_ip);
        }
    } else if (opcode == 2) {
        /* ARP Reply */
        if (net_same(sender_ip, iface->gateway, 4)) {
            serial("NET ARP gateway resolved\n");
        }
        icmp_on_arp_resolved(iface, sender_ip);
    }
}
