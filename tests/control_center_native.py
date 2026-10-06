from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1];exe=root/'build/control_center_native.exe'
cmd=['clang','-O2','-fno-builtin','-Wall','-Wextra','-Werror','-fuse-ld=lld','tests/control_center_native.c','-o',str(exe)]
print('COMMAND '+' '.join(cmd),flush=True);subprocess.run(cmd,cwd=root,check=True);subprocess.run([str(exe)],check=True)
