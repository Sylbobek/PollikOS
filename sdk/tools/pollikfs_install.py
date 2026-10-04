"""Safe PollikFS v2 image updates shared by pollikinstall and fixture tooling.

The module never formats an image. Every mutating method works on an in-memory
copy; save() writes it beside the target and replaces the original only after
the complete image has been written. Superblock validation happens before any
change, so unrelated or damaged images are refused rather than altered.
"""
import struct
from pathlib import Path

POLLIK2_MAGIC = 0x504B4632
BLOCK_SIZE = 1024
START = 64 * 512
VFS_FILE = 1
VFS_DIR = 2
INODE_SIZE = 60
INODES_PER_BLOCK = 17
DIRENT_SIZE = 64
DIRENTS_PER_BLOCK = 16
DIRECT_BLOCKS = 8
NAME_CAPACITY = 55


class PollikFsError(Exception):
    pass


def _pack_dirent(inode, name, file_type):
    name_bytes = name.encode("ascii")[:NAME_CAPACITY]
    if not name_bytes:
        raise PollikFsError("directory entry name must not be empty")
    padded = name_bytes + b"\x00" * (56 - len(name_bytes))
    return struct.pack("<IHBB56s", inode, DIRENT_SIZE, len(name_bytes), file_type, padded)


class PollikFsImage:
    def __init__(self, data):
        self.data = bytearray(data)
        if len(self.data) < START + BLOCK_SIZE:
            raise PollikFsError("image too small for a PollikFS superblock")
        (self.magic, self.block_size, self.total_blocks, self.inode_count,
         self.free_blocks, self.free_inodes, self.root_inode, self.bitmap_block,
         self.bitmap_count, self.inode_table_block, self.inode_table_count,
         self.data_start, self.generation) = struct.unpack_from("<13I", self.data, START)
        if self.magic != POLLIK2_MAGIC or self.block_size != BLOCK_SIZE:
            raise PollikFsError("not a PollikFS v2 image (refusing to modify)")
        if self.inode_count == 0 or self.total_blocks * BLOCK_SIZE > len(self.data) - START:
            raise PollikFsError("image is smaller than its superblock geometry")
        if self.bitmap_block == 0 or self.bitmap_count == 0 or self.inode_table_block == 0:
            raise PollikFsError("PollikFS superblock layout is not supported")

    @classmethod
    def load(cls, path):
        return cls(Path(path).read_bytes())

    def save(self, path):
        target = Path(path)
        pending = target.with_name(target.name + ".pending")
        pending.write_bytes(self.data)
        pending.replace(target)

    # -- bitmap ------------------------------------------------------------
    def _bitmap_bit(self, block):
        offset = START + self.bitmap_block * BLOCK_SIZE + block // 8
        return (self.data[offset] >> (block % 8)) & 1

    def _set_bitmap_bit(self, block, used):
        offset = START + self.bitmap_block * BLOCK_SIZE + block // 8
        if used:
            self.data[offset] |= 1 << (block % 8)
        else:
            self.data[offset] &= ~(1 << (block % 8)) & 0xFF

    def allocate_block(self):
        for block in range(self.data_start, self.total_blocks):
            if not self._bitmap_bit(block):
                self._set_bitmap_bit(block, True)
                self.free_blocks -= 1
                return block
        raise PollikFsError("no free PollikFS data blocks")

    # -- inodes ------------------------------------------------------------
    def inode_offset(self, number):
        block = self.inode_table_block + number // INODES_PER_BLOCK
        return START + block * BLOCK_SIZE + (number % INODES_PER_BLOCK) * INODE_SIZE

    def read_inode(self, number):
        offset = self.inode_offset(number)
        mode, size = struct.unpack_from("<II", self.data, offset)
        direct = list(struct.unpack_from("<8I", self.data, offset + 8))
        indirect = struct.unpack_from("<I", self.data, offset + 40)[0]
        return mode, size, direct, indirect

    def write_inode(self, number, mode, size, direct, indirect=0):
        directs = (list(direct) + [0] * DIRECT_BLOCKS)[:DIRECT_BLOCKS]
        offset = self.inode_offset(number)
        struct.pack_into("<15I", self.data, offset, mode, size, *directs, indirect, 0, 0, 0, 0)

    def _write_inode_full(self, number, mode, size, direct, indirect=0, double_indirect=0):
        directs = (list(direct) + [0] * DIRECT_BLOCKS)[:DIRECT_BLOCKS]
        struct.pack_into("<15I", self.data, self.inode_offset(number),
                         mode, size, *directs, indirect, 0, 0, double_indirect, 0)

    def allocate_inode(self):
        for number in range(1, self.inode_count):
            if self.read_inode(number)[0] == 0:
                self.free_inodes -= 1
                return number
        raise PollikFsError("no free PollikFS inodes")

    # -- directories -------------------------------------------------------
    def _indirect_blocks(self, indirect):
        if not indirect:
            return []
        raw = struct.unpack_from("<256I", self.data, START + indirect * BLOCK_SIZE)
        return [block for block in raw if block]

    def directory_entries(self, inode_number):
        mode, size, direct, indirect = self.read_inode(inode_number)
        if mode != VFS_DIR:
            raise PollikFsError("inode is not a directory")
        blocks = [block for block in direct if block] + self._indirect_blocks(indirect)
        entries = []
        remaining = size
        for block in blocks:
            if remaining <= 0:
                break
            count = min(BLOCK_SIZE, remaining)
            for offset in range(0, count, DIRENT_SIZE):
                position = START + block * BLOCK_SIZE + offset
                inode, struct_size, name_length, file_type = struct.unpack_from("<IHBB", self.data, position)
                raw = self.data[position + 8:position + 8 + 56]
                name = raw.split(b"\x00", 1)[0].decode("ascii", "replace")
                if inode:
                    entries.append({"inode": inode, "type": file_type, "name": name,
                                    "struct_size": struct_size, "name_length": name_length})
            remaining -= count
        return entries

    def resolve_directory(self, path):
        if not path.startswith("/"):
            raise PollikFsError("absolute directory path required")
        current = self.root_inode
        for part in [piece for piece in path.split("/") if piece]:
            entry = next((item for item in self.directory_entries(current) if item["name"] == part), None)
            if not entry:
                raise PollikFsError(f"no such directory: /{part}")
            if self.read_inode(entry["inode"])[0] != VFS_DIR:
                raise PollikFsError(f"not a directory: /{part}")
            current = entry["inode"]
        return current

    def ensure_directory(self, path):
        """Create a directory path without changing existing disk structures."""
        if not path.startswith("/"):
            raise PollikFsError("absolute directory path required")
        current = self.root_inode
        for part in [piece for piece in path.split("/") if piece]:
            entry = next((item for item in self.directory_entries(current)
                          if item["name"] == part), None)
            if entry:
                if self.read_inode(entry["inode"])[0] != VFS_DIR:
                    raise PollikFsError(f"not a directory: {part}")
                current = entry["inode"]
                continue
            child = self.allocate_inode()
            block = self.allocate_block()
            start = START + block * BLOCK_SIZE
            self.data[start:start + BLOCK_SIZE] = bytes(BLOCK_SIZE)
            self._write_inode_full(child, VFS_DIR, BLOCK_SIZE, [block])
            self._insert_dirent(current, part, child, VFS_DIR)
            self._write_superblock()
            current = child
        return current

    def read_file(self, path):
        directory_path, _, name = path.rpartition("/")
        directory = self.resolve_directory(directory_path or "/")
        entry = next((item for item in self.directory_entries(directory)
                      if item["name"] == name), None)
        if not entry:
            raise PollikFsError(f"no such file: {path}")
        mode, size, direct, indirect = self.read_inode(entry["inode"])
        if mode != VFS_FILE:
            raise PollikFsError(f"not a regular file: {path}")
        blocks = [block for block in direct if block]
        if indirect:
            blocks += self._indirect_blocks(indirect)
        offset = self.inode_offset(entry["inode"]) + 52
        double_indirect = struct.unpack_from("<I", self.data, offset)[0]
        if double_indirect:
            middle = struct.unpack_from("<256I", self.data,
                                        START + double_indirect * BLOCK_SIZE)
            for table in middle:
                if table:
                    blocks += self._indirect_blocks(table)
        return b"".join(self.data[START + block * BLOCK_SIZE:
                                   START + (block + 1) * BLOCK_SIZE]
                         for block in blocks)[:size]

    def _slot_offsets(self, inode_number):
        mode, size, direct, indirect = self.read_inode(inode_number)
        blocks = [block for block in direct if block] + self._indirect_blocks(indirect)
        remaining = size
        for block in blocks:
            if remaining <= 0:
                break
            count = min(BLOCK_SIZE, remaining)
            for offset in range(0, count, DIRENT_SIZE):
                yield block, offset
            remaining -= count

    def _insert_dirent(self, inode_number, name, inode, file_type):
        mode, size, direct, indirect = self.read_inode(inode_number)
        for block, offset in self._slot_offsets(inode_number):
            position = START + block * BLOCK_SIZE + offset
            if struct.unpack_from("<I", self.data, position)[0] == 0:
                self.data[position:position + DIRENT_SIZE] = _pack_dirent(inode, name, file_type)
                return
        used = len([block for block in direct if block])
        if used >= DIRECT_BLOCKS:
            raise PollikFsError("directory has no free slot and no direct block available")
        block = self.allocate_block()
        direct[used] = block
        size += BLOCK_SIZE
        self.write_inode(inode_number, mode, size, direct, indirect)
        position = START + block * BLOCK_SIZE
        self.data[position:position + DIRENT_SIZE] = _pack_dirent(inode, name, file_type)

    # -- installation ------------------------------------------------------
    def exists(self, vfs_path):
        directory_path, _, name = vfs_path.rpartition("/")
        directory = self.resolve_directory(directory_path or "/")
        return any(entry["name"] == name for entry in self.directory_entries(directory))

    def install_file(self, vfs_path, payload, file_type=VFS_FILE):
        if not vfs_path.startswith("/") or vfs_path.endswith("/"):
            raise PollikFsError("absolute target path required")
        directory_path, _, name = vfs_path.rpartition("/")
        if not name or len(name) > NAME_CAPACITY:
            raise PollikFsError("invalid PollikFS file name")
        directory = self.resolve_directory(directory_path or "/")
        if any(entry["name"] == name for entry in self.directory_entries(directory)):
            raise PollikFsError(f"{vfs_path} already exists")
        blocks = []
        for offset in range(0, len(payload), BLOCK_SIZE):
            chunk = payload[offset:offset + BLOCK_SIZE]
            block = self.allocate_block()
            position = START + block * BLOCK_SIZE
            self.data[position:position + len(chunk)] = chunk
            blocks.append(block)
        indirect = 0
        if len(blocks) > DIRECT_BLOCKS:
            indirect = self.allocate_block()
            self.data[START + indirect * BLOCK_SIZE:START + (indirect + 1) * BLOCK_SIZE] = bytes(BLOCK_SIZE)
            tail = blocks[DIRECT_BLOCKS:DIRECT_BLOCKS + BLOCK_SIZE // 4]
            struct.pack_into(f"<{len(tail)}I", self.data,
                             START + indirect * BLOCK_SIZE, *tail)
        double_indirect = 0
        if len(blocks) > DIRECT_BLOCKS + BLOCK_SIZE // 4:
            double_indirect = self.allocate_block()
            self.data[START + double_indirect * BLOCK_SIZE:
                      START + (double_indirect + 1) * BLOCK_SIZE] = bytes(BLOCK_SIZE)
            remaining = blocks[DIRECT_BLOCKS + BLOCK_SIZE // 4:]
            tables = []
            for offset in range(0, len(remaining), BLOCK_SIZE // 4):
                table = self.allocate_block()
                tables.append(table)
                self.data[START + table * BLOCK_SIZE:
                          START + (table + 1) * BLOCK_SIZE] = bytes(BLOCK_SIZE)
                group = remaining[offset:offset + BLOCK_SIZE // 4]
                struct.pack_into(f"<{len(group)}I", self.data,
                                 START + table * BLOCK_SIZE, *group)
            struct.pack_into(f"<{len(tables)}I", self.data,
                             START + double_indirect * BLOCK_SIZE, *tables)
        inode = self.allocate_inode()
        self._write_inode_full(inode, file_type, len(payload), blocks[:DIRECT_BLOCKS],
                               indirect, double_indirect)
        self._insert_dirent(directory, name, inode, file_type)
        self._write_superblock()
        return inode, blocks

    def consume_free_blocks(self, keep):
        """Mark free data blocks used, keeping the first `keep` free. This is
        only for building disposable ENOSPC test images; metadata is untouched."""
        kept = 0
        for block in range(self.data_start, self.total_blocks):
            if self._bitmap_bit(block):
                continue
            if kept < keep:
                kept += 1
                continue
            self._set_bitmap_bit(block, True)
            self.free_blocks -= 1
        self._write_superblock()
        return kept

    def _write_superblock(self):
        struct.pack_into("<13I", self.data, START, self.magic, self.block_size,
                         self.total_blocks, self.inode_count, self.free_blocks,
                         self.free_inodes, self.root_inode, self.bitmap_block,
                         self.bitmap_count, self.inode_table_block,
                         self.inode_table_count, self.data_start, self.generation)
