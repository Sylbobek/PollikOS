from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
cmd=['clang','-O2','-fno-math-errno','-Wall','-Wextra','-Werror','-fuse-ld=lld','tests/calc_native.c','common/calc.c','-o','build/calc_native.exe']
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
subprocess.run(cmd,cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/calc_native.exe')],check=True)
