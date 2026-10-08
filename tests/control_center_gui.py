"""Detached Control Center flyouts using real guest PS/2 input.

Only a freshly created disposable account disk is attached.
"""
import argparse
import tempfile
import time
from pathlib import Path
from PIL import Image
from gui_metrics import Guest, BUILD
from login_gui import create_login_disk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resolution', default='1920x1080')
    parser.add_argument('--theme', choices=('dark', 'light'), default='dark')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pollik-control-center-') as folder:
        disk = Path(folder) / 'data.img'
        create_login_disk(disk, args.theme == 'dark')
        with Guest(args.resolution, 'control-center-' + args.theme, data_image=disk,
                   boot_only=True, headless=True, boot_timeout=120) as g:
            def value(name):
                return g.words(name)[0]

            def click(x, y):
                g.move(x, y)
                g.button(True)
                g.button(False)

            def key(name):
                g.hmp('sendkey ' + name + ' 1')
                time.sleep(.12)

            def snapshot(name, checks=()):
                path = BUILD / f'control-center-{args.theme}-{args.resolution}-{name}.ppm'
                deadline = time.monotonic() + 20
                while True:
                    time.sleep(.12)
                    g.screendump(path)
                    picture = Image.open(path)
                    if all(picture.getpixel(point) == color for point, color in checks):
                        break
                    assert time.monotonic() < deadline, (name, 'flyout has not finished presenting')
                picture.save(path.with_suffix('.png'))
                return picture, path.with_suffix('.png')

            # Boot's autonomous PMM stress must finish before login frees its
            # backdrop; otherwise its before/after counters include that cache.
            g.wait(lambda: '[TEST] PHASE 2 PASS' in g.log.read_text(), 'boot stress completes', 120)
            for char in 'test123':
                key(char)
            key('ret')
            g.wait(lambda: 'AUTH: login accepted' in g.log.read_text(), 'fixture login', 45)
            x = (g.width - 632) // 2 + 232
            limit = g.width - 400 - 248 - 16 - 20
            if limit >= 248:
                x = min(x, limit)
            y, fx = 38, x + 416
            click(g.width // 2, 15)
            g.wait(lambda: value('cc_shown') and not value('cc_animating'), 'panel opens')
            samples = [(x + 180, y + 222), (x + 190, y + 470), (x + 170, y + 450)]
            theme = g.words('g_dark_theme' if args.theme == 'dark' else 'g_light_theme')
            def mix(a, b, weight):
                return sum(((((a >> shift) & 255) * (256 - weight) +
                             ((b >> shift) & 255) * weight) // 256) << shift
                           for shift in (16, 8, 0))
            def rgb(color):
                return tuple((color >> shift) & 255 for shift in (16, 8, 0))
            panel = rgb(mix(theme[3], theme[9], 6))
            card = rgb(mix(mix(theme[3], theme[9], 6), theme[2], 160))
            stable = [card, panel, card]
            base, _ = snapshot('main', [((x + 200, y + 30), panel), *zip(samples, stable)])
            assert base.getpixel((x, y)) != base.getpixel((x + 200, y + 30)), 'square panel corner'
            assert fx >= x + 400 + 16 and fx + 248 <= g.width - 12, 'flyout outside screen'
            for kind, point, name, popup_top in (
                (1, (x + 166, y + 74), 'wifi', 52),
                (2, (x + 352, y + 74), 'bluetooth', 52),
                (3, (x + 360, y + 360), 'audio', 264),
                (4, (x + 70, y + 444), 'profile', 284),
            ):
                click(*point)
                g.wait(lambda: value('cc_list') == kind and value('cc_detail_kind') == kind
                       and value('cc_detail_slide') == 256, name + ' flyout opens')
                height = 204 if kind == 3 else 184 if kind == 4 else 176
                picture, path = snapshot(name, (
                    ((fx + 124, y + popup_top + 8), panel),
                    ((fx + 124, y + popup_top + height - 8), panel),
                    *zip(samples, stable),
                ))
                assert [picture.getpixel(point) for point in samples] == stable, 'flyout covered main controls'
                assert picture.getpixel((fx, y + popup_top)) != picture.getpixel((fx + 120, y + popup_top + 48)), 'square flyout corner'
                gap = (x + 408, y + popup_top + 45)
                assert picture.getpixel(gap) == base.getpixel(gap), 'flyout touches main panel'
                print(f'PASS {name}: one animated flyout, rounded corners, 16 px gap; {path}', flush=True)
            key('esc')
            g.wait(lambda: value('cc_list') == 0 and value('cc_detail_kind') == 0, 'flyout closes')
            assert value('cc_shown'), 'Escape closed the main panel with its flyout'
            key('esc')
            g.wait(lambda: not value('cc_shown'), 'main panel closes')
            log = g.log.read_text()
            assert 'PANIC' not in log and 'GUI MEMORY CORRUPTION' not in log
            print('PASS guest: exclusive Wi-Fi/Bluetooth/audio/profile, unchanged main controls, Escape and no panic', flush=True)


if __name__ == '__main__':
    main()
