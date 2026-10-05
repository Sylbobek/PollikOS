"""Check polished chrome/dock pixels and capture the real guest framebuffer."""
import argparse
from pathlib import Path
import time
from PIL import Image
from gui_metrics import Guest, BUILD
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--data', type=Path, required=True)
parser.add_argument('--theme', choices=('light','dark'), required=True)
parser.add_argument('--resolution', default='1920x1080')
args=parser.parse_args()
def blend(a,b,t):
    return sum(((((a>>s)&255)*(256-t)+((b>>s)&255)*t)>>8)<<s for s in (0,8,16))
with Guest(args.resolution, 'ui-polish-'+args.theme, data_image=args.data,
           boot_only=True, headless=True) as g:
    assert 'AUTH: login required' in g.log.read_text()
    for key in 'test123':
        g.hmp('sendkey '+key+' 1')
        time.sleep(.08)
    g.hmp('sendkey ret 1')
    g.wait(lambda:'AUTH: login accepted' in g.log.read_text(), 'fixture login', 30)
    g.key('f5',lambda:g.words('g_focused_window')[0]==4,'Settings focus',)
    g.wait(lambda:not g.words('g_window_anims',4)[0], 'Settings animation completion')
    dark=args.theme=='dark'
    base=0x171b28 if dark else 0xf2eff6
    highlight=blend(base,0xffffff,22 if dark else 130)
    def samples():
        w=g.window(4)
        return [(w[2]+w[4]-40,w[3]+1,highlight,'title-highlight'),
                (w[2]+18,w[3]+17,0xef4444,'close-control'),
                (g.width//2,g.height-96,0x3d465c if dark else 0xf4f5ff,'dock-outline')]
    g.wait(lambda:all(g.backbuffer_pixel(x,y)==c for x,y,c,_ in samples()),
           'polished chrome and dock not presented',20)
    lfb,pitch,bpp=(g.words(n)[0] for n in ('address','stride','bytes'))
    g.wait(lambda:all(g.memory(lfb+y*pitch+x*bpp,bpp)[:3]==c.to_bytes(4,'little')[:3]
                     for x,y,c,_ in samples()),'physical LFB polish pixels',20)
    g.qmp('stop')
    try:
        shot=BUILD/f'ui-polish-{args.theme}-{args.resolution}.ppm'
        g.screendump(shot)
        frame=Image.open(shot)
        for x,y,c,name in samples():
            expected=((c>>16)&255,(c>>8)&255,c&255)
            assert frame.getpixel((x,y))==expected,(name,frame.getpixel((x,y)),expected)
            print(f'PIXEL {name} {x},{y} expected={c:06x} screenshot={c:06x}',flush=True)
        frame.save(shot.with_suffix('.png'))
    finally:g.qmp('cont')
    print(f'PASS UI polish {args.theme} {args.resolution}: Settings focus, completed animation, exact scene/LFB/screenshot chrome and dock pixels')
