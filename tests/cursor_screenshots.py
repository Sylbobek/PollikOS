"""Real PS/2 Settings changes and exact sprite/LFB checks on disposable guests."""
import argparse
import importlib.util
import os
from pathlib import Path
import struct
import time
from PIL import Image
from gui_metrics import Guest, ROOT, BUILD
from pollikfs_install import PollikFsImage

parser=argparse.ArgumentParser()
parser.add_argument('--resolution',choices=('1024x768','1920x1080'),required=True)
args=parser.parse_args()
os.environ['POLLIK_GUI_IMAGE']='PollikOS-Surface.img'
spec=importlib.util.spec_from_file_location('cursor_generator',ROOT/'assets/build_cursor.py')
art=importlib.util.module_from_spec(spec);spec.loader.exec_module(art)
names={0:'arrow',1:'ibeam',2:'hand',3:'resize_ew',4:'resize_ns',5:'resize_nwse',6:'resize_nesw',7:'busy',8:'move',9:'not_allowed'}
hotspots=dict(art.SPRITES)
out=BUILD/'cursor-shots'/args.resolution;out.mkdir(parents=True,exist_ok=True)
with Guest(args.resolution,'cursor-followup') as g:
    g.wait(lambda:g.words('cursor_previous_valid')[0], 'initial cursor')
    assert g.words('g_cursor_scale_index')[0]==0,'default size must be 100%'
    previous=None
    def move(x,y):
        # Single non-overflowing PS/2 packet at a time. Account for the real
        # modest acceleration formula, then require the exact consumed delta.
        end=time.monotonic()+20
        accelerated=g.words('pointer_acceleration')[0]
        while g.pointer()!=(x,y):
            assert time.monotonic()<end,('pointer target',x,y,g.pointer())
            mx,my=g.pointer()
            def delta(distance):
                raw=int(distance*5/6) if accelerated and abs(distance)>=7 else distance
                return max(-100,min(100,raw))
            dx,dy=delta(x-mx),delta(y-my)
            sx,sy=dx,dy
            if accelerated and max(abs(dx),abs(dy))>=6:sx+=int(dx/5);sy+=int(dy/5)
            expected=(max(0,min(g.width-1,mx+sx)),max(0,min(g.height-1,my+sy)))
            g.hmp(f'mouse_move {dx} {dy}')
            g.wait(lambda:g.pointer()==expected,'non-overflowing PS/2 packet not consumed')
    def check(label,kind=None,save=False):
        global previous
        # The scene buffer is updated before its LFB submission. Observe the
        # completed, idle Dock animation rather than stopping midway through it.
        g.wait(lambda:not any(g.words('g_dock_motion_active')),'Dock animation completion',20)
        time.sleep(.4)
        g.wait(lambda:g.words('cursor_previous_x')[0]==g.pointer()[0] and g.words('cursor_previous_y')[0]==g.pointer()[1], 'cursor not presented')
        g.qmp('stop')
        try:
            x,y=g.pointer();actual_kind=g.words('cursor_previous_kind')[0]
            if kind is not None:assert actual_kind==kind,(label,actual_kind,kind)
            percent=art.SCALES[g.words('g_cursor_scale_index')[0]];side=32*percent//100
            sprite=art.render(names[actual_kind],percent)
            hx,hy=hotspots[names[actual_kind]];hx=round(hx*percent/100);hy=round(hy*percent/100)
            flipx=actual_kind==0 and x-hx+side>g.width
            flipy=actual_kind==0 and y-hy+side>g.height
            if flipx:hx=side-1
            if flipy:hy=side-1
            left,top=x-hx,y-hy
            scene=struct.unpack('<'+'I'*(g.width*g.height),g.memory(g.words('pixels')[0],g.width*g.height*4))
            lfb,pitch,bpp=(g.words(n)[0] for n in ('address','stride','bytes'))
            assert bpp==4
            raw=g.memory(lfb,pitch*g.height)
            current=(max(0,left),max(0,top),min(g.width,left+side),min(g.height,top+side))
            regions=[current]+([previous] if previous else [])
            checked=visible=0
            for x1,y1,x2,y2 in regions:
                for py in range(y1,y2):
                    for px in range(x1,x2):
                        expected=scene[py*g.width+px]
                        sx,sy=px-left,py-top
                        if 0<=sx<side and 0<=sy<side:
                            if flipx:sx=side-1-sx
                            if flipy:sy=side-1-sy
                            pixel=sprite[sy*side+sx];alpha=pixel>>24
                            if alpha:
                                visible+=1;a=(alpha*256+127)//255
                                expected=sum((((((expected>>shift)&255)*(256-a)+((pixel>>shift)&255)*a)>>8)&255)<<shift for shift in (0,8,16))
                        actual=struct.unpack_from('<I',raw,py*pitch+px*4)[0]&0xffffff
                        assert actual==expected,(label,px,py,hex(expected),hex(actual))
                        checked+=1
            assert visible>0,(label,'invisible sprite')
            previous=current
            if save:
                shot=out/f'{label}.ppm';g.qmp('screendump',{'filename':str(shot)})
                image=Image.open(shot);assert image.size==(g.width,g.height)
                image.save(shot.with_suffix('.png'))
            print(f'PASS {args.resolution} {label}: kind={actual_kind} size={percent}% hotspot=({x},{y}) exact_LFB_pixels={checked} visible_samples={visible}',flush=True)
        finally:g.qmp('cont')
    def choose(percent):
        g.key('f5',lambda:g.words('g_focused_window')[0]==4,'Settings focus')
        g.wait(lambda:g.presented(4),'Settings surface',20)
        w=g.window(4)
        move(w[2]+80,w[3]+132);g.button(True);g.button(False)
        g.wait(lambda:g.words('g_settings_tab')[0]==1,'Desktop & Dock tab')
        move(w[2]+188+14+art.SCALES.index(percent)*72+32,w[3]+469)
        g.button(True);g.button(False)
        g.wait(lambda:g.words('g_cursor_scale_index')[0]==art.SCALES.index(percent),'size setting')
        time.sleep(.2);check(f'switch-{percent}')
        g.qmp('stop')
        try:
            saved=PollikFsImage.load(g.folder/'data.img').read_file('/home/.config/appearance.conf').decode()
            assert f'cursor_size={percent}\n' in saved,('size persistence',percent,saved)
        finally:g.qmp('cont')
        print(f'PASS persisted cursor_size={percent} after real Settings click',flush=True)
        g.hmp('sendkey alt-f4 1')
        g.wait(lambda:not g.window(4)[7],'close Settings')
    for percent in (100,150):
        if percent!=100:choose(percent)
        move(g.width-100,200);check(f'arrow-{percent}',0,True)
        for x,y in ((0,0),(g.width-1,0),(0,g.height-1),(g.width-1,g.height-1)):
            move(x,y)
            check(f'corner-{percent}-{x}-{y}',0)
        g.key('f4',lambda:g.words('g_focused_window')[0]==3,'Notes focus')
        g.wait(lambda:g.presented(3),'Notes surface')
        w=g.window(3)
        move(w[2]+250,w[3]+190);check(f'ibeam-{percent}',1,True)
        stationary=g.pointer();state=w[6]
        g.key('f11',lambda:g.window(3)[6]!=state,'maximize Notes')
        time.sleep(.4);assert g.pointer()==stationary;check(f'moving-window-{percent}')
        g.key('f11',lambda:g.window(3)[6]==state,'restore Notes')
        time.sleep(.4);check(f'restored-window-{percent}')
        w=g.window(3);move(w[2]+w[4]-1,w[3]+w[5]//2)
        check(f'resize-{percent}',3,True)
        move(g.width//2,g.height-60);check(f'dock-{percent}')
        g.hmp('sendkey alt-f4 1');g.wait(lambda:not g.window(3)[7],'close Notes')
    for percent in (125,200,100):choose(percent)
    g.qmp('quit');g.process.wait(timeout=5)
    config=PollikFsImage.load(g.folder/'data.img').read_file('/home/.config/appearance.conf').decode()
    assert 'cursor_size=100\n' in config
    print('PERSIST '+repr(config),flush=True)
print(f'PASS cursor screenshots and Settings persistence: {args.resolution}; PNGs={out}',flush=True)
