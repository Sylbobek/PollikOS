"""New sparse 30-GiB VM disk, cloning a v2 system image without formatting.
The v2 filesystem remains 32768 KiB; virtual disk size != usable FS capacity.
Never reads/writes PollikData.img or backup images. No existing target is erased.
"""
from pathlib import Path
import ctypes,hashlib,json,os,sys,uuid
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from sync_system_files import SPAN,validate
def sparse(file):
    if os.name!='nt':return
    import msvcrt
    dll=ctypes.WinDLL('kernel32',use_last_error=True)
    ctl=dll.DeviceIoControl;ctl.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_void_p]
    ctl.restype=ctypes.c_int;returned=ctypes.c_ulong()
    if not ctl(msvcrt.get_osfhandle(file.fileno()),0x900c4,None,0,None,0,ctypes.byref(returned),None):raise ctypes.WinError(ctypes.get_last_error())
def zero_hole(file,start,end):
    if os.name!='nt':return
    import msvcrt,struct
    dll=ctypes.WinDLL('kernel32',use_last_error=True);ctl=dll.DeviceIoControl
    ctl.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_void_p];ctl.restype=ctypes.c_int
    request=ctypes.create_string_buffer(struct.pack('<qq',start,end));returned=ctypes.c_ulong()
    if not ctl(msvcrt.get_osfhandle(file.fileno()),0x980c8,request,16,None,0,ctypes.byref(returned),None):raise ctypes.WinError(ctypes.get_last_error())
def allocated(path):
    if os.name!='nt':return path.stat().st_blocks*512
    high=ctypes.c_ulong();dll=ctypes.WinDLL('kernel32',use_last_error=True)
    fn=dll.GetCompressedFileSizeW;fn.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_ulong)];fn.restype=ctypes.c_ulong
    low=fn(str(path),ctypes.byref(high));return low|(high.value<<32)
def create(source,target,gib=30):
    source,target=Path(source).resolve(),Path(target).resolve()
    if gib<1 or gib>120:raise ValueError('ATA profile range is 1..120 GiB')
    if source.name.lower()=='pollikdata.img' or target.name.lower()=='pollikdata.img' or '.backup' in source.name.lower() or '.backup' in target.name.lower():raise ValueError('user/backup images are protected')
    if source==target:raise ValueError('source and target must differ')
    if target.exists():
        with target.open('rb') as f:validate(f.read(SPAN))
        if target.stat().st_size!=gib*(1<<30):raise ValueError('existing profile size differs; refusing replacement')
        print(f'PROFILE preserved {target.name} bytes={target.stat().st_size} filesystem_bytes={32768*1024}');return target
    size=source.stat().st_size
    if size>1<<30:raise ValueError('source exceeds 1 GiB; refusing an unbounded copy')
    with source.open('rb') as f:validate(f.read(SPAN))
    pending=target.with_name(target.name+'.pending-'+uuid.uuid4().hex)
    before=hashlib.sha256();after=hashlib.sha256();target.parent.mkdir(parents=True,exist_ok=True)
    with source.open('rb') as src,pending.open('xb') as dst:
        sparse(dst);dst.truncate(gib*(1<<30));zero_hole(dst,0,gib*(1<<30));position=0
        while chunk:=src.read(1024*1024):
            before.update(chunk)
            if any(chunk):dst.seek(position);dst.write(chunk)
            position+=len(chunk)
        dst.flush();os.fsync(dst.fileno())
    with source.open('rb') as src,pending.open('rb') as dst:
        while chunk:=src.read(1024*1024):
            after.update(chunk)
            if dst.read(len(chunk))!=chunk:raise ValueError('profile prefix does not match source')
    if before.digest()!=after.digest():raise ValueError('source changed during copy; pending profile retained')
    pending.rename(target)
    print(f'PROFILE created {target.name} bytes={target.stat().st_size} allocated_bytes={allocated(target)} source_bytes={size} source_sha256={before.hexdigest()} filesystem_bytes={32768*1024}')
    return target
if __name__=='__main__':
    if len(sys.argv) not in (3,4):raise SystemExit('create_vm_profile.py SOURCE NEW_TARGET [GiB]')
    create(sys.argv[1],sys.argv[2],int(sys.argv[3]) if len(sys.argv)==4 else 30)
