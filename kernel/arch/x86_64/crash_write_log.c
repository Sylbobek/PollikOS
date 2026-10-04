#include <stdint.h>

int __real_ata_write_sector(uint32_t lba, const void *buffer);

static uint32_t sequence;

static void debugcon_write(uint8_t value) {
    uint16_t port = 0x00e9;
    __asm__ volatile("outb %0,%1" : : "a"(value), "Nd"(port));
}

static void write_frame(uint32_t lba, const uint8_t *data) {
    static const uint8_t magic[4] = {'P', 'K', 'W', 'L'};
    uint32_t frame_sequence = sequence++;
    for (unsigned i = 0; i < 4; ++i) debugcon_write(magic[i]);
    for (unsigned i = 0; i < 4; ++i)
        debugcon_write((uint8_t)(frame_sequence >> (i * 8)));
    for (unsigned i = 0; i < 4; ++i)
        debugcon_write((uint8_t)(lba >> (i * 8)));
    for (unsigned i = 0; i < 512; ++i) debugcon_write(data[i]);
}

/* Linker-wrapped only by build-x86_64.ps1 -CrashWriteLog. Each successful
 * sector write is recorded as PKWL + sequence + LBA + 512 raw data bytes. */
int __wrap_ata_write_sector(uint32_t lba, const void *buffer) {
    int result = __real_ata_write_sector(lba, buffer);
    if (result && buffer) write_frame(lba, (const uint8_t *)buffer);
    return result;
}
