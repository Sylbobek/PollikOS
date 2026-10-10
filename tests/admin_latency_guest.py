"""Time a real administrator dialog frame on a disposable i386 guest."""
import argparse,json,os,time
from pathlib import Path
from PIL import Image
from gui_metrics import Guest,BUILD
def run(resolution):
    with Guest(resolution,'admin-latency',boot_timeout=150) as g:
        def scalar(n):return g.words(n)[0]
        def click(x,y):g.move(x,y);g.button(True);g.button(False)
        left=(g.width-400-220-12)//2+232;limit=g.width-400-248-16-20
        if limit>=248:left=min(left,limit)
        search=left-232
        click(g.width//2,15);g.wait(lambda:scalar('cc_shown') and not scalar('cc_animating'),'open Control Center')
        click(search+204,38+116+30+9);g.wait(lambda:scalar('cc_context')==2,'Terminal menu')
        g.move(left+16,38+146+60);start=time.perf_counter();g.hmp('mouse_button 1')
        g.wait(lambda:scalar('cancel_w')!=0,'administrator frame',30)
        elapsed=time.perf_counter()-start;g.button(False)
        picture=BUILD/('admin-latency-'+resolution+'.ppm');g.screendump(picture);Image.open(picture).save(picture.with_suffix('.png'))
        print(json.dumps({'resolution':resolution,'wall_ready_ms':round(elapsed*1000,1),'observer':'QMP layout probe; TCG guest, includes observer overhead'}),flush=True)
        g.hmp('sendkey esc 1');g.wait(lambda:scalar('admin_app')==0xffffffff,'cancel administrator request')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--resolution',default='1024x768');a=p.parse_args();os.environ.setdefault('POLLIK_GUI_IMAGE','PollikOS-input-QA.img');run(a.resolution)
