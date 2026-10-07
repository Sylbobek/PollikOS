"""Pinned software codecs; standalone userspace archive, never kernel objects."""
from pathlib import Path
import argparse,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def build(output,native):
 output=Path(output);output.mkdir(parents=True,exist_ok=True);objects=[]
 flags=['-O2','-fno-vectorize','-fno-slp-vectorize','-DNDEBUG']
 if native:flags+=['--target=x86_64-none-elf','-ffreestanding','-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector','-mno-red-zone','-msse2','-mno-avx','-mcmodel=large','-Isdk/include']
 else:flags+=['-D_CRT_SECURE_NO_WARNINGS']
 groups=[(ROOT/'third_party/h264bsd/src',['-Dabs=h264bsd_abs']),
         (ROOT/'third_party/faad2/libfaad',['-Ithird_party/faad2/include','-DHAVE_STDINT_H=1','-DHAVE_STRING_H=1','-DHAVE_MEMCPY=1','-DLC_ONLY_DECODER','-DDISABLE_SBR','-DPACKAGE_VERSION="pollikos-vendored"'])]
 for directory,defines in groups:
  for source in sorted(directory.glob('*.c')):
   target=output/(directory.parent.name+'-'+source.stem+'.o')
   if not target.exists() or target.stat().st_mtime<source.stat().st_mtime:
    cmd=['clang',*flags,*defines,'-I'+str(directory),'-c',str(source),'-o',str(target)]
    r=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
    if r.returncode:print(r.stdout+r.stderr);raise SystemExit(r.returncode)
   objects.append(str(target))
 archive=output/'libpollikvideo.a';subprocess.run(['llvm-ar','rcs',str(archive),*objects],check=True)
 print(f'CODECS {"native" if native else "host"} objects={len(objects)} archive={archive} bytes={archive.stat().st_size}',flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--native',action='store_true');a=p.parse_args();build(a.output,a.native)
