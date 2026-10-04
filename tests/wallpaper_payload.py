"""Check wallpaper package reproducibility and kernel/installer placement."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pollikos-wallpaper-pkg-") as temp:
    package = Path(temp) / "wallpapers.pkg"
    subprocess.run(["python", str(ROOT / "tools/build_wallpaper_package.py"),
                    "--output", str(package)], check=True, cwd=ROOT)
    first = package.read_bytes()
    subprocess.run(["python", str(ROOT / "tools/build_wallpaper_package.py"),
                    "--output", str(package)], check=True, cwd=ROOT)
    assert package.read_bytes() == first, "installer wallpaper package is not reproducible"
    light = (ROOT / "assets/Background_LightTheme.png").read_bytes()
    dark = (ROOT / "assets/Background_BlackTheme.png").read_bytes()
    assert light in first and dark in first, "installer package lacks wallpaper payload"
    kernel = ROOT / "build/kernel.bin"
    installer = ROOT / "build/install/kernel.bin"
    if kernel.exists() and installer.exists():
        runtime = kernel.read_bytes()
        install_image = installer.read_bytes()
        assert light not in runtime and dark not in runtime, "main kernel still embeds a wallpaper"
        assert light in install_image and dark in install_image, "installer kernel lacks wallpaper files"
        print(f"PASS: main kernel excludes PNGs; installer payload includes both ({len(first)} bytes)")
    else:
        print("PASS: reproducible installer wallpaper package contains both files")
