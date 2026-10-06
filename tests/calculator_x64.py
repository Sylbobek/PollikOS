"""Native Ring 3 x86-64 calculator, real shell and PS/2 framebuffer evidence."""
import argparse,json,socket,subprocess,tempfile,time
from pathlib import Path
from PIL import Image
from x86_64_console import Console,ROOT,BUILD
from calc_layout import button_rect

def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--data',type=Path,default=BUILD/'PollikData-test.img');parser.add_argument('--kernel',type=Path,default=BUILD/'PollikOS-x86_64.img');options=parser.parse_args()
    oracle=BUILD/'calculator-text-oracle.exe';reference=BUILD/'calculator-reference.ppm'
    compile=['clang','-O2','-Wall','-Wextra','-Werror','-fuse-ld=lld','tests/calculator_text_oracle.c','-o',str(oracle)]
    print('COMMAND '+subprocess.list2cmdline(compile),flush=True);subprocess.run(compile,cwd=ROOT,check=True)
    subprocess.run([str(oracle),'5',str(reference)],check=True)
    expected=Image.open(reference).crop((0,44,328,60)).tobytes()
    serial_port,qmp_port=port(),port()
    cmd=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64','-m','256M','-vga','std','-nic','none','-display','none','-no-reboot',
         '-drive',f'file={options.kernel.resolve()},format=raw,if=ide,index=0,snapshot=on',
         '-drive',f'file={options.data.resolve()},format=raw,if=ide,index=1,snapshot=on',
         '-serial',f'tcp:127.0.0.1:{serial_port},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qmp_port},server=on,wait=off']
    print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
    proc=subprocess.Popen(cmd,cwd=ROOT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    stream=q=None;c=None
    try:
        end=time.monotonic()+30
        while stream is None:
            try:stream=socket.create_connection(('127.0.0.1',serial_port),timeout=1)
            except OSError:assert time.monotonic()<end;time.sleep(.05)
        q=socket.create_connection(('127.0.0.1',qmp_port),timeout=5);f=q.makefile('rwb',buffering=0);f.readline()
        def call(command,args=None):
            f.write((json.dumps({'execute':command,'arguments':args or {}})+'\n').encode())
            while True:
                r=json.loads(f.readline());assert 'error' not in r,r
                if 'return' in r:return r['return']
        # Attach the serial reader before executing the first guest instruction.
        # A clean disk boots fast enough to lose early TCP serial bytes otherwise.
        call('qmp_capabilities');c=Console(stream);call('cont')
        c.wait_for('[AUTH64] First run: create a local account.',120)
        for value,prompt in [('calcuser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:
            c.send(value);c.wait_for(prompt,120)
        c.wait_for('[terminal] output active',120)
        c.wait_for_prompt(120)
        for expr,result in [('(2+3)*4','20'),('sqrt(81)','9'),('200*10%','20'),('2^3^2','512'),('1/0','Error: Division by zero')]:
            start=len(c.transcript);c.run('calc '+expr,expected='\n'+result+'\n')
            print(c.text()[start:].strip(),flush=True)
        start=len(c.transcript);c.send('/bin/calculator.pol')
        c.wait_for('[calculator] ready: native x86-64',120,start)
        def key(k):call('human-monitor-command',{'command-line':'sendkey '+k+' 1'});time.sleep(.14)
        for k in ('2','shift-equal','3','ret'):key(k)
        c.wait_for('[calculator] result=5',30,start)
        print('[calculator] result=5 (PS/2 keyboard)',flush=True)
        # UART result precedes drawing. The current native WM copies rows to
        # the LFB: a screendump may catch a partially restored underlay.
        shot=BUILD/'calculator-native.ppm';end=time.monotonic()+30
        while True:
            call('screendump',{'filename':str(shot)})
            img=Image.open(shot);width,height=img.size
            left=(width-364)//2;top=(height-500)//2
            result=img.crop((left+18,top+88,left+346,top+104)).tobytes()
            if img.getpixel((left+10,top+40))==(23,27,40) and result==expected:break
            assert time.monotonic()<end,('calculator presentation',img.getpixel((left+10,top+40)))
            time.sleep(.12)
        img.save(shot.with_suffix('.png'))
        xy=[width//2,height//2]
        def move(x,y):
            while xy!=[x,y]:
                dx=max(-90,min(90,x-xy[0]));dy=max(-90,min(90,y-xy[1]))
                call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},{'type':'rel','data':{'axis':'y','value':dy}}]})
                time.sleep(.12);xy[0]+=dx;xy[1]+=dy
        def click(x,y):
            move(x,y)
            for down in (True,False):
                call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]});time.sleep(.12)
        for i in (0,8,11,10,23):
            x,y,w,h=button_rect(360,466,0,i);click(left+2+x+w//2,top+32+y+h//2)
        c.wait_for('[calculator] result=63',30,start)
        print('[calculator] result=63 (PS/2 buttons)',flush=True)
        subprocess.run([str(oracle),'63',str(reference)],check=True)
        expected=Image.open(reference).crop((0,44,328,60)).tobytes();end=time.monotonic()+30
        while True:
            call('screendump',{'filename':str(shot)});img=Image.open(shot)
            if img.crop((left+18,top+88,left+346,top+104)).tobytes()==expected:break
            assert time.monotonic()<end,'rendered result 63';time.sleep(.12)
        img.save(shot.with_suffix('.png'))
        print('PIXELS result 5 and 63: 5248 RGB pixels each identical to SDK font oracle',flush=True)
        click(left+350,top+15);c.wait_for_prompt(30,start)
        c.run('echo $?',expected='\n0\n')
        assert '[X64] FAIL' not in c.text() and 'PANIC' not in c.text()
        print(f'SCREENSHOT {shot.with_suffix(".png")} size={img.size} client pixel={img.getpixel((left+10,top+40))}',flush=True)
        print('PASS native x86-64 calculator: shell, floating point, PS/2 keys/buttons, framebuffer, clean exit status 0',flush=True)
    finally:
        if c:(BUILD/'calculator-native.log').write_text(c.text(),encoding='utf-8')
        if stream:stream.close()
        if q:q.close()
        proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
if __name__=='__main__':main()
