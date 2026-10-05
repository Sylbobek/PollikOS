"""Snapshot boot a specified data image, or pixel-check a synced GUI fixture."""
import argparse
import hashlib
import os
from pathlib import Path
from PIL import Image
from gui_metrics import Guest, ROOT, BUILD

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--data', type=Path)
parser.add_argument('--resolution', default='1920x1080')
args = parser.parse_args()
os.environ['POLLIK_GUI_IMAGE'] = 'PollikOS-Alpha.img'
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''): h.update(chunk)
    return h.hexdigest()
before = digest(args.data) if args.data else None
with Guest(args.resolution, 'wallpaper-persistent-probe' if args.data else 'wallpaper-probe',
           data_image=args.data, boot_only=bool(args.data)) as g:
    if args.data and 'AUTH: login required' in g.log.read_text():
        for key in 'test123':
            g.hmp('sendkey ' + key)
        g.hmp('sendkey ret')
        g.wait(lambda: 'AUTH: login accepted' in g.log.read_text(), 'snapshot desktop login', 60)
    for line in g.log.read_text().splitlines():
        if 'WALLPAPER' in line or 'wallpaper unavailable' in line or 'superblock mounted' in line:
            print(line, flush=True)
    log_text = g.log.read_text()
    path_line = next((line for line in log_text.splitlines()
                      if line.startswith('[WALLPAPER] path=')), None)
    assert path_line, 'wallpaper loader did not report the selected path'
    selected = path_line.split('path=', 1)[1].split()[0].rsplit('/', 1)[-1]
    stock_assets = {
        'light.png': ROOT / 'assets/Background_LightTheme.png',
        'dark.png': ROOT / 'assets/Background_BlackTheme.png',
    }
    if selected in stock_assets:
        image = Image.open(stock_assets[selected]).convert('RGBA')
        iw, ih = image.size
        vw, vh, cx, cy = iw, ih, 0, 0
        if iw*g.height > ih*g.width: vw=ih*g.width//g.height; cx=(iw-vw)//2
        else: vh=iw*g.height//g.width; cy=(ih-vh)//2
        step_x, step_y = (vw << 16)//g.width, (vh << 16)//g.height
        cache = g.words('wallpaper')[0]
        for x, y in ((25,100),(g.width-25,110),(25,g.height//2),
                     (g.width-25,g.height//2),(g.width-25,g.height-190)):
            sx, sy = cx+(x*step_x >> 16), cy+(y*step_y >> 16)
            r, green, b, a = image.getpixel((sx,sy))
            assert a == 255
            expected = r<<16 | green<<8 | b
            cached = int.from_bytes(g.memory(cache+(y*g.width+x)*4,4),'little')
            actual = g.backbuffer_pixel(x,y)
            assert cached == actual == expected, (x,y,hex(expected),hex(cached),hex(actual))
            print(f'PIXEL {x},{y} source={sx},{sy} expected={expected:06x} cache={cached:06x} scene={actual:06x}',flush=True)
        assert 'GFX wallpaper unavailable' not in log_text
        shot = BUILD / f'wallpaper-synced-{selected[:-4]}-{args.resolution}.ppm'
        points=((25,100),(g.width-25,110),(g.width-25,g.height-190))
        lfb,pitch,bpp=(g.words(n)[0] for n in ('address','stride','bytes'))
        # Account setup's marker precedes the first desktop presentation. Wait
        # for the physical LFB, then take one coherent screenshot observation.
        g.wait(lambda:all(g.memory(lfb+y*pitch+x*bpp,bpp)[:3] ==
                         g.backbuffer_pixel(x,y).to_bytes(4,'little')[:3]
                         for x,y in points),'wallpaper LFB presentation',20)
        g.qmp('stop')
        try:
            g.qmp('screendump', {'filename':str(shot)})
            frame=Image.open(shot)
            for x,y in points:
                expected=g.backbuffer_pixel(x,y)
                assert frame.getpixel((x,y)) == ((expected>>16)&255,(expected>>8)&255,expected&255)
            frame.save(shot.with_suffix('.png'))
        finally:g.qmp('cont')
        print(f'PASS synced wallpaper {selected}: five independent cache/scene pixel samples; three LFB samples; fallback absent')
    else:
        assert args.data, f'unexpected non-stock wallpaper selection: {selected}'
        print(f'NOT RUN stock pixel comparison: selected custom wallpaper {selected}')
if args.data:
    after=digest(args.data)
    assert after==before
    print(f'SNAPSHOT original SHA256 before={before} after={after} bytes={args.data.stat().st_size}')
