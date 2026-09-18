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
    '-display', 'none', '-serial', 'file:build/test-alttab.log',
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
    # Open Terminal (F3) and Notes (F4)
    q('human-monitor-command', {'command-line': 'sendkey f3'})
    time.sleep(0.5)
    q('human-monitor-command', {'command-line': 'sendkey f4'})
    time.sleep(0.5)
    
    # Send Alt press via QMP input-send-event
    q('input-send-event', {'events': [{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': 'alt'}}}]})
    time.sleep(0.1)
    q('input-send-event', {'events': [{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': 'tab'}}}]})
    time.sleep(0.1)
    q('input-send-event', {'events': [{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': 'tab'}}}]})
    time.sleep(0.3)
    q('screendump', {'filename': 'build/stage1_alttab_visible.ppm'})

    # Release Alt
    q('input-send-event', {'events': [{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': 'alt'}}}]})
    time.sleep(0.3)
    q('screendump', {'filename': 'build/stage1_alttab_released.ppm'})
    q('quit')
finally:
    p.wait()

Image.open('build/stage1_alttab_visible.ppm').save('build/stage1_alttab_visible.png')
shutil.copy('build/stage1_alttab_visible.png', r'C:\Users\syltu\.gemini\antigravity-ide\brain\ff4bc62d-80a3-418b-93d6-06b0d425e6ff')
print('Alt-Tab capture complete!')
