"""Real stock-app close/reopen, PID reaping, fresh Calculator pixels and PMM."""
import json,re,shutil,socket,subprocess,sys,tempfile,time
from pathlib import Path
from PIL import Image
from x86_64_console import Console,ROOT
sys.path.insert(0,str(ROOT/'tools'))
from sync_system_files import sync
B=ROOT/'build/x86_64/system'
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def main():
    subprocess.run(['powershell','-NoProfile','-File','sdk/tools/pollikcc.ps1',
        'sdk/tests/window_lifecycle.c','-o',str(B/'userspace/window_lifecycle.elf')],cwd=ROOT,check=True)
    with tempfile.TemporaryDirectory(prefix='pollik-window-life-') as directory:
        data=Path(directory)/'data.img';shutil.copyfile(B/'PollikData-system.img',data)
        sync(data,{'/bin/window_lifecycle.pol':B/'userspace/window_lifecycle.elf'})
        sp,qp=port(),port()
        command=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64','-m','8192',
            '-vga','std','-display','none','-nic','none','-no-reboot',
            '-drive',f'file={B/"PollikOS-x86_64.img"},format=raw,if=ide,index=0,snapshot=on',
            '-drive',f'file={data},format=raw,if=ide,index=1,snapshot=on',
            '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off']
        print('COMMAND '+subprocess.list2cmdline(command),flush=True)
        process=subprocess.Popen(command,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
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
                    answer=json.loads(f.readline());assert 'error' not in answer,answer
                    if 'return' in answer:return answer['return']
            call('qmp_capabilities');c=Console(stream);call('cont')
            c.wait_for('[AUTH64] First run: create a local account.',120)
            for value,prompt in [('lifeuser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:
                c.send(value);c.wait_for(prompt,120)
            c.wait_for('[terminal] output active',120);c.wait_for_prompt(120)
            symbols=[row.split() for row in subprocess.check_output(['llvm-nm','-S',str(B/'kernel.elf')],text=True).splitlines()]
            addresses=[int(row[0],16)+8 for row in symbols if len(row)==4 and row[3]=='stats' and int(row[1],16)==32]
            assert len(addresses)==1,addresses
            def free_frames():
                raw=call('human-monitor-command',{'command-line':f'xp /1gx 0x{addresses[0]:x}'})
                values=re.findall(r':\s*(0x[0-9a-f]+)',raw);assert len(values)==1,raw
                return int(values[0],16)
            mx,my=512,384
            def move(x,y):
                nonlocal mx,my
                while (mx,my)!=(x,y):
                    dx=max(-90,min(90,x-mx));dy=max(-90,min(90,y-my))
                    call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},
                        {'type':'rel','data':{'axis':'y','value':dy}}]});time.sleep(.05);mx+=dx;my+=dy
            def snapshot():
                path=B/'window-lifecycle.ppm';call('screendump',{'filename':str(path)})
                return Image.open(path).convert('RGB')
            cold=free_frames()
            # First shell command opens/appends history, initializing its FILE
            # allocator. Measure that separately from child-process lifetime.
            c.run('pwd',expected='/')
            baseline=free_frames();all_pids=[]
            print(f'SHELL_FIRST_COMMAND PMM before={cold} after={baseline}',flush=True)
            apps=[('calculator',360,466,'[calculator] ready:'),('browser',920,640,'[browser] ready:'),
                  ('files',650,400,'[files] ready:'),('notes',760,560,'[notes] ready:'),
                  ('terminal',760,440,'[terminal] output active')]
            for name,width,height,ready in apps:
                mark=len(c.transcript);c.send('/bin/window_lifecycle.pol /bin/'+name+'.pol')
                initial=None
                for cycle in range(5):
                    c.wait_for(f'[LIFE] start cycle={cycle}',60,mark)
                    start=c.text().index(f'[LIFE] start cycle={cycle}',mark)
                    c.wait_for(ready,60,start)
                    move(980,720);time.sleep(.1)
                    im=snapshot();x=(im.width-width-4)//2;y=(im.height-height-34)//2
                    if name=='calculator':
                        region=(x+18,y+44,x+width-14,y+132);blank=im.crop(region).tobytes()
                        if initial is None:initial=blank
                        assert blank==initial,('Calculator did not restart fresh',cycle)
                        for key in ('2','shift-equal','3','ret'):
                            call('human-monitor-command',{'command-line':'sendkey '+key+' 1'});time.sleep(.1)
                        c.wait_for('[calculator] result=5',10,start)
                        assert snapshot().crop(region).tobytes()!=blank,'calculation did not change framebuffer'
                    move(x+width-8,y+15)
                    for down in (True,False):
                        call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]});time.sleep(.12)
                    c.wait_for(f'[LIFE] reaped cycle={cycle}',30,mark)
                c.wait_for('PASS window lifecycle:',30,mark);c.wait_for_prompt(30,mark)
                pids=re.findall(r'\[LIFE\] start cycle=\d+ pid=(\d+)',c.text()[mark:])
                assert len(pids)==5 and len(set(pids))==5,pids;all_pids+=pids
                after=free_frames();assert after==baseline,('PMM leak',name,baseline,after)
                print(f'PASS {name}: closed and reaped 5 PIDs={pids}; PMM before={baseline} after={after}',flush=True)
            assert len(set(all_pids))==25,all_pids
            assert 'PANIC' not in c.text() and '[X64] FAIL' not in c.text()
            print('PASS 25 window lifecycles: fresh processes; Calculator display resets; Terminal shell group exits; exact PMM baseline',flush=True)
        finally:
            if c:(B/'window-lifecycle.log').write_text(c.text(),encoding='utf-8')
            if q:q.close()
            if stream:stream.close()
            if process.poll() is None:process.terminate();process.wait(timeout=5)
if __name__=='__main__':main()
