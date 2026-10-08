"""Build a disposable almost-full PollikFS image for ENOSPC safety tests.

Usage: make_full_image.py <source-image> <target-image>

The target is a copy of an existing valid image plus an /etc/diskfull marker;
space is occupied by a real file with valid indirect blocks. Nothing is formatted and the
source image is never modified.
"""
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk" / "tools"))
from pollikfs_install import PollikFsImage  # noqa: E402

# Small enough that the C5 ENOSPC fill loop (256 KiB max) still exhausts the
# disk, large enough for the C7 fixtures (20 KiB seek file plus tool chains).
KEEP_FREE_BLOCKS = 64


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    source, target = Path(sys.argv[1]), Path(sys.argv[2])
    if not source.exists():
        raise SystemExit(f"make_full_image: missing source image {source}")
    if source.resolve() == target.resolve():
        raise SystemExit("make_full_image: refusing to modify the source in place")
    shutil.copyfile(source, target)
    image = PollikFsImage.load(target)
    image.install_file("/etc/diskfull", b"1\n")
    # Reserve data through a real inode, so mount-time fsck can distinguish
    # this legitimate ENOSPC fixture from a damaged/leaking bitmap.
    blocks=image.free_blocks-KEEP_FREE_BLOCKS-2
    while blocks>0:
        overhead=(1 if blocks>8 else 0)+(1 if blocks>264 else 0)+((blocks-264+255)//256 if blocks>264 else 0)
        if blocks+overhead<=image.free_blocks-KEEP_FREE_BLOCKS-2: break
        blocks-=1
    image.install_file("/home/.diskfull-reserve",bytes(blocks*1024))
    image.save(target)
    print(f"Prepared almost-full image {target} "
          f"(free blocks {image.free_blocks}, free inodes {image.free_inodes})")


if __name__ == "__main__":
    main()
