#ifndef POLLIK_BLOCK_DEVICE_H
#define POLLIK_BLOCK_DEVICE_H
#include <stdint.h>
/* Sector operations on the selected ATA/AHCI backend or a test device.
 * sector_limit bounds the accessible volume window; fs_start is its layout. */
typedef struct {
    int (*read)(uint32_t lba,void *buffer);
    int (*write)(uint32_t lba,const void *buffer);
    int (*flush)(void);
    uint64_t sector_limit;
    uint32_t fs_start;
} BlockDevice;
static inline int block_read(const BlockDevice *d,uint32_t lba,void *p) {
    return d && d->read && lba<d->sector_limit && d->read(lba,p);
}
static inline int block_write(const BlockDevice *d,uint32_t lba,const void *p) {
    return d && d->write && lba<d->sector_limit && d->write(lba,p);
}
static inline int block_flush(const BlockDevice *d) { return d && d->flush && d->flush(); }
#endif
