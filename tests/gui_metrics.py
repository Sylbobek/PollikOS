"""Shared, symbol-checked QMP GUI benchmark driver (no network or real data disk).

No memory writes or guest function calls: all interactions use PS/2 input.
Snapshots briefly stop vCPUs for coherent inspection; host overhead is reported,
not hidden. Timing numbers are emulator wall-time, not physical GPU throughput.
"""
import hashlib
import json
import pathlib
import platform
import socket
import struct
import subprocess
import tempfile
import time
from format_pollikfs2 import format_disk

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
PREFIX = ('frame_count fps avg_frame_us p95_frame_us p99_frame_us worst_frame_us '
          'input_events_sec coalesced_mouse_sec client_paints_sec compositor_frames_sec '
          'presents_sec pixels_composed_sec pixels_presented_sec full_redraw_count '
          'damage_rects_count layout_us paint_us present_us input_us compose_us window_count').split()
EXTRA = ('total_us min_frame_us max_frame_us history_count interval_count avg_interval_us '
         'p95_interval_us p99_interval_us low_1pct_fps slow_1pct_interval_us render_fps '
         'clock_source clock_resolution_us tsc_khz effective_rects composed_pixels '
         'presented_pixels cursor_frames dock_frames client_paint_count').split()
TAIL = ('elapsed_us total_time_us paint_time_us compose_time_us present_time_us '
        'composed_pixels_total presented_pixels_total').split()
COUNTERS = ('frame_count full_redraw_count damage_rects_count cursor_frames dock_frames '
            'client_paint_count').split() + TAIL[1:]


def distribution(values):
    if not values:
        return dict(count=0, mean_us=0, min_us=0, max_us=0, p95_us=0, p99_us=0, low_1pct_fps=0)
    v = sorted(values)
    n = len(v)
    slow = v[-((n + 99) // 100):]
    return dict(count=n, mean_us=sum(v) / n, min_us=v[0], max_us=v[-1],
                p95_us=v[(n * 95 + 99) // 100 - 1], p99_us=v[(n * 99 + 99) // 100 - 1],
                low_1pct_fps=1e6 * len(slow) / sum(slow) if sum(slow) else 0)


class Guest:
    def __init__(self, resolution, label='benchmark'):
        self.resolution = resolution
        self.width, self.height = map(int, resolution.split('x'))
        self.log = BUILD / f'{label}-{resolution}.log'
        self.process = self.connection = self.stream = None
        self.temp = tempfile.TemporaryDirectory(prefix='pollikos-gui-')
        self.folder = pathlib.Path(self.temp.name)
        self.symbols = {}
        image, elf = BUILD / 'PollikOS-Surface.img', BUILD / 'kernel.elf'
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
        assert self.symbol('g_perf_stats')[1] == 4 * (len(PREFIX) + len(EXTRA)) + 8 * len(TAIL)
        for name in ('mx', 'my', 'pointer_packet.held', 'g_dragged_window', 'g_resized_window',
                     'g_frame_time_history', 'g_frame_interval_history', 'shell', 'address', 'stride', 'bytes'):
            self.symbol(name)
        self.environment = dict(resolution=resolution, memory_mib=256, cpu='max', nic='none',
                                accelerator='tcg', host=platform.platform(), python=platform.python_version(),
                                qemu=subprocess.check_output(['qemu-system-x86_64', '--version'], text=True).splitlines()[0],
                                kernel_sha256=hashlib.sha256(blob).hexdigest(), kernel_bytes=len(blob),
                                elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                                image='PollikOS-Surface.img (snapshot=on; ELF load bytes verified)',
                                sampling='QMP memory probes; stage-boundary vCPU stops; includes observer overhead')

    def __enter__(self):
        data = self.folder / 'data.img'
        format_disk(data, total_size_mb=40)
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        self.log.write_text('')
        self.process = subprocess.Popen([
            'qemu-system-x86_64', '-machine', 'pc', '-accel', 'tcg', '-cpu', 'max', '-m', '256M',
            '-device', 'VGA,vgamem_mb=32', '-display', 'none', '-no-reboot',
            '-fw_cfg', f'name=opt/pollikos/display,string={self.resolution}',
            '-drive', f'format=raw,file={BUILD / "PollikOS-Surface.img"},if=ide,index=0,snapshot=on',
            '-drive', f'format=raw,file={data},if=ide,index=1', '-nic', 'none',
            '-serial', f'file:{self.log}', '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
        ], cwd=ROOT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            end = time.monotonic() + 40
            while self.connection is None:
                try:
                    self.connection = socket.create_connection(('127.0.0.1', port), timeout=10)
                except OSError:
                    assert self.process.poll() is None and time.monotonic() < end, 'QMP unavailable'
                    time.sleep(.03)
            self.stream = self.connection.makefile('rwb', buffering=0)
            self.stream.readline()
            self.qmp('qmp_capabilities')
            self.wait(lambda: 'desktop ready' in self.log.read_text(), 'desktop boot', 40)
            assert f'GFX resolution: {self.resolution}' in self.log.read_text()
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

    def stats(self):
        address, size = self.symbol('g_perf_stats')
        values = struct.unpack('<' + 'I' * (len(PREFIX) + len(EXTRA)) + 'Q' * len(TAIL), self.memory(address, size))
        return dict(zip(PREFIX + EXTRA + TAIL, values))

    def move(self, x, y):
        assert 0 <= x < self.width and 0 <= y < self.height
        end = time.monotonic() + 15
        while self.pointer() != (x, y):
            assert time.monotonic() < end, f'pointer stuck targeting {(x, y)}'
            mx, my = self.pointer()
            dx, dy = max(-80, min(80, x - mx)), max(-80, min(80, y - my))
            self.hmp(f'mouse_move {dx} {dy}')
            self.wait(lambda: self.pointer() == (mx + dx, my + dy), 'PS/2 delta not consumed')

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
            s['duration_history'] = list(self.words('g_frame_time_history')[:s['history_count']])
            s['interval_history'] = list(self.words('g_frame_interval_history')[:s['interval_count']])
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
        assert 'FPS:' in text and 'compose:' in text and 'present:' in text, text
        return text


def stage_result(name, before, after, started, operations):
    elapsed = after['elapsed_us'] - before['elapsed_us']
    counts = {k: after[k] - before[k] for k in COUNTERS}
    assert counts['frame_count'] > 0, f'{name}: no completed presents'
    assert elapsed > 0 and all(v >= 0 for v in counts.values()), (name, counts)
    assert counts['total_time_us'] >= counts['paint_time_us'] + counts['present_time_us']
    assert counts['total_time_us'] == counts['paint_time_us'] + counts['compose_time_us'] + counts['present_time_us']
    return dict(name=name, duration_host_s=time.monotonic() - started,
                duration_guest_s=elapsed / 1e6, operations=operations, counts=counts,
                actual_fps=counts['frame_count'] * 1e6 / elapsed,
                render_throughput_fps=counts['frame_count'] * 1e6 / counts['total_time_us'],
                mean_paint_us=counts['paint_time_us'] / counts['frame_count'],
                mean_compose_us=counts['compose_time_us'] / counts['frame_count'],
                mean_present_us=counts['present_time_us'] / counts['frame_count'],
                frame_history=distribution(after['duration_history']),
                interval_history=distribution(after['interval_history']),
                history_scope='last up to 128 completed frames, may include preceding stage',
                stats=after)
