"""All seven REAL apps: PS/2 resize + viewport pixels + scene/runtime LFB.

No guest writes/calls, no production build, no whole-screenshot equality.
Native app_layout.py separately checks exact reflow/columns/hit bounds. QEMU
checks resized client content, not just Window/Surface metadata or titlebars.
"""
import argparse
import json
import struct
from gui_metrics import Guest, BUILD
from surface_support import checked_surface_symbols, check_guards
import app_layout

NAMES = ('Welcome', 'Files', 'Terminal', 'Notes', 'Settings', 'Browser', 'PollikMark3D')


def run(resolution):
    symbols, count = checked_surface_symbols()
    report = dict(status='RUNNING', resolution=resolution, stages=[])
    path = BUILD / f'resize-layout-{resolution}.json'
    try:
        with Guest(resolution, 'resize-layout') as g:
            report['environment'] = g.environment
            assert count == g.apps == 7

            def scalar(name):
                return g.words(name)[0]

            def idle():
                g.wait(lambda: not any(g.words('g_animations')[::17]), 'animations settle', 30)

            def key(name):
                tick = scalar('ticks')
                g.hmp('sendkey ' + name + ' 1')
                g.wait(lambda: scalar('ticks') - tick >= 3, 'key release ticks')

            def focus(app):
                key(f'f{app+1}')
                g.wait(lambda: scalar('g_focused_window') == app and g.window(app)[7:9] == (1, 0), 'app focus', 30)
                idle()
                g.wait(lambda: g.presented(app), 'app presented', 30)
                if app == 5:
                    # BrowserApp prefix: 24-byte geometry, URL/input512,
                    # cursor/focus16, title/status128, pending URL256.
                    g.wait(lambda: not scalar('load_active') and not any(g.words('g_browser')[234:236]), 'browser home loaded', 30)

            def drag(x, y, tx, ty):
                g.move(x, y)
                g.button(True)
                g.move(tx, ty)
                g.button(False)
                g.wait(lambda: scalar('g_dragged_window') == 0xffffffff and
                       scalar('g_resized_window') == 0xffffffff, 'drag/resize released')

            def place(app):
                w = g.window(app)
                # Titlebar point remains clear of top/left snap activation zones.
                drag(w[2]+120, w[3]+17, 128, 53)
                g.wait(lambda: g.window(app)[2:4] == (8, 36), 'top-left placement')
                g.wait(lambda: g.presented(app), 'placed client presented')

            def resize(app, targets, rapid=False):
                w = g.window(app)
                x, y, ww, hh = w[2:6]
                g.move(x+ww-1, y+hh-1)
                g.button(True)
                for width, height in targets:
                    g.move(x+width-1, y+height-1)
                    expected = (max(width, 640), max(height, 410)) if app == 4 else (width, height)
                    g.wait(lambda: g.window(app)[4:6] == expected, 'PS/2 resize geometry')
                    if not rapid:
                        g.wait(lambda: g.presented(app), 'held resize painted', 30)
                g.button(False)
                g.wait(lambda: scalar('g_resized_window') == 0xffffffff, 'resize release')
                g.move(2, g.height-2)  # Keep cursor outside all client probes.

            def content(app, stage):
                idle()
                g.wait(lambda: g.presented(app), f'{stage}: surface/scene/runtime LFB', 30)
                if app == 3:
                    for _ in range(8):
                        key('pgup')
                    g.wait(lambda: scalar('note_scroll') == 0, 'Notes viewport top')
                observed = {}

                def inspect():
                    w, s = g.window(app), g.words('g_surfaces', app)
                    width, height = w[4:6]
                    if s[1:5] != (width, height, width, width):
                        return False
                    raw = g.memory(s[0], width*height*4)
                    pixels = memoryview(raw).cast('I')
                    probes = []
                    def exact(x, y, color, label):
                        probes.append((x, y, color, label))
                    def ink(rect, color):
                        x1, y1, x2, y2 = rect
                        return sum(pixels[y*width+x] == color for y in range(y1, y2) for x in range(x1, x2))
                    if app == 0:
                        cw = (width-80)//3
                        cy = height-(110 if height<410 else 142)
                        ch = 72 if height<410 else 76
                        for k in range(3):
                            exact(32+k*(cw+8)+cw//2, cy+ch-5, 0xf0ecf6, 'resized Welcome card')
                    elif app == 1:
                        left = 146 if width<600 else 196
                        exact(width-42, 130, 0xeae2f4, 'Files selected row right edge')
                        exact(60, height-23, 0xf0edf5, 'Files sidebar bottom')
                        observed['list_width'] = width-left-32
                        observed['visible_rows'] = min(8, (height-162)//31)
                    elif app == 2:
                        cols = (width-68)//12
                        shown = min(48, cols-1)
                        observed['estimated_columns'] = shown
                        # The prompt now follows scrollback from the top, so
                        # check the real prompt/input colors across the viewport
                        # instead of assuming a fixed bottom input row.
                        observed['prompt_ink'] = ink((18,48,width-24,height-36), 0xc8b6ef)
                        observed['command_ink'] = ink((18,48,width-24,height-36), 0xf4f3fa)
                        if not observed['prompt_ink'] or not observed['command_ink']:
                            return False
                    elif app == 3:
                        exact(width-40, 60, 0xe5dcf1, 'Notes Save button in editor header')
                        rows = max(1, (height-150)//20)
                        observed['editor_rows'] = rows
                        occupied = [r for r in range(rows) if ink((24, 89+r*20, width-33, 109+r*20), 0x50445e)]
                        observed['painted_text_rows'] = occupied
                        # Fixture has >40 lines, so the editor must paint content
                        # below the old 280px/410px viewport as it grows.
                        if not occupied or max(occupied) < min(rows-1, 30):
                            return False
                    elif app == 4:
                        exact(211, 148, 0x2563eb, 'Settings selected theme outline')
                        exact(width-50, 350, 0xffffff, 'Settings material-card padding')
                    elif app == 5:
                        exact(width-55, height-25, 0xffffff, 'Browser viewport bottom-right')
                        exact(width-40, height-5, 0xf4f2f8, 'Browser bottom status strip')
                        exact(width-50, 69, 0xcbc5d8, 'Browser address bar right extent')
                    else:
                        exact(20, 85, 0x405574, 'PollikMark selected workload row')
                        exact(width-20, height-12, 0x202b40, 'PollikMark responsive status background')
                        y = 212 if height>=410 else 80
                        label = (190, y, min(width-8, 325), y+18)
                        observed['level_label_ink'] = ink(label, 0x64718c) + ink(label, 0x8595b0)
                        if not observed['level_label_ink']:
                            return False
                    lfb, pitch, bpp = (scalar(n) for n in ('address', 'stride', 'bytes'))
                    assert bpp in (3, 4) and pitch >= g.width*bpp
                    scene = scalar('pixels')
                    observed['probes'] = []
                    success = True
                    for x, y, color, label in probes:
                        sx, sy = w[2]+x, w[3]+y
                        a = pixels[y*width+x]
                        b = int.from_bytes(g.memory(scene+(sy*g.width+sx)*4, 4), 'little')
                        c = int.from_bytes(g.memory(lfb+sy*pitch+sx*bpp, bpp), 'little')
                        observed['probes'].append(dict(label=label, xy=[x,y], expected=color, actual=[a,b,c]))
                        success &= a == b == c == color
                    return success

                try:
                    g.wait(inspect, f'{NAMES[app]} {stage}: content did not reach viewport/LFB', 30)
                except AssertionError:
                    print('CONTENT FAILURE', NAMES[app], stage, json.dumps(observed), flush=True)
                    g.qmp('screendump', {'filename': str(BUILD / f'resize-layout-failed-{resolution}.ppm')})
                    report['failure_observed'] = observed
                    raise
                check_guards(lambda a, n: struct.unpack('<'+'I'*(n//4), g.memory(a,n)),
                             symbols, count, g.width, g.height)
                entry = dict(app=app, name=NAMES[app], stage=stage, window=list(g.window(app)[2:6]), content=observed)
                report['stages'].append(entry)
                path.write_text(json.dumps(report, indent=2))
                print('PASS', resolution, NAMES[app], stage, entry['window'], flush=True)

            # Populate through real input; no private memory writes.
            focus(2)
            for i in range(48):
                key('w')
                g.wait(lambda: scalar('cmdlen') == i+1, 'Terminal fixture character')
            focus(3)
            for _ in range(24):
                old = scalar('note_len')
                key('ret'); key('w')
                g.wait(lambda: scalar('note_len') == old+2, 'Notes fixture line')
            key('ctrl-c'); key('ctrl-v')
            for app in range(count):
                focus(app)
                if app == 1:
                    key('down')  # The Files probe below checks the selected-row fill.
                place(app)
                for size in ((480,280), (640,480), (800,600), (g.width-16,g.height-142)):
                    resize(app, [size])
                    content(app, f'{size[0]}x{size[1]}')
                resize(app, [(640,480), (800,600), (480,280), (800,600), (640,480)], rapid=True)
                content(app, 'rapid-held-resize-final')
                original = g.window(app)[2:6]
                key('f11')
                g.wait(lambda: g.window(app)[6] == 2, 'maximized state')
                content(app, 'maximized')
                key('f11')
                g.wait(lambda: g.window(app)[6] == 0 and g.window(app)[2:6] == original, 'restored geometry')
                content(app, 'restored')
            text = g.log.read_text()
            assert all(error not in text for error in ('KERNEL PANIC','[GUI MEMORY CORRUPTION]','[WM] FATAL','PAGE FAULT')), text
            report['status'] = 'PASS'
    except BaseException as error:
        report['status'] = 'FAIL'
        report['error'] = repr(error)
        raise
    finally:
        path.write_text(json.dumps(report, indent=2))
    print(f'PASS {resolution}: all seven apps, 49 viewport-content stages, external guards', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--resolution', choices=['1024x768', '1920x1080'])
    parser.add_argument('--skip-native', action='store_true', help='Only run QEMU; native must be reported separately')
    args = parser.parse_args()
    if not args.skip_native:
        app_layout.main()
    for resolution in ([args.resolution] if args.resolution else ['1024x768', '1920x1080']):
        run(resolution)
