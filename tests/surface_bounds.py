"""QEMU regression: surface geometry/stride must follow maximize and restore.
Uses a disposable data disk; inspects the running kernel via ELF symbols/QMP.
Run after building build/PollikOS-Surface.img with the matching kernel.elf.
"""
import argparse
import json
import pathlib
import socket
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
parser = argparse.ArgumentParser()
parser.add_argument('--resolution', default='3440x1440')
parser.add_argument('--wm', action='store_true', help='Also exercise desktop window interactions')
args = parser.parse_args()
from surface_support import checked_surface_symbols, check_guards
symbols, APP_COUNT = checked_surface_symbols()
screen_w, screen_h = map(int, args.resolution.split('x'))
log = BUILD / f'surface-{args.resolution}.log'
shot = BUILD / f'surface-{args.resolution}.ppm'
data = BUILD / 'surface-test-data.img'
from gui_fixture import create_gui_disk, finish_setup
create_gui_disk(data, total_size_mb=64)
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]
process = subprocess.Popen([
    'qemu-system-x86_64', '-machine', 'pc', '-cpu', 'max', '-m', '2G',
    '-device', 'VGA,vgamem_mb=32', '-display', 'none', '-no-reboot',
    '-fw_cfg', f'name=opt/pollikos/display,string={args.resolution}',
    '-drive', f'format=raw,file={BUILD / "PollikOS-Surface.img"},if=ide,index=0,snapshot=on',
    '-drive', f'format=raw,file={data},if=ide,index=1',
    # This is a graphics regression, independent of external network services.
    '-nic', 'none',
    '-serial', f'file:{log}', '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
], creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
try:
    deadline = time.monotonic() + 40
    while True:
        try:
            connection = socket.create_connection(('127.0.0.1', port), timeout=3)
            break
        except OSError:
            assert process.poll() is None and time.monotonic() < deadline
            time.sleep(.1)
    stream = connection.makefile('rwb', buffering=0)
    stream.readline()

    def qmp(command, arguments=None):
        stream.write((json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n').encode())
        while True:
            result = json.loads(stream.readline())
            assert 'error' not in result, result
            if 'return' in result:
                return result['return']

    def hmp(command):
        return qmp('human-monitor-command', {'command-line': command})

    def words(address, size):
        dump = BUILD / 'surface-memory.bin'
        qmp('pmemsave', {'val': address, 'size': size, 'filename': str(dump)})
        raw = dump.read_bytes()
        return tuple(raw) if size % 4 else struct.unpack('<' + 'I' * (size // 4), raw)

    def record(name, index=0):
        address, size = symbols[name]
        if name in ('g_windows', 'g_surfaces'):
            size //= APP_COUNT
        return words(address + index * size, size)

    def move(x, y):
        deadline = time.monotonic() + 30
        while True:
            mx, my = record('mx')[0], record('my')[0]
            if (mx, my) == (x, y):
                break
            assert time.monotonic() < deadline, f'pointer stuck at {(mx, my)}, target={(x, y)}'
            dx = max(-5, min(5, x - mx))
            dy = max(-5, min(5, y - my))
            hmp(f'mouse_move {dx} {dy}')
            # Do not enqueue another delta until the guest consumes this one.
            while (record('mx')[0], record('my')[0]) != (mx + dx, my + dy):
                assert time.monotonic() < deadline, 'PS/2 motion was not consumed'
                time.sleep(.005)

    def click(x, y):
        move(x, y)
        hmp('mouse_button 1')
        time.sleep(.3)
        hmp('mouse_button 0')
        time.sleep(2)

    def check(stage, app):
        deadline = time.monotonic() + 30
        while any(record('g_animations')[i] for i in range(0, APP_COUNT * 17, 17)):
            if time.monotonic() >= deadline:
                qmp('stop')
                print(hmp('info registers'), flush=True)
                for name in ('g_frame_time_idx', 'g_last_frame_ms', 'ticks', 'g_surfaces', 'dirty_client'):
                    print(name, record(name), flush=True)
                raise AssertionError('renderer did not become idle')
            time.sleep(.2)
        deadline = time.monotonic() + 30
        while True:
            w, s = record('g_windows', app), record('g_surfaces', app)
            if s[1:5] == (w[4], w[5], w[4], w[4]) or time.monotonic() >= deadline:
                break
            time.sleep(.2)
        assert s[1:5] == (w[4], w[5], w[4], w[4]), f'{stage}: stale surface {s[1:5]}'
        # Metadata is assigned before drawing. Wait for actual pixels in the
        # completed surface, scene and hardware LFB, not just width/height.
        lfb, pitch, pixel_bytes = (record(n)[0] for n in ('address', 'stride', 'bytes'))
        assert pixel_bytes in (3, 4) and pitch >= screen_w * pixel_bytes
        deadline = time.monotonic() + 30
        while True:
            w, s = record('g_windows', app), record('g_surfaces', app)
            x, y = w[4] - 20, 30
            offset = ((w[3] + y) * screen_w + w[2] + x) * 4
            expected_color = 0xf2eff6
            scene = record('pixels')[0]
            lfb_pixel = words(lfb + (w[3] + y) * pitch + (w[2] + x) * pixel_bytes, pixel_bytes)
            lfb_color = lfb_pixel[0] if pixel_bytes == 4 else sum(v << (i * 8) for i, v in enumerate(lfb_pixel))
            colors = (words(s[0] + (y * s[3] + x) * 4, 4)[0],
                      words(scene + offset, 4)[0], lfb_color)
            if colors == (expected_color,) * 3:
                break
            assert time.monotonic() < deadline, f'{stage}: surface/scene/LFB={colors}'
            time.sleep(.2)
        qmp('stop')
        try:
            w, s = record('g_windows', app), record('g_surfaces', app)
            expected = (w[4], w[5], w[4], w[4])
            actual = s[1:5]
            print(f'{stage} app={app}: window={w[4]}x{w[5]}, surface={actual}', flush=True)
            assert actual == expected, f'{stage}: stale surface {actual}; expected {expected}'
            # Guards live outside the entire drawable allocation, not at corner pixels.
            check_guards(words, symbols, APP_COUNT, screen_w, screen_h)
            assert s[0] != 0
            if 'g_surface_pages' in symbols:
                pages = words(symbols['g_surface_pages'][0] + app * 4, 4)[0]
                assert w[4] * w[5] * 4 <= pages * 4096
            if stage == 'maximized':
                qmp('screendump', {'filename': str(shot)})
        finally:
            qmp('cont')
        return w

    qmp('qmp_capabilities')
    while not log.exists() or 'desktop ready' not in log.read_text():
        assert process.poll() is None and time.monotonic() < deadline, log.read_text() if log.exists() else ''
        time.sleep(.1)
    finish_setup(qmp, log)
    assert 'GFX resolution: ' + args.resolution in log.read_text()
    if args.wm:
        # The old hit-test accepted close/maximize/minimize on the blank right
        # titlebar. Probe all three former hit areas through real PS/2 input.
        w = check('titlebar-before', 0)
        original = w[2:9]
        for offset in (20, 55, 95):
            click(w[2] + w[4] - offset, w[3] + 17)
            actual = record('g_windows', 0)
            assert actual[2:9] == original, ('invisible titlebar control', offset, actual)
            assert record('g_focused_window')[0] == 0
        check('titlebar-after', 0)
        path = BUILD / f'wm-titlebar-{args.resolution}.ppm'
        qmp('screendump', {'filename': str(path)})
        from PIL import Image
        Image.open(path).save(path.with_suffix('.png'))
        print('PASS: blank right titlebar has no invisible window actions', flush=True)

        def key_event(key, down):
            qmp('input-send-event', {'events': [{'type': 'key', 'data': {
                'down': down, 'key': {'type': 'qcode', 'data': key}}}]})

        def tap(key):
            key_event(key, True)
            time.sleep(.12)
            key_event(key, False)
            time.sleep(.4)

        def capture(name):
            path = BUILD / f'wm-{name}-{args.resolution}.ppm'
            qmp('screendump', {'filename': str(path)})
            Image.open(path).save(path.with_suffix('.png'))

        def drag_to(x, y, tx, ty, preview=None):
            move(x, y)
            hmp('mouse_button 1')
            time.sleep(.3)
            move(tx, ty)
            time.sleep(.5)
            if preview:
                capture(preview)
            hmp('mouse_button 0')
            time.sleep(.8)

        screen_w, screen_h = map(int, args.resolution.split('x'))
        for app in (1, 2, 3):
            tap(f'f{app + 1}')
            check('opened', app)
        capture('overlap')
        assert record('g_focused_window')[0] == 3
        key_event('alt', True)
        tap('tab')
        assert record('g_focused_window')[0] == 3, 'switcher changed focus before Alt release'
        capture('alttab')
        key_event('alt', False)
        time.sleep(.8)
        assert record('g_focused_window')[0] == 2
        check('alttab-commit', 2)
        # Escape cancels the switcher without minimizing the focused window.
        key_event('alt', True)
        tap('tab')
        tap('esc')
        key_event('alt', False)
        time.sleep(.5)
        assert record('g_focused_window')[0] == 2 and record('g_windows', 2)[8] == 0

        # Minimize maximized Terminal, then restore through its real Dock icon.
        tap('f11')
        w = check('maximized', 2)
        before = w[2:7]
        click(w[2] + 36, w[3] + 17)
        w = record('g_windows', 2)
        if not (w[6] == 1 and w[7:10] == (1, 1, 0) and w[10] == 0):
            capture('minimize-state-failed')
            print('MINIMIZE DIAGNOSTIC:', json.dumps(dict(
                before_geometry_state=before, actual_window=w,
                pre_minimized_state=record('g_pre_minimized_state'),
                minimized_rect=record('g_minimized_rect'),
                focused_window=record('g_focused_window'),
                animation=record('g_animations')[2*17:3*17])), flush=True)
        assert w[6] == 1 and w[7:10] == (1, 1, 0) and w[10] == 0, w
        assert record('g_pre_minimized_state')[2] == 2, 'pre-minimize state lost'
        assert record('g_minimized_rect')[8:12] == before[:4], 'minimize rect lost'
        assert w[2:6] == before[:4], 'minimize shrank authoritative geometry'
        capture('minimized')
        terminal_dock_x = screen_w // 2 - (APP_COUNT - 1) * 34 + 2 * 68
        click(terminal_dock_x, screen_h - 60)
        w = check('dock-restored', 2)
        assert w[2:7] == before and w[8] == 0
        click(terminal_dock_x, screen_h - 60)
        assert record('g_windows', 2)[8] == 0, 'active dock click minimized app'
        tap('f11')
        w = check('restored', 2)

        # All eight resize directions, checking the moving and fixed edges.
        for left, right, top, bottom in ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1),
                                        (1,0,1,0),(0,1,1,0),(1,0,0,1),(0,1,0,1)):
            w = record('g_windows', 2)
            x, y, ww, hh = w[2:6]
            px = x if left else x + ww - 1 if right else x + ww // 2
            py = y if top else y + hh - 1 if bottom else y + hh // 2
            dx = 12 if left else -12 if right else 0
            dy = 12 if top else -12 if bottom else 0
            drag_to(px, py, px + dx, py + dy)
            actual = record('g_windows', 2)[2:6]
            expected = (x + (12 if left else 0), y + (12 if top else 0),
                        ww - (12 if left or right else 0), hh - (12 if top or bottom else 0))
            assert actual == expected, ('resize', (left,right,top,bottom), actual, expected)
        w = check('eight-resize-edges', 2)
        capture('resized')
        # Resize down to minimum; fixed top-left must not move.
        x, y, ww, hh = w[2:6]
        drag_to(x + ww - 1, y + hh - 1, x + 20, y + 20)
        w = check('minimum-size', 2)
        assert w[2:6] == (x, y, 480, 280)

        # Pure translation must leave every cached surface pixel unchanged.
        move(w[2] + 120, w[3] + 17)
        time.sleep(.8)
        s = record('g_surfaces', 2)
        cached = words(s[0], s[3] * s[2] * 4)
        drag_to(w[2] + 120, w[3] + 17, w[2] + 144, w[3] + 41)
        w = check('drag-cache', 2)
        assert words(s[0], s[3] * s[2] * 4) == cached, 'drag changed cached content'
        normal_size = w[4:6]
        drag_to(w[2] + 120, w[3] + 17, 2, 250, 'snap-preview')
        w = check('snap-left', 2)
        assert w[6] == 3 and w[2:6] == (8, 36, (screen_w - 24)//2, screen_h - 142)
        capture('snapped')
        drag_to(w[2] + 120, w[3] + 17, 300, 180)
        w = check('drag-snapped-restore', 2)
        assert w[6] == 0 and w[4:6] == normal_size
        tap('f11')
        w = check('max-before-drag', 2)
        drag_to(w[2] + 150, w[3] + 17, 400, 220)
        w = check('drag-maximized-restore', 2)
        assert w[6] == 0 and w[4:6] == normal_size
        capture('drag-restored')
        # Close via Alt+F4: no new Notes launch via the F4 fallback.
        key_event('alt', True)
        tap('f4')
        key_event('alt', False)
        time.sleep(.5)
        assert record('g_windows', 2)[7] == 0
        assert record('g_focused_window')[0] != 2
        print('PASS: focus, AltTab/cancel, Dock restore, eight resize edges, minimum, cache drag, snap, drag-restore, AltF4', flush=True)
    for app in range(0 if args.wm else APP_COUNT):
        hmp(f'sendkey f{app + 1} 100')
        time.sleep(1.5)
        w = check('normal', app)
        original = w[2:6]
        click(w[2] + 54, w[3] + 17)
        w = check('maximized', app)
        screen_w, screen_h = map(int, args.resolution.split('x'))
        assert w[2:6] == (0, 32, screen_w, screen_h - 128)
        click(w[2] + 54, w[3] + 17)
        w = check('restored', app)
        assert w[2:6] == original
    text = log.read_text()
    for error in ('KERNEL PANIC', 'PAGE FAULT', '[WM] FATAL', '[GUI MEMORY CORRUPTION]'):
        assert error not in text, text
    print('PASS: ' + args.resolution + (', WM interactions' if args.wm else
          f', all {APP_COUNT} surfaces maximize/restore, runtime LFB and external guards'))
finally:
    process.terminate()
    process.wait(timeout=10)
