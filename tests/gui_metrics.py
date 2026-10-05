"""Shared, symbol-checked QMP GUI benchmark driver (no network or real data disk).

No memory writes or guest function calls: all interactions use PS/2 input.
Snapshots briefly stop vCPUs for coherent inspection; host overhead is reported,
not hidden. Timing numbers are emulator wall-time, not physical GPU throughput.
"""
import hashlib
import json
import os
import pathlib
import platform
import socket
import struct
import subprocess
import tempfile
import time
from format_pollikfs2 import format_disk
from gui_fixture import create_gui_disk

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
PREFIX = ('frame_count fps avg_frame_us p95_frame_us p99_frame_us worst_frame_us '
          'input_events_sec coalesced_mouse_sec client_paints_sec compositor_frames_sec '
          'presents_sec pixels_composed_sec pixels_presented_sec full_redraw_count '
          'damage_rects_count layout_us paint_us present_us input_us compose_us window_count').split()
EXTRA = ('total_us min_frame_us max_frame_us history_count interval_count avg_interval_us '
         'p95_interval_us p99_interval_us low_1pct_fps slow_1pct_interval_us render_fps '
         'clock_source clock_resolution_us tsc_khz effective_rects composed_pixels '
         'presented_pixels cursor_frames dock_frames client_paint_count app_update_us').split()
TAIL = ('elapsed_us total_time_us paint_time_us compose_time_us present_time_us '
        'composed_pixels_total presented_pixels_total').split()
APPEND = ('partial_frames').split()
COUNTERS = ('frame_count full_redraw_count damage_rects_count cursor_frames dock_frames partial_frames '
            'client_paint_count').split() + TAIL[1:]


def distribution(values):
    if not values:
        return dict(count=0, mean_us=0, min_us=0, max_us=0, p50_us=0, p95_us=0, p99_us=0, low_1pct_fps=0)
    v = sorted(values)
    n = len(v)
    slow = v[-((n + 99) // 100):]
    return dict(count=n, mean_us=sum(v) / n, min_us=v[0], max_us=v[-1],
                p50_us=v[(n * 50 + 99) // 100 - 1],
                p95_us=v[(n * 95 + 99) // 100 - 1], p99_us=v[(n * 99 + 99) // 100 - 1],
                low_1pct_fps=1e6 * len(slow) / sum(slow) if sum(slow) else 0)


def history_samples(history, write_index, count):
    """Return the newest count samples in chronological order from a ring."""
    capacity = len(history)
    n = min(max(0, count), capacity)
    if not n:
        return []
    start = (write_index - n) % capacity
    return [history[(start + i) % capacity] for i in range(n)]


class Guest:
    def __init__(self, resolution, label='benchmark', data_image=None, boot_only=False,
                 boot_timeout=None, headless=False, accel=None, cpu=None):
        self.data_image = pathlib.Path(data_image).resolve() if data_image else None
        self.boot_only = boot_only
        self.resolution = resolution
        self.width, self.height = map(int, resolution.split('x'))
        self.log = BUILD / f'{label}-{resolution}.log'
        self.process = self.connection = self.stream = None
        self.temp = tempfile.TemporaryDirectory(prefix='pollikos-gui-')
        self.folder = pathlib.Path(self.temp.name)
        self.symbols = {}
        image_name = os.environ.get('POLLIK_GUI_IMAGE', 'PollikOS-Alpha.img')
        accel = (accel or os.environ.get('POLLIK_GUI_ACCEL', 'tcg')).lower()
        cpu = cpu or os.environ.get('POLLIK_GUI_CPU') or ('qemu64' if accel == 'whpx' else 'max')
        self.boot_timeout = boot_timeout if boot_timeout is not None else (180 if accel == 'whpx' else 40)
        if accel not in ('tcg', 'whpx'):
            raise ValueError(f'unsupported POLLIK_GUI_ACCEL: {accel}')
        image, elf = BUILD / image_name, BUILD / 'kernel.elf'
        self.image = image
        # Compare the exact ELF load bytes to the boot image, not timestamps.
        binary = self.folder / 'kernel.bin'
        subprocess.run(['llvm-objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
        blob = binary.read_bytes()
        with image.open('rb') as disk:
            disk.seek(4608)
            assert disk.read(len(blob)) == blob, 'Surface image does not match kernel.elf'
        for line in subprocess.check_output(['llvm-nm', '-S', str(elf)], text=True).splitlines():
            p = line.split()
            if len(p) == 4:
                self.symbols.setdefault(p[3], []).append((int(p[0], 16), int(p[1], 16)))
        self.apps = self.symbol('g_windows')[1] // 84
        assert self.symbol('g_windows')[1] == self.apps * 84 and self.apps >= 6
        assert self.symbol('g_surfaces')[1] == self.apps * 36
        assert self.symbol('g_perf_stats')[1] == 4 * (len(PREFIX) + len(EXTRA) + len(APPEND)) + 8 * len(TAIL)
        for name in ('mx', 'my', 'pointer_packet.held', 'g_dragged_window', 'g_resized_window',
                     'g_frame_time_history', 'g_frame_interval_history', 'g_frame_time_idx',
                     'g_interval_idx', 'g_perf_overlay_enabled', 'g_window_anims',
                     'shell', 'address', 'stride', 'bytes'):
            self.symbol(name)
        self.accel = accel
        self.cpu = cpu
        self.headless = bool(headless or os.environ.get('POLLIK_GUI_HEADLESS', '').lower() in ('1', 'true', 'yes'))
        self.display = 'none' if self.headless else os.environ.get('POLLIK_GUI_DISPLAY', 'none')
        self.environment = dict(resolution=resolution, memory_mib=256, cpu=cpu, nic='none',
                                accelerator=accel, headless=self.headless, display=self.display,
                                host=platform.platform(), python=platform.python_version(),
                                qemu=subprocess.check_output(['qemu-system-x86_64', '--version'], text=True).splitlines()[0],
                                kernel_sha256=hashlib.sha256(blob).hexdigest(), kernel_bytes=len(blob),
                                elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                                image=f'{image.name} (snapshot=on; ELF load bytes verified)',
                                sampling='QMP memory probes; stage-boundary vCPU stops; includes observer overhead')

    def __enter__(self):
        data = self.data_image or self.folder / 'data.img'
        if not self.data_image:
            create_gui_disk(data)
        data_snapshot = ',snapshot=on' if self.data_image else ''
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        self.log.write_text('')
        self.process = subprocess.Popen([
            'qemu-system-x86_64', '-machine', 'pc', '-accel', self.accel, '-cpu', self.cpu, '-m', '256M',
            '-device', 'VGA,vgamem_mb=32', '-display', self.display, '-no-reboot',
            '-fw_cfg', f'name=opt/pollikos/display,string={self.resolution}',
            '-drive', f'format=raw,file={self.image},if=ide,index=0,snapshot=on',
            '-drive', f'format=raw,file={data},if=ide,index=1{data_snapshot}', '-nic', 'none',
            '-serial', f'file:{self.log}', '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
        ], cwd=ROOT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            end = time.monotonic() + 40
            while self.connection is None:
                try:
                    self.connection = socket.create_connection(('127.0.0.1', port), timeout=10)
                except OSError:
                    assert self.process.poll() is None and time.monotonic() < end, f'QMP unavailable; QEMU exit={self.process.poll()}'
                    time.sleep(.03)
            self.stream = self.connection.makefile('rwb', buffering=0)
            self.stream.readline()
            self.qmp('qmp_capabilities')
            self.wait(lambda: 'desktop ready' in self.log.read_text(), 'desktop boot', self.boot_timeout)
            assert f'GFX resolution: {self.resolution}' in self.log.read_text()
            if self.boot_only:
                return self
            if 'SETUP: first-run installer ready' in self.log.read_text():
                def send_key(key):
                    self.hmp('sendkey ' + key)
                    time.sleep(.12)
                send_key('ret')
                for key in 'benchmark': send_key(key)
                send_key('ret')
                for key in 'test123': send_key(key)
                send_key('ret')
                for key in 'test123': send_key(key)
                send_key('ret')
                self.wait(lambda: 'AUTH: account created; installation complete' in self.log.read_text(),
                          'disposable benchmark account setup', 20)
            else:
                assert 'AUTH: login required' in self.log.read_text(), 'formatted test disk was not recognized'
            self.wait(lambda: self.stats()['frame_count'] > 0, 'initial frame')
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_args):
        if self.stream:
            self.stream.close()
        if self.connection:
            self.connection.close()
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.temp.cleanup()

    def symbol(self, name):
        candidates = self.symbols.get(name, [])
        assert len(candidates) == 1, f'ambiguous/missing ELF symbol {name}: {candidates}'
        return candidates[0]

    def qmp(self, command, arguments=None):
        self.stream.write((json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n').encode())
        while True:
            line = self.stream.readline()
            assert line, f'QMP EOF during {command}'
            r = json.loads(line)
            assert 'error' not in r, r
            if 'return' in r:
                return r['return']

    def screendump(self, path):
        path = pathlib.Path(path).resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        self.qmp('screendump', {'filename': str(path)})
        return path

    def hmp(self, command):
        return self.qmp('human-monitor-command', {'command-line': command})

    def wait(self, predicate, message, seconds=10):
        end = time.monotonic() + seconds
        while not predicate():
            assert self.process.poll() is None and time.monotonic() < end, message
            time.sleep(.005)

    def memory(self, address, size):
        file = self.folder / 'memory.bin'
        self.qmp('pmemsave', {'val': address, 'size': size, 'filename': str(file)})
        return file.read_bytes()

    def words(self, name, index=None):
        address, size = self.symbol(name)
        if index is not None:
            size //= self.apps
            address += index * size
        raw = self.memory(address, size)
        return struct.unpack('<' + 'I' * (size // 4), raw) if size % 4 == 0 else tuple(raw)

    def window(self, app=2):
        return self.words('g_windows', app)

    def pointer(self):
        return self.words('mx')[0], self.words('my')[0]

    def backbuffer_pixel(self, x, y):
        address = self.words('pixels')[0] + (y * self.width + x) * 4
        return struct.unpack('<I', self.memory(address, 4))[0]

    def stats(self):
        address, size = self.symbol('g_perf_stats')
        values = struct.unpack('<' + 'I' * (len(PREFIX) + len(EXTRA)) + 'Q' * len(TAIL) + 'I' * len(APPEND), self.memory(address, size))
        return dict(zip(PREFIX + EXTRA + TAIL + APPEND, values))

    def move(self, x, y):
        assert 0 <= x < self.width and 0 <= y < self.height
        end = time.monotonic() + 15
        accelerated = self.words('pointer_acceleration')[0]
        while self.pointer() != (x, y):
            assert time.monotonic() < end, f'pointer stuck targeting {(x, y)}'
            mx, my = self.pointer()
            # Same measured packet model as cursor_screenshots.py: bounded
            # signed-byte packets, exact consumption, including acceleration.
            def delta(distance):
                raw=int(distance*5/6) if accelerated and abs(distance)>=7 else distance
                return max(-100,min(100,raw))
            dx,dy=delta(x-mx),delta(y-my)
            sx,sy=dx,dy
            if accelerated and max(abs(dx),abs(dy))>=6:
                sx+=int(dx/5);sy+=int(dy/5)
            expected=(max(0,min(self.width-1,mx+sx)),max(0,min(self.height-1,my+sy)))
            self.hmp(f'mouse_move {dx} {dy}')
            self.wait(lambda: self.pointer() == expected, 'PS/2 delta not consumed')

    def button(self, down):
        self.hmp(f'mouse_button {1 if down else 0}')
        self.wait(lambda: bool(self.words('pointer_packet.held')[0]) == down, 'button not consumed')

    def key(self, name, predicate, message):
        self.hmp('sendkey ' + name + ' 1')
        self.wait(predicate, message)

    def presented(self, app=2):
        # Geometry metadata alone is not a completed render. Validate a fresh
        # surface, scene and runtime hardware LFB pixel at the new geometry.
        w, s = self.window(app), self.words('g_surfaces', app)
        if s[1:5] != (w[4], w[5], w[4], w[4]):
            return False
        if self.words('surface_w')[app] != w[4] or self.words('surface_h')[app] != w[5]:
            return False
        x, y = w[4] - 20, 30
        sx, sy = w[2] + x, w[3] + y
        if not (0 <= sx < self.width and 0 <= sy < self.height):
            return False
        lfb, pitch, bpp = (self.words(n)[0] for n in ('address', 'stride', 'bytes'))
        color = struct.pack('<I', 0xf2eff6)
        return (self.memory(s[0] + (y * s[3] + x) * 4, 4) == color and
                self.memory(self.words('pixels')[0] + (sy * self.width + sx) * 4, 4) == color and
                self.memory(lfb + sy * pitch + sx * bpp, bpp) == color[:bpp])

    def capture(self):
        self.qmp('stop')
        try:
            s = self.stats()
            s['frame_write_index'] = self.words('g_frame_time_idx')[0]
            s['interval_write_index'] = self.words('g_interval_idx')[0]
            s['guest_ticks'] = self.words('ticks')[0]
            s['duration_history'] = list(self.words('g_frame_time_history'))
            s['interval_history'] = list(self.words('g_frame_interval_history'))
            return s
        finally:
            self.qmp('cont')

    def perf_command(self):
        start = len(self.log.read_text())
        self.key('f3', lambda: self.words('g_focused_window')[0] == 2, 'terminal focus')
        for char in 'perf':
            # Key release/pacing is tied to guest ticks, not blind host sleeps.
            tick = self.words('ticks')[0]
            self.hmp(f'sendkey {char} 1')
            self.wait(lambda: self.words('ticks')[0] - tick >= 3, 'keyboard ticks stalled')
        self.hmp('sendkey ret 1')
        self.wait(lambda: 'SHELL END' in self.log.read_text()[start:], 'perf did not complete')
        text = self.log.read_text()[start:]
        assert 'FPS:' in text and 'compose:' in text and 'LFB:' in text, text
        return text


def stage_result(name, before, after, started, operations):
    elapsed = after['elapsed_us'] - before['elapsed_us']
    counts = {k: after[k] - before[k] for k in COUNTERS}
    if name.startswith('idle'):
        guest_ticks = (after['guest_ticks'] - before['guest_ticks']) & 0xffffffff
        elapsed = guest_ticks * 9943000000 // 1193180
        assert elapsed > 0, f'{name}: guest PIT did not advance'
    if counts['frame_count'] == 0:
        assert name.startswith('idle'), f'{name}: no completed presents'
        return dict(name=name, duration_host_s=time.monotonic() - started,
                    duration_guest_s=elapsed / 1e6, operations=operations, counts=counts,
                    actual_fps=0, render_throughput_fps=0, mean_paint_us=0,
                    mean_compose_us=0, mean_present_us=0,
                    mean_composed_pixels_per_frame=0,
                    phase_us=dict(input=0, app_update=0, layout=0, draw=0, composition=0, present=0),
                    frame_history=distribution([]), interval_history=distribution([]),
                    history_scope='no presents occurred during idle; no frame intervals exist', stats=after)
    assert elapsed > 0 and all(v >= 0 for v in counts.values()), (name, counts)
    assert counts['total_time_us'] >= counts['paint_time_us'] + counts['present_time_us']
    assert counts['total_time_us'] == counts['paint_time_us'] + counts['compose_time_us'] + counts['present_time_us']
    frame_samples = history_samples(after['duration_history'], after['frame_write_index'], counts['frame_count'])
    # interval_count is a capped occupancy value, not a cumulative counter.
    # Each completed frame after boot also records one presentation interval.
    sample_count = min(counts['frame_count'], len(after['interval_history']))
    interval_samples = history_samples(after['interval_history'], after['interval_write_index'], sample_count)
    return dict(name=name, duration_host_s=time.monotonic() - started,
                duration_guest_s=elapsed / 1e6, operations=operations, counts=counts,
                actual_fps=counts['frame_count'] * 1e6 / elapsed,
                render_throughput_fps=counts['frame_count'] * 1e6 / counts['total_time_us'],
                mean_paint_us=counts['paint_time_us'] / counts['frame_count'],
                mean_compose_us=counts['compose_time_us'] / counts['frame_count'],
                mean_present_us=counts['present_time_us'] / counts['frame_count'],
                mean_composed_pixels_per_frame=counts['composed_pixels_total'] / counts['frame_count'],
                phase_us=dict(input=after['input_us'], app_update=after['app_update_us'],
                              layout=after['layout_us'], draw=after['paint_us'],
                              composition=after['compose_us'], present=after['present_us']),
                frame_history=distribution(frame_samples),
                interval_history=distribution(interval_samples),
                history_scope='latest guest samples in stage; capped at 128; ordered by ring write index',
                stats=after)
