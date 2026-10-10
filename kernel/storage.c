#include "arch/x86_64/fs_platform.h"
#include "arch/x86_64/net_platform.h"

static u64 ata_cycles_per_ms;
static int ata_clock_tried;
/* Device completion is asynchronous. An iteration count can expire while
 * the host/disk is flushing; measure elapsed time even with IRQs disabled. */
static void ata_clock_init(void) {
    if(ata_clock_tried)return;ata_clock_tried=1;
    u32 a,b,c,d;hal_cpuid(1,0,&a,&b,&c,&d);if(!(d&16u))return;
    u8 speaker=inb(0x61);outb(0x61,speaker&(u8)~3u);
    outb(0x43,0xb0);outb(0x42,11932&255);outb(0x42,11932>>8);
    u64 begin=hal_read_tsc_serialized();outb(0x61,(speaker&(u8)~2u)|1u);
    u32 limit=2000000;while(!(inb(0x61)&32u) && --limit)hal_cpu_relax();
    u64 delta=hal_read_tsc_serialized()-begin;outb(0x61,speaker);
    if(limit && delta/10>=1000 && delta/10<=10000000)ata_cycles_per_ms=delta/10;
}
static int ata_wait(int data) {
    ata_clock_init();u64 begin=hal_read_tsc_serialized();
    u32 budget=ata_cycles_per_ms?100000000:1000000;
    for(u32 i=0;i<budget;i++) {
        if(ata_cycles_per_ms && !(i&255u) && hal_read_tsc_serialized()-begin>ata_cycles_per_ms*5000u)break;
        u8 s = inb(0x1f7);
        if (s == 0 || s == 255) {
            serial("[ATA64] no device while waiting\n");
            return 0;
        }
        if (!(s & 0x80)) {
            if (s & 0x21) {
                char value[12];number(value,s);serial("[ATA64] status error=");serial(value);serial(data?" data\n":" completion\n");
                return 0;
            }
            /* After a 256-word PIO transfer, DRQ must drop before another
             * command is issued. Seeing BSY clear alone can be an intermediate
             * device phase and leaves the next ATA request wedged busy. */
            if (data ? !!(s & 8) : !(s & 8))
                return 1;
        }
    }
    serial(data?"[ATA64] data wait timeout\n":"[ATA64] completion wait timeout\n");
    return 0;
}
static int ata_read_once(u8 drive, u32 lba, void *buffer) {
    outb(0x1f6, drive | ((lba >> 24) & 15));
    for (int i = 0; i < 4; i++)
        inb(0x3f6);
    if (!ata_wait(0)) return 0;
    outb(0x1f2, 1);
    outb(0x1f3, lba);
    outb(0x1f4, lba >> 8);
    outb(0x1f5, lba >> 16);
    outb(0x1f7, 0x20);
    if (!ata_wait(1)) return 0;
    for (int i = 0; i < 256; i++) {
        ((u16 *)buffer)[i] = inw(0x1f0);
    }
    for (int i = 0; i < 4; i++)
        inb(0x3f6);
    if (!ata_wait(0)) return 0;
    return 1;
}

/* A lost completion phase can leave the PIO channel busy indefinitely. Reset
 * the channel with nIEN retained, wait for the device to settle, and reselect
 * the requested drive before one bounded retry. */
static int ata_reset_channel(u8 drive) {
    outb(0x3f6, 0x06); /* SRST + nIEN */
    for (u32 i = 0; i < 20000; ++i) (void)inb(0x3f6);
    outb(0x3f6, 0x02); /* deassert SRST; keep polling mode */
    for (u32 i = 0; i < 4; ++i) (void)inb(0x3f6);
    outb(0x1f6, drive);
    for (u32 i = 0; i < 4; ++i) (void)inb(0x3f6);
    return ata_wait(0);
}

static int ata_read_on(u8 drive, u32 lba, void *buffer) {
    if (!buffer || lba >= 0x10000000u) return 0;
    if (!fs64_sector_allowed(lba)) return 0;
    if (ata_read_once(drive, lba, buffer)) return 1;
    if (!ata_reset_channel(drive)) return 0;
    return ata_read_once(drive, lba, buffer);
}


int ata_read_sector(u32 lba, void *buffer) {
    return ata_read_on(0xf0, lba, buffer);
}

static int ata_write_once(u8 drive, u32 lba, const void *buffer) {
    outb(0x1f6, drive | ((lba >> 24) & 15));
    for (int i = 0; i < 4; i++)
        inb(0x3f6);
    if (!ata_wait(0)) return 0;
    outb(0x1f2, 1);
    outb(0x1f3, lba);
    outb(0x1f4, lba >> 8);
    outb(0x1f5, lba >> 16);
    outb(0x1f7, 0x30);
    if (!ata_wait(1)) return 0;
    for (int i = 0; i < 256; i++) {
        outw(0x1f0, ((const u16 *)buffer)[i]);
    }
    for (int i = 0; i < 4; i++)
        inb(0x3f6);
    if (!ata_wait(0)) return 0;
    return 1;
}

static int ata_write_on(u8 drive, u32 lba, const void *buffer) {
    if (!buffer || lba >= 0x10000000u) return 0;
    if (!fs64_sector_allowed(lba)) return 0;
    if (ata_write_once(drive, lba, buffer)) return 1;
    if (!ata_reset_channel(drive)) return 0;
    return ata_write_once(drive, lba, buffer);
}

static int ata_flush_on(u8 drive) {
    outb(0x1f6, drive);
    for (int i = 0; i < 4; ++i) inb(0x3f6);
    outb(0x1f7, 0xe7);
    return ata_wait(0);
}

int ata_write_sector(u32 lba, const void *buffer) {
    return ata_write_on(0xf0, lba, buffer);
}

int ata_flush(void) {
    return ata_flush_on(0xf0);
}


