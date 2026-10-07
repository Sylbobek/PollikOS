"""Observe real boot animation and AC97 startup/login DMA on a disposable disk."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import wave
from PIL import Image
from gui_metrics import Guest, BUILD
from login_gui import create_login_disk

capture = BUILD / 'boot-chimes-qemu.wav'
original_popen = subprocess.Popen

def launch(args, *positional, **keywords):
    if args[0] == 'qemu-system-x86_64' and '-drive' in args:
        args = args + ['-audiodev', f'wav,id=chime,path={capture},out.frequency=48000',
                       '-device', 'AC97,audiodev=chime']
    return original_popen(args, *positional, **keywords)

subprocess.Popen = launch

class BootGuest(Guest):
    def wait(self, predicate, message, seconds=10):
        if message != 'desktop boot':
            return super().wait(predicate, message, seconds)
        super().wait(lambda: 'GFX: boot splash presented' in self.log.read_text(), 'splash appeared', seconds)
        observations, colors = [], set()
        end = time.monotonic() + seconds
        shot = BUILD / 'boot-animated-1920x1080.ppm'
        captured = False
        while not predicate():
            assert self.process.poll() is None and time.monotonic() < end, 'boot animation timed out'
            if self.words('splash_active')[0]:
                frames = self.words('splash_frames')[0]
                percent = self.words('splash_value')[0] // 256
                target = self.words('splash_target')[0]
                assert 0 <= percent <= target <= 100
                if not observations or frames > observations[-1][0]:
                    observations.append((frames, percent, target))
                    colors.add(self.backbuffer_pixel(self.width // 2, self.height // 2 - 100))
                if frames >= 10 and not captured:
                    self.qmp('stop')
                    try:
                        self.screendump(shot)
                        Image.open(shot).save(shot.with_suffix('.png'))
                        captured = True
                    finally:
                        self.qmp('cont')
            time.sleep(.025)
        assert captured and len(colors) >= 3, ('no animated background', len(colors), observations)
        percentages = [item[1] for item in observations]
        assert percentages == sorted(percentages), observations
        assert self.words('splash_value')[0] == 25600
        assert self.words('splash_mask')[0] == 0 and self.words('splash_active')[0] == 0
        assert self.words('splash_clock_rate')[0] > 0
        assert self.words('splash_frames')[0] >= 10
        changes = [entry for i, entry in enumerate(observations) if i == 0 or entry[1:] != observations[i-1][1:]]
        print(f'PASS animated boot: frames={self.words("splash_frames")[0]}, clock_rate={self.words("splash_clock_rate")[0]}, progress={changes}, '
              f'background colors={len(colors)}, 100% only on completion, mask released', flush=True)

def check_dma(g, login):
    with wave.open(str(BUILD / ('login-chime.wav' if login else 'startup-chime.wav')), 'rb') as f:
        golden = f.readframes(f.getnframes())
    samples = g.words('s_chime_pcm')[0]
    assert samples and g.words('s_ac97_present')[0] == 1
    assert g.memory(samples, len(golden)) == golden, 'guest PCM differs from native synthesizer'
    bdl = g.words('s_bdl')[0]
    frames = len(golden) // 4
    blocks = (frames + 1023) // 1024
    for block in range(blocks):
        pointer, count, flags = struct.unpack('<IHH', g.memory(bdl + block * 8, 8))
        assert pointer == samples + block * 4096
        assert count == min(1024, frames - block * 1024) * 2 and flags == 32768
    print(f'PASS {"login" if login else "startup"} AC97: {blocks} descriptors, {frames} stereo frames, '
          'guest PCM exactly matches native output', flush=True)

os.environ['POLLIK_GUI_IMAGE'] = os.environ.get('POLLIK_GUI_IMAGE', 'PollikOS-Alpha.img')
with tempfile.TemporaryDirectory(prefix='pollik-boot-ui-') as folder:
    path = Path(folder) / 'data.img'
    create_login_disk(path, True)
    before = hashlib.sha256(path.read_bytes()).digest()
    with BootGuest('1920x1080', 'boot-presentation', data_image=path, boot_only=True) as g:
        assert 'Audio: startup chime started' in g.log.read_text()
        check_dma(g, False)
        shot = BUILD / 'login-matched-1920x1080.ppm'
        g.screendump(shot)
        Image.open(shot).save(shot.with_suffix('.png'))
        time.sleep(.6)
        for key in 'test123':
            g.hmp('sendkey ' + key + ' 1')
            time.sleep(.12)
        g.hmp('sendkey ret 1')
        g.wait(lambda: 'Audio: login chime started' in g.log.read_text(), 'login chime', 20)
        assert 'AUTH: login accepted' in g.log.read_text()
        check_dma(g, True)
        time.sleep(.6)
        g.qmp('quit')
        g.process.wait(timeout=5)
    assert hashlib.sha256(path.read_bytes()).digest() == before
raw = capture.read_bytes()
# This QEMU Windows WAV backend leaves the RIFF/data lengths zero on quit;
# validate its fixed PCM header and inspect the actual captured payload.
assert raw[:4] == b'RIFF' and raw[8:16] == b'WAVEfmt ' and raw[36:40] == b'data'
assert struct.unpack('<HHI', raw[20:28]) == (1, 2, 48000) and raw[34:36] == b'\x10\0'
assert (len(raw) - 44) % 4 == 0
samples = struct.unpack('<' + 'h' * ((len(raw) - 44) // 2), raw[44:])
assert max(samples) > 100 and min(samples) < -100, 'QEMU audio capture is silent'
print(f'PASS actual QEMU audio captured: {(len(raw) - 44) // 4} frames, '
      f'peak={max(abs(s) for s in samples)}; snapshot fixture unchanged', flush=True)
