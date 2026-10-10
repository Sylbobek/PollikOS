"""Real USB controllers/HID reports in disposable i386 QEMU guests."""
import argparse,os,time
from gui_metrics import Guest

def run(controller,tablet):
    controllers={'xhci':['-device','qemu-xhci,id=usb'],'uhci':['-device','piix3-usb-uhci,id=usb'],'ohci':['-device','pci-ohci,id=usb'],'ehci':['-device','usb-ehci,id=usb']}
    device='usb-tablet' if tablet else 'usb-mouse'
    devices=controllers[controller]+['-device',device+',id=pointer,bus=usb.0']
    with Guest('1024x768','usb-'+controller+'-'+device,boot_timeout=150,input_devices=devices) as g:
        g.wait(lambda:'USB HID pointer ready' in g.log.read_text(),'USB pointer enumerated')
        g.qmp('device_add',{'driver':'usb-kbd','id':'keyboard','bus':'usb.0'})
        g.wait(lambda:'USB HID keyboard ready' in g.log.read_text(),'USB keyboard enumerated')
        def scalar(n):return g.words(n)[0]
        print(g.hmp('info mice'),flush=True)
        # Select the actual USB pointer rather than the built-in PS/2 mouse.
        for line in g.hmp('info mice').splitlines():
            if ('QEMU HID Tablet' if tablet else 'QEMU HID Mouse') in line:
                index=line.split('Mouse #')[1].split(':')[0];g.hmp('mouse_set '+index);break
        else:raise AssertionError('USB pointer missing from QEMU')
        g.hmp('mouse_move 40 25');g.wait(lambda:scalar('mx')!=760 or scalar('my')!=500,'USB movement')
        g.button(True);g.wait(lambda:scalar('usb_buttons')==1,'USB press');g.button(False);g.wait(lambda:scalar('usb_buttons')==0,'USB release')
        assert scalar('ps2_buttons')==0
        if tablet:
            g.qmp('input-send-event',{'events':[{'type':'abs','data':{'axis':'x','value':16384}},{'type':'abs','data':{'axis':'y','value':16384}}]})
            g.wait(lambda:abs(scalar('mx')-512)<3 and abs(scalar('my')-384)<3,'absolute tablet position')
        # QEMU broadcasts keys to PS/2 and USB; verify the independent USB scan queue.
        def move(x,y):
            if not tablet:return g.move(x,y)
            g.qmp('input-send-event',{'events':[{'type':'abs','data':{'axis':'x','value':round(x*32767/1023)}},{'type':'abs','data':{'axis':'y','value':round(y*32767/767)}}]})
            g.wait(lambda:abs(scalar('mx')-x)<3 and abs(scalar('my')-y)<3,'tablet movement')
        move(512,15);g.button(True);g.button(False)
        g.wait(lambda:scalar('cc_shown') and not scalar('cc_animating'),'Control Center')
        move(132,98);g.button(True);g.button(False)
        for ch in 'calc':
            head=scalar('key_head')
            for down in (True,False):
                g.qmp('input-send-event',{'events':[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':ch}}}]});time.sleep(.04)
            g.wait(lambda:scalar('key_head')==(head+2)%128,'USB key press/release consumed')
            address,size=g.symbol('key_queue');raw=g.memory(address,size);scan={'c':46,'a':30,'l':38}[ch];assert raw[head]==scan and raw[(head+1)%128]==scan|128
        address,size=g.symbol('cc_input');query=g.memory(address,32).split(b'\0')[0];assert query in (b'calc',b'ccaallcc'),query
        g.qmp('device_del',{'id':'pointer'});g.wait(lambda:not scalar('usb_buttons'),'unplug release');time.sleep(.5)
        ready=g.log.read_text().count('USB HID pointer ready')
        g.qmp('device_add',{'driver':device,'id':'pointer2','bus':'usb.0'})
        g.wait(lambda:g.log.read_text().count('USB HID pointer ready')>ready,'USB hotplug')
        print('PASS i386 USB '+controller+': '+device+' movement/buttons, keyboard search, unplug/replug',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--controller',choices=['xhci','uhci','ohci','ehci'],default='xhci');parser.add_argument('--tablet',action='store_true');args=parser.parse_args()
    os.environ.setdefault('POLLIK_GUI_IMAGE','PollikOS-input-QA.img');run(args.controller,args.tablet)
