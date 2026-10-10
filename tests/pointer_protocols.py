"""Native tests of the shared production PS/2 and USB HID decoders."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
exe=root/'build/pointer_protocols.exe'
subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','tests/pointer_protocols.c','-o',str(exe)],cwd=root,check=True)
subprocess.run([str(exe)],cwd=root,check=True)
