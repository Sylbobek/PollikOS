"""Pinned QuickJS, optional userspace archive; no kernel objects or host scripts."""
from pathlib import Path
import argparse,subprocess
ROOT=Path(__file__).resolve().parents[1]
def build(output,native):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    flags=['-O2','-DNDEBUG','-DPOLLIK_NO_ATOMICS=1','-DPOLLIK_JS_LIMB32=1','-DCONFIG_VERSION="2026-06-04"',
           '-ffunction-sections','-fdata-sections','-Ithird_party/quickjs']
    if native:
        flags+=['--target=x86_64-none-elf','-DPOLLIKOS=1','-ffreestanding','-fno-builtin',
                '-fno-pic','-fno-pie','-fno-stack-protector','-mno-red-zone','-mcmodel=large',
                '-msse2','-mno-avx','-fno-vectorize','-fno-slp-vectorize',
                '-Ithird_party/quickjs/pollikos','-Isdk/include']
    else:flags+=['-D_CRT_SECURE_NO_WARNINGS']
    objects=[]
    for name in ('quickjs','dtoa','libregexp','libunicode','cutils'):
        source=ROOT/f'third_party/quickjs/{name}.c';obj=output/(name+'.o')
        command=['clang',*flags,'-c',str(source),'-o',str(obj)]
        print('COMMAND '+subprocess.list2cmdline(command),flush=True)
        subprocess.run(command,cwd=ROOT,check=True);objects.append(str(obj))
    if native:
        libm=['e_hypot','e_cosh','e_sinh','s_tanh','e_acosh','s_asinh','e_atanh',
              's_expm1','s_log1p','e_log2','s_cbrt','s_lrint','s_rint','k_exp','s_creal','s_cimag']
        for name in libm:
            obj=output/('olm-'+name+'.o')
            command=['clang',*flags,'-DOPENLIBM_STATIC_DEFINE=1','-Ithird_party/openlibm/include',
                     '-Ithird_party/openlibm/src','-Ithird_party/openlibm/amd64',
                     '-c',str(ROOT/f'third_party/openlibm/src/{name}.c'),'-o',str(obj)]
            print('COMMAND '+subprocess.list2cmdline(command),flush=True)
            subprocess.run(command,cwd=ROOT,check=True);objects.append(str(obj))
        obj=output/'olm-fenv.o'
        command=['clang',*flags,'-DOPENLIBM_STATIC_DEFINE=1','-Ithird_party/openlibm/include',
                 '-Ithird_party/openlibm/src','-Ithird_party/openlibm/amd64',
                 '-c',str(ROOT/'third_party/openlibm/amd64/fenv.c'),'-o',str(obj)]
        subprocess.run(command,cwd=ROOT,check=True);objects.append(str(obj))
    archive=output/'libquickjs.a'
    subprocess.run(['llvm-ar','rcs',str(archive),*objects],cwd=ROOT,check=True)
    print(f'QUICKJS {"native" if native else "host"} objects={len(objects)} bytes={archive.stat().st_size}',flush=True)
    return archive
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',required=True)
    parser.add_argument('--native',action='store_true');args=parser.parse_args()
    build(args.output,args.native)
