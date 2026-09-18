#include "rtl8139.h"
#include "net_util.h"

#define RX_CONFIG 0x0000e70a /* 8 KiB wrapping ring, physical + broadcast, unlimited DMA */

static u16 io = 0;
static volatile u8 rx_ring[8192 + 16 + 1536] __attribute__((aligned(256)));
static u8 tx_buffers[4][1536] __attribute__((aligned(256)));
static u8 rx_packet[1536];
static u32 rx_offset = 0;
static u32 tx_index = 0;

/* Declared in net_manager.h */
void net_on_frame_received(NetworkInterface *iface, const u8 *frame, int len);

static u32 pci_read(int dev, int fn, int reg) {
    outl(0xcf8, 0x80000000u | (dev << 11) | (fn << 8) | (reg & 0xfc));
    return inl(0xcfc);
}

int rtl8139_init(NetworkInterface *iface) {
    io = 0;
    for (int d = 0; d < 32 && !io; d++) {
        for (int f = 0; f < 8; f++) {
            if (pci_read(d, f, 0) == 0x813910ec) {
                u32 bar = pci_read(d, f, 0x10);
                if (!(bar & 1))
                    continue;
                io = (u16)(bar & 0xfffc);
                u16 cmd = (u16)pci_read(d, f, 4);
                outl(0xcf8, 0x80000000u | (d << 11) | (f << 8) | 4);
                outw(0xcfc, cmd | 5); /* Enable Bus Master and I/O Space */
                break;
            }
        }
    }

    if (!io) {
        serial("RTL8139: device not found\n");
        return 0;
    }

    /* Power on */
    outb(io + 0x52, 0);

    /* Software reset */
    outb(io + 0x37, 0x10);
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(io + 0x37) & 0x10))
            break;
    }
    if (inb(io + 0x37) & 0x10) {
        serial("RTL8139: reset timeout\n");
        return 0;
    }

    /* Read MAC */
    for (int i = 0; i < 6; i++) {
        iface->mac[i] = inb(io + i);
    }

    /* Set up RX buffer ring */
    rx_offset = 0;
    outl(io + 0x30, (u32)rx_ring);

    /* Clear missed packet counter and set interrupt mask */
    outw(io + 0x3c, 0);
    outw(io + 0x3e, 0xffff);

    /* Enable RX and TX */
    outb(io + 0x37, 0x0c);

    /* Configure RX and TX */
    outl(io + 0x44, RX_CONFIG);
    outl(io + 0x40, 0x03000700);

    iface->name = "eth0";
    iface->type = IF_TYPE_ETHERNET;
    memset(iface->ip, 0, 4);
    memset(iface->netmask, 0, 4);
    memset(iface->gateway, 0, 4);
    memset(iface->dns, 0, 4);
    iface->dhcp_enabled = 1;
    iface->dhcp_bound = 0;
    iface->mtu = 1500;
    iface->link_up = rtl8139_is_link_up(iface);
    iface->send_frame = rtl8139_send_frame;
    iface->poll = rtl8139_poll;
    iface->driver_data = (void *)(u32)io;

    serial("NET RTL8139 ready; awaiting DHCP\n");
    return 1;
}

int rtl8139_is_link_up(NetworkInterface *iface) {
    (void)iface;
    if (!io)
        return 0;
    /* MSR offset 0x58: bit 2 is LINKB (Link Fail). 0 = Link OK, 1 = Link Fail */
    u8 msr = inb(io + 0x58);
    return (msr & 4) ? 0 : 1;
}

int rtl8139_send_frame(NetworkInterface *iface, const u8 *frame, int len) {
    if (!io || !frame || len <= 0 || len > 1536)
        return 0;

    int i = (int)tx_index;
    if (!(inl(io + 0x10 + i * 4) & 0x2000))
        return 0; /* Descriptor busy */

    memset(tx_buffers[i], 0, 1536);
    memcpy(tx_buffers[i], frame, (unsigned)len);
    __asm__ volatile("" ::: "memory");

    outl(io + 0x20 + i * 4, (u32)tx_buffers[i]);
    outl(io + 0x10 + i * 4, (u32)(len < 60 ? 60 : len));

    tx_index = (tx_index + 1) % 4;
    iface->tx_packets++;
    iface->tx_bytes += (u32)len;
    return 1;
}

void rtl8139_poll(NetworkInterface *iface) {
    if (!io)
        return;

    /* Update link status */
    iface->link_up = rtl8139_is_link_up(iface);

    for (int budget = 0; budget < 16 && !(inb(io + 0x37) & 1); budget++) {
        u16 status = (u16)(rx_ring[rx_offset] | (rx_ring[(rx_offset + 1) % 8192] << 8));
        u16 size = (u16)(rx_ring[(rx_offset + 2) % 8192] | (rx_ring[(rx_offset + 3) % 8192] << 8));

        if (size < 4 || size > 1536) {
            /* Reset RX logic */
            outb(io + 0x37, 4);
            rx_offset = 0;
            outl(io + 0x30, (u32)rx_ring);
            outb(io + 0x37, 12);
            outl(io + 0x44, RX_CONFIG);
            break;
        }

        if (status & 1) { /* ROK: Receive OK */
            int packet_len = (int)size - 4; /* Strip 4-byte CRC */
            for (int i = 0; i < packet_len; i++) {
                rx_packet[i] = rx_ring[(rx_offset + 4 + i) % 8192];
            }
            iface->rx_packets++;
            iface->rx_bytes += (u32)packet_len;
            net_on_frame_received(iface, rx_packet, packet_len);
        }

        rx_offset = (rx_offset + size + 4 + 3) & ~3u;
        rx_offset %= 8192;
        outw(io + 0x38, (u16)(rx_offset - 16));
    }

    /* Clear interrupts */
    outw(io + 0x3e, 0xffff);
}
