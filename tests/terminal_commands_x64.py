"""Real shell commands and authenticated administrator shells in QEMU.

Only a disposable data copy is modified; no user disk is opened.
"""
from pathlib import Path
import subprocess,shutil,tempfile,time,os,json
from PIL import Image
from x86_64_console import ROOT,newest_image
from x86_64_security_session import port,connect,RecordingConsole
B=ROOT/'build/x86_64'/os.environ.get('POLLIK_X64_VARIANT','kernel')
def run():
    subprocess.run(['powershell','-NoProfile','-File','sdk/tools/pollikcc.ps1','sdk/tests/system_query.c','-o','build/system_query.pol'],cwd=ROOT,check=True)
    with tempfile.TemporaryDirectory(prefix='pollik-terminal-') as temporary:
        disk=Path(temporary)/'data.img';shutil.copyfile(B/('PollikData-test.img' if B.name=='kernel' else 'PollikData-system.img'),disk)
        subprocess.run(['python','sdk/tools/pollikinstall.py',str(disk),'/bin/system_query.pol','build/system_query.pol'],cwd=ROOT,check=True)
        sp,qp=port(),port();process=subprocess.Popen(['qemu-system-x86_64','-accel','tcg','-cpu','qemu64,+rdrand','-rtc','base=utc','-m','256','-vga','std','-display','none','-nic','none','-no-reboot',
            '-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off',
            '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=on','-drive',f'file={newest_image(B)},format=raw,if=ide,index=0,snapshot=on',
            '-drive',f'file={disk},format=raw,if=ide,index=1'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        stream=None;c=None;monitor=None
        try:
            stream=connect(sp);c=RecordingConsole(stream,ROOT/'build/terminal-commands-native.log')
            c.wait_for('Create account name:',180);c.send('termuser');c.wait_for('Create password (6-63 characters):',180);c.send('secret123');c.wait_for('Confirm password:',180);c.send('secret123');c.wait_for('Account created.',180);c.wait_for_prompt(240)
            def command(text,needle=None,allow_error=False):
                at=len(c.transcript);c.run(text,expected=needle,timeout=120)
                assert 'PANIC' not in c.text() and '[USER64] fault' not in c.text(),c.text()[-2000:]
                output=c.text()[at:]
                if not allow_error:
                    name=text.split()[0]
                    for line in output.splitlines():
                        assert not line.startswith('Error:') and not (line.startswith(name+':') and ': applied' not in line),output
                return output
            command('/bin/system_query.pol','SYSTEM_QUERY_PASS')
            command('calc (2+3)*4','20');command('math sqrt(144)+max(3,7)','19');command('expr 2^8','256')
            assert 'Division by zero' in command('calc 1/0',allow_error=True);assert '\n1\n' in command('status')
            for text,needle in [('sysinfo','RAM:'),('uname','x86_64'),('free','RAM KiB:'),('df','filesystem capacity is separate'),('ps','pollish'),('uptime','Uptime:'),('date','UTC'),('abi','features=0x'),('devices','Devices:'),('display','Display:'),('net','Network:'),('whoami','termuser'),('id','role=user'),('help sudo','authenticate'),('which notes','/bin/notes.pol'),('commands sys','sysinfo')]:command(text,needle)
            command('mkdir /home/cli-dir');command('echo hello world > /home/cli-dir/a.txt');command('cp /home/cli-dir/a.txt /home/cli-dir/b.txt');command('cat /home/cli-dir/b.txt','hello world')
            command('stat /home/cli-dir/b.txt','bytes=12');command('wc /home/cli-dir/b.txt','1 2 12');command('head -n 1 /home/cli-dir/b.txt','hello world');command('find /home/cli-dir','/home/cli-dir/b.txt')
            command('echo piped text | wc','1 2 11');command('sleep 0')
            assert 'permission denied' in command('touch /etc/cli-admin-proof',allow_error=True)
            assert 'operation not permitted' in command('volume 37',allow_error=True)
            assert 'requires authenticated kernel rights' in command('/bin/pollish --admin-shell',allow_error=True)
            at=len(c.transcript);c.send('sudo touch /etc/cli-admin-proof');c.wait_for('Administrator password (Esc cancels):',120,at)
            c.type_bytes(b'\x1b');c.wait_for_prompt(120,at);command('id','role=user')
            at=len(c.transcript);c.send('sudo touch /etc/cli-admin-proof');c.wait_for('Administrator password (Esc cancels):',120,at)
            c.send('wrong123');c.wait_for('Incorrect password.',120,at);c.wait_for('Administrator password (Esc cancels):',120,at);c.send('secret123');c.wait_for_prompt(120,at)
            command('stat /etc/cli-admin-proof','bytes=0');command('id','role=user')
            at=len(c.transcript);c.send('admin');c.wait_for('Administrator password (Esc cancels):',120,at);c.send('secret123');c.wait_for('[pollish] administrator shell:',120,at);c.wait_for_prompt(120,at)
            command('id','role=administrator');command('/bin/system_query.pol','SYSTEM_QUERY_PASS');command('volume 37','volume: applied');command('volume','37%')
            command('echo privileged data > /etc/cli-admin-data');command('cat /etc/cli-admin-data','privileged data')
            command('cp /home/cli-dir/a.txt /etc/cli-admin-copy');command('cat /etc/cli-admin-copy','hello world')
            command('rm /etc/cli-admin-data');command('rm /etc/cli-admin-copy')
            command('touch /etc/cli-admin-nested');command('/bin/pollish -c id','role=administrator');command('rm /etc/cli-admin-proof');command('rm /etc/cli-admin-nested')
            command('exit');command('id','role=user');assert 'permission denied' in command('touch /etc/cli-admin-nested',allow_error=True)
            command('rm /home/cli-dir/a.txt');command('rm /home/cli-dir/b.txt');command('rmdir /home/cli-dir')
            # The GUI terminal uses the same shell through pipes. Type actual
            # PS/2 keys and verify its output files through the independent UART shell.
            monitor=connect(qp);q=monitor.makefile('rwb',buffering=0);q.readline()
            def call(op,args=None):
                q.write((json.dumps({'execute':op,'arguments':args or {}})+'\n').encode())
                while True:
                    reply=json.loads(q.readline());assert 'error' not in reply,reply
                    if 'return' in reply:return reply['return']
            call('qmp_capabilities');time.sleep(2)
            def type_gui(text):
                names={' ':'spc','/':'slash','-':'minus','.':'dot','>':'shift-dot','_':'shift-minus'}
                for char in text:
                    call('human-monitor-command',{'command-line':'sendkey '+names.get(char,char)});time.sleep(.25);c.pump(.01)
                call('human-monitor-command',{'command-line':'sendkey ret'});time.sleep(1)
            type_gui('sysinfo > /home/cli-gui.txt');command('cat /home/cli-gui.txt','RAM:')
            at=len(c.transcript);type_gui('admin');c.wait_for('Administrator password (Esc cancels):',120,at)
            c.send('secret123');time.sleep(3)
            type_gui('id > /home/cli-gui-admin.txt');command('cat /home/cli-gui-admin.txt','role=administrator')
            shot=ROOT/'build/terminal-admin-native.ppm';call('screendump',{'filename':str(shot)});Image.open(shot).save(shot.with_suffix('.png'))
            type_gui('exit');type_gui('id > /home/cli-gui-user.txt');command('cat /home/cli-gui-user.txt','role=user')
            for path in ('/home/cli-gui.txt','/home/cli-gui-admin.txt','/home/cli-gui-user.txt'):command('rm '+path)
            print('TERMINAL_COMMANDS_PASS native commands, arithmetic, real system/process snapshots, pipelines/atomic copy, administrator cancellation/wrong password/sudo/admin/inheritance/exit',flush=True)
        finally:
            if c:(ROOT/'build/terminal-commands-native.log').write_text(c.text(),encoding='utf-8')
            if stream:stream.close()
            if monitor:monitor.close()
            if process.poll() is None:process.terminate()
            process.communicate(timeout=10)
if __name__=='__main__':run()
