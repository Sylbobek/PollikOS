import shutil, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
from pollikfs_install import PollikFsImage  # noqa: E402

variant = sys.argv[1] if len(sys.argv) > 1 else "selftest"
cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 5000
src = ROOT / "build" / "x86_64" / variant / "PollikData-test.img"
dst = ROOT / "build" / "x86_64" / variant / f"PollikData-churn{cycles}.img"
shutil.copyfile(src, dst)
image = PollikFsImage.load(dst)
if not image.exists("/etc/churn_cycles"):
    image.install_file("/etc/churn_cycles", f"{cycles}\n".encode())
image.save(dst)
print(f"churn image {dst} cycles={cycles}")