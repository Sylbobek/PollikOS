"""Add the stock wallpaper files to a PollikFS image without formatting it."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sdk/tools"))
from pollikfs_install import PollikFsImage, PollikFsError, START, BLOCK_SIZE

def install(image_path):
    path = Path(image_path)
    with path.open("r+b") as disk:
        disk.seek(START)
        sb = disk.read(52)
        if len(sb) != 52:
            raise PollikFsError("image is too small for a PollikFS superblock")
        words = struct.unpack("<13I", sb)
        if words[0] != 0x504B4632 or words[1] != BLOCK_SIZE:
            raise PollikFsError("not a PollikFS v2 image; refusing to modify")
        span = START + words[2] * BLOCK_SIZE
        if span > path.stat().st_size:
            raise PollikFsError("image is smaller than PollikFS geometry")
        disk.seek(0)
        data = bytearray(disk.read(span))
        fs = PollikFsImage(data)
        fs.ensure_directory("/usr/share/wallpapers")
        added = 0
        for filename, asset in (("light.png", "Background_LightTheme.png"),
                                ("dark.png", "Background_BlackTheme.png")):
            target = "/usr/share/wallpapers/" + filename
            payload = (ROOT / "assets" / asset).read_bytes()
            try:
                existing = fs.read_file(target)
            except PollikFsError:
                fs.install_file(target, payload)
                added += 1
            else:
                if existing != payload:
                    raise PollikFsError(f"refusing to overwrite existing {target}")
        if added:
            fs._write_superblock()
            disk.seek(0)
            disk.write(fs.data)
            disk.flush()
            import os
            os.fsync(disk.fileno())
        print(f"Wallpapers: added={added}, filesystem_prefix={span} bytes; existing files preserved")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: install_wallpapers.py PollikData.img")
    install(sys.argv[1])
