"""Real PMM guard/ABI regression against the matching Surface image and ELF.
Runs without network or the user's data disk. No production sources included.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
from surface_support import checked_surface_symbols, check_guards
parser = argparse.ArgumentParser()
parser.add_argument('--resolution', default='1024x768')
args = parser.parse_args()
width, height = map(int, args.resolution.split('x'))
symbols, APP_COUNT = checked_surface_symbols()
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]
log = BUILD / f'corners-guards-{args.resolution}.log'
log.write_text('')
proc = subprocess.Popen([
    'qemu-system-x86_64', '-machine', 'pc', '-cpu', 'max', '-m', '256M',
    '-device', 'VGA,vgamem_mb=32', '-display', 'none', '-no-reboot',
    '-fw_cfg', f'name=opt/pollikos/display,string={args.resolution}',
    '-drive', f'format=raw,file={BUILD / "PollikOS-Surface.img"},if=ide,index=0,snapshot=on',
    '-nic', 'none', '-serial', f'file:{log}',
    '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
], creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
try:
    deadline = time.monotonic() + 45
    while True:
        try:
            connection = socket.create_connection(('127.0.0.1', port), timeout=3)
            break
        except OSError:
            assert proc.poll() is None and time.monotonic() < deadline
            time.sleep(.1)
    stream = connection.makefile('rwb', buffering=0)
    stream.readline()

    def qmp(command, arguments=None):
        stream.write((json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n').encode())
        while True:
            response = json.loads(stream.readline())
            assert 'error' not in response, response
            if 'return' in response:
                return response['return']

    def words(address, count):
        path = BUILD / 'corners-memory.bin'
        qmp('pmemsave', {'val': address, 'size': count * 4, 'filename': str(path)})
        return struct.unpack('<' + 'I' * count, path.read_bytes())

    def record(name, index=0, count=1):
        return words(symbols[name][0] + index * 4, count)

    qmp('qmp_capabilities')
    while not log.exists() or 'desktop ready' not in log.read_text():
        assert proc.poll() is None and time.monotonic() < deadline
        time.sleep(.1)

    def check(stage):
        qmp('stop')
        try:
            check_guards(lambda address, size: words(address, size // 4), symbols, APP_COUNT, width, height)
            s = record('g_surfaces', 0, 9)
            w = record('g_windows', 0, 21)
            assert s[1:5] == (w[4], w[5], w[4], w[4])
            assert words(s[0], 2) == (0xf2eff6, 0xf2eff6), 'canary/preblended RGB in drawable corners'
            print(f'PASS {stage}: ABI36, {APP_COUNT} prefix/suffix guards, pixels=base+4 bytes, capacity={width*height}, window={w[4]}x{w[5]}', flush=True)
        finally:
            qmp('cont')

    assert f'GFX resolution: {args.resolution}' in log.read_text()
    check('boot')
    for stage, target in [('maximized', (width - 16, height - 142)), ('restored', (680, 410))]:
        qmp('human-monitor-command', {'command-line': 'sendkey f11'})
        deadline = time.monotonic() + 30
        while True:
            s = record('g_surfaces', 0, 9)
            size = record('sizes', 0, 2)
            # Check actual completed corner + far edge, not just early metadata.
            if s[1:3] == target and size == target and words(s[0], 2) == (0xf2eff6,) * 2:
                far = words(s[0] + (30*s[3]+s[1]-20)*4, 1)
                if far == (0xf2eff6,):
                    break
            assert time.monotonic() < deadline, (stage, s, size)
            time.sleep(.1)
        check(stage)
    assert 'GUI MEMORY CORRUPTION' not in log.read_text()
    print('PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption', flush=True)
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
