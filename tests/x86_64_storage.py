"""Boot damaged copies of the generated PollikFS fixture; never open user data."""
from pathlib import Path
import json
import struct
import tempfile
from x86_64_boot import BUILD, boot

START = 64 * 512

def main():
    for variant in ("kernel", "selftest"):
        directory = BUILD / variant
        kernel = (directory / "kernel.bin").read_bytes()
        for elf in (directory / "userspace").glob("*.elf"):
            assert elf.read_bytes() not in kernel, f"embedded executable: {elf}"
    print("PASS: neither kernel variant embeds userspace ELF files")
    directory = BUILD / "kernel"
    original = (directory / "PollikData-test.img").read_bytes()
    manifest = json.loads((directory / "data-manifest.json").read_text())
    inode = manifest["hello"]["inode_offset"]
    indirect = START + manifest["hello"]["indirect"] * 1024
    assert manifest["hello"]["indirect"], "fixture must exercise indirect file blocks"
    mutations = {
        "bad-signature": (START, struct.pack("<I", 0), "[VFS64] mount refused; disk unchanged"),
        "old-geometry": (START+40, struct.pack("<I", 30), "[VFS64] mount refused; disk unchanged"),
        "bad-directory-name": (START+37*1024+6, b"\xff", "[LAUNCH64] /bin/hello: corrupt filesystem"),
        "bad-directory-inode": (START+37*1024, struct.pack("<I", 0xffffffff), "[LAUNCH64] /bin/hello: corrupt filesystem"),
        "bad-direct-block": (inode+8, struct.pack("<I", 0xffffffff), "[LAUNCH64] /bin/hello: corrupt filesystem"),
        "bad-indirect-entry": (indirect, struct.pack("<I", 1), "[LAUNCH64] /bin/hello: corrupt filesystem"),
        "overflow-inode-size": (inode+4, struct.pack("<I", 0xffffffff), "[LAUNCH64] /bin/hello: corrupt filesystem"),
    }
    with tempfile.TemporaryDirectory(prefix="pollikos-storage-") as temporary:
        for name, (offset, value, marker) in mutations.items():
            data = bytearray(original)
            data[offset:offset+len(value)] = value
            path = Path(temporary) / f"{name}.img"
            path.write_bytes(data)
            markers = [marker]
            if marker.startswith("[LAUNCH64]"):
                markers.append("[LAUNCH64] rejected image: PMM/handles/queue balanced")
            boot("kernel", "qemu64", 64, markers, data_image=path, suffix=f"-{name}")
        tiny = Path(temporary) / "tiny.img"
        tiny.write_bytes(original[:1024*1024])
        boot("kernel", "qemu64", 64, ["[VFS64] mount refused; disk unchanged"], data_image=tiny, suffix="-small-disk")
    boot("kernel", "qemu64", 64, ["[VFS64] mount refused; disk unchanged"], data_image=None, suffix="-no-disk")
    print("PASS: missing/corrupt/unsupported/truncated storage refused without image changes")

if __name__ == "__main__":
    main()
