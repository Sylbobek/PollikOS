"""Curated native system disk; never format or erase an existing system disk."""
from pathlib import Path
import sys
from sync_system_files import sync,FILES,manifest,validate,SPAN
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from format_pollikfs2 import format_disk

def build(output):
    output=Path(output).resolve()
    if output not in ((ROOT/'build/x86_64/kernel').resolve(),(ROOT/'build/x86_64/system').resolve()):raise ValueError('production output must be a managed x64 output directory')
    with (output/'PollikData-test.img').open('rb') as f:source=validate(f.read(SPAN))
    records=manifest(source)
    apps=('pollish','tcc','desktop.pol','files.pol','terminal.pol','notes.pol','browser.pol','calculator.pol','windowdemo.pol')
    selected=dict(FILES)
    selected['/bin/media.pol']=(output/'userspace/media_player.elf').read_bytes()
    selected['/bin/video.pol']=(output/'userspace/video_player.elf').read_bytes()
    selected['/usr/lib/libpollikvideo.a']=(output/'codecs/libpollikvideo.a').read_bytes()
    selected['/usr/share/videos/demo.mp4']=(ROOT/'assets/media/demo.mp4').read_bytes()
    selected['/usr/share/licenses/h264bsd.txt']=(ROOT/'third_party/h264bsd/LICENSE.md').read_bytes()
    selected['/usr/share/licenses/faad2.txt']=(ROOT/'third_party/faad2/COPYING').read_bytes()
    from build_demo_audio import demo_wave
    selected['/usr/share/sounds/demo.wav']=demo_wave()
    for path,record in records.items():
        if record[0]!='file':continue
        if (path in tuple('/bin/'+a for a in apps) or path.startswith('/usr/include/') or
            path.startswith('/usr/lib/') or path in ('/home/welcome.html','/home/welcome.css','/home/welcome.js')):
            selected[path]=source.read_file(path)
    for app in apps:
        if '/bin/'+app not in selected:raise ValueError(f'missing production app {app}')
    target=output/'PollikData-system.img'
    created=not target.exists()
    if created:
        pending=target.with_suffix('.img.pending');format_disk(pending,total_size_mb=40)
        sync(pending,selected);pending.replace(target)
    else:sync(target,selected)
    with target.open('rb') as f:fs=validate(f.read(SPAN))
    actual=manifest(fs)
    print(f'Native system disk: {target.name}; stock_files={len(selected)}; user files preserved')
    for path in sorted(selected):print('SYSTEM',path)
    for bad in ('/bin/invalid','/bin/tiny','/bin/truncated','/bin/large','/testdir/'):
        if created and bad in actual:raise ValueError('test fixture unexpectedly present in system image: '+bad)
    if output.name=='system':
        from create_vm_profile import create
        profile=create(target,output/'PollikData-system-30g.img',30)
        sync(profile,selected)
if __name__=='__main__':build(sys.argv[1])
