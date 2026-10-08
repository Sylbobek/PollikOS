/* Real PollikFS read path, synthetic sectors; no user disk and no host I/O. */
#define POLLIK_X64 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/pollikfs.c"
u32 ticks;
void *fs_journal_alloc(size_t bytes) { (void)bytes;abort(); }
static u8 disk[2048][1024], output[580 * 1024];
static u32 reads, metadata_reads, failed_lba = 0xffffffffu;
int ata_read_sector(u32 lba, void *buffer) {
    ++reads;
    if (lba == failed_lba) return 0;
    if (lba < 64 || (lba - 64) / 2 >= 2048) return 0;
    u32 block = (lba - 64) / 2;
    if (block >= 40 && block <= 43) ++metadata_reads;
    memcpy(buffer, disk[block] + ((lba - 64) & 1) * 512, 512);
    return 1;
}
int ata_write_sector(u32 lba, const void *buffer) { (void)lba; (void)buffer; abort(); }
int ata_flush(void) { abort(); }
void klog(const char *c, const char *l, const char *m) { (void)c; (void)l; (void)m; }
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static u8 pattern(u32 position) { return (u8)((position * 37u + position / 1024u) ^ (position >> 8)); }
static vfs_file_t descriptor(void) {
    vfs_file_t f = {0}; f.inode = 2; f.type = VFS_FILE; return f;
}
int main(void) {
    g_sb.total_blocks = 2048; g_sb.inode_table_block = 5;
    g_sb.inode_count = 512; g_sb.data_blocks_start = 36;
    g_fs_mounted = 1;
    PollikInode node = {0}; node.mode = VFS_FILE; node.size = sizeof(output);
    node.indirect = 40; node.double_indirect = 41;
    u32 pointers[256] = {0}; pointers[0] = 42; pointers[1] = 43;
    memcpy(disk[41], pointers, sizeof(pointers));
    for (u32 i = 0; i < 580; ++i) {
        if (i < 8) node.direct[i] = 100 + i;
        else {
            u32 block = i < 264 ? 40 : 42 + (i - 264) / 256;
            u32 index = i < 264 ? i - 8 : (i - 264) % 256;
            u32 physical = 100 + i;
            memcpy(disk[block] + index * 4, &physical, 4);
        }
        for (u32 j = 0; j < 1024; ++j) disk[100+i][j] = pattern(i*1024+j);
    }
    memcpy(disk[5]+2*sizeof(node), &node, sizeof(node));
    vfs_file_t f = descriptor();
    CHECK(pollikfs_read(&f, output, sizeof(output)) == sizeof(output));
    for (u32 i = 0; i < sizeof(output); ++i) CHECK(output[i] == pattern(i));
    CHECK(f.offset == sizeof(output));
    printf("READ sectors=%u pointer_sectors=%u bytes=%u\n", reads, metadata_reads, (u32)sizeof(output));
#ifdef EXPECT_CACHED_READS
    CHECK(metadata_reads == 10); /* single + two root reads + two middle reads */
    CHECK(reads == 1172); /* inode + 580 data blocks + five pointer blocks */
#endif
    CHECK(pollikfs_read(&f, output, 1) == 0);
    const u32 starts[] = {7*1024+997, 263*1024+997, 519*1024+997};
    for (u32 n = 0; n < 3; ++n) {
        f = descriptor(); f.offset = starts[n];
        CHECK(pollikfs_read(&f, output, 2051) == 2051);
        for (u32 i = 0; i < 2051; ++i) CHECK(output[i] == pattern(starts[n]+i));
    }
    /* A new call must reread metadata, not reuse stale pointers. */
    u32 zero = 0;
    memcpy(disk[42], &zero, 4);
    f = descriptor(); f.offset = 264*1024;
    CHECK(pollikfs_read(&f, output, 1024) == 1024);
    for (u32 i = 0; i < 1024; ++i) CHECK(output[i] == 0);
    for (u32 block = 40; block <= 43; ++block) for (u32 sector = 0; sector < 2; ++sector) {
        failed_lba = 64 + block*2 + sector;
        f = descriptor(); f.offset = block == 40 ? 8*1024 : block == 43 ? 520*1024 : 264*1024;
        CHECK(pollikfs_read(&f, output, 1024) == -1);
        CHECK(pollikfs_error() == VFS_IO);
        CHECK(f.offset == (block == 40 ? 8*1024u : block == 43 ? 520*1024u : 264*1024u));
    }
    printf("PASS PollikFS native read: full-byte golden pattern, three boundary crossings, EOF, sparse hole, per-call metadata refresh, eight I/O failures\n");
    return 0;
}
