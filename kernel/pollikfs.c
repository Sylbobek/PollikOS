#include "pollikfs.h"
#include "klog.h"
#include "fs_journal.h"
#ifndef POLLIK_X64
#include "storage.h"
#endif

extern int ata_read_sector(u32 lba, void *buffer);
extern int ata_write_sector(u32 lba, const void *buffer);
extern int ata_flush(void);

#if !defined(POLLIK_X64) && !defined(POLLIK_FS_READONLY)
extern u8 _binary_build_hello_elf_start[];
extern u8 _binary_build_hello_elf_end[];
extern u8 _binary_build_fault_test_elf_start[];
extern u8 _binary_build_fault_test_elf_end[];
extern u8 _binary_build_fault_kernel_elf_start[];
extern u8 _binary_build_fault_kernel_elf_end[];
extern u8 _binary_build_fault_stack_elf_start[];
extern u8 _binary_build_fault_stack_elf_end[];
#endif
static BlockDevice volume={ata_read_sector,ata_write_sector,ata_flush,UINT32_MAX,POLLIK2_START_LBA};
static PollikSuperblock g_sb;
static u8 g_block_buf[POLLIK2_BLOCK_SIZE];
static u8 g_block_buf2[POLLIK2_BLOCK_SIZE];
static int g_fs_mounted = 0;
static int g_formatting,g_format_failed,g_format_success;
int pollikfs_format_status(void) { return g_format_success; }
/* Monotonic per-slot allocation epoch. Incremented every time alloc_inode
 * hands out a slot, so an open descriptor can detect that its inode was
 * unlinked and the slot was reused by a different file. In-memory only; no
 * on-disk byte changes and the format stays byte-for-byte identical. */
#ifdef POLLIK_X64
static u32 g_inode_generation[POLLIK2_INODE_COUNT];
static int inode_generation_valid(const vfs_file_t *file) {
    if (!file || file->inode == 0 || file->inode >= POLLIK2_INODE_COUNT) return 0;
    return file->inode_generation == g_inode_generation[file->inode];
}
#endif

static int g_fs_error;
int pollikfs_error(void) { return g_fs_error; }
void pollikfs_access_denied(void) { g_fs_error=VFS_DENIED; }
void pollikfs_range_denied(void) { g_fs_error=VFS_NOSPACE; }
int pollikfs_mounted(void) { return g_fs_mounted; }
static u32 le32(const void *source) {
    const u8 *p = source;
    return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24);
}
static int corrupt(void) { g_fs_error = VFS_CORRUPT; return 0; }
static int data_block(u32 block) {
    return block >= g_sb.data_blocks_start && block < g_sb.total_blocks;
}
static int entry_valid(const PollikDirent *e) {
    if (!le32(&e->inode)) return 1;
    const u8 *record_length = (const u8 *)&e->rec_len;
    if (le32(&e->inode) >= g_sb.inode_count || (record_length[0] | ((u32)record_length[1]<<8)) != 64 || !e->name_len ||
        e->name_len > 55 || e->name[e->name_len] || (e->file_type != VFS_FILE && e->file_type != VFS_DIR))
        return corrupt();
    for (u32 i = 0; i < e->name_len; ++i) if (!e->name[i] || e->name[i] == '/') return corrupt();
    return 1;
}
static int filesystem_check(void);
static int read_block(u32 block_num, void *buf) {
    if (block_num >= POLLIK2_TOTAL_BLOCKS) return corrupt();
    if(fs_journal_overlay(block_num,buf)) return 1;
#ifdef POLLIK_X64
    u32 lba = POLLIK2_START_LBA + block_num * POLLIK2_SECTORS_PER_BLOCK;
#else
    u32 lba = storage_pollikfs_start_lba() + block_num * POLLIK2_SECTORS_PER_BLOCK;
#endif
    if (!block_read(&volume,lba,buf) || !block_read(&volume,lba+1,(u8 *)buf+512)) {
        if(g_formatting) g_format_failed=1;
        g_fs_error = VFS_IO; return 0;
    }
    return 1;
}

static int write_raw_block(u32 block_num, const void *buf) {
#ifdef POLLIK_X64
    u32 lba = POLLIK2_START_LBA + block_num * POLLIK2_SECTORS_PER_BLOCK;
#else
    u32 lba = storage_pollikfs_start_lba() + block_num * POLLIK2_SECTORS_PER_BLOCK;
#endif
    if (!block_write(&volume,lba,buf)) {
        if(g_formatting) g_format_failed=1;
        return 0;
    }
    if (!block_write(&volume,lba+1,(const u8 *)buf+512)) {
        if(g_formatting) g_format_failed=1;
        return 0;
    }
    return 1;
}

static int write_block(u32 block,const void *data) {
    int ok=fs_journal_active()?fs_journal_stage(block,data):write_raw_block(block,data);
    if(!ok) g_fs_error=VFS_IO;return ok;
}
static int flush_volume(void) {
    int ok=fs_journal_active()?1:block_flush(&volume);
    if(!ok && g_formatting) g_format_failed=1;
    return ok;
}
static int read_inode(u32 inode_num, PollikInode *out_inode) {
    if (inode_num == 0 || inode_num >= g_sb.inode_count)
        return 0;

    u32 inodes_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikInode);
    u32 block = g_sb.inode_table_block + (inode_num / inodes_per_block);
    u32 offset = (inode_num % inodes_per_block) * sizeof(PollikInode);

    if (!read_block(block, g_block_buf))
        return 0;

    u32 words[15];
    for (u32 i = 0; i < 15; ++i) words[i] = le32(g_block_buf+offset+i*4);
    memcpy(out_inode, words, sizeof(PollikInode));
    if (out_inode->mode > VFS_DIR || out_inode->size > POLLIK2_MAX_FILE) return corrupt();
    for (u32 i = 0; i < 8; ++i)
        if (out_inode->direct[i] && !data_block(out_inode->direct[i])) return corrupt();
    if (out_inode->indirect && !data_block(out_inode->indirect)) return corrupt();
    if (out_inode->double_indirect && !data_block(out_inode->double_indirect)) return corrupt();
    if (out_inode->mode == VFS_DIR && (out_inode->size > 8192 || out_inode->size % 1024)) return corrupt();
    return 1;
}

static int write_inode(u32 inode_num, const PollikInode *in_inode) {
    if (inode_num == 0 || inode_num >= g_sb.inode_count)
        return 0;

    u32 inodes_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikInode);
    u32 block = g_sb.inode_table_block + (inode_num / inodes_per_block);
    u32 offset = (inode_num % inodes_per_block) * sizeof(PollikInode);

    if (!read_block(block, g_block_buf))
        return 0;

    memcpy(g_block_buf + offset, in_inode, sizeof(PollikInode));
    return write_block(block, g_block_buf);
}

#ifdef SELFTEST
static long long allocation_budget = -1;
void pollikfs_fail_after(long long successful_allocations) { allocation_budget = successful_allocations; }
static int allocation_allowed(void) {
    if (!allocation_budget) return 0;
    if (allocation_budget > 0) --allocation_budget;
    return 1;
}
#else
static int allocation_allowed(void) { return 1; }
#endif

static u32 alloc_block(void) {
    if (!allocation_allowed())
        return 0;
    for (u32 b = 0; b < g_sb.block_bitmap_count; b++) {
        if (!read_block(g_sb.block_bitmap_block + b, g_block_buf))
            return 0;

        for (u32 byte_idx = 0; byte_idx < POLLIK2_BLOCK_SIZE; byte_idx++) {
            if (g_block_buf[byte_idx] != 0xFF) {
                for (u32 bit = 0; bit < 8; bit++) {
                    if (!(g_block_buf[byte_idx] & (1u << bit))) {
                        u32 block_num = (b * POLLIK2_BLOCK_SIZE + byte_idx) * 8 + bit;
                        /* Zero out newly allocated block */
                        memset(g_block_buf2, 0, POLLIK2_BLOCK_SIZE);
                        if(!write_raw_block(block_num,g_block_buf2)) { g_fs_error=VFS_IO;return 0; }
                        g_block_buf[byte_idx] |= (1u << bit);
                        if(!write_block(g_sb.block_bitmap_block+b,g_block_buf)) return 0;
                        g_sb.free_blocks--;

                        return block_num;
                    }
                }
            }
        }
    }
    return 0;
}

static void free_block(u32 block_num) {
    if (block_num < g_sb.data_blocks_start || block_num >= g_sb.total_blocks)
        return;

    u32 byte_total = block_num / 8;
    u32 b = byte_total / POLLIK2_BLOCK_SIZE;
    u32 byte_idx = byte_total % POLLIK2_BLOCK_SIZE;
    u32 bit = block_num % 8;

    if (!read_block(g_sb.block_bitmap_block + b, g_block_buf))
        return;

    g_block_buf[byte_idx] &= ~(1u << bit);
    write_block(g_sb.block_bitmap_block + b, g_block_buf);
    g_sb.free_blocks++;
}

static u32 alloc_inode(u32 mode) {
    if (!allocation_allowed() || g_sb.free_inodes == 0)
        return 0;
    PollikInode node;
    for (u32 i = 1; i < g_sb.inode_count; i++) {
        if (!read_inode(i, &node))
            continue;
        if (node.mode == 0) {
            memset(&node, 0, sizeof(PollikInode));
            node.mode = mode;
            node.created = ticks;
            node.modified = ticks;
            write_inode(i, &node);
            g_sb.free_inodes--;
            /* A new occupant of this slot is a different file: invalidate any
             * descriptor still holding the previous allocation's token. */
#ifdef POLLIK_X64
            g_inode_generation[i]++;
#endif
            return i;
        }
    }
    return 0;
}

static int dir_add_entry(u32 dir_inode_num, const char *name, u32 entry_inode_num, u32 type) {
    PollikInode dir_node;
    if (!read_inode(dir_inode_num, &dir_node) || dir_node.mode != VFS_DIR)
        return 0;

    u32 entries_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikDirent);

    for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; b++) {
        if (dir_node.direct[b] == 0) {
            u32 new_blk = alloc_block();
            if (!new_blk)
                return 0;
            dir_node.direct[b] = new_blk;
            dir_node.size += POLLIK2_BLOCK_SIZE;
            write_inode(dir_inode_num, &dir_node);
        }

        if (!read_block(dir_node.direct[b], g_block_buf))
            return 0;

        PollikDirent *entries = (PollikDirent *)g_block_buf;
        for (u32 i = 0; i < entries_per_block; i++) {
            if (entries[i].inode == 0) {
                memset(&entries[i], 0, sizeof(PollikDirent));
                entries[i].inode = entry_inode_num;
                entries[i].rec_len = sizeof(PollikDirent);
                entries[i].file_type = (u8)type;

                u8 len = 0;
                while (name[len] && len < sizeof(entries[i].name) - 1) {
                    entries[i].name[len] = name[len];
                    len++;
                }
                entries[i].name[len] = 0;
                entries[i].name_len = len;

                return write_block(dir_node.direct[b], g_block_buf);
            }
        }
    }
    return 0;
}

static u32 dir_find_entry(u32 dir_inode_num, const char *name, u32 *out_type) {
    PollikInode dir_node;
    if (!read_inode(dir_inode_num, &dir_node) || dir_node.mode != VFS_DIR)
        return 0;

    u32 entries_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikDirent);

    for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; b++) {
        if (dir_node.direct[b] == 0)
            continue;

        if (!read_block(dir_node.direct[b], g_block_buf)) return 0;

        PollikDirent *entries = (PollikDirent *)g_block_buf;
        for (u32 i = 0; i < entries_per_block; i++) {
            if (!entry_valid(&entries[i])) return 0;
            if (entries[i].inode != 0) {
                int match = 1;
                u32 idx = 0;
                while (idx < 56 && (name[idx] || entries[i].name[idx])) {
                    if (name[idx] != entries[i].name[idx]) {
                        match = 0;
                        break;
                    }
                    idx++;
                }
                if (match) {
                    if (out_type)
                        *out_type = entries[i].file_type;
                    return le32(&entries[i].inode);
                }
            }
        }
    }
    return 0;
}

static u32 resolve_path_parent(const char *path, char *out_name) {
    if (!path || path[0] != '/')
        return 0;

    u32 curr_inode = g_sb.root_inode;
    const char *p = path + 1;

    while (*p) {
        char comp[56];
        u32 ci = 0;
        while (*p && *p != '/' && ci < 55) {
            comp[ci++] = *p++;
        }
        comp[ci] = 0;
        if ((*p && *p != '/') || !ci || (ci == 1 && comp[0] == '.') ||
            (ci == 2 && comp[0] == '.' && comp[1] == '.')) {
            g_fs_error = VFS_BAD_PATH; return 0;
        }

        while (*p == '/')
            p++;

        if (*p == 0) {
            /* Last component: copy to out_name */
            u32 j = 0;
            while (comp[j]) {
                out_name[j] = comp[j];
                j++;
            }
            out_name[j] = 0;
            return curr_inode;
        } else {
            /* Subdirectory traversal */
            u32 type = 0;
            curr_inode = dir_find_entry(curr_inode, comp, &type);
            if (!curr_inode) {
                if (!g_fs_error) g_fs_error = VFS_NOT_FOUND;
                return 0;
            }
            if (type != VFS_DIR) {
                g_fs_error = VFS_NOTDIR;
                return 0;
            }
        }
    }

    return 0;
}

static u32 resolve_path(const char *path) {
    if (!path || path[0] != '/') { g_fs_error = VFS_BAD_PATH; return 0; }
    u32 length = 0;
    while (length < VFS_MAX_PATH && path[length]) ++length;
    if (length == VFS_MAX_PATH) { g_fs_error = VFS_BAD_PATH; return 0; }

    if (path[1] == 0)
        return g_sb.root_inode;

    char name[56];
    u32 parent = resolve_path_parent(path, name);
    if (!parent)
        return 0;

    u32 type = 0;
    return dir_find_entry(parent, name, &type);
}

#ifndef POLLIK_FS_READONLY
void pollikfs_format(void) {
    g_formatting=1;g_format_failed=g_format_success=0;
    fs_journal_detach();
#ifndef POLLIK_X64
    volume.fs_start=storage_pollikfs_start_lba();
#endif
    volume.sector_limit=(u64)volume.fs_start+POLLIK2_TOTAL_BLOCKS*2u;
    /* Destructive operation: callers must explicitly request formatting.
     * Never invoke this as recovery from a mount failure. */
    g_fs_mounted = 0;
    KLOG_INFO(KLOG_CAT_BOOT, "Formatting PollikFS v2 filesystem");
#ifdef POLLIK_X64
    memset(g_inode_generation, 0, sizeof(g_inode_generation));
#endif

    memset(&g_sb, 0, sizeof(PollikSuperblock));
    g_sb.magic = POLLIK2_MAGIC;
    g_sb.block_size = POLLIK2_BLOCK_SIZE;
    g_sb.total_blocks = POLLIK2_TOTAL_BLOCKS;
    g_sb.inode_count = POLLIK2_INODE_COUNT;
    g_sb.free_blocks = g_sb.total_blocks;
    g_sb.free_inodes = g_sb.inode_count;
    g_sb.root_inode = 1;

    g_sb.block_bitmap_block = 1;
    g_sb.block_bitmap_count = 4; /* 4 blocks = 4096 bytes = 32768 bits */

    g_sb.inode_table_block = 5;
    /* Inodes never straddle blocks: 60-byte inodes give 17 slots per block.
     * Round the slot count up; byte-based division reserves one block too few. */
    const u32 inodes_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikInode);
    g_sb.inode_table_count = (POLLIK2_INODE_COUNT + inodes_per_block - 1) / inodes_per_block;

    g_sb.data_blocks_start = g_sb.inode_table_block + g_sb.inode_table_count; /* Block 36 */

    /* Clear and mark reserved blocks in bitmap */
    memset(g_block_buf, 0, POLLIK2_BLOCK_SIZE);
    for (u32 b = 0; b < g_sb.block_bitmap_count; b++) {
        write_block(g_sb.block_bitmap_block + b, g_block_buf);
    }

    /* Mark blocks 0 .. data_blocks_start as allocated */
    read_block(g_sb.block_bitmap_block, g_block_buf);
    for (u32 i = 0; i < g_sb.data_blocks_start; i++) {
        g_block_buf[i / 8] |= (1u << (i % 8));
        g_sb.free_blocks--;
    }
    write_block(g_sb.block_bitmap_block, g_block_buf);

    /* Zero inode table */
    memset(g_block_buf, 0, POLLIK2_BLOCK_SIZE);
    for (u32 b = 0; b < g_sb.inode_table_count; b++) {
        write_block(g_sb.inode_table_block + b, g_block_buf);
    }

    /* Create Root Directory (inode 1) */
    u32 root_ino = alloc_inode(VFS_DIR);
    PollikInode root_node;
    read_inode(root_ino, &root_node);
    root_node.direct[0] = alloc_block();
    root_node.size = POLLIK2_BLOCK_SIZE;
    write_inode(root_ino, &root_node);

    /* Populate standard system hierarchy */
    u32 bin_ino = alloc_inode(VFS_DIR);
    dir_add_entry(root_ino, "bin", bin_ino, VFS_DIR);

    u32 home_ino = alloc_inode(VFS_DIR);
    dir_add_entry(root_ino, "home", home_ino, VFS_DIR);

    u32 dev_ino = alloc_inode(VFS_DIR);
    dir_add_entry(root_ino, "dev", dev_ino, VFS_DIR);

    u32 etc_ino = alloc_inode(VFS_DIR);
    dir_add_entry(root_ino, "etc", etc_ino, VFS_DIR);

    u32 tmp_ino = alloc_inode(VFS_DIR);
    dir_add_entry(root_ino, "tmp", tmp_ino, VFS_DIR);

    /* Helper to install embedded binary into directory */
    #define INSTALL_EMBEDDED(name_str, sym_start, sym_end) do { \
        u32 sz = (u32)(sym_end - sym_start); \
        if (sz > 0) { \
            u32 ino = alloc_inode(VFS_FILE); \
            dir_add_entry(bin_ino, name_str, ino, VFS_FILE); \
            PollikInode node; \
            read_inode(ino, &node); \
            node.size = sz; \
            u32 written = 0; \
            u32 blk_idx = 0; \
            while (written < sz && blk_idx < POLLIK2_DIRECT_BLOCKS) { \
                u32 chunk = sz - written; \
                if (chunk > POLLIK2_BLOCK_SIZE) chunk = POLLIK2_BLOCK_SIZE; \
                u32 blk = alloc_block(); \
                node.direct[blk_idx++] = blk; \
                memset(g_block_buf, 0, POLLIK2_BLOCK_SIZE); \
                memcpy(g_block_buf, sym_start + written, chunk); \
                write_block(blk, g_block_buf); \
                written += chunk; \
            } \
            write_inode(ino, &node); \
        } \
    } while(0)

#ifndef POLLIK_X64
    INSTALL_EMBEDDED("hello", _binary_build_hello_elf_start, _binary_build_hello_elf_end);
    INSTALL_EMBEDDED("fault_test", _binary_build_fault_test_elf_start, _binary_build_fault_test_elf_end);
    INSTALL_EMBEDDED("fault_kernel", _binary_build_fault_kernel_elf_start, _binary_build_fault_kernel_elf_end);
    INSTALL_EMBEDDED("fault_stack", _binary_build_fault_stack_elf_start, _binary_build_fault_stack_elf_end);
#endif
    #undef INSTALL_EMBEDDED

    /* Install default /home/notes.txt */
    const char *default_note = "Welcome to PollikOS! Fast, freestanding x86 operating system.\n";
    u32 note_len = 0;
    while (default_note[note_len]) note_len++;

    u32 note_ino = alloc_inode(VFS_FILE);
    dir_add_entry(home_ino, "notes.txt", note_ino, VFS_FILE);
    PollikInode note_node;
    read_inode(note_ino, &note_node);
    note_node.size = note_len;
    note_node.direct[0] = alloc_block();
    memset(g_block_buf, 0, POLLIK2_BLOCK_SIZE);
    memcpy(g_block_buf, default_note, note_len);
    write_block(note_node.direct[0], g_block_buf);
    write_inode(note_ino, &note_node);

    /* Write superblock */
    memset(g_block_buf, 0, POLLIK2_BLOCK_SIZE);
    memcpy(g_block_buf, &g_sb, sizeof(PollikSuperblock));
    write_block(0, g_block_buf);
    flush_volume();

    g_format_success=!g_format_failed && fs_journal_format_done(&volume);
    g_formatting=0;
    if(g_format_success) KLOG_INFO(KLOG_CAT_BOOT,"PollikFS v2 format complete (/bin/hello, /home/notes.txt created)");
    else KLOG_ERROR(KLOG_CAT_BOOT,"PollikFS format incomplete; I/O error or protected journal prefix");
}

#endif
void pollikfs_init(void) {
    fs_journal_detach();
    g_fs_mounted = 0; g_fs_error = VFS_OK;
#ifndef POLLIK_X64
    storage_select_pollikfs();
#endif
    volume.fs_start=
#ifdef POLLIK_X64
        POLLIK2_START_LBA;
#else
        storage_pollikfs_start_lba();
#endif
    volume.sector_limit=(u64)volume.fs_start+POLLIK2_TOTAL_BLOCKS*2u;
    /* Establish the v2 volume identity before allowing replay to write it.
     * Metadata transactions never change these fixed geometry fields. */
    if(!block_read(&volume,volume.fs_start,g_block_buf) ||
       le32(g_block_buf)!=POLLIK2_MAGIC || le32(g_block_buf+4)!=POLLIK2_BLOCK_SIZE ||
       le32(g_block_buf+8)!=POLLIK2_TOTAL_BLOCKS || le32(g_block_buf+12)!=POLLIK2_INODE_COUNT) {
        g_fs_error=VFS_CORRUPT;
        KLOG_ERROR(KLOG_CAT_BOOT,"PollikFS v2 signature/geometry invalid; replay refused, disk unchanged");
        return;
    }
    (void)fs_journal_attach(&volume);
    if (!read_block(0, g_block_buf)) {
        KLOG_ERROR(KLOG_CAT_BOOT, "PollikFS v2: failed to read superblock from data disk");
        return;
    }

    u32 words[13];
    for (u32 i = 0; i < 13; ++i) words[i] = le32(g_block_buf+i*4);
    memset(&g_sb, 0, sizeof(g_sb));
    memcpy(&g_sb, words, sizeof(words));
    if (g_sb.magic != POLLIK2_MAGIC) {
        g_fs_error = VFS_CORRUPT;
        KLOG_ERROR(KLOG_CAT_BOOT, "PollikFS v2: invalid signature; mount refused, disk unchanged (explicit format required)");
        return;
    }

    /* Only the current fixed on-disk layout is supported. Validate before
     * using any disk-supplied offsets to read or modify metadata.
     * Legacy 30-block tables overlap inode 510/511 with the data region;
     * refuse them without writing rather than silently changing the layout. */
    const u32 inodes_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikInode);
    const u32 inode_blocks = (POLLIK2_INODE_COUNT + inodes_per_block - 1) / inodes_per_block;
    if (g_sb.block_size != POLLIK2_BLOCK_SIZE ||
        g_sb.total_blocks != POLLIK2_TOTAL_BLOCKS ||
        g_sb.inode_count != POLLIK2_INODE_COUNT ||
        g_sb.root_inode != 1 ||
        g_sb.block_bitmap_block != 1 || g_sb.block_bitmap_count != 4 ||
        g_sb.inode_table_block != 5 || g_sb.inode_table_count != inode_blocks ||
        g_sb.data_blocks_start != 5 + inode_blocks ||
        g_sb.free_blocks > POLLIK2_TOTAL_BLOCKS - (5 + inode_blocks) ||
        g_sb.free_inodes > POLLIK2_INODE_COUNT - 1) {
        g_fs_error = VFS_CORRUPT;
        KLOG_ERROR(KLOG_CAT_BOOT, "PollikFS v2: invalid superblock geometry; mount refused, disk unchanged");
        return;
    }

    PollikInode root;
    if (!read_inode(g_sb.root_inode, &root) || root.mode != VFS_DIR) { corrupt(); return; }
    g_fs_mounted = 1;
    if(!filesystem_check()) {
        fs_journal_force_readonly();g_fs_error=VFS_CORRUPT;
        KLOG_ERROR(KLOG_CAT_BOOT,"PollikFS consistency check failed; mounted read-only, no repair or formatting");
    }
    KLOG_INFO(KLOG_CAT_BOOT, "PollikFS v2 superblock mounted successfully");
}

/* Persist updated free counters with the bitmap changes. Mutations call this
 * once at the end of the operation; single-CPU IF=0 writers serialize it. */
static int superblock_sync(void) {
    memset(g_block_buf, 0, POLLIK2_BLOCK_SIZE);
    memcpy(g_block_buf, &g_sb, sizeof(PollikSuperblock));
    return write_block(0, g_block_buf);
}

/* Release every data block owned by an inode, including a single indirect. */
static void free_inode_blocks(PollikInode *node) {
    for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; ++b) {
        if (node->direct[b]) {
            free_block(node->direct[b]);
            node->direct[b] = 0;
        }
    }
    if (node->indirect) {
        if (read_block(node->indirect, g_block_buf2)) {
            u32 *entries = (u32 *)g_block_buf2;
            for (u32 i = 0; i < POLLIK2_INDIRECT_ENTRIES; ++i)
                if (entries[i]) free_block(entries[i]);
        }
        free_block(node->indirect);
        node->indirect = 0;
    }
    if (node->double_indirect) {
        /* free_block clobbers g_block_buf, so re-read the outer table each
         * iteration instead of caching it. */
        for (u32 i = 0; i < POLLIK2_INDIRECT_ENTRIES; ++i) {
            if (!read_block(node->double_indirect, g_block_buf)) break;
            u32 middle = ((u32 *)g_block_buf)[i];
            if (!middle) continue;
            if (read_block(middle, g_block_buf2)) {
                u32 *entries = (u32 *)g_block_buf2;
                for (u32 j = 0; j < POLLIK2_INDIRECT_ENTRIES; ++j)
                    if (entries[j]) free_block(entries[j]);
            }
            free_block(middle);
        }
        free_block(node->double_indirect);
        node->double_indirect = 0;
    }
}

static int mutate_open(const char *path, int flags, vfs_file_t *out_file) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !path || !out_file)
        return -1;
    unsigned access = (unsigned)flags & (O_RDONLY|O_WRONLY|O_RDWR);
    if ((flags & ~(O_RDONLY|O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND|O_EXCL)) ||
        access == 0 || access > O_RDWR ||
        ((flags & (O_TRUNC|O_APPEND)) && access == O_RDONLY)) {
        g_fs_error = VFS_DENIED;
        return -1;
    }

    u32 ino = resolve_path(path);
    if (!ino && !g_fs_error) g_fs_error = VFS_NOT_FOUND;
    if (!ino) {
        if (!(flags & O_CREAT))
            return -1;
        char name[56];
        u32 parent = resolve_path_parent(path, name);
        if (!parent)
            return -1;
        u32 created = alloc_inode(VFS_FILE);
        if (!created) {
            g_fs_error = g_sb.free_inodes == 0 ? VFS_NOSPACE : VFS_IO;
            return -1;
        }
        if (!dir_add_entry(parent, name, created, VFS_FILE)) {
            /* Roll back the inode so a failed create leaks nothing. */
            PollikInode empty;
            memset(&empty, 0, sizeof(empty));
            write_inode(created, &empty);
            g_sb.free_inodes++;
            superblock_sync();
            if (g_fs_error == VFS_OK) g_fs_error = VFS_IO;
            return -1;
        }
        ino = created;
        superblock_sync();
    }

    PollikInode node;
    if (!read_inode(ino, &node))
        return -1;

    if (node.mode == VFS_DIR) {
        if ((unsigned)flags != O_RDONLY) { g_fs_error = VFS_ISDIR; return -1; }
    } else if (node.mode != VFS_FILE) {
        g_fs_error = VFS_DENIED;
        return -1;
    } else if (flags & O_TRUNC) {
        free_inode_blocks(&node);
        node.size = 0;
        node.modified = ticks;
        if (!write_inode(ino, &node))
            return -1;
        superblock_sync();
    }

    out_file->type = (node.mode == VFS_DIR) ? VFS_DIR : VFS_FILE;
    out_file->flags = flags;
    out_file->offset = (flags & O_APPEND) ? node.size : 0;
    out_file->size = node.size;
    out_file->inode = ino;
#ifdef POLLIK_X64
    out_file->inode_generation = g_inode_generation[ino];
#endif
    out_file->ref_count = 1;
    out_file->fs_private = 0;

    return 0;
}

int pollikfs_read(vfs_file_t *file, void *buf, u32 count) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !file || !buf || count == 0)
        return 0;

    PollikInode node;
    if (!read_inode(file->inode, &node))
        return -1;
#ifdef POLLIK_X64
    if (!inode_generation_valid(file)) {
        /* The slot was unlinked and reallocated to a different file. The
         * descriptor keeps the original unlink-while-open semantics: reads
         * observe EOF and never touch the new occupant's data. */
        return 0;
    }
#endif

    if (file->offset >= node.size)
        return 0;

    if (count > node.size - file->offset)
        count = node.size - file->offset;

    u32 total_read = 0;
    u8 *dest = (u8 *)buf;
    /* Pointer blocks are immutable for this synchronous read. Keep one local
     * block of pointers across data blocks; the data scratch buffer is reused
     * below. No persistent cache: a later call observes writes/unlink normally. */
    u8 pointer_buf[POLLIK2_BLOCK_SIZE];
    u32 cached_pointer = 0, cached_outer = 0xffffffffu, cached_middle = 0;

    while (total_read < count) {
        u32 blk_idx = (file->offset + total_read) / POLLIK2_BLOCK_SIZE;
        u32 blk_offset = (file->offset + total_read) % POLLIK2_BLOCK_SIZE;
        u32 chunk = POLLIK2_BLOCK_SIZE - blk_offset;
        if (chunk > count - total_read)
            chunk = count - total_read;

        u32 phys_blk = 0;
        if (blk_idx < POLLIK2_DIRECT_BLOCKS) {
            phys_blk = node.direct[blk_idx];
        } else if (blk_idx < POLLIK2_DIRECT_BLOCKS + POLLIK2_INDIRECT_ENTRIES) {
            if (node.indirect) {
                if (cached_pointer != node.indirect) {
                    if (!read_block(node.indirect, pointer_buf)) return -1;
                    cached_pointer = node.indirect;
                }
                phys_blk = le32(pointer_buf+(blk_idx-POLLIK2_DIRECT_BLOCKS)*4);
            }
        } else {
            u32 rest = blk_idx - POLLIK2_DIRECT_BLOCKS - POLLIK2_INDIRECT_ENTRIES;
            u32 outer = rest / POLLIK2_INDIRECT_ENTRIES;
            u32 inner = rest % POLLIK2_INDIRECT_ENTRIES;
            if (outer >= POLLIK2_INDIRECT_ENTRIES) { corrupt(); return -1; }
            if (node.double_indirect) {
                if (cached_outer != outer) {
                    if (!read_block(node.double_indirect, g_block_buf)) return -1;
                    cached_middle = le32(g_block_buf+outer*4);
                    cached_outer = outer;
                }
                if (cached_middle) {
                    if (cached_pointer != cached_middle) {
                        if (!read_block(cached_middle, pointer_buf)) return -1;
                        cached_pointer = cached_middle;
                    }
                    phys_blk = le32(pointer_buf+inner*4);
                }
            }
        }

        if (phys_blk) {
            if (!data_block(phys_blk)) { corrupt(); return -1; }
            if (!read_block(phys_blk, g_block_buf)) return -1;
            memcpy(dest + total_read, g_block_buf + blk_offset, chunk);
        } else {
            memset(dest + total_read, 0, chunk);
        }

        total_read += chunk;
    }

    file->offset += total_read;
    return total_read;
}

static int mutate_write(vfs_file_t *file, const void *buf, u32 count) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !file || !buf || count == 0)
        return 0;

    PollikInode node;
    if (!read_inode(file->inode, &node))
        return -1;
    if (node.mode != VFS_FILE) {
        g_fs_error = VFS_DENIED; /* deleted/reused or non-regular inode */
        return -1;
    }
#ifdef POLLIK_X64
    if (!inode_generation_valid(file)) {
        g_fs_error = VFS_DENIED; /* inode slot now belongs to a different file */
        return -1;
    }
#endif
    /* Validate the complete range before changing offsets or allocating. */
    u32 start = (file->flags & O_APPEND) ? node.size : file->offset;
    if (start > POLLIK2_MAX_FILE || count > POLLIK2_MAX_FILE - start) {
        g_fs_error = VFS_NOSPACE;
        return -1;
    }
    if (file->flags & O_APPEND) file->offset = start;

    u32 total_written = 0;
    const u8 *src = (const u8 *)buf;
    int no_space = 0;

    while (total_written < count) {
        u32 position = file->offset + total_written;
        u32 blk_idx = position / POLLIK2_BLOCK_SIZE;
        u32 blk_offset = position % POLLIK2_BLOCK_SIZE;
        u32 chunk = POLLIK2_BLOCK_SIZE - blk_offset;
        if (chunk > count - total_written)
            chunk = count - total_written;
        if (blk_idx >= POLLIK2_DIRECT_BLOCKS + POLLIK2_INDIRECT_ENTRIES +
                       (u32)POLLIK2_INDIRECT_ENTRIES*POLLIK2_INDIRECT_ENTRIES) {
            no_space = 1; /* beyond the double-indirect PollikFS limit */
            break;
        }

        u32 phys_blk = 0;
        if (blk_idx < POLLIK2_DIRECT_BLOCKS) {
            if (!node.direct[blk_idx]) {
                node.direct[blk_idx] = alloc_block();
                if (!node.direct[blk_idx]) { no_space = 1; break; }
            }
            phys_blk = node.direct[blk_idx];
        } else if (blk_idx < POLLIK2_DIRECT_BLOCKS + POLLIK2_INDIRECT_ENTRIES) {
            u32 ind_idx = blk_idx - POLLIK2_DIRECT_BLOCKS;
            if (!node.indirect) {
                node.indirect = alloc_block();
                if (!node.indirect) { no_space = 1; break; }
            }
            u32 entry = 0;
            if (!read_block(node.indirect, g_block_buf2)) break;
            entry = ((u32 *)g_block_buf2)[ind_idx];
            if (!entry) {
                /* alloc_block zeroes g_block_buf2, so allocate first and then
                 * re-read the table before updating it. */
                entry = alloc_block();
                if (!entry) { no_space = 1; break; }
                if (!read_block(node.indirect, g_block_buf2)) break;
                ((u32 *)g_block_buf2)[ind_idx] = entry;
                if (!write_block(node.indirect, g_block_buf2)) break;
            }
            phys_blk = entry;
        } else {
            u32 rest = blk_idx - POLLIK2_DIRECT_BLOCKS - POLLIK2_INDIRECT_ENTRIES;
            u32 outer = rest / POLLIK2_INDIRECT_ENTRIES;
            u32 inner = rest % POLLIK2_INDIRECT_ENTRIES;
            if (!node.double_indirect) {
                node.double_indirect = alloc_block();
                if (!node.double_indirect) { no_space = 1; break; }
            }
            u32 middle = 0;
            if (!read_block(node.double_indirect, g_block_buf)) break;
            middle = ((u32 *)g_block_buf)[outer];
            if (!middle) {
                middle = alloc_block(); /* clobbers g_block_buf */
                if (!middle) { no_space = 1; break; }
                if (!read_block(node.double_indirect, g_block_buf)) break;
                ((u32 *)g_block_buf)[outer] = middle;
                if (!write_block(node.double_indirect, g_block_buf)) break;
            }
            u32 entry = 0;
            if (!read_block(middle, g_block_buf2)) break;
            entry = ((u32 *)g_block_buf2)[inner];
            if (!entry) {
                entry = alloc_block(); /* clobbers g_block_buf2 */
                if (!entry) { no_space = 1; break; }
                if (!read_block(middle, g_block_buf2)) break;
                ((u32 *)g_block_buf2)[inner] = entry;
                if (!write_block(middle, g_block_buf2)) break;
            }
            phys_blk = entry;
        }

        if (blk_offset > 0 || chunk < POLLIK2_BLOCK_SIZE) {
            if (!read_block(phys_blk, g_block_buf)) break;
        }
        memcpy(g_block_buf + blk_offset, src + total_written, chunk);
        if (!write_raw_block(phys_blk, g_block_buf)) break;

        total_written += chunk;
    }

    file->offset += total_written;
    if (total_written && file->offset > node.size)
        node.size = file->offset;
    node.modified = ticks;
    if (!write_inode(file->inode, &node)) { g_fs_error = VFS_IO; return -1; }
    superblock_sync();
    file->size = node.size;
    flush_volume();

    if (total_written == 0 && count) {
        g_fs_error = no_space ? VFS_NOSPACE : VFS_IO;
        return -1;
    }
    return (int)total_written;
}
int pollikfs_seek(vfs_file_t *file, int offset, int whence) {
    if (!file)
        return -1;

    long long new_pos = 0;
    if (whence == SEEK_SET) {
        new_pos = offset;
    } else if (whence == SEEK_CUR) {
        new_pos = (long long)file->offset + offset;
    } else if (whence == SEEK_END) {
        new_pos = (long long)file->size + offset;
    } else {
        return -1;
    }

    if (new_pos < 0 || new_pos > 0x7fffffff)
        return -1;

    file->offset = (u32)new_pos;
    return new_pos;
}

int pollikfs_close(vfs_file_t *file) {
    if (!file)
        return -1;
    return 0;
}

int pollikfs_stat(const char *path, vfs_stat_t *st) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !path || !st)
        return -1;

    u32 ino = resolve_path(path);
    if (!ino && !g_fs_error) g_fs_error = VFS_NOT_FOUND;
    if (!ino)
        return -1;

    PollikInode node;
    if (!read_inode(ino, &node))
        return -1;

    st->inode = ino;
    st->size = node.size;
    st->type = (node.mode == VFS_DIR) ? VFS_DIR : VFS_FILE;
    st->created = node.created;
    st->modified = node.modified;
    return 0;
}

int pollikfs_fstat(const vfs_file_t *file, vfs_stat_t *st) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !file || !st || file->ref_count <= 0) return -1;
#ifdef POLLIK_X64
    if (!inode_generation_valid(file)) { g_fs_error = VFS_DENIED; return -1; }
#endif
    PollikInode node;
    if (!read_inode(file->inode, &node)) return -1;
    st->inode = file->inode;
    st->size = node.size;
    st->type = (node.mode == VFS_DIR) ? VFS_DIR : VFS_FILE;
    st->created = node.created;
    st->modified = node.modified;
    return 0;
}

/* Find a directory entry in a parent directory, returning its block/slot. */
static int find_dirent(u32 parent, const char *name, u32 *block_out, u32 *index_out,
                       u32 *inode_out, u32 *type_out) {
    PollikInode parent_node;
    if (!read_inode(parent, &parent_node) || parent_node.mode != VFS_DIR)
        return 0;
    u32 entries_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikDirent);
    for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; b++) {
        if (!parent_node.direct[b]) continue;
        if (!read_block(parent_node.direct[b], g_block_buf)) return 0;
        PollikDirent *entries = (PollikDirent *)g_block_buf;
        for (u32 i = 0; i < entries_per_block; i++) {
            if (entries[i].inode == 0) continue;
            int match = 1;
            u32 idx = 0;
            while (name[idx] || entries[i].name[idx]) {
                if (name[idx] != entries[i].name[idx]) { match = 0; break; }
                idx++;
            }
            if (match) {
                if (block_out) *block_out = parent_node.direct[b];
                if (index_out) *index_out = i;
                if (inode_out) *inode_out = entries[i].inode;
                if (type_out) *type_out = entries[i].file_type;
                return 1;
            }
        }
    }
    return 0;
}

static int clear_dirent(u32 block, u32 index) {
    if (!read_block(block, g_block_buf)) return 0;
    PollikDirent *entries = (PollikDirent *)g_block_buf;
    entries[index].inode = 0;
    entries[index].name[0] = 0;
    return write_block(block, g_block_buf);
}

static int directory_empty(u32 inode_num) {
    PollikInode node;
    if (!read_inode(inode_num, &node) || node.mode != VFS_DIR) return 0;
    if (node.indirect) return 0; /* directories never use indirect blocks */
    u32 entries_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikDirent);
    for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; b++) {
        if (!node.direct[b]) continue;
        if (!read_block(node.direct[b], g_block_buf)) return 0;
        PollikDirent *entries = (PollikDirent *)g_block_buf;
        for (u32 i = 0; i < entries_per_block; i++)
            if (entries[i].inode != 0) return 0;
    }
    return 1;
}

/* True when a directory equal to `inode` appears anywhere on `path`; this
 * prevents moving a directory beneath its own descendant. */
static int path_contains_inode(const char *path, u32 inode) {
    if (!path || path[0] != '/') return 0;
    u32 current = g_sb.root_inode;
    if (current == inode) return 1;
    const char *p = path + 1;
    while (*p) {
        char comp[56];
        u32 ci = 0;
        while (*p && *p != '/' && ci < 55) comp[ci++] = *p++;
        comp[ci] = 0;
        while (*p == '/') ++p;
        if (!ci) break;
        u32 type = 0;
        u32 next = dir_find_entry(current, comp, &type);
        if (!next) return 0;
        if (next == inode) return 1;
        if (type != VFS_DIR) return 0;
        current = next;
    }
    return 0;
}

static int mutate_mkdir(const char *path) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !path)
        return -1;

    char name[56];
    u32 parent = resolve_path_parent(path, name);
    if (!parent)
        return -1;

    u32 existing_type = 0;
    if (dir_find_entry(parent, name, &existing_type)) {
        g_fs_error = VFS_EXISTS;
        return -1;
    }

    u32 new_ino = alloc_inode(VFS_DIR);
    if (!new_ino) {
        g_fs_error = g_sb.free_inodes == 0 ? VFS_NOSPACE : VFS_IO;
        return -1;
    }

    PollikInode node;
    memset(&node, 0, sizeof(node));
    node.mode = VFS_DIR;
    node.created = ticks;
    node.modified = ticks;
    node.direct[0] = alloc_block();
    if (!node.direct[0]) {
        PollikInode empty;
        memset(&empty, 0, sizeof(empty));
        write_inode(new_ino, &empty);
        g_sb.free_inodes++;
        superblock_sync();
        g_fs_error = VFS_NOSPACE;
        return -1;
    }
    node.size = POLLIK2_BLOCK_SIZE;
    if (!write_inode(new_ino, &node)) {
        free_block(node.direct[0]);
        g_fs_error = VFS_IO;
        return -1;
    }

    if (!dir_add_entry(parent, name, new_ino, VFS_DIR)) {
        free_inode_blocks(&node);
        PollikInode empty;
        memset(&empty, 0, sizeof(empty));
        write_inode(new_ino, &empty);
        g_sb.free_inodes++;
        superblock_sync();
        if (g_fs_error == VFS_OK) g_fs_error = VFS_IO;
        return -1;
    }

    superblock_sync();
    flush_volume();
    return 0;
}

static int mutate_unlink(const char *path) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !path)
        return -1;

    char name[56];
    u32 parent = resolve_path_parent(path, name);
    if (!parent)
        return -1;

    u32 block = 0, index = 0, target_ino = 0, target_type = 0;
    if (!find_dirent(parent, name, &block, &index, &target_ino, &target_type)) {
        if (g_fs_error == VFS_OK) g_fs_error = VFS_NOT_FOUND;
        return -1;
    }
    if (target_type == VFS_DIR) { g_fs_error = VFS_ISDIR; return -1; }

    PollikInode target;
    if (!read_inode(target_ino, &target))
        return -1;
    if (!clear_dirent(block, index))
        return -1;
    free_inode_blocks(&target);
    if (target.mode == VFS_FILE)
        g_sb.free_inodes++;
    memset(&target, 0, sizeof(PollikInode));
    if (!write_inode(target_ino, &target))
        return -1;
    superblock_sync();
    flush_volume();
    return 0;
}

static int mutate_rmdir(const char *path) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !path)
        return -1;

    char name[56];
    u32 parent = resolve_path_parent(path, name);
    if (!parent)
        return -1;

    u32 block = 0, index = 0, target_ino = 0, target_type = 0;
    if (!find_dirent(parent, name, &block, &index, &target_ino, &target_type)) {
        if (g_fs_error == VFS_OK) g_fs_error = VFS_NOT_FOUND;
        return -1;
    }
    if (target_type != VFS_DIR) { g_fs_error = VFS_NOTDIR; return -1; }
    if (!directory_empty(target_ino)) { g_fs_error = VFS_NOT_EMPTY; return -1; }

    PollikInode target;
    if (!read_inode(target_ino, &target))
        return -1;
    if (!clear_dirent(block, index))
        return -1;
    free_inode_blocks(&target);
    g_sb.free_inodes++;
    memset(&target, 0, sizeof(PollikInode));
    if (!write_inode(target_ino, &target))
        return -1;
    superblock_sync();
    flush_volume();
    return 0;
}

int pollikfs_readdir(vfs_file_t *file, vfs_dirent_t *dirent) {
    if (!g_fs_mounted || !file || !dirent || file->type != VFS_DIR)
        return -1;
#ifdef POLLIK_X64
    if (!inode_generation_valid(file)) { g_fs_error = VFS_DENIED; return -1; }
#endif

    PollikInode dir_node;
    if (!read_inode(file->inode, &dir_node) || dir_node.mode != VFS_DIR)
        return -1;

    u32 entries_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikDirent);
    u32 entry_index = 0;

    for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; b++) {
        if (!dir_node.direct[b])
            break;

        if (!read_block(dir_node.direct[b], g_block_buf))
            return -1;

        PollikDirent *entries = (PollikDirent *)g_block_buf;
        for (u32 i = 0; i < entries_per_block; i++) {
            if (!entry_valid(&entries[i])) return -1;
            if (entries[i].inode != 0) {
                if (entry_index == file->offset) {
                    dirent->inode = le32(&entries[i].inode);
                    dirent->type = entries[i].file_type;
                    u32 n = 0;
                    while (n < entries[i].name_len) {
                        dirent->name[n] = entries[i].name[n];
                        n++;
                    }
                    dirent->name[n] = 0;
                    file->offset++;
                    return 1;
                }
                entry_index++;
            }
        }
    }

    return 0;
}

static int mutate_rename(const char *oldpath, const char *newpath) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !oldpath || !newpath)
        return -1;

    char old_name[56], new_name[56];
    u32 old_parent = resolve_path_parent(oldpath, old_name);
    if (!old_parent)
        return -1;
    u32 new_parent = resolve_path_parent(newpath, new_name);
    if (!new_parent)
        return -1;

    u32 old_block = 0, old_index = 0, target_ino = 0, target_type = 0;
    if (!find_dirent(old_parent, old_name, &old_block, &old_index, &target_ino, &target_type)) {
        if (g_fs_error == VFS_OK) g_fs_error = VFS_NOT_FOUND;
        return -1;
    }
    if (dir_find_entry(new_parent, new_name, 0)) {
        g_fs_error = VFS_EXISTS; /* documented: rename never overwrites */
        return -1;
    }
    if (target_type == VFS_DIR && path_contains_inode(newpath, target_ino)) {
        g_fs_error = VFS_DENIED; /* would create a directory cycle */
        return -1;
    }

    /* Add the destination first so a failure cannot lose the only entry. */
    if (!dir_add_entry(new_parent, new_name, target_ino, target_type)) {
        if (g_fs_error == VFS_OK) g_fs_error = VFS_IO;
        return -1;
    }
    if (!clear_dirent(old_block, old_index))
        return -1;

    superblock_sync();
    flush_volume();
    return 0;
}

/* Replace two directory records without allocating space. With the x86_64
 * single-BSP IF=0 VFS contract, the operation is not visible between record
 * writes; if the second write fails, restore the first record's full block. */
static int mutate_rename_replace(const char *oldpath, const char *newpath) {
    g_fs_error = VFS_OK;
    if (!g_fs_mounted || !oldpath || !newpath) return -1;

    char old_name[56], new_name[56];
    u32 old_parent = resolve_path_parent(oldpath, old_name);
    if (!old_parent) return -1;
    u32 new_parent = resolve_path_parent(newpath, new_name);
    if (!new_parent) return -1;

    u32 source_block = 0, source_index = 0, source_ino = 0, source_type = 0;
    if (!find_dirent(old_parent, old_name, &source_block, &source_index,
                     &source_ino, &source_type)) {
        if (g_fs_error == VFS_OK) g_fs_error = VFS_NOT_FOUND;
        return -1;
    }
    if (old_parent == new_parent) {
        u32 i = 0;
        while (old_name[i] && old_name[i] == new_name[i]) ++i;
        if (!old_name[i] && !new_name[i]) {
            if (source_type != VFS_FILE) { g_fs_error = VFS_ISDIR; return -1; }
            return 0;
        }
    }

    u32 target_block = 0, target_index = 0, target_ino = 0, target_type = 0;
    if (!find_dirent(new_parent, new_name, &target_block, &target_index,
                     &target_ino, &target_type)) {
        if (g_fs_error != VFS_OK) return -1;
        return mutate_rename(oldpath,newpath);
    }
    if (source_ino == target_ino) return 0;
    if (source_type != VFS_FILE || target_type != VFS_FILE) {
        g_fs_error = VFS_ISDIR;
        return -1;
    }

    PollikInode source_node, target_node;
    if (!read_inode(source_ino, &source_node) || !read_inode(target_ino, &target_node)) return -1;
    if (source_node.mode != VFS_FILE || target_node.mode != VFS_FILE) {
        g_fs_error = VFS_ISDIR;
        return -1;
    }

    if (source_block == target_block) {
        if (!read_block(source_block, g_block_buf)) return -1;
        PollikDirent *entries = (PollikDirent *)g_block_buf;
        if (entries[source_index].inode != source_ino || entries[target_index].inode != target_ino) {
            g_fs_error = VFS_IO;
            return -1;
        }
        entries[target_index].inode = source_ino;
        entries[target_index].file_type = VFS_FILE;
        memset(&entries[source_index], 0, sizeof(PollikDirent));
        if (!write_block(source_block, g_block_buf)) { g_fs_error = VFS_IO; return -1; }
    } else {
        u8 target_before[POLLIK2_BLOCK_SIZE];
        u8 source_before[POLLIK2_BLOCK_SIZE];
        if (!read_block(target_block, g_block_buf)) return -1;
        memcpy(target_before, g_block_buf, sizeof(target_before));
        PollikDirent *target_entries = (PollikDirent *)g_block_buf;
        if (target_entries[target_index].inode != target_ino) { g_fs_error = VFS_IO; return -1; }
        target_entries[target_index].inode = source_ino;
        target_entries[target_index].file_type = VFS_FILE;
        if (!write_block(target_block, g_block_buf)) {
            (void)write_block(target_block, target_before);
            g_fs_error = VFS_IO;
            return -1;
        }

        if (!read_block(source_block, g_block_buf)) {
            (void)write_block(target_block, target_before);
            return -1;
        }
        memcpy(source_before, g_block_buf, sizeof(source_before));
        PollikDirent *source_entries = (PollikDirent *)g_block_buf;
        if (source_entries[source_index].inode != source_ino) {
            (void)write_block(target_block, target_before);
            g_fs_error = VFS_IO;
            return -1;
        }
        memset(&source_entries[source_index], 0, sizeof(PollikDirent));
        if (!write_block(source_block, g_block_buf)) {
            (void)write_block(source_block, source_before);
            (void)write_block(target_block, target_before);
            g_fs_error = VFS_IO;
            return -1;
        }
    }

    free_inode_blocks(&target_node);
    PollikInode empty;
    memset(&empty, 0, sizeof(empty));
    if (!write_inode(target_ino, &empty)) { g_fs_error = VFS_IO; return -1; }
    g_sb.free_inodes++;
    if (!superblock_sync()) { g_fs_error = VFS_IO; return -1; }
    flush_volume();
    return 0;
}

u32 pollikfs_free_blocks(void) { return g_fs_mounted ? g_sb.free_blocks : 0; }
u32 pollikfs_capacity_blocks(void) { return g_fs_mounted ? g_sb.total_blocks-g_sb.data_blocks_start : 0; }
u32 pollikfs_free_inodes(void) { return g_fs_mounted ? g_sb.free_inodes : 0; }

static PollikSuperblock transaction_before;
#include "pollikfs_check.inc"
#ifdef POLLIK_X64
static u32 generations_before[POLLIK2_INODE_COUNT];
#endif
static int mutation_begin(void) {
    if(!g_fs_mounted || !fs_journal_begin()) { g_fs_error=VFS_DENIED;return 0; }
    transaction_before=g_sb;
#ifdef POLLIK_X64
    memcpy(generations_before,g_inode_generation,sizeof(generations_before));
#endif
    return 1;
}
static int mutation_end(int result) {
    int saved=g_fs_error;
    if(!fs_journal_end(result>=0)) result=-1;
    if(result<0 && !fs_journal_committed()) {
        g_sb=transaction_before;
#ifdef POLLIK_X64
        memcpy(g_inode_generation,generations_before,sizeof(generations_before));
#endif
    }
    if(result<0) g_fs_error=saved?saved:VFS_IO;
    return result;
}
int pollikfs_readonly(void) { return fs_journal_readonly(); }
int pollikfs_open(const char *path,int flags,vfs_file_t *out) {
    if(!(flags&(O_CREAT|O_TRUNC))) return mutate_open(path,flags,out);
    if(!mutation_begin()) return -1;
    return mutation_end(mutate_open(path,flags,out));
}
int pollikfs_write(vfs_file_t *file,const void *buffer,u32 count) {
    if(!count) return 0;
    /* Invalid range must not even initialize the journal. */
    if(!file || !buffer) return -1;
    PollikInode node;if(!read_inode(file->inode,&node)) return -1;
    u32 at=(file->flags&O_APPEND)?node.size:file->offset;
    if(at>POLLIK2_MAX_FILE || count>POLLIK2_MAX_FILE-at) { g_fs_error=VFS_NOSPACE;return -1; }
    if(!mutation_begin()) return -1;
    u32 old_offset=file->offset,old_size=file->size;
    int result=mutation_end(mutate_write(file,buffer,count));
    if(result<0) { file->offset=old_offset;file->size=old_size; }
    return result;
}
int pollikfs_mkdir(const char *path) { if(!mutation_begin()) return -1;return mutation_end(mutate_mkdir(path)); }
int pollikfs_unlink(const char *path) { if(!mutation_begin()) return -1;return mutation_end(mutate_unlink(path)); }
int pollikfs_rmdir(const char *path) { if(!mutation_begin()) return -1;return mutation_end(mutate_rmdir(path)); }
int pollikfs_rename(const char *a,const char *b) { if(!mutation_begin()) return -1;return mutation_end(mutate_rename(a,b)); }
int pollikfs_rename_replace(const char *a,const char *b) { if(!mutation_begin()) return -1;return mutation_end(mutate_rename_replace(a,b)); }
