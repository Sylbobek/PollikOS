"""USB HID through the real x64 kernel, UART setup and disposable guest disk."""
import argparse,json,os,shutil,struct,subprocess,tempfile,time
from pathlib import Path
from x86_64_security_session import port,connect,RecordingConsole
ROOT=Path(__file__).resolve().parents[1];BUILD=Path(os.environ.get('POLLIK_X64_BUILD',ROOT/'build/x86_64/kernel'))

def run(controller,tablet):
    ctl={'xhci':'qemu-xhci','uhci':'piix3-usb-uhci','ohci':'pci-ohci','ehci':'usb-ehci'}[controller]
    driver='usb-tablet' if tablet else 'usb-mouse'
    with tempfile.TemporaryDirectory(prefix='pollikos-usb64-') as temporary:
        folder=Path(temporary);disk=folder/'data.img';shutil.copyfile(BUILD/'PollikData-test.img',disk)
        serial,qmp_port=port(),port()
        process=subprocess.Popen(['qemu-system-x86_64','-accel','tcg','-cpu','qemu64','-m','256',
            '-vga','std','-display','none','-nic','none','-no-reboot',
            '-device',ctl+',id=usb','-device',driver+',id=pointer,bus=usb.0','-device','usb-kbd,id=keyboard,bus=usb.0',
            '-qmp',f'tcp:127.0.0.1:{qmp_port},server=on,wait=off',
            '-serial',f'tcp:127.0.0.1:{serial},server=on,wait=on',
            '-drive',f'file={BUILD/"PollikOS-x86_64.img"},format=raw,if=ide,index=0,snapshot=on',
            '-drive',f'file={disk},format=raw,if=ide,index=1'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        stream=monitor=qmp=None
        try:
            stream=connect(serial);c=RecordingConsole(stream,ROOT/f'build/usb64-{controller}-{driver}.log')
            monitor=connect(qmp_port);qmp=monitor.makefile('rwb',buffering=0);qmp.readline()
            def call(command,args=None):
                qmp.write((json.dumps({'execute':command,'arguments':args or {}})+'\n').encode())
                while True:
                    raw=qmp.readline();assert raw,'QEMU exited: '+process.stderr.read().decode(errors='replace')
                    reply=json.loads(raw);assert 'error' not in reply,reply
                    if 'return' in reply:return reply['return']
            call('qmp_capabilities')
            c.wait_for('Create account name:',180);c.send('usbuser')
            c.wait_for('Create password (6-63 characters):',180);c.send('test123')
            c.wait_for('Confirm password:',180);c.send('test123')
            c.wait_for('[desktop] ready',180)
            c.wait_for('USB HID pointer ready',30);c.wait_for('USB HID keyboard ready',30)
            symbols={}
            for line in subprocess.check_output(['llvm-nm','-S',str(BUILD/'kernel.elf')],text=True).splitlines():
                p=line.split()
                if len(p)==4:symbols.setdefault(p[3],[]).append((int(p[0],16),int(p[1],16)))
            def memory(name):
                assert len(symbols[name])==1;address,size=symbols[name][0];probe=folder/'probe.bin'
                call('pmemsave',{'val':address,'size':size,'filename':str(probe)});return probe.read_bytes()
            def scalar(name):return struct.unpack('<I',memory(name))[0]
            def wait(predicate,message):
                end=time.monotonic()+20
                while not predicate():
                    assert process.poll() is None and time.monotonic()<end,message;c.pump(.03)
            def event(events):call('input-send-event',{'events':events})
            mice=call('human-monitor-command',{'command-line':'info mice'})
            for line in mice.splitlines():
                if ('QEMU HID Tablet' if tablet else 'QEMU HID Mouse') in line:
                    index=line.split('Mouse #')[1].split(':')[0];call('human-monitor-command',{'command-line':'mouse_set '+index});break
            else:raise AssertionError(mice)
            before=(scalar('mouse_x'),scalar('mouse_y'))
            if tablet:
                event([{'type':'abs','data':{'axis':'x','value':10000}},{'type':'abs','data':{'axis':'y','value':12000}}])
                wait(lambda:abs(scalar('mouse_x')-round(10000*1023/32767))<3,'USB absolute x')
            else:event([{'type':'rel','data':{'axis':'x','value':40}},{'type':'rel','data':{'axis':'y','value':25}}])
            wait(lambda:(scalar('mouse_x'),scalar('mouse_y'))!=before,'USB pointer movement')
            for down in (True,False):
                event([{'type':'btn','data':{'button':'left','down':down}}]);wait(lambda:scalar('usb_buttons')==int(down),'USB button')
            head=scalar('key_head')
            for down in (True,False):event([{'type':'key','data':{'down':down,'key':{'type':'qcode','data':'a'}}}]);time.sleep(.08)
            wait(lambda:scalar('key_head')==(head+2)%128,'USB keyboard consumed')
            keys=memory('key_queue');assert keys[head]==30 and keys[(head+1)%128]==158
            ready=c.text().count('USB HID pointer ready');call('device_del',{'id':'pointer'});time.sleep(.5)
            call('device_add',{'driver':driver,'id':'pointer2','bus':'usb.0'})
            wait(lambda:c.text().count('USB HID pointer ready')>ready,'USB hotplug')
            print('PASS x64 USB '+controller+': '+driver+' movement/buttons, keyboard, unplug/replug',flush=True)
        finally:
            if qmp:qmp.close()
            if monitor:monitor.close()
            if stream:stream.close()
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--controller',choices=['xhci','uhci','ohci','ehci'],default='xhci');p.add_argument('--tablet',action='store_true');a=p.parse_args();run(a.controller,a.tablet)
