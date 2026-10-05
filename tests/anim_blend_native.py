"""Host parity and throughput probe for the animation constant-alpha blend."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
exe = ROOT / 'build' / ('anim_blend_native.exe' if os.name == 'nt' else 'anim_blend_native')
cmd = ['clang', '-std=c11', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror']
if os.name == 'nt':
    cmd += ['-fuse-ld=lld']
cmd += ['tests/anim_blend_native.c', '-o', str(exe)]
subprocess.run(cmd, cwd=ROOT, check=True)
result = subprocess.run([str(exe)], cwd=ROOT, check=True, text=True, capture_output=True)
print(result.stdout, end='')
