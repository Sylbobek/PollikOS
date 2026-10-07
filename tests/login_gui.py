"""QEMU login/blur check using disposable PollikFS images and real PS/2 input."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile
import time

from PIL import Image
from gui_fixture import format_disk, sync, PollikFsImage
from gui_metrics import Guest, BUILD


def blend(a, b, t):
    return sum(((((a >> s) & 255) * (256 - t) + ((b >> s) & 255) * t) >> 8) << s
               for s in (16, 8, 0))


def create_login_disk(path, dark):
    format_disk(path, total_size_mb=40)
    sync(path)
    fs = PollikFsImage.load(path)
    fs.ensure_directory('/home/.config')
    fs.install_file('/home/.config/appearance.conf',
                    f'theme={0 if dark else 1}\nanimations=1\ncursor_size=100\n'.encode())
    salt = bytes(range(16))
    digest = hashlib.sha256(salt + b'test123').digest()
    for _ in range(8191):
        digest = hashlib.sha256(digest + salt + b'test123').digest()
    fs.ensure_directory('/etc')
    fs.install_file('/etc/account.db', struct.pack('<III32s16s32s36s',
                    0x31524341, 1, 8192, b'fixture', salt, digest, bytes(36)))
    path.write_bytes(fs.data)


def check(resolution, dark):
    with tempfile.TemporaryDirectory(prefix='pollik-login-') as folder:
        path = Path(folder) / 'data.img'
        create_login_disk(path, dark)
        before = hashlib.sha256(path.read_bytes()).digest()
        with Guest(resolution, f'login-clean-{resolution}-{dark}', data_image=path, boot_only=True) as g:
            g.wait(lambda: 'AUTH: filesystem wallpaper blur cached' in g.log.read_text(), 'blur cache', 30)
            assert 'AUTH: login required' in g.log.read_text()
            assert 'decoder=ok' in g.log.read_text() and 'wallpaper unavailable' not in g.log.read_text()
            w, h = g.width, g.height
            cache = g.words('auth_backdrop')[0]
            source = g.words('theme_wallpapers')[0 if dark else 1]
            raw = g.memory(source, w * h * 4)
            pixels = struct.unpack('<' + 'I' * (w * h), raw)
            sw, sh = (w + 3) // 4, (h + 3) // 4

            def blurred(x, y):
                rows = []
                for dy in range(-3, 4):
                    sy = min(sh - 1, max(0, y + dy))
                    row = [pixels[sy * 4 * w + min(sw - 1, max(0, x + dx)) * 4]
                           for dx in range(-3, 4)]
                    rows.append(sum((sum((c >> s) & 255 for c in row) // 7) << s
                                    for s in (16, 8, 0)))
                return sum((sum((c >> s) & 255 for c in rows) // 7) << s for s in (16, 8, 0))

            for x, y in ((25, 100), (w - 25, h // 2), (w - 25, h - 190)):
                sx, sy = x // 4, y // 4
                nx, ny = min(sw - 1, sx + 1), min(sh - 1, sy + 1)
                a = blend(blurred(sx, sy), blurred(nx, sy), (x & 3) * 64)
                b = blend(blurred(sx, ny), blurred(nx, ny), (x & 3) * 64)
                tinted = blend(blend(a, b, (y & 3) * 64), 0x765591, 112)
                expected = blend(tinted, 0x100b20, 64 + y * 48 // h)
                assert int.from_bytes(g.memory(cache + (y * w + x) * 4, 4), 'little') == expected
                g.wait(lambda: g.backbuffer_pixel(x, y) == expected, 'blur presentation')
            shot = BUILD / f'login-clean-{resolution}-{"dark" if dark else "light"}.ppm'
            time.sleep(.3)
            g.screendump(shot)
            Image.open(shot).save(shot.with_suffix('.png'))

            def key(name):
                g.hmp('sendkey ' + name + ' 1')
                time.sleep(.15)

            for ch in 'wrong123': key(ch)
            key('ret')
            g.wait(lambda: 'AUTH: login rejected' in g.log.read_text(), 'wrong password rejected', 15)
            for ch in 'test123': key(ch)
            # Visibility persists after release and movement, until clicked again.
            eye_x, eye_y = g.words('eye_x')[0], g.words('eye_y')[0]
            g.move(eye_x + 13, eye_y + 13)
            g.hmp('mouse_button 1')
            g.wait(lambda: g.words('show_password')[0] == 1, 'password reveal')
            g.hmp('mouse_button 0')
            g.wait(lambda: g.words('pointer_was_down')[0] == 0, 'toggle released')
            g.move(eye_x - 20, eye_y + 13)
            assert g.words('show_password')[0] == 1, 'release/movement hid password'
            g.move(eye_x + 13, eye_y + 13)
            g.hmp('mouse_button 1')
            g.wait(lambda: g.words('show_password')[0] == 0, 'password hidden')
            g.hmp('mouse_button 0')
            g.wait(lambda: g.words('pointer_was_down')[0] == 0, 'second toggle released')
            g.hmp('mouse_button 1')
            g.wait(lambda: g.words('show_password')[0] == 1, 'password revealed again')
            g.hmp('mouse_button 0')
            g.wait(lambda: g.words('pointer_was_down')[0] == 0, 'third toggle released')
            assert g.words('auth_backdrop')[0] == cache
            assert g.log.read_text().count('AUTH: filesystem wallpaper blur cached') == 1
            bx, by = g.words('button_x')[0], g.words('button_y')[0]
            g.move(bx + 21, by + 21)
            g.hmp('mouse_button 1')
            g.wait(lambda: 'AUTH: login accepted' in g.log.read_text(), 'arrow login', 20)
            g.hmp('mouse_button 0')
            g.wait(lambda: g.words('auth_backdrop')[0] == 0, 'blur cache released')
            assert g.words('show_password')[0] == 0, 'visibility was not reset after login'
            print(f'PASS {resolution} {"dark" if dark else "light"}: blur pixels, cache reuse/free, '
                  'wrong/correct passwords, visibility, pointer submit; screenshot=' + str(shot.with_suffix('.png')))
        assert hashlib.sha256(path.read_bytes()).digest() == before, 'snapshot changed fixture'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resolution', default='1024x768')
    parser.add_argument('--dark', action='store_true')
    args = parser.parse_args()
    os.environ['POLLIK_GUI_IMAGE'] = 'PollikOS-Alpha.img'
    check(args.resolution, args.dark)
