"""Exercise actual animation code on the host, with a deterministic WM clock."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
cmd=['clang','-O2','-fno-builtin','-Wall','-Wextra','-Werror','-fuse-ld=lld','tests/animation_continuity.c','-o','build/animation_continuity.exe']
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
subprocess.run(cmd,cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/animation_continuity.exe')],cwd=ROOT,check=True)
