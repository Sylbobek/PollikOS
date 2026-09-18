"""Run only native held-drag graphics regressions; never build/boot an OS image."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    clang = shutil.which("clang")
    if not clang:
        raise RuntimeError("clang not found on PATH")
    executable = ROOT / "build" / ("held_drag.exe" if os.name == "nt" else "held_drag")
    executable.parent.mkdir(exist_ok=True)
    command = [clang, "-std=c11", "-O2", "-fno-builtin", "-Wall", "-Wextra", "-Werror"]
    if os.name == "nt":
        command.append("-fuse-ld=lld")
    command += ["tests/held_drag.c", "kernel/graphics.c", "-o", str(executable)]
    print("Compile native regression: " + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
