"""Real PS/2 desktop search, profile logout and power actions on disposable disks."""
import argparse,json,shutil,subprocess,tempfile,time
from pathlib import Path
from PIL import Image
from gui_metrics import Guest,ROOT,BUILD
from x86_64_console import newest_image
from x86_64_security_session import port,connect,RecordingConsole

def shutdown_event(stream,reason):
    deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        raw=stream.readline()
        assert raw,'QMP closed before power event'
        reply=json.loads(raw)
        if reply.get('event')=='SHUTDOWN':
            assert reply['data']['guest'] and reply['data']['reason']==reason,reply
            print('PASS power event: '+reason,flush=True);return
    raise AssertionError('no guest power event')

def i386():
    for action in (0,1):
        with Guest('1024x768','desktop-menus-'+str(action),boot_timeout=120) as g:
            def scalar(name):return g.words(name)[0]
            def click(x,y):g.move(x,y);g.button(True);g.button(False)
            def key(name):g.hmp('sendkey '+name+' 1');time.sleep(.12)
            def open_panel():
                click(512,15);g.wait(lambda:scalar('cc_shown') and not scalar('cc_animating'),'Control Center opens')
            def profile():
                click(340+30,38+412);g.wait(lambda:scalar('cc_detail_kind')==4 and scalar('cc_detail_slide')==256,'profile slides open')
            open_panel()
            click(108+24,38+60)
            for ch in 'calc':key(ch)
            if action==0:
                key('ctrl-a');address,size=g.symbol('cc_input')
                import struct
                values=struct.unpack('<32sii',g.memory(address,size));assert values[1:]==(4,0),values
                g.wait(lambda:scalar('cc_blink')==0,'caret off');g.wait(lambda:scalar('cc_blink')==1,'caret on')
                key('backspace');g.wait(lambda:not g.memory(address,32).split(b'\0')[0],'selected query deleted')
                for ch in 'calc':key(ch)
                click(108+204,38+124);click(108+20,38+198);assert scalar('cc_pins')==128
                click(108+24,38+60);key('ctrl-a');key('backspace');assert scalar('cc_pins')==128
                print('PASS i386: caret blink, selection/replacement and saved recommendations',flush=True)
                click(108+40,38+124)
            else:key('ret')
            g.wait(lambda:scalar('g_focused_window')==7 and not scalar('cc_shown'),'search launches Calculator')
            if action==0:
                def terminal_command(text):
                    start=len(g.log.read_text());
                    for ch in text:key({' ':'spc','/':'slash','-':'minus'}.get(ch,ch))
                    key('ret');g.wait(lambda:'SHELL END' in g.log.read_text()[start:],'terminal command');return g.log.read_text()[start:]
                key('f3');g.wait(lambda:scalar('g_focused_window')==2,'Terminal opens')
                assert 'touch: failed' in terminal_command('touch /etc/admin-probe')
                open_panel();click(108+24,38+60)
                for ch in 'term':key(ch)
                click(108+204,38+124);click(108+20,38+164)
                g.wait(lambda:'AUTH: session locked' in g.log.read_text(),'administrator password')
                assert not any(g.words('admin_sessions',2))
                key('esc');open_panel();click(108+24,38+60)
                for ch in 'term':key(ch)
                click(108+204,38+124);click(108+20,38+164)
                for ch in 'test123':key(ch)
                key('ret');g.wait(lambda:'AUTH: administrator application authorized' in g.log.read_text(),'administrator authorized',30)
                g.wait(lambda:scalar('g_focused_window')==2,'administrator Terminal focus')
                assert any(g.words('admin_sessions',2))
                assert 'File ready.' in terminal_command('touch /etc/admin-probe')
                print('PASS i386: normal protected write denied, cancellation grants nothing, authenticated administrator write succeeds',flush=True)
            open_panel();profile()
            picture=BUILD/'desktop-menus-i386.ppm';g.screendump(picture);Image.open(picture).save(picture.with_suffix('.png'))
            if action==0:
                start=len(g.log.read_text())
                click(786,38+248+70+76);g.wait(lambda:'AUTH: session locked' in g.log.read_text()[start:],'profile logout')
                g.wait(lambda:all(not g.window(i)[7] for i in range(g.apps)),'logout closes all apps')
                start=len(g.log.read_text())
                for ch in 'test123':key(ch)
                key('ret');g.wait(lambda:'AUTH: login accepted' in g.log.read_text()[start:],'sign in after logout',30)
                open_panel();profile()
                print('PASS i386: actual app search, Calculator launch, logout/window cleanup and sign in',flush=True)
            g.move(786,38+248+70+action*38)
            g.hmp('mouse_button 1')
            shutdown_event(g.stream,'guest-reset' if action else 'guest-shutdown')

def x64():
    kernel=ROOT/'build/x86_64/kernel'
    with tempfile.TemporaryDirectory(prefix='pollikos-desktop-menus-') as folder:
        disk=Path(folder)/'data.img';shutil.copyfile(kernel/'PollikData-test.img',disk)
        for action in (0,1):
            serial,qmp_port=port(),port()
            process=subprocess.Popen(['qemu-system-x86_64','-accel','tcg','-cpu','qemu64','-m','128',
                '-vga','std','-display','none','-nic','none','-no-reboot',
                '-qmp',f'tcp:127.0.0.1:{qmp_port},server=on,wait=off',
                '-serial',f'tcp:127.0.0.1:{serial},server=on,wait=on',
                '-drive',f'file={newest_image(kernel)},format=raw,if=ide,index=0,snapshot=on',
                '-drive',f'file={disk},format=raw,if=ide,index=1'],stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            stream=monitor=qmp=None
            try:
                stream=connect(serial);monitor=connect(qmp_port);monitor.settimeout(30)
                c=RecordingConsole(stream,BUILD/f'desktop-menus-x64-{action}.log')
                qmp=monitor.makefile('rwb',buffering=0);qmp.readline()
                def call(command,args=None):
                    qmp.write((json.dumps({'execute':command,'arguments':args or {}})+'\n').encode())
                    while True:
                        reply=json.loads(qmp.readline());assert 'error' not in reply,reply
                        if 'return' in reply:return reply['return']
                call('qmp_capabilities')
                if action==0:
                    c.wait_for('Create account name:',180);c.send('menuuser')
                    c.wait_for('Create password (6-63 characters):',180);c.send('test123')
                    c.wait_for('Confirm password:',180);c.send('test123')
                    c.wait_for('Account created.',180)
                else:
                    c.wait_for('Username:',180);c.send('menuuser');c.wait_for('Password:',180);c.send('test123')
                c.wait_for('[desktop] ready',180);time.sleep(1)
                def key(name):call('human-monitor-command',{'command-line':'sendkey '+name+' 1'});time.sleep(.16);c.pump(.01)
                symbols={}
                for line in subprocess.check_output(['llvm-nm','-S',str(kernel/'kernel.elf')],text=True).splitlines():
                    fields=line.split()
                    if len(fields)==4 and fields[3] in ('mouse_x','mouse_y'):symbols[fields[3]]=int(fields[0],16)
                def position():
                    import struct
                    result=[]
                    for name in ('mouse_x','mouse_y'):
                        probe=Path(folder)/'pointer.bin';call('pmemsave',{'val':symbols[name],'size':4,'filename':str(probe)})
                        result.append(struct.unpack('<i',probe.read_bytes())[0])
                    return result
                def move(x,y):
                    deadline=time.monotonic()+15;mouse=position()
                    while mouse!=[x,y]:
                        assert time.monotonic()<deadline,('mouse did not reach',x,y,mouse)
                        dx=max(-90,min(90,x-mouse[0]));dy=max(-90,min(90,y-mouse[1]))
                        call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},
                             {'type':'rel','data':{'axis':'y','value':dy}}]})
                        before=mouse
                        while position()==before:
                            assert time.monotonic()<deadline,'PS/2 move not consumed';time.sleep(.04)
                        mouse=position()
                def button(down):call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]})
                def click(x,y):move(x,y);button(True);time.sleep(.1);button(False);time.sleep(.4)
                # Native window ABI limits the desktop to its 720x500 fallback.
                width,height=720,500
                ox,oy=(1024-width-4)//2+2,(768-height-34)//2+32
                def presented(point,color):
                    deadline=time.monotonic()+60
                    probe=Path(folder)/'frame.ppm'
                    while time.monotonic()<deadline:
                        call('screendump',{'filename':str(probe)})
                        if Image.open(probe).getpixel(point)==color:return
                        time.sleep(.2)
                    raise AssertionError(('desktop frame not presented',point,color))
                def open_panel():
                    click(ox+20,oy-16) # The desktop titlebar remains above Terminal.
                    click(ox+width//2,oy+15)
                    presented((ox+panel_x+190,oy+40+356),(0x34,0x39,0x4d))
                def profile():
                    click(ox+panel_x+30,oy+40+350)
                    presented((ox+panel_x+190,oy+40+235),(0x49,0x4c,0x63))
                search_x=(width-612)//2;panel_x=search_x+232
                open_panel()
                shot=BUILD/'desktop-menus-x64-search.ppm';call('screendump',{'filename':str(shot)})
                picture=Image.open(shot);picture.save(shot.with_suffix('.png'));assert picture.size==(1024,768),picture.size
                click(ox+search_x+20,oy+40+54)
                for ch in 'calc':key(ch)
                if action==0:
                    key('ctrl-a');presented((ox+search_x+17,oy+40+49),(0x68,0x54,0xbf))
                    key('backspace')
                    for ch in 'calc':key(ch)
                    click(ox+search_x+205,oy+40+124);click(ox+search_x+20,oy+40+192)
                    click(ox+search_x+20,oy+40+54);key('ctrl-a');key('backspace')
                    print('PASS x64: field selection/replacement and saved recommendations',flush=True)
                start=len(c.transcript);key('ret');c.wait_for('[calculator] ready',120,start)
                if action==0:
                    open_panel();click(ox+search_x+20,oy+40+54)
                    for ch in 'files':key(ch)
                    click(ox+search_x+205,oy+40+124)
                    start=len(c.transcript);click(ox+search_x+20,oy+40+162)
                    c.wait_for('Administrator password (Esc cancels):',120,start)
                    c.send('test123');c.wait_for('[desktop] administrator launched /bin/files.pol',120,start)
                    c.wait_for('[files] ready',120,start)
                    print('PASS x64: GUI Run as admin authenticates and launches native Files',flush=True)
                # Raise the desktop through its visible menu bar.
                open_panel();profile()
                shot=BUILD/'desktop-menus-x64.ppm';call('screendump',{'filename':str(shot)})
                Image.open(shot).save(shot.with_suffix('.png'))
                if action==0:
                    start=len(c.transcript);click(ox+panel_x+30,oy+40+234+80)
                    c.wait_for('[SESSION64] logged out; resources reclaimed',180,start)
                    c.wait_for('Username:',180,start);c.send('menuuser')
                    c.wait_for('Password:',180,start);c.send('test123')
                    c.wait_for('[desktop] ready',180,start);time.sleep(1)
                    open_panel();profile()
                    print('PASS x64: actual app search, Calculator launch, logout/resource cleanup and sign in',flush=True)
                move(ox+panel_x+30,oy+40+234+16+action*32)
                button(True);shutdown_event(qmp,'guest-reset' if action else 'guest-shutdown')
            finally:
                if qmp:qmp.close()
                if stream:stream.close()
                if monitor:monitor.close()
                if process.poll() is None:process.terminate()
                _,error=process.communicate(timeout=10)
                if error:print(error.decode(errors='replace')[-1000:])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--arch',choices=('i386','x64','both'),default='both');a=p.parse_args()
    if a.arch in ('i386','both'):i386()
    if a.arch in ('x64','both'):x64()
