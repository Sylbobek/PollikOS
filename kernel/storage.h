#ifndef POLLIK_STORAGE_H
#define POLLIK_STORAGE_H

#include "system.h"

#define POLLIK_INSTALLED_FS_LBA 16384u /* 8 MiB, after bootloader and kernel */
#define POLLIK_DATA_FS_LBA 64u
#define POLLIK_INSTALLED_LEGACY_LBA 86016u /* legacy desktop snapshot after PollikFS */

int storage_select_pollikfs(void);
void storage_use_install_target(void);
u32 storage_pollikfs_start_lba(void);
int storage_install_target_info(u32 *sector_count);
int storage_install_write_sector(u32 lba, const void *buffer);
int storage_install_flush(void);

#endif
