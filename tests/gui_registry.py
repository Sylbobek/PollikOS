"""Compile and run only the native callback-routing test against real apps.c."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    # build.ps1 likewise resolves clang through PATH; do not build the OS image.
    clang = shutil.which("clang")
    if clang is None:
        print("ERROR: clang not found on PATH", file=sys.stderr)
        return 1
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    executable = build / ("gui_registry.exe" if os.name == "nt" else "gui_registry")
    command = [clang, "-std=c11", "-O0", "-g", "-fno-builtin",
               "-Wall", "-Wextra", "-Werror"]
    if os.name == "nt":
        command += ["-fuse-ld=lld", "-D_UINTPTR_T_DEFINED"]
    command += ["tests/gui_registry.c", "kernel/gui/apps.c", "-o", str(executable)]
    print("Compile: " + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)
    print("Run: " + str(executable), flush=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
