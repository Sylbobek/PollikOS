import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from x86_64_boot import boot, fresh_data  # noqa: E402

markers = [
    "[C5] PASS: unlink-while-open never observes or modifies a reused inode",
    "[X64] SELFTEST PASS",
]
boot("selftest", "qemu64", 64, markers,
     data_image=fresh_data("selftest", "-reuse"), mutable_data=True, deadline=900)
print("FOCUSED C5 REUSE PASS")