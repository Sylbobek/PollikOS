"""Exercise the real desktop calculator and terminal without writing user data."""
import argparse,time,re
from pathlib import Path
from PIL import Image
from gui_metrics import Guest,BUILD
from calc_layout import button_rect

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data',type=Path,required=True)
    p.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    p.add_argument('--resolution',default='1920x1080')
    a=p.parse_args()
    with Guest(a.resolution,'calculator',data_image=a.data,boot_only=True,accel=a.accel) as g:
        g.wait(lambda:'[TEST] PMM no leak' in g.log.read_text(),'boot stress',120)
        def type_text(s):
            mapping={' ':'spc','+':'shift-equal','*':'shift-8','(':'shift-9',')':'shift-0','^':'shift-6','%':'shift-5','/':'slash','.':'dot','-':'minus',',':'comma'}
            for c in s:g.hmp('sendkey '+mapping.get(c,c)+' 1');time.sleep(.09)
        type_text('test123');g.hmp('sendkey ret 1')
        g.wait(lambda:'AUTH: login accepted' in g.log.read_text(),'fixture login',30)
        g.key('f3',lambda:g.words('g_focused_window')[0]==2,'Terminal')
        g.wait(lambda:not g.words('g_window_anims',2)[0],'Terminal animation')
        for expr,wanted in [('(2+3)*4','20'),('sqrt(81)','9'),('200*10%','20'),('1/0','Error: Division by zero')]:
            start=len(g.log.read_text());type_text('calc '+expr);g.hmp('sendkey ret 1')
            g.wait(lambda:'SHELL END' in g.log.read_text()[start:],'calc command',15)
            text=g.log.read_text()[start:]
            print(text.strip(),flush=True)
            assert re.search(r'\n'+re.escape(wanted)+r'\nSHELL END',text),text
        type_text('calculator');g.hmp('sendkey ret 1')
        g.wait(lambda:g.words('g_focused_window')[0]==7,'Calculator window')
        g.wait(lambda:not g.words('g_window_anims',7)[0],'Calculator animation')
        w=g.window(7)
        for i in (0,8,11,10,23):
            x,y,bw,bh=button_rect(w[4],w[5],34,i)
            g.move(w[2]+x+bw//2,w[3]+y+bh//2)
            g.hmp('mouse_button 1');time.sleep(.09);g.hmp('mouse_button 0');time.sleep(.09)
        g.wait(lambda:'[CALC] 63\n' in g.log.read_text(),'button result')
        print('[CALC] 63 (buttons: C, 7, *, 9, =)',flush=True)
        # Escape is the desktop's global minimize key; clear through C.
        x,y,bw,bh=button_rect(w[4],w[5],34,0)
        g.move(w[2]+x+bw//2,w[3]+y+bh//2)
        g.hmp('mouse_button 1');time.sleep(.09);g.hmp('mouse_button 0');time.sleep(.12)
        type_text('2^3^2');g.hmp('sendkey ret 1')
        time.sleep(1)
        raw=g.memory(*g.symbol('model'))
        print(f'RAW keyboard model expression={raw[:128].split(bytes([0]))[0]!r} result={raw[128:224].split(bytes([0]))[0]!r} focus={g.words("g_focused_window")[0]}',flush=True)
        assert 'calc_probe' not in g.symbols,'production build must exclude diagnostic probes'
        g.wait(lambda:'[CALC] 512\n' in g.log.read_text(),'keyboard result')
        print('[CALC] 512 (keyboard: 2^3^2)',flush=True)
        path=BUILD/f'calculator-{a.resolution}.ppm';g.screendump(path)
        img=Image.open(path);img.save(path.with_suffix('.png'))
        assert img.size==(g.width,g.height)
        print(f'SCREENSHOT {path.with_suffix(".png")} size={img.size}',flush=True)
        print('PASS calculator: terminal expressions/errors, GUI buttons/keyboard, real framebuffer',flush=True)
if __name__=='__main__':main()
