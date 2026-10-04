"""Read-only i386 BrowserApp offsets derived from the actual C header."""
from pathlib import Path
import re
import subprocess

def browser_offsets():
    root = Path(__file__).resolve().parents[1]
    names = ('input_url', 'input_cursor', 'is_typing_url',
             'has_pending_navigation', 'is_loading', 'document')
    source = '#include "browser/browser.h"\nunsigned offsets[]={' + ','.join(
        '__builtin_offsetof(BrowserApp,' + name + ')' for name in names) + '};\n'
    ir = subprocess.check_output(['clang', '--target=i386-none-elf', '-ffreestanding',
        '-I' + str(root / 'kernel'), '-x', 'c', '-S', '-emit-llvm', '-o', '-', '-'],
        input=source, text=True)
    line = next(line for line in ir.splitlines() if line.startswith('@offsets ='))
    values = [int(n) for n in re.findall(r'i32 (\d+)', line)]
    assert len(values) == len(names), line
    return dict(zip(names, values))
