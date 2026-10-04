import struct
from pathlib import Path

POLLIK2_MAGIC = 0x504B4632  # "PKF2"
POLLIK2_BLOCK_SIZE = 1024
POLLIK2_TOTAL_BLOCKS = 32768
POLLIK2_INODE_COUNT = 512
START = 64 * 512  # 32768 bytes

VFS_FILE = 1
VFS_DIR = 2

def pack_dirent(inode, name, file_type):
    name_bytes = name.encode('ascii')[:55]
    name_len = len(name_bytes)
    name_padded = name_bytes + b'\x00' * (56 - len(name_bytes))
    return struct.pack("<IHBB56s", inode, 64, name_len, file_type, name_padded)

def pack_inode(mode, size, direct_blocks, indirect=0, double_indirect=0):
    directs = list(direct_blocks) + [0] * (8 - len(direct_blocks))
    return struct.pack("<15I", mode, size, *directs, indirect, 0, 0, double_indirect, 0)

def format_disk(disk_path, total_size_mb=16):
    disk_bytes = bytearray(total_size_mb * 1024 * 1024)

    def write_block(blk_num, data):
        offset = START + blk_num * POLLIK2_BLOCK_SIZE
        disk_bytes[offset:offset+len(data)] = data

    # Blocks allocated:
    # 0..35: superblock, bitmap (1..4), inode table (5..35)
    # 36: root dir (inode 1)
    # 37: bin dir (inode 2)
    # 38: home dir (inode 3)
    # 39: dev dir (inode 4)
    # 40: etc dir (inode 5)
    # 41: Desktop dir (inode 6)
    # 42: Trash dir (inode 7)
    # Total allocated blocks: 43 (0..42)
    allocated_blocks = 43

    # Bitmap:
    bitmap = bytearray(4096)
    for b in range(allocated_blocks):
        bitmap[b // 8] |= (1 << (b % 8))

    for i in range(4):
        chunk = bitmap[i*1024:(i+1)*1024]
        write_block(1 + i, chunk)

    # Inode table starts at block 5 (31 blocks)
    # Inode 1: root dir
    # Inode 2: bin dir
    # Inode 3: home dir
    # Inode 4: dev dir
    # Inode 5: etc dir
    # Inode 6: Desktop dir
    # Inode 7: Trash dir

    def write_inode(ino_num, inode_bytes):
        blk = 5 + (ino_num // 17)
        off = START + blk * POLLIK2_BLOCK_SIZE + (ino_num % 17) * 60
        disk_bytes[off:off+60] = inode_bytes

    write_inode(1, pack_inode(VFS_DIR, 1024, [36]))
    write_inode(2, pack_inode(VFS_DIR, 1024, [37]))
    write_inode(3, pack_inode(VFS_DIR, 1024, [38]))
    write_inode(4, pack_inode(VFS_DIR, 1024, [39]))
    write_inode(5, pack_inode(VFS_DIR, 1024, [40]))
    write_inode(6, pack_inode(VFS_DIR, 1024, [41]))
    write_inode(7, pack_inode(VFS_DIR, 1024, [42]))

    # Root dir entries in block 36: bin, home, dev, etc
    root_data = bytearray(1024)
    root_data[0:64] = pack_dirent(2, "bin", VFS_DIR)
    root_data[64:128] = pack_dirent(3, "home", VFS_DIR)
    root_data[128:192] = pack_dirent(4, "dev", VFS_DIR)
    root_data[192:256] = pack_dirent(5, "etc", VFS_DIR)
    write_block(36, root_data)

    # Bin dir (block 37)
    write_block(37, bytearray(1024))

    # Home dir entries in block 38: Desktop, Trash
    home_data = bytearray(1024)
    home_data[0:64] = pack_dirent(6, "Desktop", VFS_DIR)
    home_data[64:128] = pack_dirent(7, "Trash", VFS_DIR)
    write_block(38, home_data)

    # Dev dir (block 39)
    write_block(39, bytearray(1024))
    # Etc dir (block 40)
    write_block(40, bytearray(1024))
    # Desktop dir (block 41)
    write_block(41, bytearray(1024))
    # Trash dir (block 42)
    write_block(42, bytearray(1024))

    # Superblock (block 0)
    sb_bytes = struct.pack(
        "<13I460s",
        POLLIK2_MAGIC,
        POLLIK2_BLOCK_SIZE,
        POLLIK2_TOTAL_BLOCKS,
        POLLIK2_INODE_COUNT,
        POLLIK2_TOTAL_BLOCKS - allocated_blocks,
        POLLIK2_INODE_COUNT - 7,
        1,  # root_inode
        1,  # block_bitmap_block
        4,  # block_bitmap_count
        5,  # inode_table_block
        31, # inode_table_count
        36, # data_blocks_start
        0,  # generation
        b'\x00' * 460
    )
    write_block(0, sb_bytes)

    with open(disk_path, "wb") as f:
        f.write(disk_bytes)
    print(f"Formatted PollikFS v2 disk at {disk_path}")

if __name__ == "__main__":
    import sys
    target = sys.argv[1] if len(sys.argv) > 1 else "build/test-data.img"
    format_disk(target)
    print(f"Formatted PollikFS v2 disk at {target}")
