"""Create an account in the real i386 guest and verify its hash independently.
Only the shared Guest helper's disposable data image is used.
"""
from pathlib import Path
import subprocess,time
from PIL import Image
from gui_metrics import Guest
from gui_fixture import PollikFsImage
ROOT=Path(__file__).resolve().parents[1]
subprocess.run(['python','tests/security_account_native.py'],cwd=ROOT,check=True)
with Guest('1024x768',label='stage15-auth32',headless=True,accel='tcg',cpu='qemu64',boot_timeout=90) as g:
    time.sleep(1)
    g.qmp('stop')
    record=PollikFsImage.load(g.folder/'data.img').read_file('/etc/account.db')
    target=ROOT/'build/stage15-account32.bin';target.write_bytes(record)
    subprocess.run([str(ROOT/'build/security_account_native.exe'),'--verify-account',str(target),'test123'],cwd=ROOT,check=True)
    shot=ROOT/'build/stage15-auth32.ppm';g.screendump(shot);Image.open(shot).save(shot.with_suffix('.png'))
    print('PASS: i386 first-run Argon2id account, interoperability and desktop',flush=True)
