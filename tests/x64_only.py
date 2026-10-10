"""Reject an executable PollikOS target other than x86_64, including regressions."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
removed = ['kernel/entry.asm','kernel/interrupts.asm','kernel/linker.ld',
           'kernel/kernel.c','kernel/pmm.c','kernel/vmm.c','kernel/process.c',
           'kernel/syscall.c','kernel/elf.c','kernel/mem.c','include/pollikos.h',
           'kernel/desktop.c','kernel/gui/apps.c','kernel/browser/browser_app.c',
           'kernel/browser/js_engine.c']
for name in removed:
    assert not (ROOT/name).exists(), 'legacy runtime restored: '+name
assert not list((ROOT/'apps').glob('*/**/*.c')), 'legacy application source restored'
for directory in ('kernel','sdk','common','tools'):
    for path in (ROOT/directory).rglob('*'):
        if path.suffix not in ('.c','.h','.asm','.ps1','.py','.ld'): continue
        text = path.read_text(encoding='utf-8')
        assert not re.search(r'--target=i386|elf32-i386|elf_i386|-march=i386|\bELFCLASS32\b', text), path
        if directory == 'kernel':
            assert not re.search(r'^\s*#\s*(?:if|ifdef|ifndef|elif).*\b(?:POLLIK_X64|__i386__)\b', text, re.M), path
for name in ('build.ps1','build-x86_64.ps1','run.ps1','run-x86_64.ps1','.clangd','.github/workflows/kernel.yml'):
    text = (ROOT/name).read_text(encoding='utf-8')
    assert not re.search(r'\bi386\b|elf32|-m32', text), name
assert 'bits 32' in (ROOT/'kernel/arch/x86_64/entry.asm').read_text()
assert 'bits 16' in (ROOT/'boot/boot.asm').read_text()
with tempfile.TemporaryDirectory(prefix='pollik-x64-layout-') as tmp:
    subprocess.run(['clang','--target=x86_64-none-elf','-ffreestanding','-Isdk/include',
                    '-Wall','-Wextra','-Werror','-c','tests/pollikfs_layout.c',
                    '-o',str(Path(tmp)/'layout.o')],cwd=ROOT,check=True)
print('PASS x64-only: no legacy OS/ELF32 target; BIOS bridge retained; PollikFS layout unchanged')
