"""Native button-state regression using the actual UI renderer."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
cmd=['clang','-O2','-fno-builtin','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror',
     '-fuse-ld=lld','-Wl,/opt:ref','tests/ui_button_native.c','-o','build/ui_button_native.exe']
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
subprocess.run(cmd,cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/ui_button_native.exe')],cwd=ROOT,check=True)
