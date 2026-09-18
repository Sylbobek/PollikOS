#ifndef POLLIK_POLLIKFS_H
#define POLLIK_POLLIKFS_H

#include "system.h"
#include "vfs.h"

#define POLLIK2_MAGIC        0x504B4632u /* "PKF2" */
#define POLLIK2_BLOCK_SIZE   1024u
#define POLLIK2_SECTORS_PER_BLOCK (POLLIK2_BLOCK_SIZE / 512u)
#define POLLIK2_START_LBA    64u /* Start at 32 KiB offset on data disk */

#define POLLIK2_INODE_COUNT  512u
#define POLLIK2_TOTAL_BLOCKS 32768u
#define POLLIK2_DIRECT_BLOCKS 8u

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 block_size;
    u32 total_blocks;
    u32 inode_count;
    u32 free_blocks;
    u32 free_inodes;
    u32 root_inode;
    u32 block_bitmap_block;
    u32 block_bitmap_count;
    u32 inode_table_block;
    u32 inode_table_count;
    u32 data_blocks_start;
    u32 generation;
    u8  reserved[460];
} PollikSuperblock;

typedef struct __attribute__((packed)) {
    u32 mode;       /* VFS_FILE or VFS_DIR */
    u32 size;       /* Size in bytes */
    u32 direct[POLLIK2_DIRECT_BLOCKS];
    u32 indirect;   /* Single indirect block pointer */
    u32 created;
    u32 modified;
    u32 reserved[2];
} PollikInode;

typedef struct __attribute__((packed)) {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    char name[56];
} PollikDirent;

void pollikfs_init(void);
/* Destructive explicit format; never called by mount. Remount afterwards.
 * Existing formatter does not yet provide transactional I/O guarantees. */
void pollikfs_format(void);
int pollikfs_open(const char *path, int flags, vfs_file_t *out_file);
int pollikfs_read(vfs_file_t *file, void *buf, u32 count);
int pollikfs_write(vfs_file_t *file, const void *buf, u32 count);
int pollikfs_seek(vfs_file_t *file, int offset, int whence);
int pollikfs_close(vfs_file_t *file);
int pollikfs_stat(const char *path, vfs_stat_t *st);
int pollikfs_mkdir(const char *path);
int pollikfs_unlink(const char *path);

#endif
