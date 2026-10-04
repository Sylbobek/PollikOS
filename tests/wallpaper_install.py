"""Verify wallpaper installation preserves PollikFS accounting and image tail."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from format_pollikfs2 import format_disk
sys.path.insert(0, str(ROOT / "sdk/tools"))
from pollikfs_install import PollikFsImage, START, BLOCK_SIZE, INODES_PER_BLOCK
sys.path.insert(0, str(ROOT / "tools"))
from install_wallpapers import install

with tempfile.TemporaryDirectory(prefix="pollikos-wallpaper-") as temp:
    image = Path(temp) / "data.img"
    format_disk(image, total_size_mb=40)
    tail_marker = b"PRESERVE-OUTSIDE-POLLIKFS"
    with image.open("r+b") as disk:
        disk.seek(35 * 1024 * 1024)
        disk.write(tail_marker)
    install(image)
    raw = image.read_bytes()
    fs = PollikFsImage(raw[:START + 32768 * BLOCK_SIZE])
    assert fs.read_file("/usr/share/wallpapers/light.png") == (ROOT / "assets/Background_LightTheme.png").read_bytes()
    assert fs.read_file("/usr/share/wallpapers/dark.png") == (ROOT / "assets/Background_BlackTheme.png").read_bytes()
    actual_free_blocks = sum(not fs._bitmap_bit(block) for block in range(fs.data_start, fs.total_blocks))
    assert actual_free_blocks == fs.free_blocks, "free block superblock count mismatch"
    # PollikFS's existing counters include reserved inode 0 as a zero-mode slot.
    actual_free_inodes = sum(fs.read_inode(number)[0] == 0 for number in range(fs.inode_count))
    assert actual_free_inodes == fs.free_inodes, "free inode superblock count mismatch"
    assert raw[35 * 1024 * 1024:35 * 1024 * 1024 + len(tail_marker)] == tail_marker
    print(f"PASS: two wallpaper files installed; free_blocks={fs.free_blocks}; free_inodes={fs.free_inodes}; tail preserved")
