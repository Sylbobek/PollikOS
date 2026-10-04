"""Pack the two stock PNGs as installer-only file payload (not main-kernel assets)."""
import argparse
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    light = (ROOT / "assets/Background_LightTheme.png").read_bytes()
    dark = (ROOT / "assets/Background_BlackTheme.png").read_bytes()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(struct.pack("<4sII", b"WLPK", len(light), len(dark)) + light + dark)
    print(f"Packed installer wallpapers: light={len(light)} dark={len(dark)} bytes")

if __name__ == "__main__": main()
