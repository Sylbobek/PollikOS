#ifdef POLLIK_X64
#include "arch/x86_64/fs_platform.h"
#else
#include "system.h"
#include "storage.h"
#include "ahci.h"
#endif

#if !defined(POLLIK_X64) && !defined(POLLIK_FS_READONLY)
/* PollikFS: two complete, checksummed snapshots. Commit payload before header.
 * The data disk is primary IDE slave; the boot disk is never written here. */
#define SLOT_SECTORS 18
#define FS_MAGIC 0x32534650u
File files[FS_FILES];
int fs_ready;
const char *fs_status = "NO DATA DISK";
static u32 generation;
static int active_slot = -1;
static u8 snapshot[17 * 512], candidate[17 * 512];
static u8 header[512];

#endif
static int ata_wait(int data) {
    for (u32 i = 0; i < 1000000; i++) {
        u8 s = inb(0x1f7);
        if (s == 0 || s == 255)
            return 0;
        if (!(s & 0x80)) {
            if (s & 0x21)
                return 0;
            /* After a 256-word PIO transfer, DRQ must drop before another
             * command is issued. Seeing BSY clear alone can be an intermediate
             * device phase and leaves the next ATA request wedged busy. */
            if (data ? !!(s & 8) : !(s & 8))
                return 1;
        }
    }
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
#ifdef POLLIK_X64
    if (!fs64_sector_allowed(lba)) return 0;
#endif
    if (ata_read_once(drive, lba, buffer)) return 1;
    if (!ata_reset_channel(drive)) return 0;
    return ata_read_once(drive, lba, buffer);
}

#ifndef POLLIK_X64
enum { STORAGE_ATA, STORAGE_AHCI };
static u8 pollik_drive = 0xf0;
static u32 pollik_base_lba = POLLIK_DATA_FS_LBA;
static int pollik_backend = STORAGE_ATA;
static int install_backend = STORAGE_ATA;
#endif

int ata_read_sector(u32 lba, void *buffer) {
#ifdef POLLIK_X64
    return ata_read_on(0xf0, lba, buffer);
#else
    return pollik_backend == STORAGE_AHCI ? ahci_read_sector(lba, buffer)
                                           : ata_read_on(pollik_drive, lba, buffer);
#endif
}

#if defined(POLLIK_X64) || !defined(POLLIK_FS_READONLY)
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
#ifdef POLLIK_X64
    if (!fs64_sector_allowed(lba)) return 0;
#endif
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
#ifdef POLLIK_X64
    return ata_write_on(0xf0, lba, buffer);
#else
    return pollik_backend == STORAGE_AHCI ? ahci_write_sector(lba, buffer)
                                           : ata_write_on(pollik_drive, lba, buffer);
#endif
}

int ata_flush(void) {
#ifdef POLLIK_X64
    return ata_flush_on(0xf0);
#else
    return pollik_backend == STORAGE_AHCI ? ahci_flush() : ata_flush_on(pollik_drive);
#endif
}

#ifndef POLLIK_X64
static int disk_magic(u8 drive, u32 base) {
    u8 sector_buf[512];
    if (!ata_read_on(drive, base, sector_buf)) return 0;
    u32 magic = (u32)sector_buf[0] | ((u32)sector_buf[1] << 8) |
                ((u32)sector_buf[2] << 16) | ((u32)sector_buf[3] << 24);
    return magic == 0x504b4632u;
}
static int ahci_magic(u32 base) {
    u8 sector_buf[512];
    if (!ahci_read_sector(base, sector_buf)) return 0;
    u32 magic = (u32)sector_buf[0] | ((u32)sector_buf[1] << 8) |
                ((u32)sector_buf[2] << 16) | ((u32)sector_buf[3] << 24);
    return magic == 0x504b4632u;
}

int storage_select_pollikfs(void) {
    if (disk_magic(0xf0, POLLIK_DATA_FS_LBA)) {
        pollik_drive = 0xf0;
        pollik_base_lba = POLLIK_DATA_FS_LBA;
        pollik_backend = STORAGE_ATA;
        serial("STORAGE: PollikFS on primary slave data disk\n");
        return 1;
    }
    if (ahci_present() && ahci_magic(POLLIK_INSTALLED_FS_LBA)) {
        pollik_backend = STORAGE_AHCI;
        pollik_base_lba = POLLIK_INSTALLED_FS_LBA;
        serial("STORAGE: PollikFS on installed AHCI SATA disk\n");
        return 1;
    }
    if (disk_magic(0xe0, POLLIK_INSTALLED_FS_LBA)) {
        pollik_drive = 0xe0;
        pollik_base_lba = POLLIK_INSTALLED_FS_LBA;
        pollik_backend = STORAGE_ATA;
        serial("STORAGE: PollikFS on installed primary master\n");
        return 1;
    }
    pollik_drive = 0xf0;
    pollik_base_lba = POLLIK_DATA_FS_LBA;
    pollik_backend = STORAGE_ATA;
    return 0;
}

void storage_use_install_target(void) {
    pollik_backend = install_backend;
    pollik_drive = 0xe0;
    pollik_base_lba = POLLIK_INSTALLED_FS_LBA;
}

u32 storage_pollikfs_start_lba(void) { return pollik_base_lba; }

int storage_install_target_info(u32 *sector_count) {
    if (sector_count) *sector_count = 0;
    u32 ahci_count = ahci_sector_count();
    if (ahci_count >= POLLIK_INSTALLED_LEGACY_LBA + 36u) {
        install_backend = STORAGE_AHCI;
        if (sector_count) *sector_count = ahci_count;
        return 1;
    }
    outb(0x3f6, 2);
    outb(0x1f6, 0xe0);
    for (int i = 0; i < 4; ++i) inb(0x3f6);
    outb(0x1f2, 0); outb(0x1f3, 0); outb(0x1f4, 0); outb(0x1f5, 0);
    outb(0x1f7, 0xec);
    if (!ata_wait(1)) return 0;
    u16 words[256];
    for (int i = 0; i < 256; ++i) words[i] = inw(0x1f0);
    if (!(words[49] & (1u << 9))) return 0;
    u32 count = (u32)words[60] | ((u32)words[61] << 16);
    if (count < POLLIK_INSTALLED_LEGACY_LBA + 36u) return 0;
    install_backend = STORAGE_ATA;
    if (sector_count) *sector_count = count;
    return 1;
}

int storage_install_write_sector(u32 lba, const void *buffer) {
    return install_backend == STORAGE_AHCI ? ahci_write_sector(lba, buffer)
                                           : ata_write_on(0xe0, lba, buffer);
}

int storage_install_flush(void) {
    return install_backend == STORAGE_AHCI ? ahci_flush() : ata_flush_on(0xe0);
}
#endif

#if !defined(POLLIK_X64)
static u8 legacy_drive = 0xf0;
static u32 legacy_base_lba;
static int legacy_backend = STORAGE_ATA;
static int sector(u32 lba, u8 *buffer, int write) {
    if (legacy_backend == STORAGE_AHCI)
        return write ? ahci_write_sector(legacy_base_lba + lba, buffer)
                     : ahci_read_sector(legacy_base_lba + lba, buffer);
    return write ? ata_write_on(legacy_drive, legacy_base_lba + lba, buffer)
                 : ata_read_on(legacy_drive, legacy_base_lba + lba, buffer);
}

static int flush(void) {
    return legacy_backend == STORAGE_AHCI ? ahci_flush() : ata_flush_on(legacy_drive);
}
static u32 checksum(const u8 *p, u32 count) {
    u32 hash = 2166136261u;
    while (count--)
        hash = (hash ^ *p++) * 16777619u;
    return hash;
}
static int valid_files(const u8 *buffer) {
    const File *f = (const File *)buffer;
    for (int i = 0; i < FS_FILES; i++) {
        if (f[i].length > FS_CAPACITY || f[i].name[23] || f[i].data[f[i].length])
            return 0;
        for (int j = 0; j < 23 && f[i].name[j]; j++)
            if (!((f[i].name[j] >= 'a' && f[i].name[j] <= 'z') ||
                  (f[i].name[j] >= '0' && f[i].name[j] <= '9') || f[i].name[j] == '.' ||
                  f[i].name[j] == '-' || f[i].name[j] == '_'))
                return 0;
    }
    return 1;
}
static int commit(void) {
    if (!fs_ready)
        return 0;
    int slot = active_slot == 0 ? 1 : 0;
    memset(snapshot, 0, sizeof(snapshot));
    memcpy(snapshot, files, sizeof(files));
    for (int i = 0; i < 17; i++)
        if (!sector(slot * SLOT_SECTORS + 1 + i, snapshot + i * 512, 1))
            goto failed;
    if (!flush())
        goto failed;
    memset(header, 0, sizeof(header));
    u32 *h = (u32 *)header;
    h[0] = FS_MAGIC;
    h[1] = generation + 1;
    h[2] = sizeof(files);
    h[3] = checksum(snapshot, sizeof(snapshot));
    h[4] = checksum(header, 16);
    if (!sector(slot * SLOT_SECTORS, header, 1) || !flush())
        goto failed;
    active_slot = slot;
    generation++;
    fs_status = "SAVED TO DISK";
    serial("FS commit OK\n");
    return 1;
failed:
    fs_status = "DISK WRITE FAILED";
    serial("FS commit FAILED\n");
    return 0;
}
void fs_init(void) {
    if (ahci_present() && ahci_magic(POLLIK_INSTALLED_FS_LBA)) {
        legacy_backend = STORAGE_AHCI;
        legacy_base_lba = POLLIK_INSTALLED_LEGACY_LBA;
        serial("FS using installed AHCI snapshot area\n");
    } else if (disk_magic(0xe0, POLLIK_INSTALLED_FS_LBA)) {
        legacy_backend = STORAGE_ATA;
        legacy_drive = 0xe0;
        legacy_base_lba = POLLIK_INSTALLED_LEGACY_LBA;
        serial("FS using installed primary-master snapshot area\n");
    } else {
        legacy_backend = STORAGE_ATA;
        legacy_drive = 0xf0;
        legacy_base_lba = 0;
    }
    if (!sector(0, header, 0)) {
        serial("FS no data disk\n");
        return;
    }
    int blank = 1;
    for (int slot = 0; slot < 2; slot++) {
        if (!sector(slot * SLOT_SECTORS, header, 0))
            continue;
        for (int j = 0; j < 512; j++)
            if (header[j])
                blank = 0;
        u32 *h = (u32 *)header;
        if (h[0] != FS_MAGIC || h[2] != sizeof(files) || h[4] != checksum(header, 16))
            continue;
        u32 gen = h[1], sum = h[3];
        int good = 1;
        for (int i = 0; i < 17; i++)
            if (!sector(slot * SLOT_SECTORS + 1 + i, candidate + i * 512, 0)) {
                good = 0;
                break;
            }
        if (!good || sum != checksum(candidate, sizeof(candidate)) || !valid_files(candidate))
            continue;
        if (active_slot < 0 || (int)(gen - generation) > 0) {
            memcpy(files, candidate, sizeof(files));
            generation = gen;
            active_slot = slot;
        }
    }
    if (active_slot < 0 && !blank) {
        fs_status = "INVALID DATA DISK";
        serial("FS invalid disk; writes disabled\n");
        return;
    }
    fs_ready = 1;
    fs_status = active_slot < 0 ? "EMPTY DATA DISK" : "LOADED FROM DISK";
    serial(active_slot < 0 ? "FS empty disk ready\n" : "FS mounted persistent snapshot\n");
}
int fs_find(const char *name) {
    for (int i = 0; i < FS_FILES; i++) {
        int j = 0;
        while (name[j] && name[j] == files[i].name[j])
            j++;
        if (!name[j] && !files[i].name[j])
            return i;
    }
    return -1;
}
int fs_save(int index, const char *name, const char *data, u32 length) {
    if (!fs_ready || index < 0 || index >= FS_FILES || length > FS_CAPACITY)
        return 0;
    File old = files[index];
    memset(&files[index], 0, sizeof(File));
    int i = 0;
    while (name[i] && i < 23) {
        files[index].name[i] = name[i];
        i++;
    }
    files[index].length = length;
    memcpy(files[index].data, data, length);
    if (commit())
        return 1;
    files[index] = old;
    return 0;
}
int fs_remove(int index) {
    if (!fs_ready || index < 0 || index >= FS_FILES)
        return 0;
    File old = files[index];
    memset(&files[index], 0, sizeof(File));
    if (commit())
        return 1;
    files[index] = old;
    return 0;
}

#endif /* !POLLIK_X64 */
#endif /* POLLIK_X64 || !POLLIK_FS_READONLY */
