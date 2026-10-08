#ifndef POLLIK_FS_JOURNAL_H
#define POLLIK_FS_JOURNAL_H
#include "block_device.h"
#define FS_JOURNAL_RECORDS 31
int fs_journal_attach(const BlockDevice *device); /* validates/replays, no formatting */
int fs_journal_begin(void);
int fs_journal_stage(uint32_t block,const void *data);
int fs_journal_overlay(uint32_t block,void *data);
int fs_journal_end(int commit);
int fs_journal_readonly(void);
void fs_journal_force_readonly(void);
int fs_journal_active(void);
int fs_journal_committed(void);
void fs_journal_detach(void);
int fs_journal_format_done(const BlockDevice *device);
#endif
