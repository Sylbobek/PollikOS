"""System-file sync preservation, update, locking, ENOSPC and corruption tests."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'sdk/tools'))
from sync_system_files import sync, validate, manifest, exclusive_image, SPAN, FILES
from pollikfs_install import PollikFsImage, PollikFsError, START, BLOCK_SIZE
from format_pollikfs2 import format_disk
parser=argparse.ArgumentParser()
parser.add_argument('--old-image',type=Path,default=ROOT/'build/system-sync-evidence/old-layout.img')
parser.add_argument('--boot-check',action='store_true')
args=parser.parse_args()
out=ROOT/'build/system-sync-evidence';out.mkdir(exist_ok=True)

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def refusal(path, label):
    before=sha(path)
    try: sync(path)
    except PollikFsError as error: print(f'REFUSED {label}: {error}')
    else: raise AssertionError(f'{label}: unexpectedly accepted')
    after=sha(path);assert before==after
    print(f'PASS {label} byte-identical SHA256={before}')

with tempfile.TemporaryDirectory(prefix='pollikos-sync-') as temp:
    temp=Path(temp)
    copy=temp/'old-copy.img';shutil.copyfile(args.old_image,copy)
    original=copy.read_bytes();fs=validate(original[:SPAN]);before=manifest(fs)
    assert not any(p in before for p in FILES), 'old fixture already has stock wallpaper files'
    def save_list(name, records):
        lines=[f'{path} {kind} {size} {digest}' for path,(kind,size,digest) in sorted(records.items())]
        (out/name).write_text('\n'.join(lines)+'\n')
        print(name)
        for line in lines: print(line)
    save_list('before.txt',before)
    sync(copy)
    modified=copy.read_bytes();after_fs=validate(modified[:SPAN]);after=manifest(after_fs)
    save_list('after.txt',after)
    assert all(after[path]==value for path,value in before.items())
    added=set(after)-set(before)
    assert added==set(FILES)|{'/usr/','/usr/share/','/usr/share/wallpapers/'}
    for i in range(fs.inode_count):
        if fs.read_inode(i)[0]==0: continue
        offset=fs.inode_offset(i)
        assert original[offset:offset+60]==modified[offset:offset+60], f'existing inode changed: {i}'
        if fs.read_inode(i)[0]==2:
            for block,offset in fs._slot_offsets(i):
                at=START+block*BLOCK_SIZE+offset
                if struct.unpack_from('<I',original,at)[0]:
                    assert original[at:at+64]==modified[at:at+64], f'directory entry changed: {i}'
    assert original[SPAN:]==modified[SPAN:]
    print('DIFF lists: only +/usr/, +/usr/share/, +/usr/share/wallpapers/, +light.png, +dark.png')
    print('PASS old image: all original file hashes, live inode bytes, existing directory entries and tail preserved')
    synced=sha(copy);assert sync(copy)==[];assert sha(copy)==synced
    print(f'PASS second sync changed nothing SHA256={synced}')
    # Replace an obsolete stock file; every other live inode and file is unchanged.
    update=temp/'update.img';format_disk(update,total_size_mb=40)
    fs=PollikFsImage.load(update);fs.ensure_directory('/usr/share/wallpapers')
    fs.install_file('/usr/share/wallpapers/light.png',b'old content')
    fs.install_file('/home/user.txt',b'preserve me');fs.save(update)
    user=fs.read_file('/home/user.txt');sync(update);fs=validate(update.read_bytes()[:SPAN])
    assert fs.read_file('/home/user.txt')==user
    assert fs.read_file('/usr/share/wallpapers/light.png')==FILES['/usr/share/wallpapers/light.png'].read_bytes()
    print('PASS obsolete stock wallpaper updated; unrelated file preserved')
    # A valid, almost-full filesystem has real owned data, not fake orphan bits.
    full=temp/'full.img';format_disk(full,total_size_mb=40)
    class FastFixture(PollikFsImage):
        next_block=36
        def allocate_block(self):
            while self.next_block < self.total_blocks and self._bitmap_bit(self.next_block):
                self.next_block+=1
            if self.next_block == self.total_blocks: raise PollikFsError('fixture ENOSPC')
            block=self.next_block;self.next_block+=1
            self._set_bitmap_bit(block,True);self.free_blocks-=1
            return block
    fs=FastFixture.load(full)
    fs.install_file('/home/filler',bytes(31500*BLOCK_SIZE));fs.save(full)
    validate(full.read_bytes()[:SPAN]);refusal(full,'ENOSPC')
    validate(full.read_bytes()[:SPAN]);print('PASS ENOSPC image still structurally mountable')
    if args.boot_check:
        from gui_metrics import Guest
        digest=sha(full)
        with Guest('1024x768','sync-enospc-mount',data_image=full,boot_only=True) as guest:
            lines=guest.log.read_text()
            assert 'PollikFS v2 superblock mounted successfully' in lines,lines
            print('QEMU ENOSPC remount: '+next(line for line in lines.splitlines() if 'superblock mounted successfully' in line))
        assert sha(full)==digest
        print('PASS ENOSPC QEMU snapshot remount; source image byte-identical SHA256='+digest)
    # Existing full parent directories must grow without rewriting old entries.
    grow=temp/'directory-growth.img';format_disk(grow,total_size_mb=40)
    fs=PollikFsImage.load(grow)
    for i in range(12): fs.install_file('/root%02d'%i,b'unchanged')
    fs.save(grow);old=grow.read_bytes();before_grow=manifest(validate(old[:SPAN]))
    assert len(fs.directory_entries(1))==16
    # Existing root timestamps/reserved fields carry sentinel values.
    offset=fs.inode_offset(1)+44
    with grow.open('r+b') as disk:disk.seek(offset);disk.write(struct.pack('<4I',123456,654321,0,17))
    old=grow.read_bytes();sync(grow);new=grow.read_bytes()
    assert old[offset:offset+16]==new[offset:offset+16]
    after_grow=manifest(validate(new[:SPAN]))
    assert all(after_grow[p]==v for p,v in before_grow.items())
    for block,entry_offset in fs._slot_offsets(1):
        pos=START+block*BLOCK_SIZE+entry_offset
        assert old[pos:pos+64]==new[pos:pos+64]
    print('PASS parent directory growth: 16 existing entries and timestamp/reserved bytes preserved')
    short=temp/'damaged-tail.img';shutil.copyfile(args.old_image,short)
    with short.open('r+b') as f:f.truncate(SPAN-512)
    refusal(short,'damaged tail')
    legacy=temp/'legacy.img';shutil.copyfile(args.old_image,legacy)
    with legacy.open('r+b') as f:f.seek(START+40);f.write(struct.pack('<II',30,35))
    refusal(legacy,'legacy geometry 30/35')
    corrupt=temp/'bad-pointer.img';shutil.copyfile(args.old_image,corrupt)
    with corrupt.open('r+b') as f:f.seek(START+5*BLOCK_SIZE+60+8);f.write(struct.pack('<I',32768))
    refusal(corrupt,'damaged block reference')
    locked_before=sha(copy)
    with exclusive_image(copy):
        try: sync(copy)
        except PollikFsError as error: print(f'REFUSED exclusive lock: {error}')
        else: raise AssertionError('exclusive lock unexpectedly accepted')
    assert sha(copy)==locked_before
    print(f'PASS exclusive lock byte-identical SHA256={locked_before}')
    # Actual QEMU lock, not just the tool's own exclusive handle.
    lock=temp/'qemu-lock.img';shutil.copyfile(args.old_image,lock)
    proc=subprocess.Popen(['qemu-system-x86_64','-S','-display','none','-monitor','none',
                          '-serial','none','-drive',f'format=raw,file={lock},if=ide'],
                         creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    try:
        import time;time.sleep(1);assert proc.poll() is None;refusal(lock,'running QEMU lock')
    finally:proc.terminate();proc.wait(timeout=5)
print('PASS system-file sync preservation and refusal cases')
