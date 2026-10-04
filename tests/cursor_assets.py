"""Determinism and shape checks for the generated software cursor sprites."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("arrow", "ibeam", "hand", "busy", "resize_ns", "resize_ew",
         "resize_nwse", "resize_nesw", "move", "not_allowed")

with tempfile.TemporaryDirectory(prefix="pollikos-cursors-") as temp:
    output = Path(temp) / "cursor_sprites.h"
    subprocess.run(["python", str(ROOT / "assets/build_cursor.py"), "--output", str(output)],
                   check=True, cwd=ROOT)
    first = output.read_bytes()
    subprocess.run(["python", str(ROOT / "assets/build_cursor.py"), "--output", str(output)],
                   check=True, cwd=ROOT)
    assert output.read_bytes() == first, "cursor generator output is not reproducible"
    source = first.decode("ascii")
    for name in NAMES:
        assert re.search(rf"CURSOR_SPRITE_{name.upper()}\s*=", source), f"missing {name} sprite"
    assert "CURSOR_SPRITE_WIDTH 32" in source and "CURSOR_SPRITE_HEIGHT 32" in source
    rgba = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{8})u", source)]
    assert len(rgba) == len(NAMES) * 32 * 32, f"expected 10x32x32 pixels, got {len(rgba)}"
    alpha = [pixel >> 24 for pixel in rgba]
    assert any(value == 0 for value in alpha), "sprite backgrounds must be transparent"
    assert any(0 < value < 255 for value in alpha), "sprite edges must be antialiased"
print("PASS: generated 10 deterministic 32x32 ARGB cursor sprites")
