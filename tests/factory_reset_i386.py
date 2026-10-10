"""Legacy Settings factory reset through real PS/2 input on disposable data."""
from pathlib import Path
import time,sys
from PIL import Image
from gui_metrics import Guest,ROOT,BUILD
sys.path.insert(0,str(ROOT/'sdk/tools'))
from pollikfs_install import PollikFsImage
with Guest('1024x768',label='factory-reset32',headless=True,accel='tcg',cpu='qemu64',boot_timeout=120) as g:
 def scalar(name):return g.words(name)[0]
 def click(x,y):g.move(x,y);g.button(True);g.button(False);time.sleep(.2)
 def key(name):g.hmp('sendkey '+name);time.sleep(.15)
 g.wait(lambda:'[TEST] PHASE 2 PASS' in g.log.read_text(),'boot PMM stress',120)
 # Open the existing Settings app through the actual Control Center search.
 click(512,15);g.wait(lambda:scalar('cc_shown') and not scalar('cc_animating'),'Control Center')
 click(132,98)
 for char in 'settings':key(char)
 key('ret');g.wait(lambda:scalar('g_focused_window')==4,'Settings focus')
 x,y=g.window(4)[2:4];click(x+78,y+276)
 g.wait(lambda:scalar('g_settings_tab')==5,'System page')
 click(x+260,y+474);g.wait(lambda:scalar('reset_confirm')==1,'reset confirmation')
 shot=BUILD/'factory-reset-i386-confirm.ppm';g.screendump(shot);Image.open(shot).save(shot.with_suffix('.png'))
 click(x+235,y+370);g.wait(lambda:not scalar('reset_confirm'),'Cancel leaves data')
 click(x+260,y+474);click(x+410,y+370)
 g.wait(lambda:scalar('admin_app')==4,'administrator password requested')
 key('esc');g.wait(lambda:scalar('admin_app')==0xffffffff,'administrator cancelled')
 click(x+410,y+370);g.wait(lambda:scalar('admin_app')==4,'administrator prompt again')
 for char in 'test123':key(char)
 key('ret');g.wait(lambda:'AUTH: administrator application authorized' in g.log.read_text(),'authorized Settings',30)
 click(x+410,y+370)
 g.wait(lambda:'RESET: factory reset complete; restarting' in g.log.read_text(),'factory reset finishes',60)
 if g.process.poll() is None:g.process.terminate();g.process.wait(timeout=10)
 fs=PollikFsImage.load(g.folder/'data.img')
 for path in ('/etc/account.db','/etc/pollikos-installed','/etc/factory-reset.pending'):assert not fs.exists(path),path
 assert not fs.directory_entries(fs.resolve_directory('/home')),'Legacy preferences retained'
 print('FACTORY_RESET_I386_PASS real Settings/Cancel/password/reset, account/preferences removed on disposable disk',flush=True)
