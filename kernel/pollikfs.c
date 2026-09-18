#include "pollikfs.h"
#include "klog.h"

extern int ata_read_sector(u32 lba, void *buffer);
extern int ata_write_sector(u32 lba, const void *buffer);
extern int ata_flush(void);

extern u8 _binary_build_hello_elf_start[];
extern u8 _binary_build_hello_elf_end[];
extern u8 _binary_build_fault_test_elf_start[];
extern u8 _binary_build_fault_test_elf_end[];
extern u8 _binary_build_fault_kernel_elf_start[];
extern u8 _binary_build_fault_kernel_elf_end[];
extern u8 _binary_build_fault_stack_elf_start[];
extern u8 _binary_build_fault_stack_elf_end[];

static PollikSuperblock g_sb;
static u8 g_block_buf[POLLIK2_BLOCK_SIZE];
static u8 g_block_buf2[POLLIK2_BLOCK_SIZE];
static int g_fs_mounted = 0;

static int read_block(u32 block_num, void *buf) {
    u32 lba = POLLIK2_START_LBA + block_num * POLLIK2_SECTORS_PER_BLOCK;
    if (!ata_read_sector(lba, buf))
        return 0;
    if (!ata_read_sector(lba + 1, (u8 *)buf + 512))
        return 0;
    return 1;
}

static int write_block(u32 block_num, const void *buf) {
    u32 lba = POLLIK2_START_LBA + block_num * POLLIK2_SECTORS_PER_BLOCK;
    if (!ata_write_sector(lba, buf))
        return 0;
    if (!ata_write_sector(lba + 1, (const u8 *)buf + 512))
        return 0;
    return 1;
}

static int read_inode(u32 inode_num, PollikInode *out_inode) {
    if (inode_num == 0 || inode_num >= g_sb.inode_count)
        return 0;

    u32 inodes_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikInode);
    u32 block = g_sb.inode_table_block + (inode_num / inodes_per_block);
    u32 offset = (inode_num % inodes_per_block) * sizeof(PollikInode);

    if (!read_block(block, g_block_buf))
        return 0;

    memcpy(out_inode, g_block_buf + offset, sizeof(PollikInode));
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

static u32 alloc_block(void) {
    for (u32 b = 0; b < g_sb.block_bitmap_count; b++) {
        if (!read_block(g_sb.block_bitmap_block + b, g_block_buf))
            return 0;

        for (u32 byte_idx = 0; byte_idx < POLLIK2_BLOCK_SIZE; byte_idx++) {
            if (g_block_buf[byte_idx] != 0xFF) {
                for (u32 bit = 0; bit < 8; bit++) {
                    if (!(g_block_buf[byte_idx] & (1u << bit))) {
                        g_block_buf[byte_idx] |= (1u << bit);
                        write_block(g_sb.block_bitmap_block + b, g_block_buf);

                        u32 block_num = (b * POLLIK2_BLOCK_SIZE + byte_idx) * 8 + bit;
                        g_sb.free_blocks--;

                        /* Zero out newly allocated block */
                        memset(g_block_buf2, 0, POLLIK2_BLOCK_SIZE);
                        write_block(block_num, g_block_buf2);

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

        if (!read_block(dir_node.direct[b], g_block_buf))
            continue;

        PollikDirent *entries = (PollikDirent *)g_block_buf;
        for (u32 i = 0; i < entries_per_block; i++) {
            if (entries[i].inode != 0) {
                int match = 1;
                u32 idx = 0;
                while (name[idx] || entries[i].name[idx]) {
                    if (name[idx] != entries[i].name[idx]) {
                        match = 0;
                        break;
                    }
                    idx++;
                }
                if (match) {
                    if (out_type)
                        *out_type = entries[i].file_type;
                    return entries[i].inode;
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
            if (!curr_inode || type != VFS_DIR)
                return 0;
        }
    }

    return 0;
}

static u32 resolve_path(const char *path) {
    if (!path || path[0] != '/')
        return 0;

    if (path[1] == 0)
        return g_sb.root_inode;

    char name[56];
    u32 parent = resolve_path_parent(path, name);
    if (!parent)
        return 0;

    u32 type = 0;
    return dir_find_entry(parent, name, &type);
}

void pollikfs_format(void) {
    /* Destructive operation: callers must explicitly request formatting.
     * Never invoke this as recovery from a mount failure. */
    g_fs_mounted = 0;
    KLOG_INFO(KLOG_CAT_BOOT, "Formatting PollikFS v2 filesystem");

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

    INSTALL_EMBEDDED("hello", _binary_build_hello_elf_start, _binary_build_hello_elf_end);
    INSTALL_EMBEDDED("fault_test", _binary_build_fault_test_elf_start, _binary_build_fault_test_elf_end);
    INSTALL_EMBEDDED("fault_kernel", _binary_build_fault_kernel_elf_start, _binary_build_fault_kernel_elf_end);
    INSTALL_EMBEDDED("fault_stack", _binary_build_fault_stack_elf_start, _binary_build_fault_stack_elf_end);
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
    ata_flush();

    KLOG_INFO(KLOG_CAT_BOOT, "PollikFS v2 format complete (/bin/hello, /home/notes.txt created)");
}

void pollikfs_init(void) {
    g_fs_mounted = 0;
    if (!read_block(0, g_block_buf)) {
        KLOG_ERROR(KLOG_CAT_BOOT, "PollikFS v2: failed to read superblock from data disk");
        return;
    }

    memcpy(&g_sb, g_block_buf, sizeof(PollikSuperblock));
    if (g_sb.magic != POLLIK2_MAGIC) {
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
        KLOG_ERROR(KLOG_CAT_BOOT, "PollikFS v2: invalid superblock geometry; mount refused, disk unchanged");
        return;
    }

    g_fs_mounted = 1;
    KLOG_INFO(KLOG_CAT_BOOT, "PollikFS v2 superblock mounted successfully");
}

int pollikfs_open(const char *path, int flags, vfs_file_t *out_file) {
    if (!g_fs_mounted || !path || !out_file)
        return -1;

    u32 ino = resolve_path(path);
    if (!ino) {
        if (flags & O_CREAT) {
            char name[56];
            u32 parent = resolve_path_parent(path, name);
            if (!parent)
                return -1;

            ino = alloc_inode(VFS_FILE);
            if (!ino)
                return -1;

            if (!dir_add_entry(parent, name, ino, VFS_FILE))
                return -1;
        } else {
            return -1;
        }
    }

    PollikInode node;
    if (!read_inode(ino, &node))
        return -1;

    if (flags & O_TRUNC) {
        for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; b++) {
            if (node.direct[b]) {
                free_block(node.direct[b]);
                node.direct[b] = 0;
            }
        }
        node.size = 0;
        node.modified = ticks;
        write_inode(ino, &node);
    }

    out_file->type = (node.mode == VFS_DIR) ? VFS_DIR : VFS_FILE;
    out_file->flags = flags;
    out_file->offset = (flags & O_APPEND) ? node.size : 0;
    out_file->size = node.size;
    out_file->inode = ino;
    out_file->ref_count = 1;
    out_file->fs_private = 0;

    return 0;
}

int pollikfs_read(vfs_file_t *file, void *buf, u32 count) {
    if (!g_fs_mounted || !file || !buf || count == 0)
        return 0;

    PollikInode node;
    if (!read_inode(file->inode, &node))
        return -1;

    if (file->offset >= node.size)
        return 0;

    if (file->offset + count > node.size)
        count = node.size - file->offset;

    u32 total_read = 0;
    u8 *dest = (u8 *)buf;

    while (total_read < count) {
        u32 blk_idx = (file->offset + total_read) / POLLIK2_BLOCK_SIZE;
        u32 blk_offset = (file->offset + total_read) % POLLIK2_BLOCK_SIZE;
        u32 chunk = POLLIK2_BLOCK_SIZE - blk_offset;
        if (chunk > count - total_read)
            chunk = count - total_read;

        u32 phys_blk = 0;
        if (blk_idx < POLLIK2_DIRECT_BLOCKS) {
            phys_blk = node.direct[blk_idx];
        } else {
            /* Read from indirect block */
            if (node.indirect) {
                if (read_block(node.indirect, g_block_buf2)) {
                    u32 *ind = (u32 *)g_block_buf2;
                    phys_blk = ind[blk_idx - POLLIK2_DIRECT_BLOCKS];
                }
            }
        }

        if (phys_blk) {
            if (!read_block(phys_blk, g_block_buf))
                break;
            memcpy(dest + total_read, g_block_buf + blk_offset, chunk);
        } else {
            memset(dest + total_read, 0, chunk);
        }

        total_read += chunk;
    }

    file->offset += total_read;
    return total_read;
}

int pollikfs_write(vfs_file_t *file, const void *buf, u32 count) {
    if (!g_fs_mounted || !file || !buf || count == 0)
        return 0;

    PollikInode node;
    if (!read_inode(file->inode, &node))
        return -1;

    u32 total_written = 0;
    const u8 *src = (const u8 *)buf;

    while (total_written < count) {
        u32 blk_idx = (file->offset + total_written) / POLLIK2_BLOCK_SIZE;
        u32 blk_offset = (file->offset + total_written) % POLLIK2_BLOCK_SIZE;
        u32 chunk = POLLIK2_BLOCK_SIZE - blk_offset;
        if (chunk > count - total_written)
            chunk = count - total_written;

        u32 phys_blk = 0;
        if (blk_idx < POLLIK2_DIRECT_BLOCKS) {
            if (!node.direct[blk_idx]) {
                node.direct[blk_idx] = alloc_block();
                if (!node.direct[blk_idx])
                    break;
            }
            phys_blk = node.direct[blk_idx];
        } else {
            /* Indirect block */
            if (!node.indirect) {
                node.indirect = alloc_block();
                if (!node.indirect)
                    break;
            }
            if (!read_block(node.indirect, g_block_buf2))
                break;
            u32 *ind = (u32 *)g_block_buf2;
            u32 ind_idx = blk_idx - POLLIK2_DIRECT_BLOCKS;
            if (ind_idx >= (POLLIK2_BLOCK_SIZE / sizeof(u32)))
                break;
            if (!ind[ind_idx]) {
                ind[ind_idx] = alloc_block();
                if (!ind[ind_idx])
                    break;
                write_block(node.indirect, g_block_buf2);
            }
            phys_blk = ind[ind_idx];
        }

        if (blk_offset > 0 || chunk < POLLIK2_BLOCK_SIZE) {
            read_block(phys_blk, g_block_buf);
        }
        memcpy(g_block_buf + blk_offset, src + total_written, chunk);
        write_block(phys_blk, g_block_buf);

        total_written += chunk;
    }

    file->offset += total_written;
    if (file->offset > node.size)
        node.size = file->offset;

    node.modified = ticks;
    write_inode(file->inode, &node);
    file->size = node.size;
    ata_flush();

    return total_written;
}

int pollikfs_seek(vfs_file_t *file, int offset, int whence) {
    if (!file)
        return -1;

    int new_pos = 0;
    if (whence == SEEK_SET) {
        new_pos = offset;
    } else if (whence == SEEK_CUR) {
        new_pos = (int)file->offset + offset;
    } else if (whence == SEEK_END) {
        new_pos = (int)file->size + offset;
    } else {
        return -1;
    }

    if (new_pos < 0)
        return -1;

    file->offset = (u32)new_pos;
    return new_pos;
}

int pollikfs_close(vfs_file_t *file) {
    if (!file)
        return -1;
    file->ref_count--;
    return 0;
}

int pollikfs_stat(const char *path, vfs_stat_t *st) {
    if (!g_fs_mounted || !path || !st)
        return -1;

    u32 ino = resolve_path(path);
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

int pollikfs_mkdir(const char *path) {
    if (!g_fs_mounted || !path)
        return -1;

    char name[56];
    u32 parent = resolve_path_parent(path, name);
    if (!parent)
        return -1;

    u32 new_ino = alloc_inode(VFS_DIR);
    if (!new_ino)
        return -1;

    PollikInode node;
    read_inode(new_ino, &node);
    node.direct[0] = alloc_block();
    node.size = POLLIK2_BLOCK_SIZE;
    write_inode(new_ino, &node);

    if (!dir_add_entry(parent, name, new_ino, VFS_DIR))
        return -1;

    ata_flush();
    return 0;
}

int pollikfs_unlink(const char *path) {
    if (!g_fs_mounted || !path)
        return -1;

    char name[56];
    u32 parent = resolve_path_parent(path, name);
    if (!parent)
        return -1;

    PollikInode parent_node;
    if (!read_inode(parent, &parent_node) || parent_node.mode != VFS_DIR)
        return -1;

    u32 entries_per_block = POLLIK2_BLOCK_SIZE / sizeof(PollikDirent);

    for (u32 b = 0; b < POLLIK2_DIRECT_BLOCKS; b++) {
        if (!parent_node.direct[b])
            continue;

        if (!read_block(parent_node.direct[b], g_block_buf))
            continue;

        PollikDirent *entries = (PollikDirent *)g_block_buf;
        for (u32 i = 0; i < entries_per_block; i++) {
            if (entries[i].inode != 0) {
                int match = 1;
                u32 idx = 0;
                while (name[idx] || entries[i].name[idx]) {
                    if (name[idx] != entries[i].name[idx]) {
                        match = 0;
                        break;
                    }
                    idx++;
                }
                if (match) {
                    u32 target_ino = entries[i].inode;
                    entries[i].inode = 0;
                    write_block(parent_node.direct[b], g_block_buf);

                    /* Free target inode data blocks */
                    PollikInode target_node;
                    if (read_inode(target_ino, &target_node)) {
                        for (u32 db = 0; db < POLLIK2_DIRECT_BLOCKS; db++) {
                            if (target_node.direct[db]) {
                                free_block(target_node.direct[db]);
                            }
                        }
                        memset(&target_node, 0, sizeof(PollikInode));
                        write_inode(target_ino, &target_node);
                    }

                    ata_flush();
                    return 0;
                }
            }
        }
    }

    return -1;
}
