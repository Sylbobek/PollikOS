#include "hw.h"
#include "klog.h"

/* CMOS ports */
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

/* PCI ports */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static u8 cmos_read(u8 reg) {
    outb(CMOS_ADDRESS, (inb(CMOS_ADDRESS) & 0x80) | (reg & 0x7F));
    return inb(CMOS_DATA);
}

static int cmos_is_updating(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static u8 bcd_to_bin(u8 val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void rtc_init(void) {
    KLOG_INFO(KLOG_CAT_SYSTEM, "CMOS RTC driver initialized");
}

void rtc_get_time(RtcTime *t) {
    if (!t) return;

    /* Wait if CMOS is updating */
    int timeout = 10000;
    while (cmos_is_updating() && --timeout > 0) {}

    u8 sec   = cmos_read(0x00);
    u8 min   = cmos_read(0x02);
    u8 hour  = cmos_read(0x04);
    u8 day   = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year  = cmos_read(0x09);
    u8 regb  = cmos_read(0x0B);

    /* Convert BCD to binary if needed */
    if (!(regb & 0x04)) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = ((hour & 0x7F) ? bcd_to_bin(hour & 0x7F) : 0) | (hour & 0x80);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }

    /* Convert 12-hour format to 24-hour format if needed */
    if (!(regb & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    t->second = sec;
    t->minute = min;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    t->year   = 2000 + year;
}

void rtc_format_time(char *buf, int max_len) {
    if (!buf || max_len < 9) return;
    RtcTime t;
    rtc_get_time(&t);

    buf[0] = '0' + (t.hour / 10);
    buf[1] = '0' + (t.hour % 10);
    buf[2] = ':';
    buf[3] = '0' + (t.minute / 10);
    buf[4] = '0' + (t.minute % 10);
    buf[5] = ':';
    buf[6] = '0' + (t.second / 10);
    buf[7] = '0' + (t.second % 10);
    buf[8] = '\0';
}

void power_shutdown(void) {
    KLOG_INFO(KLOG_CAT_SYSTEM, "System shutdown initiated");
    /* QEMU / Bochs standard shutdown port */
    outw(0x604, 0x2000);
    /* Older QEMU */
    outw(0xB004, 0x2000);
    /* VirtualBox */
    outw(0x4004, 0x3400);
    /* Cloud-hypervisor */
    outw(0x600, 0x34);

    /* If ACPI shutdown failed, halt CPU */
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}

void power_reboot(void) {
    KLOG_INFO(KLOG_CAT_SYSTEM, "System reboot initiated");
    /* 8042 Keyboard Controller pulse reset line */
    u8 status;
    do {
        status = inb(0x64);
        if (status & 1) (void)inb(0x60);
    } while (status & 2);

    outb(0x64, 0xFE);

    /* Fallback: Triple fault via empty IDT */
    struct {
        u16 limit;
        u32 base;
    } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile("lidt %0; int3" :: "m"(null_idt));

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

void speaker_beep(u32 freq_hz, u32 duration_ms) {
    if (freq_hz == 0) return;
    u32 div = 1193180 / freq_hz;

    /* Program PIT Channel 2 */
    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));

    /* Turn speaker on (bits 0 and 1) */
    u8 tmp = inb(0x61);
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }

    /* Busy-wait duration using PIT ticks if available */
    volatile u32 count = duration_ms * 30000;
    while (count--) {
        __asm__ volatile("nop");
    }

    /* Turn speaker off */
    outb(0x61, inb(0x61) & 0xFC);
}

static u32 pci_config_read32(u8 bus, u8 dev, u8 fn, u8 reg) {
    u32 address = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8) | (reg & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

int pci_scan_bus(PciDevice *out_devs, int max_devs) {
    if (!out_devs || max_devs <= 0) return 0;
    int count = 0;

    for (int dev = 0; dev < 32 && count < max_devs; dev++) {
        for (int fn = 0; fn < 8 && count < max_devs; fn++) {
            u32 reg0 = pci_config_read32(0, dev, fn, 0);
            u16 vendor = (u16)(reg0 & 0xFFFF);
            if (vendor == 0xFFFF || vendor == 0x0000) {
                if (fn == 0) break; /* No device at this slot */
                continue;
            }

            u16 device = (u16)((reg0 >> 16) & 0xFFFF);
            u32 reg8 = pci_config_read32(0, dev, fn, 8);
            u8 class_code = (u8)((reg8 >> 24) & 0xFF);
            u8 subclass   = (u8)((reg8 >> 16) & 0xFF);
            u8 prog_if    = (u8)((reg8 >> 8) & 0xFF);

            out_devs[count].bus = 0;
            out_devs[count].dev = dev;
            out_devs[count].fn = fn;
            out_devs[count].vendor_id = vendor;
            out_devs[count].device_id = device;
            out_devs[count].class_code = class_code;
            out_devs[count].subclass = subclass;
            out_devs[count].prog_if = prog_if;
            count++;

            /* If single function device, don't check remaining functions */
            if (fn == 0) {
                u32 regC = pci_config_read32(0, dev, 0, 0x0C);
                u8 header_type = (u8)((regC >> 16) & 0xFF);
                if (!(header_type & 0x80)) break;
            }
        }
    }
    return count;
}

const char *pci_class_name(u8 class_code, u8 subclass) {
    switch (class_code) {
    case 0x01: return "Storage (IDE/SATA)";
    case 0x02: return "Network (Ethernet/RTL8139)";
    case 0x03: return "Display (VGA/VBE/DISPI)";
    case 0x04: return "Multimedia (Audio AC'97/HDA)";
    case 0x06: return "Bridge (Host/PCI/ISA)";
    case 0x0C:
        if (subclass == 0x03) return "Serial Bus (USB Controller)";
        return "Serial Bus";
    default: return "System Peripheral";
    }
}
