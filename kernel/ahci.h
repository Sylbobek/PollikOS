#ifndef POLLIK_AHCI_H
#define POLLIK_AHCI_H

#include "system.h"

int ahci_init(void);
int ahci_present(void);
int ahci_read_sector(u32 lba, void *buffer);
int ahci_write_sector(u32 lba, const void *buffer);
int ahci_flush(void);
u32 ahci_sector_count(void);

#endif
