"""Native regression for C-held moving-GC references in the actual Elk core."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
command=['clang','-O1','-g','-Wall','-Wextra','-Werror','-fuse-ld=lld',
         'tests/elk_roots_native.c','-o','build/elk_roots_native.exe']
print('COMMAND '+subprocess.list2cmdline(command),flush=True)
subprocess.run(command,cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/elk_roots_native.exe')],cwd=ROOT,check=True)
