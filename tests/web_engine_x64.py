"""Execute the actual upstream HTML/CSS adapters inside PollikOS Ring 3."""
from pathlib import Path
import shutil,subprocess,tempfile
from x86_64_console import Console,ROOT,newest_image
from x86_64_security_session import port,connect,RecordingConsole
B=ROOT/'build/x86_64/kernel'
def run():
    libraries=ROOT/'build/browser-upstream'
    command=['powershell','-NoProfile','-File','sdk/tools/pollikcc.ps1',
        '-DPOLLIK_BROWSER_STANDALONE=1','-DPOLLIK_BROWSER_UPSTREAM=1',
        'sdk/tests/web_engine.c','sdk/apps/browser_html.c','sdk/apps/browser_css.c',
        'kernel/browser/html_parser.c','kernel/browser/css_engine.c']
    for name in ('libdom','libhubbub','libcss','libparserutils','libwapcaplet'):command+=['-Ithird_party/'+name+'/include']
    command+=[str(libraries/(name+'.a')) for name in ('libcss','libdom','libhubbub','libparserutils','libwapcaplet')]
    command+=['-o','build/web_engine.pol']
    subprocess.run(command,cwd=ROOT,check=True)
    with tempfile.TemporaryDirectory(prefix='pollik-web-engine-') as directory:
        disk=Path(directory)/'data.img';shutil.copyfile(B/'PollikData-test.img',disk)
        subprocess.run(['python','sdk/tools/pollikinstall.py',str(disk),'/bin/web_engine.pol','build/web_engine.pol'],cwd=ROOT,check=True)
        sp=port();process=subprocess.Popen(['qemu-system-x86_64','-accel','tcg','-cpu','qemu64,+rdrand','-rtc','base=utc','-m','256','-vga','std','-display','none','-monitor','none','-nic','none',
            '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=on','-no-reboot',
            '-drive',f'file={newest_image(B)},format=raw,if=ide,index=0,snapshot=on','-drive',f'file={disk},format=raw,if=ide,index=1'],
            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        stream=None;c=None
        try:
            stream=connect(sp);c=RecordingConsole(stream,ROOT/'build/web-engine-native.log')
            c.wait_for('Create account name:',180);c.send('webengine');c.wait_for('Create password (6-63 characters):',180);c.send('test123');c.wait_for('Confirm password:',180);c.send('test123')
            c.wait_for('Account created.',180);c.wait_for_prompt(240)
            c.run('/bin/web_engine.pol',expected='WEB_ENGINE_PASS',timeout=180)
            assert 'WEB_ENGINE_FAIL' not in c.text() and 'PANIC' not in c.text(),c.text()[-3000:]
            print('PASS upstream HTML5/CSS selectors, important, calc, media and lifecycle in native guest',flush=True)
        finally:
            if c:(ROOT/'build/web-engine-native.log').write_text(c.text(),encoding='utf-8')
            if stream:stream.close()
            if process.poll() is None:process.terminate()
            process.communicate(timeout=10)
if __name__=='__main__':run()
