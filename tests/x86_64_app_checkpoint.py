"""Normal desktop boot, real Notes I/O and kernel-enforced Browser rights.

All writes use a disposable copy; the user's data disk is never opened.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import json
import time
from PIL import Image
from x86_64_console import Console, ROOT, newest_image
from x86_64_security_session import port, connect, RecordingConsole

BUILD = ROOT / "build/x86_64/kernel"
sys.path.insert(0, str(ROOT / "sdk/tools"))
from pollikfs_install import PollikFsImage
class AppConsole(RecordingConsole):
    def pump(self,seconds=.1):
        changed=super().pump(seconds)
        if changed:
            assert "Could not save the account" not in self.text(),self.text()[-3000:]
            assert "Account database is invalid" not in self.text(),self.text()[-3000:]
            for marker in ('NOTES_IO_FAIL','FILES_IO_FAIL','APP_RIGHTS_FAIL'):
                assert marker not in self.text(),self.text()[-3000:]
        return changed


def run():
    for name,source in [("notes_io","sdk/tests/notes_io.c"),("files_io","sdk/tests/files_io.c"),
                        ("notes_gui","sdk/apps/notes.c"),
                        ("files_gui","sdk/apps/files.c")]:
        subprocess.run(["powershell", "-NoProfile", "-File", "sdk/tools/pollikcc.ps1",
                        source, "-o", f"build/{name}.pol"], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory(prefix="pollikos-app-checkpoint-") as temporary:
        disk = Path(temporary) / "data.img"
        shutil.copyfile(BUILD / "PollikData-test.img", disk)
        fixture=PollikFsImage.load(disk)
        fixture.ensure_directory('/Applications')
        fixture.ensure_directory('/home/gui-files')
        fixture.install_file('/home/gui-files/source.txt',b'GUI copy test')
        fixture.install_file('/home/notes-gui-original.txt',b'original GUI note')
        fixture.save(disk)
        for name, target in [("notes_io", "/bin/notes_io.pol"),
                             ("files_io", "/bin/files_io.pol"),
                             ("notes_gui", "/bin/notes_gui.pol"),("files_gui", "/bin/files_gui.pol"),
                             ]:
            subprocess.run(["python", "sdk/tools/pollikinstall.py", str(disk), target,
                            f"build/{name}.pol"], cwd=ROOT, check=True)
        serial,monitor_port = port(),port()
        process = subprocess.Popen(["qemu-system-x86_64", "-accel", "tcg", "-cpu", "qemu64",
            "-m", "128", "-vga", "std", "-display", "none", "-nic", "none",
            "-qmp", f"tcp:127.0.0.1:{monitor_port},server=on,wait=off",
            "-serial", f"tcp:127.0.0.1:{serial},server=on,wait=on", "-no-reboot",
            "-drive", f"file={newest_image(BUILD)},format=raw,if=ide,index=0,snapshot=on",
            "-drive", f"file={disk},format=raw,if=ide,index=1"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        stream = None
        console = None
        monitor=None
        try:
            stream = connect(serial)
            console = AppConsole(stream,ROOT / "build/app-checkpoint.log")
            console.wait_for("Create account name:", 180)
            console.send("appcheck")
            console.wait_for("Create password (6-63 characters):", 180)
            console.send("test123")
            console.wait_for("Confirm password:", 180)
            console.send("test123")
            console.wait_for("Account created.", 180)
            console.wait_for("[desktop] ready", 180)
            console.wait_for_prompt(180)
            console.run("/bin/notes_io.pol", expected="NOTES_IO_PASS", timeout=240)
            console.run("/bin/files_io.pol", expected="FILES_IO_PASS", timeout=240)
            monitor=connect(monitor_port);qmp=monitor.makefile('rwb',buffering=0);qmp.readline()
            def call(operation,arguments=None):
                qmp.write((json.dumps({'execute':operation,'arguments':arguments or {}})+'\n').encode())
                while True:
                    raw=qmp.readline();assert raw,'QMP closed'
                    reply=json.loads(raw);assert 'error' not in reply,reply
                    if 'return' in reply:return reply['return']
            call('qmp_capabilities')
            def key(value):
                call('human-monitor-command',{'command-line':'sendkey '+value});time.sleep(.14);console.pump(.01)
            def type_text(text):
                names={'/':'slash','-':'minus','.':'dot','_':'shift-minus'}
                for char in text:key(names.get(char,char))
            def capture(label):
                shot=ROOT/f'build/app-checkpoint-{label}.ppm';call('screendump',{'filename':str(shot)})
                picture=Image.open(shot).convert('RGB');picture.save(shot.with_suffix('.png'));return picture
            mouse=[512,384]
            def click(x,y):
                while mouse!=[x,y]:
                    dx=max(-90,min(90,x-mouse[0]));dy=max(-90,min(90,y-mouse[1]))
                    call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},
                        {'type':'rel','data':{'axis':'y','value':dy}}]})
                    mouse[0]+=dx;mouse[1]+=dy;time.sleep(.06)
                for down in (True,False):
                    call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]});time.sleep(.12)
            mark=len(console.transcript);console.send('/bin/notes_gui.pol /home/notes-gui-original.txt')
            console.wait_for('[notes] ready',120,mark)
            key('ctrl-a');key('a');key('ctrl-alt-a');key('ctrl-s')
            console.wait_for('[notes] saved /home/notes-gui-original.txt',120,mark)
            capture('notes-polish')
            key('ctrl-shift-s');key('ctrl-a');type_text('/home/notes-gui-saved.txt');key('ret')
            console.wait_for('[notes] saved /home/notes-gui-saved.txt',120,mark)
            key('ctrl-z');key('ctrl-y');key('ctrl-s')
            click(880,102);console.wait_for_prompt(120,mark)
            console.run('/bin/notes_io.pol gui',expected='NOTES_GUI_PASS',timeout=120)
            mark=len(console.transcript);console.send('/bin/files_gui.pol /home/gui-files')
            console.wait_for('[files] ready',120,mark)
            before=capture('files-before').crop((187,533,837,599)).tobytes()
            key('ctrl-n');after=capture('files-prompt').crop((187,533,837,599)).tobytes()
            assert before!=after,'Files prompt did not reach the actual framebuffer'
            type_text('dest');key('ret');key('down');key('ctrl-c');key('up');key('ret');key('ctrl-v')
            key('delete');key('ctrl-z');key('ctrl-r');key('ctrl-a');type_text('renamed.txt');key('ret')
            key('ctrl-x');key('backspace');key('ctrl-v');capture('files-complete')
            click(825,182);console.wait_for_prompt(120,mark)
            console.run('/bin/files_io.pol gui',expected='FILES_GUI_PASS',timeout=120)
            transcript = console.text()
            assert "PANIC" not in transcript and "[USER64] fault" not in transcript
            assert "NOTES_IO_FAIL" not in transcript and "FILES_IO_FAIL" not in transcript and "APP_RIGHTS_FAIL" not in transcript
            print("PASS normal desktop, Notes UTF-8/atomic save/ENOSPC, Files operations and all Browser spawn modes", flush=True)
        finally:
            if console:
                (ROOT / "build/app-checkpoint.log").write_text(console.text(), encoding="utf-8")
            if stream:
                stream.close()
            if monitor:monitor.close()
            if process.poll() is None:
                process.terminate()
            _, error = process.communicate(timeout=10)
            if error:
                print(error.decode(errors="replace")[-1000:])


if __name__ == "__main__":
    run()
