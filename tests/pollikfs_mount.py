"""QEMU regression for PollikFS v2 mount validation, not crash recovery.

Uses disposable data disks and a snapshot of the system image. Verifies the
entire v2 region is unchanged after each boot, including refused mounts.
"""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
START = 64 * 512
SIZE = START + 32768 * 1024


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        stream.seek(START)
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.digest()


def fixture(path, fields):
    with path.open("wb") as stream:
        stream.truncate(SIZE)
        stream.seek(START)
        stream.write(struct.pack("<13I", *fields))
        # Corrected layout: bitmap blocks 1..4, inode blocks 5..35,
        # first data block 36. Root inode occupies slot 1 of block 5.
        bitmap = bytearray(4096)
        for block in range(37):
            bitmap[block // 8] |= 1 << (block % 8)
        stream.seek(START + 1024)
        stream.write(bitmap)
        stream.seek(START + 5 * 1024 + 60)
        stream.write(struct.pack("<15I", 2, 1024, 36, *([0] * 12)))
        # Root directory intentionally empty: no embedded test applications.


def boot_case(name, fields, expected):
    with tempfile.TemporaryDirectory(prefix="pollikos-v2-") as directory:
        disk = Path(directory) / "data.img"
        fixture(disk, fields)
        before = digest(disk)
        log = BUILD / f"pollikfs-mount-{name}.log"
        log.write_text("")
        image = BUILD / os.environ.get("POLLIK_TEST_IMAGE", "PollikOS-Surface.img")
        args = ["qemu-system-x86_64", "-machine", "pc", "-m", "64M",
                "-vga", "std", "-nic", "none", "-display", "none",
                "-monitor", "none", "-serial", f"file:{log}", "-no-reboot",
                "-drive", f"format=raw,file={image},if=ide,index=0,snapshot=on",
                "-drive", f"format=raw,file={disk},if=ide,index=1"]
        process = subprocess.Popen(args, stderr=subprocess.PIPE,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if "desktop ready" in text:
                    break
                if process.poll() is not None:
                    raise AssertionError(process.stderr.read().decode(errors="replace"))
                time.sleep(.1)
            text = log.read_text(errors="replace")
            assert "desktop ready" in text, text
            assert expected in text, text
            assert "Formatting PollikFS" not in text, text
            assert "PANIC" not in text and "FATAL" not in text, text
        finally:
            process.terminate()
            process.communicate(timeout=5)
        assert digest(disk) == before, f"{name}: v2 region changed during mount"
        print(f"PASS: {name}, desktop ready, v2 disk region unchanged")


def main():
    valid = [0x504B4632, 1024, 32768, 512, 32768 - 37, 510,
             1, 1, 4, 5, 31, 36, 0]
    boot_case("valid-layout", valid, "superblock mounted successfully")
    bad_magic = valid.copy()
    bad_magic[0] = 0
    boot_case("invalid-signature", bad_magic, "invalid signature; mount refused")
    legacy = valid.copy()
    legacy[10:12] = [30, 35]
    boot_case("legacy-overlapping-layout", legacy, "invalid superblock geometry; mount refused")
    for name, index, value in [("invalid-block-size", 1, 512),
                                ("invalid-inode-count", 3, 513),
                                ("invalid-free-count", 4, 32768),
                                ("invalid-root", 6, 0)]:
        fields = valid.copy()
        fields[index] = value
        boot_case(name, fields, "invalid superblock geometry; mount refused")


if __name__ == "__main__":
    main()
