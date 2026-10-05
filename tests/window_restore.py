"""Exercise real minimize/restore without assuming a fixed Dock app list."""
import argparse
from pathlib import Path
import time
from gui_metrics import Guest
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--data',type=Path,required=True)
args=parser.parse_args()
with Guest('1024x768','window-restore',data_image=args.data,boot_only=True) as g:
    for key in 'test123':
        g.hmp('sendkey '+key+' 1');time.sleep(.08)
    g.hmp('sendkey ret 1')
    g.wait(lambda:'AUTH: login accepted' in g.log.read_text(),'fixture login',30)
    g.key('f3',lambda:g.words('g_focused_window')[0]==2,'Terminal focus')
    g.wait(lambda:not g.words('g_window_anims',2)[0] and g.presented(2),'Terminal presented')
    g.key('f11',lambda:g.window(2)[6]==2 and g.presented(2),'maximize presented')
    before=g.window(2)[2:7]
    g.move(before[0]+36,before[1]+17)
    g.button(True);g.button(False)
    g.wait(lambda:not g.words('g_window_anims',2)[0],'minimize animation finished')
    w=g.window(2)
    print('MINIMIZE window='+str(w)+' prior='+str(g.words('g_pre_minimized_state')[2]),flush=True)
    assert w[6:11]==(1,1,1,0,0),w
    assert g.words('g_pre_minimized_state')[2]==2
    assert g.words('g_minimized_rect')[8:12]==before[:4]
    assert w[2:6]==before[:4]
    g.key('f3',lambda:g.words('g_focused_window')[0]==2,'restored Terminal focus')
    g.wait(lambda:not g.words('g_window_anims',2)[0] and g.presented(2),'restore presented')
    after=g.window(2)
    assert after[2:7]==before,(before,after)
    assert after[8:11]==(0,1,1),after
    print('PASS maximized Terminal minimize/restore: state and authoritative rectangle retained; focus handed off; full surface/scene/LFB restored',flush=True)
