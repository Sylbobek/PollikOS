"""Actual shared UTF-8 decoder and SDK font rasterizer; no browser/host rendering."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-fuse-ld=lld',
    'tests/utf8_ui_native.c','sdk/lib/font_data.c','-o','build/utf8_ui_native.exe'],cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/utf8_ui_native.exe')],cwd=ROOT,check=True)
