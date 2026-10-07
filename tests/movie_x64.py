"""Actual Ring 3 MP4 decoding: FFmpeg YUV hashes, AAC DMA, moving pixels."""
from pathlib import Path
import json,re,shutil,socket,subprocess,sys,tempfile,time,struct
from PIL import Image
import numpy as np
from x86_64_console import Console,ROOT
sys.path.insert(0,str(ROOT/'tools'));from sync_system_files import sync
B=ROOT/'build/x86_64/system';V=ROOT/'build/mp4-native'
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def main():
 with tempfile.TemporaryDirectory(prefix='pollik-mp4-') as tmp:
  data=Path(tmp)/'data.img';shutil.copyfile(B/'PollikData-system.img',data)
  sync(data,{'/bin/movie_probe.pol':B/'userspace/movie_probe.elf','/home/clip.mp4':V/'clip.mp4'})
  sp,qp=port(),port();capture=B/'mp4-native.wav'
  cmd=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64','-m','8192','-vga','std','-display','none','-nic','none','-no-reboot',
   '-drive',f'file={B/"PollikOS-x86_64.img"},format=raw,if=ide,index=0,snapshot=on','-drive',f'file={data},format=raw,if=ide,index=1,snapshot=on',
   '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off',
   '-audiodev',f'wav,id=a,path={capture},out.frequency=48000','-device','AC97,audiodev=a']
  print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);p=subprocess.Popen(cmd,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));q=stream=c=None
  try:
   deadline=time.monotonic()+30
   while stream is None:
    try:stream=socket.create_connection(('127.0.0.1',sp),timeout=1)
    except OSError:assert time.monotonic()<deadline;time.sleep(.05)
   q=socket.create_connection(('127.0.0.1',qp),timeout=5);f=q.makefile('rwb',buffering=0);f.readline()
   def call(op,args=None):
    f.write((json.dumps({'execute':op,'arguments':args or {}})+'\n').encode())
    while True:
     line=f.readline()
     if not line:return {}
     r=json.loads(line);assert 'error' not in r,r
     if 'return' in r:return r['return']
   call('qmp_capabilities');c=Console(stream);call('cont')
   c.wait_for('[AUTH64] First run: create a local account.',120)
   for value,prompt in [('videouser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:c.send(value);c.wait_for(prompt,120)
   c.wait_for('[terminal] output active',120);c.wait_for_prompt(120)
   start=len(c.transcript);c.run('/bin/movie_probe.pol /home/clip.mp4',expected='PASS native MP4',timeout=120)
   block=c.text()[start:];actual=re.findall(r'VIDEO_ORACLE frame=(\d+) pts_ms=(\d+) hash=([0-9a-f]+)',block)
   ref=(V/'reference.yuv').read_bytes();frame_bytes=160*96*3//2;expected=[]
   for n in range(len(ref)//frame_bytes):
    h=2166136261
    for b in ref[n*frame_bytes:(n+1)*frame_bytes]:h=((h^b)*16777619)&0xffffffff
    expected.append((str(n),str(n*1000//12),f'{h:08x}'))
   assert actual==expected,(actual,expected);print(block.strip(),flush=True)
   balances=re.findall(r'stream PMM before=(0x[0-9a-f]+) after=(0x[0-9a-f]+)',c.text());assert balances and all(a==b for a,b in balances),balances
   c.run('echo $?',expected='\n0\n')
   symbols=[line.split() for line in subprocess.check_output(['llvm-nm','-S',str(B/'kernel.elf')],text=True).splitlines()]
   locations=[int(row[0],16)+8 for row in symbols if len(row)==4 and row[3]=='stats' and int(row[1],16)==32]
   assert len(locations)==1,locations
   def free_frames():
    result=call('human-monitor-command',{'command-line':f'xp /1gx 0x{locations[0]:x}'})
    matches=re.findall(r':\s*(0x[0-9a-fA-F]+)',result);assert len(matches)==1,result;return int(matches[0],16)
   baseline=free_frames()
   start=len(c.transcript);c.run('/bin/video.pol --headless /home/clip.mp4',expected='[video] complete decoded=24',timeout=120)
   print(c.text()[start:].strip(),flush=True);c.run('echo $?',expected='\n0\n')
   after=free_frames();assert after==baseline,('process PMM leak',baseline,after)
   print(f'PROCESS_PMM before={baseline} after={after}',flush=True)
   # Independent RGB conversion from FFmpeg's YUV oracle, matching the
   # documented integer BT.601 and nearest chroma expansion/scaling.
   reference_frames=[]
   for n in range(24):
    frame=np.frombuffer(ref[n*frame_bytes:(n+1)*frame_bytes],dtype=np.uint8)
    y=frame[:160*96].reshape(96,160).astype(np.int32)-16
    u=frame[160*96:160*96*5//4].reshape(48,80).repeat(2,0).repeat(2,1).astype(np.int32)-128
    v=frame[160*96*5//4:].reshape(48,80).repeat(2,0).repeat(2,1).astype(np.int32)-128
    rgb=np.stack(((298*y+409*v+128)>>8,(298*y-100*u-208*v+128)>>8,(298*y+516*u+128)>>8),axis=2).clip(0,255).astype(np.uint8)
    reference_frames.append(rgb.repeat(4,0).repeat(4,1).tobytes())
   # Longer presentation clip gives time to inspect two actual frames.
   # The standalone player is launched through the native shell, not host code.
   # Keep the independent software cursor outside the video oracle rectangle.
   mx,my=512,384
   while (mx,my)!=(980,720):
    dx=max(-90,min(90,980-mx));dy=max(-90,min(90,720-my))
    call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},{'type':'rel','data':{'axis':'y','value':dy}}]});time.sleep(.05);mx+=dx;my+=dy
   start=len(c.transcript);c.send('/bin/video.pol /home/clip.mp4');c.wait_for('[video] ready',30,start)
   snapshots=[];end=time.monotonic()+8
   while time.monotonic()<end:
    shot=B/'mp4-present.ppm';call('screendump',{'filename':str(shot)});im=Image.open(shot).convert('RGB')
    # Native window is 720x580, centered with a 34-pixel outer frame.
    x=(im.width-724)//2+2+40;y=(im.height-614)//2+32+42+48
    region=im.crop((x,y,x+640,y+384));payload=region.tobytes()
    if payload in reference_frames:
     if not snapshots or payload!=snapshots[-1]:snapshots.append(payload);im.save(B/f'mp4-frame-{len(snapshots)}.png')
     if len(snapshots)>=2:break
    time.sleep(.08)
   assert len(snapshots)>=2,'no two moving framebuffer images'
   print(f'PIXELS MP4 moving region=640x384 distinct_screendumps={len(snapshots)} exact FFmpeg-YUV RGB oracle',flush=True)
   c.wait_for_prompt(120,start)
   after_gui=free_frames();assert after_gui==baseline,('GUI process PMM leak',baseline,after_gui)
   print(f'GUI_PROCESS_PMM before={baseline} after={after_gui}',flush=True)
   assert 'PANIC' not in c.text() and '[X64] FAIL' not in c.text()
   call('quit');p.wait(timeout=10)
  finally:
   if c:(B/'mp4-native.log').write_text(c.text(),encoding='utf-8')
   if q:q.close()
   if stream:stream.close()
   if p.poll() is None:p.terminate();p.wait(timeout=5)
  raw=capture.read_bytes();pcm=struct.unpack('<'+'h'*((len(raw)-44)//2),raw[44:]);assert min(pcm)<-1000 and max(pcm)>1000
  print(f'CAPTURE AAC native DMA frames={len(pcm)//2} min={min(pcm)} max={max(pcm)}',flush=True)
  print('PASS native MP4: 24 FFmpeg-identical YUV frame hashes, timestamps, AAC PCM/DMA, exact PMM baseline, moving framebuffer',flush=True)
if __name__=='__main__':main()
