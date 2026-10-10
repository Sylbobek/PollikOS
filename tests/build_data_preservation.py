"""System image generation must not alter existing private images or profiles."""
from pathlib import Path
import hashlib
import subprocess

ROOT=Path(__file__).resolve().parents[1]
for variant in ('kernel','system'):
    output=ROOT/'build/x86_64'/variant
    disk=output/'PollikData-system.img'
    assert disk.exists(), 'build this variant first: '+variant
    paths=[disk]
    if variant=='system': paths.append(output/'PollikData-system-30g.img')
    def fingerprint(path):
        stat=path.stat()
        with path.open('rb') as stream:
            prefix=stream.read(40*1024*1024)
            stream.seek(max(0,stat.st_size-4096));tail=stream.read()
        return stat.st_size,stat.st_mtime_ns,hashlib.sha256(prefix+tail).digest()
    before={p:fingerprint(p) for p in paths}
    subprocess.run(['python','tools/build_x64_system.py',str(output)],cwd=ROOT,check=True)
    assert before=={p:fingerprint(p) for p in paths}, 'existing image modified'
print('PASS build data preservation: existing system images and profile unchanged')
