"""Host writers must preserve every byte when kernel recovery is pending."""
from pathlib import Path
import struct
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sdk/tools"))
sys.path.insert(0, str(ROOT / "tools"))
from pollikfs_install import PollikFsImage, PollikFsError, START
from sync_system_files import sync
from install_wallpapers import install
from format_pollikfs2 import format_disk


def header(state=0, count=0):
    result = bytearray(512)
    struct.pack_into("<4I", result, 0, 0x314A4B50, 1, state, count)
    struct.pack_into("<I", result, 16, zlib.crc32(result))
    return result


with tempfile.TemporaryDirectory(prefix="pollikos-host-journal-") as directory:
    disk = Path(directory) / "copy.img"
    format_disk(disk, total_size_mb=40)
    baseline = disk.read_bytes()
    pending = header(1, 1)
    struct.pack_into("<I", pending, 144, zlib.crc32(baseline[START:START + 1024]))
    struct.pack_into("<I", pending, 16, 0)
    struct.pack_into("<I", pending, 16, zlib.crc32(pending))
    corrupt = header()
    corrupt[16] ^= 1
    unknown = bytearray(512)
    unknown[0] = 1
    unused = bytearray(START)
    unused[-1] = 1
    for label, prefix in [("pending", pending), ("bad CRC", corrupt),
                          ("unknown magic", unknown), ("nonempty unused prefix", unused),
                          ("invalid idle count", header(0, 1)), ("invalid state", header(2))]:
        fixture = bytearray(baseline)
        fixture[:len(prefix)] = prefix
        if label == "pending": fixture[512:1536] = baseline[START:START + 1024]
        disk.write_bytes(fixture)
        for name, operation in [("sync", lambda: sync(disk, files={"/home/probe": b"new"})),
                                ("install", lambda: PollikFsImage.load(disk)),
                                ("wallpapers", lambda: install(disk))]:
            try: operation()
            except PollikFsError: pass
            else: raise AssertionError(f"{name} accepted {label}")
            assert disk.read_bytes() == fixture, (name, label)
        print(f"PASS host writers refuse {label}; entire image byte-identical")
    disk.write_bytes(baseline)
    staged = PollikFsImage.load(disk)
    staged.install_file("/home/staged", b"staged")
    with disk.open("r+b") as stream: stream.write(pending)
    before = disk.read_bytes()
    try: staged.save(disk)
    except PollikFsError: pass
    else: raise AssertionError("save accepted a journal changed after load")
    assert disk.read_bytes() == before
    assert not list(disk.parent.glob("*.pending"))
    print("PASS save rechecks current journal before creating an output")
    for label, prefix in [("unused", bytes(START)), ("idle", header())]:
        fixture = bytearray(baseline)
        fixture[:START] = bytes(START)
        fixture[:len(prefix)] = prefix
        disk.write_bytes(fixture)
        sync(disk, files={"/home/probe": b"new"})
        assert PollikFsImage.load(disk).read_file("/home/probe") == b"new"
        assert disk.read_bytes()[:START] == fixture[:START]
        print(f"PASS clean {label} journal permits sync and is preserved")
