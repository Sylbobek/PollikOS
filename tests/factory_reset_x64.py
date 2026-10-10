"""Real Settings confirmation/authentication, deletion and next-boot setup.
All destructive actions operate on a disposable copy of the stock disk.
"""
from pathlib import Path
import shutil,subprocess,tempfile,json,time,sys,hashlib
from PIL import Image
from x86_64_console import ROOT,newest_image
from x86_64_security_session import port,connect,RecordingConsole
sys.path.insert(0,str(ROOT/'sdk/tools'))
from pollikfs_install import PollikFsImage
B=ROOT/'build/x86_64/system'
def main():
 subprocess.run(['powershell','-NoProfile','-File','sdk/tools/pollikcc.ps1','sdk/tests/factory_reset_rights.c','-o','build/factory_reset_rights.pol'],cwd=ROOT,check=True)
 with tempfile.TemporaryDirectory(prefix='pollik-factory-reset-') as tmp:
  disk=Path(tmp)/'data.img';shutil.copyfile(B/'PollikData-system.img',disk)
  fs=PollikFsImage.load(disk);fs.ensure_directory('/home/deep/nested');fs.ensure_directory('/home/.config');fs.ensure_directory('/tmp')
  fs.install_file('/home/personal.txt',b'keep until confirmed');fs.install_file('/home/deep/nested/private.txt',b'private')
  fs.install_file('/home/.config/appearance.conf',b'old preference');fs.install_file('/tmp/private.tmp',b'temporary')
  fs.install_file('/bin/reset_rights.pol',(ROOT/'build/factory_reset_rights.pol').read_bytes())
  executable=fs.read_file('/bin/pollish');fs.save(disk)
  for boot in range(4):
   if boot>=2:
    fs=PollikFsImage.load(disk);fs.ensure_directory('/tmp')
    fs.install_file('/home/interrupted.txt',b'personal data awaiting an authorized reset')
    fs.install_file('/etc/factory-reset.pending',b'PollikOS factory reset v1\n' if boot==2 else b'invalid reset request')
    fs.save(disk);before_bad=hashlib.sha256(disk.read_bytes()).digest()
   sp,qp=port(),port();proc=subprocess.Popen(['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64,+rdrand','-rtc','base=utc','-m','256','-vga','std','-display','none','-nic','none','-no-reboot',
    '-drive',f'file={newest_image(B)},format=raw,if=ide,index=0,snapshot=on','-drive',f'file={disk},format=raw,if=ide,index=1',
    '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
   stream=monitor=c=None
   try:
    stream=connect(sp);monitor=connect(qp);q=monitor.makefile('rwb',buffering=0);q.readline()
    def call(op,args=None):
     q.write((json.dumps({'execute':op,'arguments':args or {}})+'\n').encode())
     while True:
      reply=json.loads(q.readline());assert 'error' not in reply,reply
      if 'return' in reply:return reply['return']
    call('qmp_capabilities');c=RecordingConsole(stream,ROOT/f'build/factory-reset-boot{boot}.log');call('cont')
    if boot==3:
     c.wait_for('[RESET64] invalid reset request or read error; data unchanged',120)
     proc.terminate();proc.communicate(timeout=10)
     assert hashlib.sha256(disk.read_bytes()).digest()==before_bad,'Invalid reset marker changed the disk'
     print('PASS malformed reset request refuses boot without changing any disk byte',flush=True);continue
    c.wait_for('Create account name:',180);c.send('resetuser' if not boot else 'afterreset')
    c.wait_for('Create password (6-63 characters):',180);c.send('secret123');c.wait_for('Confirm password:',180);c.send('secret123');c.wait_for('Account created.',180);c.wait_for_prompt(240)
    if boot:
     c.run('id',expected='afterreset');c.run('which settings',expected='/bin/settings.pol')
     if boot==2:c.run('cat /home/interrupted.txt',expected='no such file or directory');print('PASS interrupted authorized reset resumes before first-run setup',flush=True)
     else:print('FACTORY_RESET_PASS Settings cancel/authentication, real personal-data/account removal, system files preserved and fresh account setup after reboot',flush=True)
     continue
    c.run('/bin/reset_rights.pol',expected='RESET_RIGHTS_PASS')
    mark=len(c.transcript);c.run('/bin/reset_rights.pol launch',expected='RESET_SETTINGS_LAUNCHED');c.wait_for('[settings] ready:',120,mark);time.sleep(2)
    mouse=[512,384]
    def click(x,y):
     while mouse!=[x,y]:
      dx=max(-90,min(90,x-mouse[0]));dy=max(-90,min(90,y-mouse[1]));call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},{'type':'rel','data':{'axis':'y','value':dy}}]});mouse[0]+=dx;mouse[1]+=dy;time.sleep(.08)
     for down in (True,False):call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]});time.sleep(.2)
     c.pump(.1)
    # 720x480 client, 724x514 outer, centered in 1024x768.
    cx,cy=152,159
    click(cx+120,cy+316);time.sleep(.5)
    shot=ROOT/'build/factory-reset-confirm.ppm';call('screendump',{'filename':str(shot)});Image.open(shot).save(shot.with_suffix('.png'))
    click(cx+90,cy+354);c.run('cat /home/personal.txt',expected='keep until confirmed')
    c.run('/bin/reset_rights.pol',expected='RESET_RIGHTS_PASS')
    click(cx+120,cy+316);at=len(c.transcript);click(cx+280,cy+354);c.wait_for('Administrator password (Esc cancels):',120,at)
    c.send('wrong123');c.wait_for('Incorrect password.',120,at);c.type_bytes(b'\x1b');c.wait_for('[AUTH64] Administrator launch denied.',120,at);time.sleep(1)
    c.run('cat /home/personal.txt',expected='keep until confirmed')
    c.run('/bin/reset_rights.pol',expected='RESET_RIGHTS_PASS')
    click(cx+120,cy+316);at=len(c.transcript);click(cx+280,cy+354);c.wait_for('Administrator password (Esc cancels):',120,at);c.send('secret123')
    c.wait_for('[RESET64] factory reset complete; restarting',180,at);proc.wait(timeout=30)
    fs=PollikFsImage.load(disk)
    for path in ('/etc/account.db','/etc/account.db.pending','/etc/pollikos-installed','/etc/factory-reset.pending'):
     assert not fs.exists(path),'Reset retained '+path
    assert not fs.directory_entries(fs.resolve_directory('/home')),'Personal data/preferences retained in /home'
    assert not fs.directory_entries(fs.resolve_directory('/tmp')),'Temporary data retained'
    assert fs.read_file('/bin/pollish')==executable,'System executable changed during personal-data reset'
   finally:
    if c:(ROOT/f'build/factory-reset-boot{boot}.log').write_text(c.text(),encoding='utf-8')
    if stream:stream.close()
    if monitor:monitor.close()
    if proc.poll() is None:proc.terminate()
    proc.communicate(timeout=10)
if __name__=='__main__':main()
