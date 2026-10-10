"""Run only native held-drag graphics regressions; never build/boot an OS image."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    clang = shutil.which("clang")
    if not clang:
        raise RuntimeError("clang not found on PATH")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    command = [clang, "-std=c11", "-O2", "-fno-builtin", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections"]
    if os.name == "nt":
        command += ["-fuse-ld=lld", "-Wl,/OPT:REF"]
    results = []
    for name, define in (("legacy", "-DGFX_RECT_LEGACY=1"), ("primitive", None)):
        executable = build / ((f"held_drag_{name}.exe") if os.name == "nt" else f"held_drag_{name}")
        variant_command = command + ([define] if define else [])
        variant_command += ["tests/held_drag.c", "kernel/graphics.c", "sdk/lib/font_data.c", "kernel/gfx/gfx_primitives.c", "-o", str(executable)]
        print("Compile native regression (" + name + "): " + subprocess.list2cmdline(variant_command), flush=True)
        subprocess.run(variant_command, cwd=ROOT, check=True)
        result = subprocess.run([str(executable)], cwd=ROOT, check=True,
                                text=True, capture_output=True)
        print(result.stdout, end="")
        hashes = re.findall(r"^PIXEL_HASH scene=([0-9a-f]{16}) hardware=([0-9a-f]{16})$",
                            result.stdout, flags=re.MULTILINE)
        if len(hashes) != 1:
            raise SystemExit(f"{name} pixel hash missing or repeated: {result.stdout}{result.stderr}")
        results.append((name, hashes[0]))
    if results[0][1] != results[1][1]:
        raise SystemExit(f"legacy/primitive pixel hashes differ: {results[0]} != {results[1]}")
    print(f"PASS rect legacy/primitive pixel parity scene={results[0][1][0]} hardware={results[0][1][1]}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
