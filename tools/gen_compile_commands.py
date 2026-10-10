"""Editor compilation database for the single PollikOS x86_64 target."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
def generate():
    script=(ROOT/'build-x86_64.ps1').read_text()
    modules=[]
    for group in re.findall(r'\$modules\s*(?:\+=|=)\s*@\(([^)]+)\)',script):modules+=re.findall(r"'([^']+)'",group)
    sources={(ROOT/'kernel/arch/x86_64'/(name+'.c')).resolve() for name in modules}
    sources|={ROOT/name for name in re.findall(r"'((?:kernel|common|sdk)/[^']+\.c)'",script)}
    sources|=set((ROOT/'kernel/net').glob('*.c'))|set((ROOT/'sdk').rglob('*.c'))
    sources|={ROOT/'kernel/usb_input.c',ROOT/'kernel/usb_platform.c'}
    entries=[]
    for source in sorted(sources):
        if not source.exists():continue
        args=['clang','--target=x86_64-none-elf','-ffreestanding','-mno-red-zone','-DPOLLIK_X64=1',
              '-Isdk/include','-Iinclude','-Ithird_party/bearssl/inc','-Ithird_party/libpayload-usb/include',
              '-Ibuild/x86_64/selftest','-c',str(source)]
        if 'arch/x86_64' in source.as_posix():args+=['-DSELFTEST=1','-mgeneral-regs-only']
        if 'kernel/browser' in source.as_posix():args+=['-DPOLLIK_BROWSER_STANDALONE=1']
        entries.append({'directory':str(ROOT),'file':str(source),'arguments':args})
    (ROOT/'compile_commands.json').write_text(json.dumps(entries,indent=2))
    print(f'Generated {len(entries)} x86_64 compile commands')
if __name__=='__main__':generate()
