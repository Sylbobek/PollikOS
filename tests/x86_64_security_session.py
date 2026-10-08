"""Native Ring 3 protection, password change, lock/unlock, logout and reboot.
Only a disposable copy of the x86_64 fixture is opened for writing.
"""
from pathlib import Path
import shutil,socket,subprocess,tempfile,time
from x86_64_console import Console,newest_image
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/x86_64/kernel'
class RecordingConsole(Console):
    def __init__(self,stream,path): super().__init__(stream);self.path=path
    def pump(self,seconds=.1):
        changed=super().pump(seconds)
        if changed:
            self.path.write_text(self.text(),encoding='utf-8')
            assert '[X64] FAIL' not in self.text() and 'PANIC' not in self.text(),self.text()[-2000:]
        return changed
def port():
    with socket.socket() as s: s.bind(('127.0.0.1',0)); return s.getsockname()[1]
def connect(p):
    for _ in range(150):
        try:return socket.create_connection(('127.0.0.1',p),timeout=2)
        except OSError:time.sleep(.1)
    raise AssertionError('QEMU socket did not accept')
def login(c,password,start):
    c.wait_for('Username:',timeout=180,start=start);c.send('secureuser')
    c.wait_for('Password:',timeout=180,start=start);c.send(password)
    c.wait_for('[AUTH64] Sign-in successful.',timeout=180,start=start)
def prompt(c,start=0):c.wait_for_prompt(timeout=300,start=start)
def run():
    subprocess.run(['powershell','-NoProfile','-File','sdk/tools/pollikcc.ps1','sdk/tests/security_session.c','-o','build/security_session.pol'],cwd=ROOT,check=True)
    subprocess.run(['powershell','-NoProfile','-File','sdk/tools/pollikcc.ps1','sdk/tests/elevation.c','-o','build/elevation.pol'],cwd=ROOT,check=True)
    with tempfile.TemporaryDirectory(prefix='pollikos-session-') as temporary:
        disk=Path(temporary)/'data.img';shutil.copyfile(BUILD/'PollikData-test.img',disk)
        subprocess.run(['python','sdk/tools/pollikinstall.py',str(disk),'/bin/security_session.pol','build/security_session.pol'],cwd=ROOT,check=True)
        subprocess.run(['python','sdk/tools/pollikinstall.py',str(disk),'/bin/elevation.pol','build/elevation.pol'],cwd=ROOT,check=True)
        for boot in range(2):
            serial_port,monitor_port=port(),port()
            process=subprocess.Popen(['qemu-system-x86_64','-accel','tcg','-machine','pc','-cpu','qemu64','-m','128','-vga','std','-nic','none','-display','none',
                '-monitor',f'tcp:127.0.0.1:{monitor_port},server=on,wait=off','-serial',f'tcp:127.0.0.1:{serial_port},server=on,wait=on','-no-reboot','-no-shutdown',
                '-drive',f'file={newest_image(BUILD)},format=raw,if=ide,index=0,snapshot=on','-drive',f'file={disk},format=raw,if=ide,index=1'],
                stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            stream=None;monitor=None;c=None
            try:
                stream=connect(serial_port);monitor=connect(monitor_port);c=RecordingConsole(stream,ROOT/f'build/stage15-session-boot{boot}.log')
                if boot==0:
                    c.wait_for('Create account name:',timeout=180);c.send('secureuser')
                    c.wait_for('Create password (6-63 characters):',timeout=180);c.send('secret123')
                    c.wait_for('Confirm password:',timeout=180);c.send('secret123')
                    c.wait_for('Account created.',timeout=180)
                else:
                    login(c,'changed123',0)
                prompt(c)
                c.run('/bin/security_session.pol',expected='SECURITY_GUEST_PASS',timeout=180)
                at=len(c.transcript);c.send('/bin/elevation.pol')
                c.wait_for('Administrator password (Esc cancels):',timeout=180,start=at)
                at=len(c.transcript);c.type_bytes(b'\x1b')
                c.wait_for('Administrator password (Esc cancels):',timeout=180,start=at)
                at=len(c.transcript);c.send('wrong123');c.wait_for('Incorrect password.',timeout=180,start=at)
                c.wait_for('Administrator password (Esc cancels):',timeout=180,start=at)
                c.send('secret123' if boot==0 else 'changed123')
                c.wait_for('ELEVATION_PASS:',timeout=180,start=at);prompt(c,at)
                print('PASS: real Ring 3 elevation, cancellation/wrong password, protected write, admin inheritance and consumed grant',flush=True)
                if boot==0:
                    at=len(c.transcript);c.send('lock');c.wait_for('Username:',timeout=180,start=at)
                    # No old session process may redraw over the lock prompt.
                    monitor.sendall(('screendump '+str(ROOT/'build/stage15-lock.ppm').replace('\\','/')+'\n').encode());time.sleep(.3)
                    c.send('secureuser');c.wait_for('Password:',start=at);c.send('wrong123')
                    c.wait_for('Sign-in failed.',timeout=180,start=at)
                    retry=len(c.transcript);login(c,'secret123',retry)
                    c.wait_for('[SESSION64] session resumed',timeout=180,start=at);prompt(c,at)
                    at=len(c.transcript);c.send('passwd');c.wait_for('Current password:',timeout=180,start=at);c.send('secret123')
                    c.wait_for('New password (6-63 characters):',start=at);c.send('changed123')
                    c.wait_for('Confirm new password:',start=at);c.send('changed123')
                    c.wait_for('[AUTH64] Password changed.',timeout=180,start=at);prompt(c,at)
                    at=len(c.transcript);c.send('logout');c.wait_for('[SESSION64] logged out; resources reclaimed',timeout=180,start=at)
                    login(c,'changed123',at);prompt(c,at)
                    c.run('/bin/security_session.pol',expected='SECURITY_GUEST_PASS',timeout=180)
                    print('PASS: native access checks, credential inheritance, lock/wrong password/unlock, password change and logout cleanup',flush=True)
                else:
                    print('PASS: reboot authenticates with the changed Argon2id account and repeats native protection checks',flush=True)
            finally:
                if c:(ROOT/f'build/stage15-session-boot{boot}.log').write_text(c.text(),encoding='utf-8')
                if stream:stream.close()
                if monitor:monitor.close()
                process.terminate();_,error=process.communicate(timeout=10)
                if error:print(error.decode(errors='replace')[-1000:])
if __name__=='__main__':run()
