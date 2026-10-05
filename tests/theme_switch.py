"""Theme-coupled wallpapers: exact pixels, no repeated PNG work, stable RAM."""
import argparse
from pathlib import Path
import time
from PIL import Image
from gui_metrics import Guest

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--data',type=Path,required=True)
p.add_argument('--accel',default='tcg',choices=('tcg','whpx'))
p.add_argument('--resolution',default='1024x768')
a=p.parse_args()
with Guest(a.resolution,'theme-switch',data_image=a.data,boot_only=True,accel=a.accel) as g:
    print('COMMAND '+g.accel+' snapshot GUI '+str(g.image)+' data='+str(a.data),flush=True)
    for k in 'test123': g.hmp('sendkey '+k+' 1');time.sleep(.08)
    g.hmp('sendkey ret 1')
    g.wait(lambda:'AUTH: login accepted' in g.log.read_text(),'login',30)
    g.key('f5',lambda:g.words('g_focused_window')[0]==4,'Settings')
    g.wait(lambda:not g.words('g_window_anims',4)[0],'Settings settled')
    initial=g.log.read_text()
    counts=lambda s:tuple(s.count('[WALLPAPER] '+m) for m in ('path=','decode_ticks=','scale_ticks=','cache=ready'))
    baseline=g.words('pmm_free_page_count')[0]
    for n in range(6):
        theme=n%2
        w=g.window(4)
        g.move(w[2]+188+(180 if theme==0 else 65),w[3]+142)
        start=time.perf_counter();g.button(True);g.button(False)
        g.wait(lambda:g.words('shell')[4]==theme and g.words('wallpaper_theme')[0]==theme,'theme presented',60)
        elapsed=(time.perf_counter()-start)*1000
        image=Image.open(g.image.parent.parent/'assets'/('Background_LightTheme.png' if theme else 'Background_BlackTheme.png')).convert('RGB')
        iw,ih=image.size;sw,sh=g.width,g.height
        vw,vh=iw,ih;cx=cy=0
        if iw*sh>ih*sw: vw=ih*sw//sh;cx=(iw-vw)//2
        else: vh=iw*sh//sw;cy=(ih-vh)//2
        for x,y in ((sw-40,sh//2),(sw-60,sh-160),(sw-120,sh-210)):
            r,b,c=image.getpixel((cx+(((vw<<16)//sw*x)>>16),cy+(((vh<<16)//sh*y)>>16)))
            expected=(r<<16)|(b<<8)|c
            actual=g.backbuffer_pixel(x,y)
            print(f'PIXEL theme={theme} x={x} y={y} expected={expected:06x} actual={actual:06x}',flush=True)
            assert actual==expected,(theme,x,y,hex(actual),hex(expected))
        current=counts(g.log.read_text())
        print(f'SWITCH {n} theme={theme} observer_ms={elapsed:.2f} PNG_counts={current} baseline_counts={counts(initial)} free_pages={g.words("pmm_free_page_count")[0]}',flush=True)
        assert current==counts(initial),'theme switch repeated disk/decode/scale work'
        assert g.words('pmm_free_page_count')[0]==baseline,'theme cache leaked pages'
    print('PASS 6 theme switches: stock PNG pixels exact, no disk/decode/scale, PMM unchanged',flush=True)

