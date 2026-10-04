"""Deterministic light-theme GUI fixtures; only disposable images are created."""
from pathlib import Path
import sys
import time
from format_pollikfs2 import format_disk

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'sdk/tools'))
from sync_system_files import sync
from pollikfs_install import PollikFsImage

def create_gui_disk(path, total_size_mb=40):
    format_disk(path, total_size_mb=total_size_mb)
    sync(path)
    fs = PollikFsImage.load(path)
    fs.ensure_directory('/home/.config')
    fs.install_file('/home/.config/appearance.conf',
                    b'theme=1\nanimations=1\ncursor_size=100\n')
    # This newly formatted disposable disk has not been attached to QEMU.
    # Keep its path stable: Windows can deny replacement after writing a
    # .pending file even though writes to the fixture itself are permitted.
    with Path(path).open('r+b') as disk:
        assert len(fs.data)==disk.seek(0,2), 'fixture geometry changed'
        disk.seek(0)
        assert disk.write(fs.data)==len(fs.data)
    print('GUI fixture: appearance installed before QEMU launch',flush=True)

def finish_setup(qmp, log, timeout=30):
    if 'SETUP: first-run installer ready' not in log.read_text():
        raise AssertionError('disposable GUI fixture did not enter first-run setup')
    def key(name):
        qmp('human-monitor-command', {'command-line': 'sendkey ' + name + ' 1'})
        time.sleep(.15)
    key('ret')
    for ch in 'fixture': key(ch)
    key('ret')
    for _ in range(2):
        for ch in 'test123': key(ch)
        key('ret')
    end = time.monotonic() + timeout
    while 'AUTH: account created; installation complete' not in log.read_text():
        assert time.monotonic() < end, 'fixture account setup did not complete'
        time.sleep(.1)
    key('f1')
    time.sleep(1)
