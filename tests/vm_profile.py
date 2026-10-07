"""Sparse profile copy/idempotence/protected-name tests; no real user disk."""
import hashlib,pathlib,shutil,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from create_vm_profile import create,allocated
from sync_system_files import sync,SPAN
with tempfile.TemporaryDirectory(prefix='pollik-profile-') as tmp:
 source=pathlib.Path(tmp)/'source.img';target=pathlib.Path(tmp)/'vm.img'
 shutil.copyfile(ROOT/'build/x86_64/system/PollikData-system.img',source)
 sync(source,{'/home/personal.txt':b'personal data remains unchanged'})
 with source.open('r+b') as f:f.seek(SPAN+64);f.write(b'opaque-tail-test')
 before=hashlib.sha256(source.read_bytes()).hexdigest();create(source,target,30)
 assert target.stat().st_size==30*(1<<30)
 with target.open('rb') as f:copy=f.read(source.stat().st_size)
 assert hashlib.sha256(copy).hexdigest()==before
 assert allocated(target)<=source.stat().st_size+1024*1024,allocated(target)
 stamp=target.stat().st_mtime_ns;create(source,target,30);assert target.stat().st_mtime_ns==stamp
 for name in ('PollikData.img','pollikdata.img','data.backup.img','data.backup3.img'):
  try:create(source,pathlib.Path(tmp)/name,30)
  except ValueError:pass
  else:raise AssertionError('protected target accepted '+name)
 try:create(source,target,40)
 except ValueError:pass
 else:raise AssertionError('existing profile resized')
 assert hashlib.sha256(source.read_bytes()).hexdigest()==before
 print(f'PASS profile: virtual_bytes={target.stat().st_size} allocated_bytes={allocated(target)}, exact prefix/personals/opaque-tail, idempotence, protected names, no replacement',flush=True)
