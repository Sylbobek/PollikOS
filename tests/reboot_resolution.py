"""Warm restart must preserve guest mode, pitch and rendered wallpaper pixels."""
import argparse,time
from pathlib import Path
from PIL import Image
from gui_metrics import Guest,BUILD
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--data',type=Path,required=True)
p.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
p.add_argument('--resolution',default='1920x1080')
a=p.parse_args()
with Guest(a.resolution,'reboot-resolution',data_image=a.data,boot_only=True,accel=a.accel,allow_reboot=True) as g:
    print(f'COMMAND snapshot GUI accel={a.accel} resolution={a.resolution} data={a.data}',flush=True)
    def login():
        g.wait(lambda:g.log.read_text().count('[TEST] PMM no leak')>boot_count,
               'boot process stress complete before login',120)
        accepted=g.log.read_text().count('AUTH: login accepted')
        for k in 'test123':g.hmp('sendkey '+k+' 1');time.sleep(.08)
        g.hmp('sendkey ret 1')
        g.wait(lambda:g.log.read_text().count('AUTH: login accepted')>accepted,'fixture login',30)
        time.sleep(.3)
    def scalar(name):
        return tuple(int.from_bytes(g.memory(addr,size),'little') for addr,size in g.symbols[name])
    def sample(label):
        g.wait(lambda:g.words('shell')[:2]==(g.width,g.height),'mode',30)
        # Decode caches and the compositor's initial presentation must finish.
        g.wait(lambda:g.log.read_text().count('desktop ready')>boot_count,'desktop ready after reset',90)
        g.screendump(BUILD/f'reboot-resolution-{label}.ppm')
        img=Image.open(BUILD/f'reboot-resolution-{label}.ppm')
        print(f'MODE {label}: size={img.size} screen={scalar("screen_w")},{scalar("screen_h")} pitch={g.words("stride")[0]} bpp={g.words("bytes")[0]*8}',flush=True)
        assert img.size==(g.width,g.height)
        assert g.words('stride')[0]==g.width*4 and g.words('bytes')[0]==4
        return img
    boot_count=0;login();time.sleep(.5);before=sample('before')
    g.key('f3',lambda:g.words('g_focused_window')[0]==2,'Terminal')
    g.wait(lambda:not g.words('g_window_anims',2)[0],'Terminal animation')
    boot_count=g.log.read_text().count('desktop ready')
    for k in 'reboot':g.hmp('sendkey '+k+' 1');time.sleep(.1)
    g.hmp('sendkey ret 1')
    g.wait(lambda:g.log.read_text().count('AUTH: login required')>=2,'reboot login screen',90)
    login();after=sample('after')
    for x,y in ((g.width-50,g.height//2),(g.width-120,g.height-160)):
        print(f'PIXEL {x},{y} before={before.getpixel((x,y))} after={after.getpixel((x,y))}',flush=True)
        assert before.getpixel((x,y))==after.getpixel((x,y))
    print('PASS warm reboot preserves resolution, pitch, 32bpp and desktop pixels',flush=True)
