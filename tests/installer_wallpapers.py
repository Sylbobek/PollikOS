"""Install to disposable ATA/AHCI targets, verify stock files, then boot targets."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import shutil
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build'
sys.path.insert(0,str(ROOT/'tools'))
from sync_system_files import validate, FILES, START, SPAN
parser=argparse.ArgumentParser();parser.add_argument('--backend',choices=('ata','ahci'),required=True);args=parser.parse_args()
target=BUILD/f'installer-followup-{args.backend}.img'
media=BUILD/f'installer-followup-{args.backend}-media.img'
shutil.copyfile(BUILD/'PollikOS-USB-Installer.img',media)
with target.open('wb') as f:f.truncate(128*1024*1024)
class Boot:
    def __init__(self,install):self.install=install
    def __enter__(self):
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        self.log=BUILD/f'installer-followup-{args.backend}-{"install" if self.install else "boot"}.log';self.log.write_text('')
        cmd=['qemu-system-x86_64','-machine','pc','-accel','tcg','-cpu','max','-m','256M','-device','VGA,vgamem_mb=32','-display','none','-no-reboot','-nic','none','-serial',f'file:{self.log}','-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
        if args.backend=='ata':cmd+=['-drive',f'format=raw,file={target},if=ide,index=0']
        else:cmd+=['-device','ich9-ahci,id=ahci','-drive',f'format=raw,file={target},if=none,id=target','-device','ide-hd,drive=target,bus=ahci.0,bootindex=2']
        if self.install:cmd+=['-device','qemu-xhci','-drive',f'format=raw,file={media},if=none,id=installer,snapshot=on','-device','usb-storage,drive=installer,bootindex=1']
        print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
        self.proc=subprocess.Popen(cmd,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            end=time.monotonic()+30
            while True:
                try:self.conn=socket.create_connection(('127.0.0.1',port),timeout=10);break
                except OSError:assert self.proc.poll() is None and time.monotonic()<end;time.sleep(.1)
            self.stream=self.conn.makefile('rwb',buffering=0);self.stream.readline();self.qmp('qmp_capabilities')
            self.wait('desktop ready',90);return self
        except BaseException:self.__exit__();raise
    def qmp(self,cmd,arguments=None):
        self.stream.write((json.dumps({'execute':cmd,'arguments':arguments or {}})+'\n').encode())
        while True:
            r=json.loads(self.stream.readline());assert 'error' not in r,r
            if 'return' in r:return r['return']
    def key(self,key):self.qmp('human-monitor-command',{'command-line':'sendkey '+key+' 1'});time.sleep(.18)
    def wait(self,marker,seconds=90):
        end=time.monotonic()+seconds
        while marker not in self.log.read_text():
            assert self.proc.poll() is None and time.monotonic()<end,self.log.read_text()[-2000:];time.sleep(.1)
    def __exit__(self,*_):
        if self.proc.poll() is None:
            self.proc.terminate();self.proc.wait(timeout=10)
        if hasattr(self,'stream'):self.stream.close();self.conn.close()
with Boot(True) as g:
    g.wait('INSTALL: removable-media installer ready');g.key('ret');g.key('ret')
    g.wait('SETUP: explicit PollikFS format complete',180)
    for ch in 'installer':g.key(ch)
    g.key('ret')
    for _ in range(2):
        for ch in 'test123':g.key(ch)
        g.key('ret')
    g.wait('INSTALL: complete; remove media and restart',60)
    for line in g.log.read_text().splitlines():
        if line.startswith('INSTALL:') or 'explicit PollikFS format' in line or 'AHCI' in line:print(args.backend.upper()+' '+line,flush=True)
    g.qmp('quit');g.proc.wait(timeout=10)
with target.open('rb') as disk:disk.seek(8*1024*1024);fsdata=disk.read(SPAN-START)
fs=validate(bytes(START)+fsdata)
for path,source in FILES.items():
    contents=fs.read_file(path);assert contents==source.read_bytes()
    print(f'PASS {args.backend.upper()} installed {path}: bytes={len(contents)} SHA256={hashlib.sha256(contents).hexdigest()}',flush=True)
with Boot(False) as g:
    g.wait('AUTH: login required')
    for ch in 'test123':g.key(ch)
    g.key('ret');g.wait('AUTH: login accepted',60)
    g.wait('[WALLPAPER] decoder=ok')
    text=g.log.read_text();assert 'GFX wallpaper unavailable' not in text,text
    for line in text.splitlines():
        if 'WALLPAPER' in line or 'superblock mounted successfully' in line or 'AHCI' in line:print(args.backend.upper()+' '+line,flush=True)
print(f'PASS {args.backend.upper()} installer completed, stock files exact, installed target boots and decodes wallpaper',flush=True)
