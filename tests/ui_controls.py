"""Real PS/2 sliders and live benchmark resize; snapshot data disk only."""
import argparse,struct,time
from pathlib import Path
from PIL import Image
from gui_metrics import Guest,BUILD

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data',type=Path,required=True)
    p.add_argument('--resolution',default='1920x1080')
    p.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    a=p.parse_args()
    with Guest(a.resolution,'ui-controls',data_image=a.data,boot_only=True,accel=a.accel) as g:
        g.wait(lambda:'[TEST] PMM no leak' in g.log.read_text(),'boot stress',120)
        for c in 'test123':g.hmp('sendkey '+c+' 1');time.sleep(.09)
        g.hmp('sendkey ret 1');g.wait(lambda:'AUTH: login accepted' in g.log.read_text(),'login',30)
        def key(k):
            tick=g.words('ticks')[0];g.hmp('sendkey '+k+' 1')
            g.wait(lambda:g.words('ticks')[0]-tick>=3,'key consumed')
        def click(x,y):g.move(x,y);g.button(True);g.button(False)
        def scalar(n):return g.words(n)[0]
        def byte(n):return g.memory(*g.symbol(n))[0]
        key('f5');g.wait(lambda:scalar('g_focused_window')==4,'Settings focus')
        g.wait(lambda:not g.words('g_window_anims',4)[0],'Settings animation')
        w=g.window(4);x,y,width,height=w[2:6]
        click(x+80,y+80+3*36+16) # Sound tab
        left=x+212;right=x+width-41;track_y=y+234
        g.move(left,track_y);g.button(True);g.wait(lambda:byte('s_volume')==0,'volume minimum')
        g.move(right,track_y);g.wait(lambda:byte('s_volume')==100,'volume maximum')
        g.move((left+right)//2,track_y);g.wait(lambda:49<=byte('s_volume')<=51,'volume middle')
        middle=byte('s_volume');g.button(False)
        print(f'RAW volume slider: left=0 right=100 middle={middle}',flush=True)
        shot=BUILD/f'settings-volume-{a.resolution}.ppm';g.screendump(shot)
        Image.open(shot).save(shot.with_suffix('.png'))
        click(x+80,y+80+36+16) # Desktop & Dock
        g.move(left,y+502);g.button(True);g.wait(lambda:scalar('g_cursor_scale_index')==0,'cursor minimum')
        g.move(right,y+502);g.wait(lambda:scalar('g_cursor_scale_index')==3,'cursor maximum')
        g.button(False)
        print('RAW cursor slider: left=100 right=200',flush=True)
        shot=BUILD/f'settings-cursor-{a.resolution}.ppm';g.screendump(shot)
        Image.open(shot).save(shot.with_suffix('.png'))
        key('f7');g.wait(lambda:scalar('g_focused_window')==6,'PollikMark focus')
        g.wait(lambda:not g.words('g_window_anims',6)[0],'PollikMark animation')
        key('1');g.wait(lambda:scalar('pollikmark_running')==1,'Fill start')
        # Forty native changes also cover allocation failure; this guest check
        # proves actual input-dispatch resize capture reaches that path.
        w=g.window(6);edge_x=w[2]+w[4]-2;edge_y=w[3]+w[5]-2
        g.move(edge_x,edge_y);g.button(True)
        g.wait(lambda:scalar('g_resized_window')==6,'resize capture')
        size=scalar('surface_bytes');start=g.memory(*g.symbol('level_start'))
        done_before=scalar('pollikmark_completed');changes=0
        for i in range(1,13):
            g.move(edge_x+i*3,edge_y+i*2);changes+=1
            if scalar('pollikmark_running'):
                assert scalar('surface_bytes')==size,'workload reallocated while resizing'
                assert g.memory(*g.symbol('level_start'))==start,'measurement reset while resizing'
        g.button(False)
        g.wait(lambda:scalar('pollikmark_running')==0,'Fill finishes despite resize',45)
        addr,_=g.symbol('pollikmark_results');r=struct.unpack('<7IQIQ5I',g.memory(addr,68))
        assert r[0]==1 and r[1]>=3 and r[7]>0,r
        assert scalar('pollikmark_completed')==done_before+1
        print(f'RAW guest live resize: changes={changes} status={r[0]} samples={r[1]} rate={r[7]} px/s window={g.window(6)[4:6]}',flush=True)
        key('1');g.wait(lambda:scalar('pollikmark_running')==1,'second Fill start')
        start=g.memory(*g.symbol('level_start'));size=scalar('surface_bytes')
        key('f11');g.wait(lambda:g.window(6)[6]==2,'maximize')
        assert g.memory(*g.symbol('level_start'))==start and scalar('surface_bytes')==size
        key('f11');g.wait(lambda:g.window(6)[6]==0,'restore')
        assert g.memory(*g.symbol('level_start'))==start and scalar('surface_bytes')==size
        g.wait(lambda:scalar('pollikmark_running')==0,'Fill finishes across maximize/restore',45)
        r=struct.unpack('<7IQIQ5I',g.memory(addr,68));assert r[0]==1 and r[1]>=3,r
        print(f'RAW benchmark maximize/restore: status={r[0]} samples={r[1]} rate={r[7]} px/s',flush=True)
        key('f3');g.wait(lambda:scalar('g_focused_window')==2,'Terminal')
        start=len(g.log.read_text())
        for c in 'cat /home/.config/appearance.conf':
            k={' ':'spc','/':'slash','.':'dot'}.get(c,c)
            g.hmp('sendkey '+k+' 1');time.sleep(.09)
        key('ret');g.wait(lambda:'SHELL END' in g.log.read_text()[start:],'settings readback')
        readback=g.log.read_text()[start:]
        assert 'audio_volume=50\n' in readback and 'cursor_size=200\n' in readback,readback
        print('RAW settings file: audio_volume=50 cursor_size=200 (guest VFS readback)',flush=True)
        log=g.log.read_text();assert 'PANIC' not in log and 'GUI MEMORY CORRUPTION' not in log
        print('PASS PS/2 sliders: clamps/persistence; benchmark completes during resize/maximize/restore',flush=True)
if __name__=='__main__':main()
