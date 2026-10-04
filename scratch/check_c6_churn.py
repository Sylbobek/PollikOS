import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from x86_64_boot import boot, fresh_data  # noqa: E402

markers = [
    "[C6] PASS: spawn/wait churn (exit/fault/kill mix) returns to baseline",
    "[C6] PASS: 100 spawn/wait lifecycles return PMM exactly to baseline",
    "[X64] SELFTEST PASS",
]
boot("selftest", "qemu64", 64, markers,
     data_image=fresh_data("selftest", "-churn"), mutable_data=True, deadline=900)
print("FOCUSED C6 CHURN PASS")