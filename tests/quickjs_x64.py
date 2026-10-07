"""QuickJS interpreter executes inside PollikOS, never on the host."""
import json,re,shutil,socket,subprocess,sys,tempfile,time
from pathlib import Path
from x86_64_console import Console,ROOT
sys.path.insert(0,str(ROOT/'tools'))
from sync_system_files import sync
B=ROOT/'build/x86_64/system'
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def main():
    with tempfile.TemporaryDirectory(prefix='pollik-quickjs-') as directory:
        data=Path(directory)/'data.img';shutil.copyfile(B/'PollikData-system.img',data)
        sync(data,{'/bin/qjs_probe.pol':ROOT/'build/quickjs/quickjs_guest.elf'})
        sp,qp=port(),port()
        cmd=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64','-m','8192','-vga','std',
             '-display','none','-nic','none','-no-reboot',
             '-drive',f'file={B/"PollikOS-x86_64.img"},format=raw,if=ide,index=0,snapshot=on',
             '-drive',f'file={data},format=raw,if=ide,index=1,snapshot=on',
             '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off']
        print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
        proc=subprocess.Popen(cmd,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));stream=q=c=None
        try:
            end=time.monotonic()+30
            while stream is None:
                try:stream=socket.create_connection(('127.0.0.1',sp),timeout=1)
                except OSError:assert time.monotonic()<end;time.sleep(.05)
            q=socket.create_connection(('127.0.0.1',qp),timeout=5);f=q.makefile('rwb',buffering=0);f.readline()
            def call(op,args=None):
                f.write((json.dumps({'execute':op,'arguments':args or {}})+'\n').encode())
                while True:
                    r=json.loads(f.readline());assert 'error' not in r,r
                    if 'return' in r:return r['return']
            call('qmp_capabilities');c=Console(stream);call('cont')
            c.wait_for('[AUTH64] First run: create a local account.',120)
            for value,prompt in [('jsuser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:
                c.send(value);c.wait_for(prompt,120)
            c.wait_for('[terminal] output active',120);c.wait_for_prompt(120)
            c.run('pwd',expected='/')
            symbols=[row.split() for row in subprocess.check_output(['llvm-nm','-S',str(B/'kernel.elf')],text=True).splitlines()]
            address=[int(row[0],16)+8 for row in symbols if len(row)==4 and row[3]=='stats' and int(row[1],16)==32]
            assert len(address)==1,address
            def free():
                s=call('human-monitor-command',{'command-line':f'xp /1gx 0x{address[0]:x}'})
                return int(re.search(r':\s*(0x[0-9a-f]+)',s)[1],16)
            baseline=free()
            for cycle in range(3):
                start=len(c.transcript);c.run('/bin/qjs_probe.pol',expected='PASS native QuickJS language and runtime teardown',timeout=120)
                print(c.text()[start:].strip(),flush=True)
                assert c.text()[start:].count('PASS QuickJS ')==8,c.text()[start:]
                assert free()==baseline,('QuickJS process leak',baseline,free())
                print(f'QUICKJS_PROCESS_PMM cycle={cycle} before={baseline} after={free()}',flush=True)
            assert 'PANIC' not in c.text()
            print('PASS native QuickJS: language vectors, jobs, three process teardowns, exact PMM',flush=True)
        finally:
            if c:(ROOT/'build/quickjs-guest-serial.log').write_text(c.text(),encoding='utf-8')
            if q:q.close()
            if stream:stream.close()
            if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
if __name__=='__main__':main()
