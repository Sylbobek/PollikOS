"""Run real x64 cursor/framebuffer code without starting a guest."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[1];exe=root/'build/console_cursor_native.exe'
flags=['clang','-O2','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror']
if os.name=='nt':flags+=['-fuse-ld=lld','-Wl,/OPT:REF']
else:flags+=['-Wl,--gc-sections']
subprocess.run(flags+['tests/console_cursor_native.c','sdk/lib/font_data.c','-o',str(exe)],cwd=root,check=True)
subprocess.run([str(exe)],cwd=root,check=True)
