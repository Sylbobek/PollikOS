"""Benchmark and compare reference/fast soft3d framebuffers."""
import ctypes
import hashlib
from pathlib import Path
import os
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
BUILD.mkdir(exist_ok=True)
common = ['clang', '-std=c11', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-shared']
if os.name == 'nt':
    common += ['-fuse-ld=lld']

def compile_library(name):
    output = BUILD / (name + ('.dll' if os.name == 'nt' else '.so'))
    cmd = common + ['tests/soft3d_bench.c', 'kernel/soft3d.c', 'kernel/gfx_device.c', '-o', str(output)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    lib = ctypes.CDLL(str(output))
    render = lib.soft3d_bench_render
    render.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int,
                       ctypes.POINTER(ctypes.c_uint32)]
    render.restype = ctypes.c_uint
    return render

def frame(render, width, height, textured):
    count = width * height
    pixels = (ctypes.c_uint32 * count)()
    written = render(width, height, textured, pixels)
    return pixels, written

current = compile_library('soft3d_current')
golden = {
    ('1024x768', 'triangle'): (335826, '8008a1c1d787cc84f03b475d30ef74f1191d0a3a6d21d8e92460f97c59c9cbf0'),
    ('1024x768', 'perspective_texture'): (335826, '22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112'),
    ('1920x1080', 'triangle'): (886465, '42d3106651618ccb3cd0554e2c9297a32929da848ac774956dd7e81ef6fcdf89'),
    ('1920x1080', 'perspective_texture'): (886465, '9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a'),
}
for resolution in ('1024x768', '1920x1080'):
    width, height = map(int, resolution.split('x'))
    for textured, label in ((False, 'triangle'), (True, 'perspective_texture')):
        pixels, writes = frame(current, width, height, int(textured))
        digest = hashlib.sha256(bytes(pixels)).hexdigest()
        expected_writes, expected_digest = golden[(resolution, label)]
        if (writes, digest) != (expected_writes, expected_digest):
            raise SystemExit(f'{resolution} {label}: baseline golden mismatch writes={writes} sha256={digest}')
        print(f'GOLDEN res={resolution} scene={label} writes={writes} sha256={digest}')
        times = []
        bench_pixels, _ = frame(current, width, height, int(textured))
        for _ in range(3):
            start = time.perf_counter()
            for _ in range(8):
                current(width, height, int(textured), bench_pixels)
            times.append((time.perf_counter() - start) * 1000.0)
        print(f'BENCH mode=current res={resolution} scene={label} rounds=8 ms={times[0]:.3f},{times[1]:.3f},{times[2]:.3f}')
