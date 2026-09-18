"""Compile the real software backend; bounded geometry and explicit guard oracles."""
from pathlib import Path
import os
import subprocess
ROOT = Path(__file__).resolve().parents[1]
exe = ROOT / 'build' / ('soft3d_native.exe' if os.name == 'nt' else 'soft3d_native')
cmd = ['clang', '-std=c11', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror']
if os.name == 'nt':
    cmd += ['-fuse-ld=lld']
cmd += ['tests/soft3d_native.c', 'kernel/soft3d.c', 'kernel/gfx_device.c', '-o', str(exe)]
subprocess.run(cmd, cwd=ROOT, check=True)
subprocess.run([str(exe)], cwd=ROOT, check=True)
