"""Check wallpaper files in the generated x64 PollikFS data fixture."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sdk/tools"))
from pollikfs_install import PollikFsImage, VFS_DIR

image_path = ROOT / "build/x86_64/kernel/PollikData-test.img"
fs = PollikFsImage.load(image_path)

def read_file(path):
    parts = [part for part in path.split("/") if part]
    current = fs.root_inode
    for part in parts[:-1]:
        entry = next((entry for entry in fs.directory_entries(current) if entry["name"] == part), None)
        assert entry is not None, f"missing directory {part} in {path}"
        assert fs.read_inode(entry["inode"])[0] == VFS_DIR, f"{part} is not a directory"
        current = entry["inode"]
    entry = next((entry for entry in fs.directory_entries(current) if entry["name"] == parts[-1]), None)
    assert entry is not None, f"missing PollikFS file {path}"
    return fs.read_file(path)

for name in ("light.png", "dark.png"):
    expected = (ROOT / "assets" / ("Background_LightTheme.png" if name == "light.png" else
                                   "Background_BlackTheme.png")).read_bytes()
    actual = read_file("/usr/share/wallpapers/" + name)
    assert actual == expected, f"PollikFS wallpaper mismatch: {name}"
    print(f"PASS: /usr/share/wallpapers/{name} ({len(actual)} bytes)")
