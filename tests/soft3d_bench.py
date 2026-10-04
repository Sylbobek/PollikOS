"""Native soft3d baseline timings and deterministic framebuffer checksums."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
exe = ROOT / 'build' / ('soft3d_bench.exe' if os.name == 'nt' else 'soft3d_bench')
cmd = ['clang', '-std=c11', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror']
if os.name == 'nt':
    cmd += ['-fuse-ld=lld']
cmd += ['tests/soft3d_bench.c', 'kernel/soft3d.c', 'kernel/gfx_device.c', '-o', str(exe)]
subprocess.run(cmd, cwd=ROOT, check=True)
for resolution in ('1024x768', '1920x1080'):
    width, height = map(int, resolution.split('x'))
    result = subprocess.run([str(exe), str(width), str(height)],
                            cwd=ROOT, check=True, text=True, capture_output=True)
    print(result.stdout, end='')
