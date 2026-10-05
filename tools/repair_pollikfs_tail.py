"""Explicitly restore an absent, entirely unallocated PollikFS v2 tail.

Never format or migrate. Refuse missing allocated blocks or any inconsistent
ownership/directory metadata. Preserve every existing byte except stale free
counters, and save an exact original image before committing the repair.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
from sync_system_files import exclusive_image, validate, manifest, SPAN
from pollikfs_install import PollikFsImage, PollikFsError, START, BLOCK_SIZE


def repair(path, backup):
    with exclusive_image(path) as disk:
        length = os.fstat(disk.fileno()).st_size
        if not START + 36 * BLOCK_SIZE <= length < SPAN:
            raise PollikFsError('repair requires a truncated image with its complete metadata present')
        original = disk.read(length)
        padded = bytearray(original + bytes(SPAN - length))
        fs = PollikFsImage(padded)
        geometry = (fs.total_blocks, fs.inode_count, fs.root_inode, fs.bitmap_block,
                    fs.bitmap_count, fs.inode_table_block, fs.inode_table_count, fs.data_start)
        if geometry != (32768, 512, 1, 1, 4, 5, 31, 36):
            raise PollikFsError(f'unsupported v2 geometry {geometry}; refusing to migrate')
        used = {b for b in range(fs.total_blocks) if fs._bitmap_bit(b)}
        for block in used:
            if START + (block + 1) * BLOCK_SIZE > length:
                raise PollikFsError(f'allocated block {block} is absent; cannot restore file data')
        free_inodes = sum(fs.read_inode(i)[0] == 0 for i in range(1, fs.inode_count))
        old_counts = (fs.free_blocks, fs.free_inodes)
        # Kernel formatting counts reserved zero-mode inode 0 as free.
        new_counts = (fs.total_blocks - len(used), free_inodes + 1)
        struct.pack_into('<2I', padded, START + 16, *new_counts)
        checked = validate(padded)
        before = manifest(checked)
        offset = START + 16
        assert padded[:offset] == original[:offset]
        assert padded[offset + 8:length] == original[offset + 8:]
        # Exclusive creation prevents overwriting any existing backup.
        with Path(backup).open('xb') as saved:
            if saved.write(original) != length: raise OSError('short backup write')
            saved.flush()
            os.fsync(saved.fileno())
        try:
            disk.truncate(SPAN)
            disk.seek(offset)
            if disk.write(padded[offset:offset + 8]) != 8: raise OSError('short counter write')
            disk.flush()
            os.fsync(disk.fileno())
            disk.seek(0)
            actual = disk.read(SPAN)
            if actual != padded or manifest(validate(actual)) != before:
                raise OSError('repair readback/preservation failed')
        except (OSError, PollikFsError):
            disk.seek(offset)
            disk.write(original[offset:offset + 8])
            disk.truncate(length)
            disk.flush()
            os.fsync(disk.fileno())
            raise
        print(f'REPAIR original_bytes={length} repaired_bytes={SPAN} allocated_blocks={len(used)} missing_allocated_blocks=0')
        print(f'REPAIR free_blocks={old_counts[0]}->{new_counts[0]} free_inodes={old_counts[1]}->{new_counts[1]}')
        print(f'BACKUP {backup} bytes={length} SHA256={hashlib.sha256(original).hexdigest()}')
        print(f'PRESERVED all_original_file_hashes={sum(r[0] == "file" for r in before.values())}; original_bytes_except_8_counter_bytes=identical')
        return before


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--backup', type=Path, required=True)
    args = parser.parse_args()
    try:
        repair(args.image, args.backup)
    except (PollikFsError, OSError, ValueError, struct.error) as error:
        raise SystemExit(f'Tail repair REFUSED: {error}')


if __name__ == '__main__': main()
