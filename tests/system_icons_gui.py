"""Real filesystem PNG cache, Control Center input and brightness pixel proof."""
import argparse,time,struct
from PIL import Image
from gui_metrics import Guest,BUILD,ROOT

def main():
    p=argparse.ArgumentParser();p.add_argument('--resolution',default='1920x1080');p.add_argument('--accel',default='whpx',choices=('tcg','whpx'));a=p.parse_args()
    with Guest(a.resolution,'system-icons',headless=True,accel=a.accel) as g:
        g.wait(lambda:'[TEST] PMM no leak' in g.log.read_text(),'boot stress',120)
        names=('welcome','files','terminal','notes','settings','browser','pollikmark','calculator','folder','folder-blue','file','trash')
        pointers=g.words('icon_pixels');assert len(pointers)==12 and all(pointers)
        for name,address in zip(names,pointers):
            expected=Image.open(ROOT/'assets/system-icons'/(name+'.png')).convert('RGBA').tobytes()
            assert g.memory(address,len(expected))==expected,name
        assert all(name not in g.symbols for name in ('icons_index','desktop_icons_index','calculator_icon','pollikmark_icon'))
        print('PASS 12 filesystem icons: 196608 decoded RGBA bytes identical to PNG oracle; no embedded bitmap symbols',flush=True)
        def value(n):return g.words(n)[0]
        def click(x,y):g.move(x,y);g.button(True);g.button(False)
        click(g.width//2,15);g.wait(lambda:value('cc_shown')==1,'Control Center opens')
        g.wait(lambda:value('cc_animating')==0,'slide completes',15)
        assert value('cc_position')==0
        x=g.width//2-200;y=38
        shot=BUILD/f'control-center-{a.resolution}.ppm';g.screendump(shot);image=Image.open(shot);image.save(shot.with_suffix('.png'))
        reference=image.getpixel((g.width-50,g.height//2))
        click(x+30,y+390);g.wait(lambda:g.memory(*g.symbol('s_volume'))[0]==0,'volume minimum')
        click(x+200,y+390);g.wait(lambda:49<=g.memory(*g.symbol('s_volume'))[0]<=51,'volume midpoint')
        click(x+115,y+304);g.wait(lambda:value('brightness_percent')==40,'brightness 40')
        time.sleep(.3);g.screendump(shot);dim=Image.open(shot).getpixel((g.width-50,g.height//2))
        expected=tuple((c*40+50)//100 for c in reference)
        assert all(abs(actual-wanted)<=1 for actual,wanted in zip(dim,expected)),(reference,dim,expected)
        print(f'PIXEL brightness: at={(g.width-50,g.height//2)} before={reference} after={dim} expected={expected}',flush=True)
        click(x+369,y+304);g.wait(lambda:value('brightness_percent')==100,'brightness restored')
        g.hmp('sendkey esc 1');g.wait(lambda:value('cc_shown')==0,'Control Center closes',15)
        g.move(g.width-120,g.height//2);g.hmp('mouse_button 2');time.sleep(.1);g.hmp('mouse_button 0')
        g.wait(lambda:g.words('g_active_menu')[0]==1,'context menu')
        time.sleep(.2);shot=BUILD/f'context-menu-{a.resolution}.ppm';g.screendump(shot);Image.open(shot).save(shot.with_suffix('.png'))
        menu=g.words('g_active_menu');mx,my,count=menu[1],menu[2],menu[5];offset=my+6
        for i in range(count):
            kind,action=menu[6+i*6:8+i*6]
            if action==6: # ACTION_ABOUT_POLLIKOS, existing stable UI action ID
                click(mx+70,offset+17);break
            offset+=9 if kind==1 else 34
        else:raise AssertionError('About action missing')
        g.wait(lambda:g.words('g_active_dialog')[0]==1,'About dialog')
        time.sleep(.3);shot=BUILD/f'about-system-{a.resolution}.ppm';g.screendump(shot);Image.open(shot).save(shot.with_suffix('.png'))
        log=g.log.read_text();assert 'PANIC' not in log and 'GUI MEMORY CORRUPTION' not in log
        print('PASS Control Center: PS/2 open/close, audio slider, brightness pixels, themed context menu/About',flush=True)
if __name__=='__main__':main()
