"""Pixel-diff fresh QEMU screenshots against archived reference screenshots.

Usage: python tests/compare_shots.py NEW.ppm REF.ppm [NEW.ppm REF.ppm ...]
Prints the number and percentage of differing pixels per pair; exits non-zero
if any pair differs in size or is missing. This is a visual-regression aid for
GUI changes, not a proof of correctness on its own.
"""
from pathlib import Path
import sys


def load(path):
    data = Path(path).read_bytes()
    parts = data.split(maxsplit=4)
    assert parts[0] == b"P6", f"{path}: not a binary PPM"
    width, height = int(parts[1]), int(parts[2])
    return width, height, data[-width * height * 3:]


def main(argv):
    if len(argv) < 2 or len(argv) % 2:
        print(__doc__)
        return 2
    status = 0
    for new, ref in zip(argv[0::2], argv[1::2]):
        try:
            wa, ha, pa = load(new)
            wb, hb, pb = load(ref)
        except FileNotFoundError as error:
            print("MISSING", error)
            status = 1
            continue
        if (wa, ha) != (wb, hb):
            print(f"{new}: size {wa}x{ha} differs from reference {wb}x{hb}")
            status = 1
            continue
        diff = sum(1 for i in range(0, len(pa), 3) if pa[i:i + 3] != pb[i:i + 3])
        print(f"{new}: {wa}x{ha} differing pixels={diff} ({100 * diff / (wa * ha):.3f}%)")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
