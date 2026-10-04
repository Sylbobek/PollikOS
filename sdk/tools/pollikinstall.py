"""Install a host-built ELF into an existing PollikFS v2 image.

Usage: pollikinstall.py <image> <vfs-path> <host-file> [--force-user-image]

Never formats, truncates or recreates an image; the image must already be a
valid PollikFS v2 filesystem and the target path must not already exist.
The repository's user data image (build/PollikData.img) is refused unless the
explicit --force-user-image flag is given. Test fixtures use disposable images.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pollikfs_install import PollikFsError, PollikFsImage  # noqa: E402

PROTECTED_IMAGES = {"pollikdata.img"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="PollikFS v2 image to update in place")
    parser.add_argument("vfs_path", help="absolute target path, e.g. /bin/hello")
    parser.add_argument("host_file", help="host file to install")
    parser.add_argument("--force-user-image", action="store_true",
                        help="allow updating build/PollikData.img (never formats it)")
    parser.add_argument("--verbose", action="store_true")
    options = parser.parse_args()

    if Path(options.image).name.lower() in PROTECTED_IMAGES and not options.force_user_image:
        raise SystemExit("pollikinstall: refusing to modify the user data image without --force-user-image")
    payload = Path(options.host_file).read_bytes()
    try:
        image = PollikFsImage.load(options.image)
        if image.exists(options.vfs_path):
            raise PollikFsError(f"{options.vfs_path} already exists in {options.image}")
        inode, blocks = image.install_file(options.vfs_path, payload)
        image.save(options.image)
    except PollikFsError as error:
        raise SystemExit(f"pollikinstall: {error}")
    if options.verbose:
        print(f"pollikinstall: inode {inode}, {len(blocks)} data block(s), {len(payload)} bytes")
    print(f"Installed {options.host_file} as {options.vfs_path} ({len(payload)} bytes)")


if __name__ == "__main__":
    main()
