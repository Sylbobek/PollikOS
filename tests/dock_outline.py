from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
cmd=['clang','-O2','-fno-builtin','-Wall','-Wextra','-Werror','-fuse-ld=lld','tests/dock_outline.c','kernel/graphics.c','kernel/gfx/gfx_primitives.c','-o','build/dock_outline.exe']
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
subprocess.run(cmd,cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/dock_outline.exe')],check=True)
