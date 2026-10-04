import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from x86_64_boot import boot  # noqa: E402

BUILD = ROOT / "build" / "x86_64" / "selftest"
markers = [
    "[C6] PASS: spawn/wait churn (exit/fault/kill mix) returns to baseline",
    "[C6] PASS: 100 spawn/wait lifecycles return PMM exactly to baseline",
    "[X64] SELFTEST PASS",
]
for ram, suffix in ((64, "-churn5000-64"), (5120, "-churn5000-5120")):
    image = BUILD / "PollikData-churn5000.img"
    boot("selftest", "qemu64", ram, markers,
         data_image=image, suffix=suffix, mutable_data=True, deadline=5400)
    print(f"FULL CHURN PASS {ram} MiB")
print("FULL 5000-CYCLE CHURN PASS")