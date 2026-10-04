"""Build the freestanding graphics primitives in reference and fast modes."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
exe = ROOT / 'build' / ('gfx_primitives_native.exe' if os.name == 'nt' else 'gfx_primitives_native')
common = ['clang', '-std=c11', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror']
if os.name == 'nt':
    common += ['-fuse-ld=lld']

for mode, define in [('reference', '-DGFX_REFERENCE=1'), ('fast', None)]:
    cmd = common + ([define] if define else []) + [
        'tests/gfx_primitives_native.c', 'kernel/gfx/gfx_primitives.c', '-o', str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    result = subprocess.run([str(exe)], cwd=ROOT, check=True, text=True, capture_output=True)
    lines = result.stdout.strip().splitlines()
    if not lines or not lines[0].startswith('PASS gfx correctness:'):
        raise SystemExit(f'{mode} correctness output missing: {result.stdout}{result.stderr}')
    if len(lines) < 2 or f'mode={mode}' not in lines[1]:
        raise SystemExit(f'{mode} benchmark output missing: {result.stdout}{result.stderr}')
    print('\n'.join(lines))
