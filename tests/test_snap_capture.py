import socket, json, time, subprocess, pathlib
from PIL import Image
import shutil

ROOT = pathlib.Path('.').resolve()
BUILD = ROOT / 'build'
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]

p = subprocess.Popen([
    'qemu-system-x86_64', '-machine', 'pc', '-cpu', 'max', '-m', '2G', '-vga', 'std',
    '-drive', f'format=raw,file={BUILD / "PollikOS-Alpha.img"},if=ide,index=0',
    '-display', 'none', '-serial', 'file:build/test-snap.log',
    '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off'
], cwd=ROOT)

try:
    time.sleep(1.5)
    s = socket.create_connection(('127.0.0.1', port))
    f = s.makefile('rwb', buffering=0)
    f.readline()
    def q(cmd, args=None):
        f.write((json.dumps({'execute': cmd, 'arguments': args or {}}) + '\n').encode())
        while True:
            r = json.loads(f.readline())
            if 'return' in r: return r['return']
    q('qmp_capabilities')
    time.sleep(3)

    # Initial Welcome window is at x=170, y=125, w=680, h=410
    # Pointer starts at (760, 500)
    # Move pointer to titlebar of Welcome window (e.g. 300, 140)
    # dx = 300 - 760 = -460, dy = 140 - 500 = -360
    q('human-monitor-command', {'command-line': 'mouse_move -460 -360'})
    time.sleep(0.3)
    # Press left mouse button to grab titlebar
    q('human-monitor-command', {'command-line': 'mouse_button 1'})
    time.sleep(0.2)
    # Drag left towards screen edge (e.g. x <= 12)
    q('human-monitor-command', {'command-line': 'mouse_move -300 0'})
    time.sleep(0.3)
    # Capture Snap Preview
    q('screendump', {'filename': 'build/stage1_snap_preview.ppm'})

    # Release mouse button to complete snap
    q('human-monitor-command', {'command-line': 'mouse_button 0'})
    time.sleep(0.5)
    # Capture Snapped Window (50% left work area)
    q('screendump', {'filename': 'build/stage1_snapped_window.ppm'})

    # Now grab titlebar of snapped window and drag right -> should restore and drag
    q('human-monitor-command', {'command-line': 'mouse_button 1'})
    time.sleep(0.2)
    q('human-monitor-command', {'command-line': 'mouse_move 200 100'})
    time.sleep(0.3)
    q('screendump', {'filename': 'build/stage1_restored_drag.ppm'})
    q('human-monitor-command', {'command-line': 'mouse_button 0'})
    time.sleep(0.2)
    q('quit')
finally:
    p.wait()

for name in ['stage1_snap_preview', 'stage1_snapped_window', 'stage1_restored_drag']:
    ppm = BUILD / f'{name}.ppm'
    if ppm.exists():
        png = BUILD / f'{name}.png'
        Image.open(ppm).save(png)
        shutil.copy(png, r'C:\Users\syltu\.gemini\antigravity-ide\brain\ff4bc62d-80a3-418b-93d6-06b0d425e6ff')
print('Snap capture complete!')
