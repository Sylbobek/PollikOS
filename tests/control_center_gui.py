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
                    assert time.monotonic() < deadline, (name, [(p,c,picture.getpixel(p)) for p,c in checks])
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
            samples = [(x + 180, y + 222), (x + 190, y + 380), (x + 170, y + 410)]
            theme = g.words('g_dark_theme' if args.theme == 'dark' else 'g_light_theme')
            def mix(a, b, weight):
                return sum(((((a >> shift) & 255) * (256 - weight) +
                             ((b >> shift) & 255) * weight) // 256) << shift
                           for shift in (16, 8, 0))
            def rgb(color):
                return tuple((color >> shift) & 255 for shift in (16, 8, 0))
            tint = mix(theme[3], theme[9], 6)
            card_tint = mix(tint, theme[2], 160)
            def glass_color(slot, point):
                values = g.words('glass_backdrops')[slot*6:slot*6+6]
                address, capacity, bx, by, bw, bh = values
                assert address and capacity and bw and bh, 'glass cache missing'
                colors = []
                for dy in range(-3, 4):
                    row = max(0, min(bh-1, point[1]-by+dy))
                    data = g.memory(address+row*bw*4, bw*4)
                    line = [int.from_bytes(data[max(0,min(bw-1,point[0]-bx+dx))*4:][:4], 'little')
                            for dx in range(-3,4)]
                    colors.append(tuple((sum((color>>shift)&255 for color in line)+3)//7
                                        for shift in (16,8,0)))
                blurred = sum(((sum(row[c] for row in colors)+3)//7)<<shift
                              for c,shift in enumerate((16,8,0)))
                return mix(blurred,tint,160)
            g.wait(lambda: g.words('glass_backdrops')[6] != 0, 'glass cache allocated')
            g.move(x+8,y+90)
            panel_point = (x + 196, y + 30)
            panel = rgb(glass_color(1,panel_point))
            stable = [rgb(mix(glass_color(1,p),card_tint,136)) if i != 1 else rgb(glass_color(1,p))
                      for i,p in enumerate(samples)]
            base, _ = snapshot('main', [(panel_point,panel), *zip(samples, stable)])
            assert panel != rgb(tint), 'panel is opaque instead of glass'
            assert base.getpixel((x, y)) != base.getpixel((x + 196, y + 30)), 'square panel corner'
            assert fx >= x + 400 + 16 and fx + 248 <= g.width - 12, 'flyout outside screen'
            for kind, point, name, popup_top in (
                (1, (x + 166, y + 40), 'wifi', 18),
                (2, (x + 352, y + 40), 'bluetooth', 18),
                (3, (x + 360, y + 310), 'audio', 228),
                (4, (x + 70, y + 412), 'profile', 248),
            ):
                click(*point)
                g.wait(lambda: value('cc_list') == kind and value('cc_detail_kind') == kind
                       and value('cc_detail_slide') == 256, name + ' flyout opens')
                height = 204 if kind == 3 else 184 if kind == 4 else 176
                g.wait(lambda: g.words('glass_backdrops')[18] != 0 and
                       g.words('glass_backdrops')[23] == height+6, 'flyout glass painted')
                g.move(point[0]+1,point[1])
                picture, path = snapshot(name, (
                    ((fx + 124, y + popup_top + 8), rgb(glass_color(3,(fx + 124,y + popup_top + 8)))),
                    ((fx + 124, y + popup_top + height - 8), rgb(glass_color(3,(fx + 124,y + popup_top + height - 8)))),
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
