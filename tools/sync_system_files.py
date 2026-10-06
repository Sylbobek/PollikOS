"""Stage and validate stock wallpaper updates before writing any image bytes.

Only the fixed PollikFS v2 [31,36] geometry is accepted. Never format, migrate,
extend, truncate, or rewrite the disk tail. ENOSPC/corruption leave it unchanged.
The exclusive image handle is held through validation, staging and commit.
"""
import argparse
import contextlib
import hashlib
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sdk/tools"))
from pollikfs_install import (PollikFsImage, PollikFsError, START, BLOCK_SIZE,
                              VFS_FILE, VFS_DIR)

SPAN = START + 32768 * BLOCK_SIZE
FILES = {"/usr/share/wallpapers/light.png": ROOT / "assets/Background_LightTheme.png",
         "/usr/share/wallpapers/dark.png": ROOT / "assets/Background_BlackTheme.png"}
ICON_NAMES=('welcome','files','terminal','notes','settings','browser','pollikmark','calculator',
            'folder','folder-blue','file','trash')
FILES.update({f'/usr/share/icons/{name}.png':ROOT/f'assets/system-icons/{name}.png' for name in ICON_NAMES})

@contextlib.contextmanager
def exclusive_image(path):
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        import msvcrt
        api = ctypes.WinDLL("kernel32", use_last_error=True)
        api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                   wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
        api.CreateFileW.restype = wintypes.HANDLE
        handle = api.CreateFileW(str(Path(path).resolve()), 0xC0000000, 0, None, 3, 0x80, None)
        if handle == wintypes.HANDLE(-1).value:
            code = ctypes.get_last_error()
            raise PollikFsError(f"exclusive open refused (locked/inaccessible image, Windows error {code})")
        fd = msvcrt.open_osfhandle(handle, os.O_RDWR | os.O_BINARY)
        stream = os.fdopen(fd, "r+b")
    else:
        import fcntl
        stream = open(path, "r+b")
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            stream.close()
            raise PollikFsError("exclusive open refused (locked image)") from error
    try:
        yield stream
    finally:
        stream.close()

def validate(data):
    fs = PollikFsImage(data)
    geometry = (fs.total_blocks, fs.inode_count, fs.root_inode, fs.bitmap_block,
                fs.bitmap_count, fs.inode_table_block, fs.inode_table_count, fs.data_start)
    if geometry != (32768, 512, 1, 1, 4, 5, 31, 36):
        raise PollikFsError(f"unsupported v2 geometry {geometry}; refusing to migrate")
    owners = {}
    def claim(block, owner):
        if block < fs.data_start or block >= fs.total_blocks:
            raise PollikFsError(f"invalid block {block} ({owner})")
        if block in owners:
            raise PollikFsError(f"duplicate block {block} ({owner}, {owners[block]})")
        owners[block] = owner
    for number in range(1, fs.inode_count):
        values = struct.unpack_from("<15I", data, fs.inode_offset(number))
        mode, size = values[:2]
        if mode == 0:
            continue
        if mode not in (VFS_FILE, VFS_DIR):
            raise PollikFsError(f"invalid inode mode {number}:{mode}")
        blocks = list(values[2:10])
        tables = []
        if values[10]: tables.append(values[10])
        if values[13]:
            claim(values[13], f"inode {number} double table")
            tables.extend(b for b in struct.unpack_from("<256I", data, START + values[13]*BLOCK_SIZE) if b)
        for table in tables:
            claim(table, f"inode {number} table")
            blocks.extend(struct.unpack_from("<256I", data, START + table*BLOCK_SIZE))
        for block in blocks:
            if block: claim(block, f"inode {number} data")
        needed = (size + BLOCK_SIZE - 1) // BLOCK_SIZE
        if needed > len(blocks) or any(not b for b in blocks[:needed]) or any(blocks[needed:]):
            raise PollikFsError(f"invalid size/block sequence in inode {number}")
        if mode == VFS_DIR:
            if size % BLOCK_SIZE or values[13]:
                raise PollikFsError(f"unsupported directory layout in inode {number}")
            names = set()
            for entry in fs.directory_entries(number):
                name = entry['name']
                if (entry['struct_size'] != 64 or entry['name_length'] != len(name) or
                        not 0 < len(name) <= 55 or '/' in name or name in names or
                        not 0 < entry['inode'] < fs.inode_count or
                        fs.read_inode(entry['inode'])[0] != entry['type'] or
                        entry['type'] not in (VFS_FILE, VFS_DIR)):
                    raise PollikFsError(f"invalid directory entry in inode {number}: {entry}")
                names.add(name)
    if fs.read_inode(1)[0] != VFS_DIR:
        raise PollikFsError("root inode is not a directory")
    expected = set(range(fs.data_start)) | set(owners)
    actual = {b for b in range(fs.total_blocks) if fs._bitmap_bit(b)}
    if actual != expected or fs.free_blocks != fs.total_blocks - len(actual):
        raise PollikFsError("bitmap/references/free-block counter mismatch")
    free = sum(fs.read_inode(i)[0] == 0 for i in range(1, fs.inode_count))
    # Kernel and historical host fixtures count reserved zero-mode inode 0
    # differently. Preserve the original convention; never normalize metadata.
    if fs.free_inodes not in (free, free + 1):
        raise PollikFsError("free-inode counter mismatch")
    return fs

def manifest(fs):
    result = {}
    def walk(inode, prefix, ancestors):
        if inode in ancestors: raise PollikFsError("directory cycle")
        for entry in fs.directory_entries(inode):
            if entry['name'] in ('.', '..'): continue
            path = prefix + '/' + entry['name']
            if entry['type'] == VFS_DIR:
                result[path + '/'] = ('dir', fs.read_inode(entry['inode'])[1], '-')
                walk(entry['inode'], path, ancestors | {inode})
            else:
                content = fs.read_file(path)
                result[path] = ('file', len(content), hashlib.sha256(content).hexdigest())
    walk(1, '', set())
    return result

class StagedImage(PollikFsImage):
    def allocate_block(self):
        block = super().allocate_block()
        self.data[START+block*BLOCK_SIZE:START+(block+1)*BLOCK_SIZE] = bytes(BLOCK_SIZE)
        return block

    def write_inode(self, number, mode, size, direct, indirect=0):
        # Growing an existing directory must preserve timestamps/reserved fields.
        offset = self.inode_offset(number)
        struct.pack_into('<10I', self.data, offset, mode, size, *direct)
        struct.pack_into('<I', self.data, offset+40, indirect)

    def remove_system_file(self, path):
        parent, _, name = path.rpartition('/')
        directory = self.resolve_directory(parent)
        for block, offset in self._slot_offsets(directory):
            position = START + block*BLOCK_SIZE + offset
            inode = struct.unpack_from('<I', self.data, position)[0]
            if not inode or self.data[position+8:position+64].split(b'\0',1)[0].decode('ascii') != name:
                continue
            mode, size, direct, indirect = self.read_inode(inode)
            if mode != VFS_FILE: raise PollikFsError(f"refusing to replace non-file {path}")
            owned = [b for b in direct if b]
            tables = [indirect] if indirect else []
            double = struct.unpack_from('<I', self.data, self.inode_offset(inode)+52)[0]
            if double:
                tables += [b for b in struct.unpack_from('<256I',self.data,START+double*BLOCK_SIZE) if b]
                owned.append(double)
            for table in tables:
                owned.extend(self._indirect_blocks(table)); owned.append(table)
            for b in owned:
                self._set_bitmap_bit(b, False); self.free_blocks += 1
            self.data[self.inode_offset(inode):self.inode_offset(inode)+60] = bytes(60)
            self.free_inodes += 1
            self.data[position:position+64] = bytes(64)
            return
        raise PollikFsError(f"missing system file {path}")

def sync(path, files=None):
    files=FILES if files is None else files
    with exclusive_image(path) as disk:
        length = os.fstat(disk.fileno()).st_size
        if length < SPAN:
            raise PollikFsError(f"damaged/truncated tail: image={length} bytes, required={SPAN}; image unchanged")
        original = disk.read(SPAN)
        before_fs = validate(original)
        before = manifest(before_fs)
        staged = StagedImage(original)
        for directory in sorted({target.rpartition('/')[0] for target in files}):
            staged.ensure_directory(directory)
        changed = []
        for target, asset in files.items():
            content = asset if isinstance(asset,bytes) else asset.read_bytes()
            if target in before:
                if before_fs.read_file(target) == content: continue
                staged.remove_system_file(target)
            staged.install_file(target, content)
            changed.append(target)
        staged._write_superblock()
        after = manifest(validate(staged.data))
        for target, record in before.items():
            if target not in files and not target.endswith('/') and after.get(target) != record:
                raise PollikFsError(f"unrelated file changed during staging: {target}")
        # No disk writes have happened above, including allocation failures.
        patches = [(i, original[i:i+BLOCK_SIZE], staged.data[i:i+BLOCK_SIZE])
                   for i in range(0, SPAN, BLOCK_SIZE)
                   if original[i:i+BLOCK_SIZE] != staged.data[i:i+BLOCK_SIZE]]
        written = []
        try:
            for offset, old, new in patches:
                written.append((offset, old))
                disk.seek(offset)
                if disk.write(new) != len(new): raise OSError("short image write")
            if patches: disk.flush(); os.fsync(disk.fileno())
        except OSError:
            for offset, old in reversed(written): disk.seek(offset); disk.write(old)
            disk.flush(); os.fsync(disk.fileno())
            raise
        print(f"System sync: installed={len(changed)} changed_blocks={len(patches)} image_bytes={length}; tail untouched")
        for target in changed:
            record = after[target]
            print(f"SYNC {target} {record[1]} {record[2]}")
        return changed

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--manifest', action='store_true')
    args = parser.parse_args()
    try:
        if args.manifest:
            with args.image.open('rb') as disk: fs = validate(disk.read(SPAN))
            for path, record in sorted(manifest(fs).items()): print(path, *record)
        else: sync(args.image)
    except (PollikFsError, OSError, ValueError, struct.error) as error:
        raise SystemExit(f"System sync REFUSED: {error}")

if __name__ == '__main__': main()
