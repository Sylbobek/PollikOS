"""Native decoder -> SYSCALL PCM -> AC97 DMA; 8-GiB/30-GiB real guest proof."""
import argparse,json,pathlib,shutil,socket,struct,subprocess,sys,tempfile,time
from PIL import Image
from x86_64_console import Console,ROOT
sys.path.insert(0,str(ROOT/'tools'))
from sync_system_files import sync
from create_vm_profile import create
B=ROOT/'build/x86_64/system'
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def main():
 a=argparse.ArgumentParser();a.add_argument('--ram',type=int,default=8192);opt=a.parse_args()
 label=f'media-{opt.ram}';capture=B/(label+'.wav');sp,qp=port(),port()
 with tempfile.TemporaryDirectory(prefix='pollik-media-') as tmp:
  source=pathlib.Path(tmp)/'source.img';shutil.copyfile(B/'PollikData-system.img',source)
  files={'/bin/media_probe.pol':B/'userspace/media_guest.elf'}
  for rate in (22050,44100,48000):files[f'/home/sine-{rate}.mp3']=ROOT/f'build/media-native/sine-{rate}.mp3'
  sync(source,files);disk=create(source,pathlib.Path(tmp)/'data-30g.img',30)
  cmd=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64','-m',str(opt.ram),'-vga','std','-display','none','-nic','none','-no-reboot',
   '-drive',f'file={B/"PollikOS-x86_64.img"},format=raw,if=ide,index=0,snapshot=on',
   '-drive',f'file={disk},format=raw,if=ide,index=1,snapshot=on',
   '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off',
   '-audiodev',f'wav,id=a,path={capture},out.frequency=48000','-device','AC97,audiodev=a']
  print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);p=subprocess.Popen(cmd,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
  stream=q=c=None
  try:
   end=time.monotonic()+30
   while stream is None:
    try:stream=socket.create_connection(('127.0.0.1',sp),timeout=1)
    except OSError:assert time.monotonic()<end;time.sleep(.05)
   q=socket.create_connection(('127.0.0.1',qp),timeout=5);f=q.makefile('rwb',buffering=0);f.readline()
   def call(op,args=None):
    f.write((json.dumps({'execute':op,'arguments':args or {}})+'\n').encode())
    while True:
     raw=f.readline()
     if not raw:return {}
     r=json.loads(raw);assert 'error' not in r,r
     if 'return' in r:return r['return']
   call('qmp_capabilities');c=Console(stream);call('cont')
   c.wait_for('[AUTH64] First run: create a local account.',120)
   for value,prompt in [('mediauser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:c.send(value);c.wait_for(prompt,120)
   c.wait_for('[terminal] output active',120);c.wait_for_prompt(120)
   start=len(c.transcript);c.run('/bin/media_probe.pol',expected='PASS audio setup');assert 'PROFILE disk_bytes=32212254720' in c.text()[start:]
   for path in ('/usr/share/sounds/demo.wav','/home/sine-22050.mp3','/home/sine-44100.mp3','/home/sine-48000.mp3'):
    mark=len(c.transcript);c.run('/bin/media.pol --headless '+path,expected='[media] complete frames=',timeout=120)
    print(c.text()[mark:].strip(),flush=True);assert '[media] audio error' not in c.text()[mark:]
    c.run('echo $?',expected='\n0\n')
   mark=len(c.transcript);c.run('/bin/media_probe.pol live',expected='PASS native pause/resume',timeout=30)
   print(c.text()[mark:].strip(),flush=True)
   c.run('echo $?',expected='\n0\n')
   import re
   balances=re.findall(r'\[AUDIO64\] stream PMM before=(0x[0-9a-f]+) after=(0x[0-9a-f]+)',c.text())
   assert len(balances)==5 and all(a==b for a,b in balances),balances
   print('PMM_BALANCES',balances,flush=True)
   # GUI player is independent of the serial console. Real PS/2 Space toggles
   # Pause, then Escape closes the application and its active DMA ownership.
   mark=len(c.transcript);c.send('/bin/media.pol /usr/share/sounds/demo.wav');c.wait_for('[media] playing',30,mark)
   time.sleep(.4);call('human-monitor-command',{'command-line':'sendkey spc 1'});time.sleep(.2)
   shot=B/(label+'.ppm');call('screendump',{'filename':str(shot)});Image.open(shot).save(shot.with_suffix('.png'))
   call('human-monitor-command',{'command-line':'sendkey esc 1'});c.wait_for_prompt(30,mark)
   assert '[X64] FAIL' not in c.text() and 'PANIC' not in c.text()
   call('quit');p.wait(timeout=10)
  finally:
   if c:(B/(label+'.log')).write_text(c.text(),encoding='utf-8')
   if q:q.close()
   if stream:stream.close()
   if p.poll() is None:p.terminate();p.wait(timeout=5)
  raw=capture.read_bytes();assert raw[:4]==b'RIFF' and raw[8:12]==b'WAVE'
  rate=struct.unpack_from('<I',raw,24)[0];assert rate==48000
  pcm=struct.unpack('<'+'h'*((len(raw)-44)//2),raw[44:]);assert max(pcm)>1000 and min(pcm)<-1000
  import re
  completed=list(map(int,re.findall(r'\[media\] complete frames=(\d+)',c.text())))
  assert len(pcm)//2>=sum(completed),('DMA lost completed frames',len(pcm)//2,completed)
  print(f'FRAME_COUNTS completed={sum(completed)} captured={len(pcm)//2}',flush=True)
  print(f'CAPTURE real DMA rate={rate} samples={len(pcm)} min={min(pcm)} max={max(pcm)}',flush=True)
  print(f'PASS native WAV/MP3: RAM={opt.ram} MiB disk=32212254720 B, 3 MP3 rates, playback, pause/resume, owner-exit cleanup, exact PMM baselines',flush=True)
if __name__=='__main__':main()
