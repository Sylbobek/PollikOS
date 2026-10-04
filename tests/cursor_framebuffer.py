"""Compile and run the real compositor cursor framebuffer regression."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
exe=ROOT/'build'/'cursor_framebuffer.exe'
cmd=['clang','-std=c11','-O2','-fno-builtin','-Wall','-Wextra','-Werror']
if os.name=='nt':cmd+=['-fuse-ld=lld']
cmd+=['tests/cursor_framebuffer.c','kernel/graphics.c','-o',str(exe)]
print(subprocess.list2cmdline(cmd),flush=True)
subprocess.run(cmd,cwd=ROOT,check=True)
subprocess.run([str(exe)],cwd=ROOT,check=True)
