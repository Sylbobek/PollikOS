"""Real guest Memory result: full 64-bit count/rate and exact UI formatting."""
import argparse
import os
import struct
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
p.add_argument('--resolution',default='1920x1080')
args=p.parse_args()
os.environ['POLLIK_GUI_ACCEL']=args.accel
from gui_metrics import Guest, BUILD
from PIL import Image

with Guest(args.resolution,'memory64',boot_timeout=180 if args.accel=='whpx' else 40) as g:
    g.wait(lambda: '[TEST] PHASE 2 PASS' in g.log.read_text(),'startup self-test',90)
    g.hmp('sendkey f7 100')
    g.wait(lambda: g.words('g_focused_window')[0]==6 and g.presented(6),'PollikMark ready',30)
    addr,size=g.symbol('pollikmark_results')
    assert size==8*30*68,('64-bit probe layout',size)
    def result():
        return struct.unpack('<7IQIQ5I',g.memory(addr+7*30*68,68))
    g.hmp('sendkey 8 100')
    g.wait(lambda: result()[0]==1,'Memory clear level completed',30)
    r=result()
    assert r[9]>0 and r[8]>0 and r[7]==r[9]*1000000//r[8],r
    assert r[7] not in (0,0xffffffff),r
    g.hmp('sendkey esc 100')
    g.wait(lambda: not g.words('pollikmark_running')[0],'memory stop')
    # Show the completed clear result, rather than a stale frame of the next
    # level. Browse through the actual UI and wait for a completed presentation.
    before_frame=g.stats()['frame_count']
    while g.words('pollikmark_level')[0]!=0:
        level=g.words('pollikmark_level')[0]
        g.hmp('sendkey p 100')
        g.wait(lambda: g.words('pollikmark_level')[0]==(level+29)%30,'previous Memory result')
    g.wait(lambda: g.stats()['frame_count']>before_frame and
           not g.words('dirty_client')[6],'completed Memory result presented')
    g.qmp('stop')
    path=BUILD/f'memory64-{args.accel}-{args.resolution}.ppm'
    g.qmp('screendump',{'filename':str(path)})
    Image.open(path).save(path.with_suffix('.png'))
    print(f'RAW Memory accel={args.accel} resolution={args.resolution} units={r[9]} work_us={r[8]} rate={r[7]} B/s samples={r[1]}',flush=True)
    print('PASS Memory rate equals full 64-bit units * 1000000 / measured work_us; no UINT32 saturation',flush=True)
