"""Measure serial boot milestones on snapshot disks; never write the originals."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time
from gui_fixture import create_gui_disk

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
parser.add_argument('--data', type=Path)
parser.add_argument('--image', default='PollikOS-Alpha.img')
parser.add_argument('--resolution', default='1920x1080')
parser.add_argument('--runs', type=int, default=3)
parser.add_argument('--label', default='boot-loading')
parser.add_argument('--timeout', type=float, default=180)
args = parser.parse_args()
markers = ('own kernel entered', 'GFX resolution:', 'MEM heap initialized',
           'superblock mounted successfully', 'PS/2 wheel ready',
           'Process manager and scheduler', 'AUTH:', '[WALLPAPER] path=',
           '[WALLPAPER] read_ticks=', '[WALLPAPER] decoder=ok',
           '[WALLPAPER] decode_ticks=', '[WALLPAPER] scale_ticks=',
           '[WALLPAPER] cache=ready', 'desktop ready')
results = []
with tempfile.TemporaryDirectory(prefix='pollikos-boot-loading-') as folder:
    data = args.data.resolve() if args.data else Path(folder) / 'data.img'
    if not args.data:
        create_gui_disk(data)
    for run in range(args.runs):
        log = BUILD / f'{args.label}-{args.accel}-{run}.log'
        log.write_bytes(b'')
        command = ['qemu-system-x86_64', '-machine', 'pc', '-accel', args.accel,
                   '-cpu', 'qemu64' if args.accel == 'whpx' else 'max', '-m', '256M',
                   '-device', 'VGA,vgamem_mb=32', '-display', 'none', '-no-reboot',
                   '-fw_cfg', f'name=opt/pollikos/display,string={args.resolution}',
                   '-drive', f'format=raw,file={BUILD / args.image},if=ide,index=0,snapshot=on',
                   '-drive', f'format=raw,file={data},if=ide,index=1,snapshot=on',
                   '-nic', 'none', '-monitor', 'none', '-serial', f'file:{log}']
        print('COMMAND ' + subprocess.list2cmdline(command), flush=True)
        start = time.perf_counter()
        timeline = []
        seen = 0
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            ready = False
            while time.perf_counter() - start < args.timeout:
                lines = log.read_text(errors='replace').splitlines(keepends=True)
                complete = [line.strip() for line in lines if line.endswith('\n')]
                now = (time.perf_counter() - start) * 1000
                for line in complete[seen:]:
                    if any(marker in line for marker in markers):
                        timeline.append(dict(host_ms=round(now, 2), line=line))
                        print(f'RUN {run+1} HOST_MS {now:.2f} {line}', flush=True)
                    if 'desktop ready' in line:
                        ready = True
                seen = len(complete)
                if ready:
                    break
                assert process.poll() is None, f'QEMU exited {process.returncode}: {process.stderr.read().decode(errors="replace")}'
                time.sleep(.005)
            assert ready, f'desktop not ready in {args.timeout}s; log={log}'
            elapsed = next(item['host_ms'] for item in timeline if 'desktop ready' in item['line'])
            results.append(dict(run=run+1, ready_ms=elapsed, timeline=timeline))
            print(f'PASS boot run={run+1} accel={args.accel} resolution={args.resolution} ready_ms={elapsed:.2f}', flush=True)
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            process.stderr.close()
output = BUILD / f'{args.label}-{args.accel}.json'
output.write_text(json.dumps(dict(accel=args.accel, resolution=args.resolution,
                                 observation='host serial polling at 5 ms; includes QEMU/BIOS startup',
                                 results=results), indent=2))
print(f'BOOT_MEAN_MS {sum(r["ready_ms"] for r in results)/len(results):.2f} runs={len(results)} accel={args.accel}')
