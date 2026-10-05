"""Rapid real PS/2 close/reopen; no guest memory writes or real data disk writes."""
import argparse
from pathlib import Path
import time
from gui_metrics import Guest
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--data',type=Path,required=True)
p.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
a=p.parse_args()
with Guest('1024x768','window-spam',data_image=a.data,boot_only=True,accel=a.accel) as g:
    print('COMMAND GUI snapshot accel='+a.accel+' data='+str(a.data),flush=True)
    for k in 'test123':g.hmp('sendkey '+k+' 1');time.sleep(.08)
    g.hmp('sendkey ret 1')
    g.wait(lambda:'AUTH: login accepted' in g.log.read_text(),'login',30)
    g.key('f3',lambda:g.window(2)[7]==1,'Terminal open')
    g.wait(lambda:not g.words('g_window_anims',2)[0] and g.presented(2),'initial settled')
    g.wait(lambda:'[TEST] PMM no leak' in g.log.read_text(),'boot spawn/reap complete',120)
    baseline=g.words('pmm_free_page_count')[0]
    for i in range(20):
        g.hmp('sendkey alt-f4 1')
        g.wait(lambda:g.window(2)[7]==0,'logical close')
        anim=g.words('g_window_anims',2)
        assert anim[0] and anim[1]==4,('close vanished before reversal',i,anim)
        assert anim[15]>0 and anim[16]>0 and 0<=anim[17]<=256,anim
        g.hmp('sendkey f3 1')
        # wm_open publishes open before the client resize/open callbacks and
        # animation setup finish. A running-vCPU probe can see that interval.
        g.wait(lambda:g.window(2)[7]==1 and g.words('g_window_anims',2)[1]==1,
               'reopen animation setup completed')
        anim=g.words('g_window_anims',2)
        assert anim[0] and anim[1]==1,('reopen animation absent',i,anim)
        assert anim[15]>0 and anim[16]>0 and 0<=anim[17]<=256,anim
    g.wait(lambda:not g.words('g_window_anims',2)[0] and g.presented(2),'final surface/scene/LFB')
    assert g.window(2)[7:11]==(1,0,1,1),g.window(2)
    free=g.words('pmm_free_page_count')[0]
    assert free==baseline,(baseline,free)
    assert 'PANIC' not in g.log.read_text() and 'canary corrupted' not in g.log.read_text()
    print(f'PASS 20 real close/open reversals during active animations; final focus/surface/scene/LFB; free_pages {baseline} -> {free}',flush=True)
