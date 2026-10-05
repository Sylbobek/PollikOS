# PollikOS measurement environment gate

Date: 2026-10-05  
Branch: `env-gate`, created at the then-current `3fa58ebab307c30b3975021c7ce9e876fdeed9e9` HEAD.

## SDL startup and display variants

The normal `run.ps1` command was captured with `-NoLaunch`; the reproduced launch used the same arguments with QEMU's global `-snapshot` appended so disk writes could not reach the base images. QEMU was version `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

```text
QEMU command: qemu-system-x86_64 -name "Pollik OS v0.1 Alpha" -machine pc -accel whpx -cpu qemu64 -smp 1 -rtc base=utc -m 2G -device "VGA,vgamem_mb=32,refresh_rate=120" -display sdl -fw_cfg "name=opt/pollikos/display,string=1920x1080" -drive "format=raw,file=build/PollikOS-Alpha.img,if=ide,index=0" -drive "format=raw,file=build/PollikData.img,if=ide,index=1" -netdev "user,id=net0" -device "rtl8139,netdev=net0" -audiodev "sdl,id=snd0" -device "AC97,audiodev=snd0" -serial file:build/serial.log
QEMU_EXIT_CODE=-1073741819
STDOUT_BEGIN

STDOUT_END
STDERR_BEGIN
C:\Users\syltu\scoop\apps\qemu\current\qemu-system-x86_64.exe: warning: Ignoring request for interrupt vector 0
STDERR_END
```

`-1073741819` is the signed representation of `3221225477` (`0xC0000005`). The Windows session query returned:

```text
UserInteractive=True SessionId=1 InputDesktop=Default
```

The active input desktop was `Default`, so the session was interactive and unlocked. A first attempt to launch through PowerShell `Start-Process -ArgumentList` split the quoted `-name` argument and returned `exit=1` with `Could not open 'OS'`; it was discarded. The result above came from an argv-preserving launch.

Backend-only startup probes used `-S -nodefaults` plus a QMP connect/quit, without opening either PollikFS disk. They separate display-plugin initialization from guest boot. Raw result counts for the complete display × CPU × accelerator matrix:

```text
display=gtk,gl=off cpu={unset,qemu64} accel={whpx,whpx,kernel-irqchip=off} exit_codes=[0,0,0,0] QMP=connected/quit
display=none cpu={unset,qemu64} accel={whpx,whpx,kernel-irqchip=off} exit_codes=[0,0,0,0] QMP=connected/quit
display=sdl,gl=off cpu={unset,qemu64} accel={whpx,whpx,kernel-irqchip=off} exit_codes=[0,0,0,0] QMP=connected/quit
```

Full-device probes used a disposable kernel copy and disk snapshots. GTK and `none` booted to the desktop with both omitted `-cpu` and explicit `-cpu qemu64`; QMP screenshots succeeded for `none`. `sdl,gl=off` greeted QMP but did not complete `qmp_capabilities` or reach the desktop within the 5-second probe deadline, for either CPU selection and both WHPX irqchip settings. Those full-device probes were stopped by the harness (`exit=1`), not reported as natural QEMU exits. This is distinct from the exact ordinary SDL launch above, which exited with the access violation.

## Headless launcher and data safety

`run.ps1 -Headless` selects `-display none`, `-cpu qemu64` for WHPX, no audio backend probe, QMP on an ephemeral loopback port, and global `-snapshot`. With `-Resolution auto`, it selects 1920×1080. It refuses to build or format missing images. The 1920×1080 `-NoLaunch` command construction produced:

```text
Acceleration selected: whpx; reason: explicit -Accel request; default CPU qemu64 (WHPX-compatible); explicit -Cpu is preserved
PollikOS: 1920x1080 (120 Hz / 120 FPS), 2 GiB RAM; accel=whpx; display=none; cpu=qemu64; smp=1; headless=True
Audio backend: none
Headless QMP: tcp:127.0.0.1:62552,server=on,wait=off; all disks use transient snapshots
QEMU command: qemu-system-x86_64 -name "Pollik OS v0.1 Alpha" -machine pc -accel whpx -cpu qemu64 -smp 1 -rtc base=utc -m 2G -device "VGA,vgamem_mb=32,refresh_rate=120" -display none -fw_cfg "name=opt/pollikos/display,string=1920x1080" -drive "format=raw,file=build/PollikOS-Alpha.img,if=ide,index=0" -drive "format=raw,file=build/PollikData.img,if=ide,index=1" -netdev "user,id=net0" -device "rtl8139,netdev=net0" -audiodev "none,id=snd0" -device "AC97,audiodev=snd0" -serial file:build/serial.log -qmp "tcp:127.0.0.1:62552,server=on,wait=off" -snapshot
```

The launcher itself was then run at 1024×768 from a new temporary directory with a copied kernel image and a freshly generated 40 MiB GUI-fixture PollikFS disk. It reached the desktop and captured a QMP screendump:

```text
GUI fixture: appearance installed before QEMU launch
PollikOS: 1024x768 (120 Hz / 120 FPS), 2 GiB RAM; accel=whpx; display=none; cpu=qemu64; smp=1; headless=True
Headless QMP: tcp:127.0.0.1:56486,server=on,wait=off; all disks use transient snapshots
GFX resolution: 1024x768 pitch=4096 bpp=32
[BOOT:INFO] Desktop items initialized
[WALLPAPER] path=/usr/share/wallpapers/light.png mount=ready
QMP_CAPABILITIES={"return": {}}
QMP_SCREENDUMP={"return": {}}
QMP_QUIT={"timestamp": ..., "event": "SHUTDOWN", "data": {"guest": false, "reason": "host-qmp-quit"}}
runps-headless.ppm Length=2359312
```

The workspace root `build\PollikData.img` and backup images were not built, synchronized, or formatted. The SDL crash and full-device startup diagnostics that referenced the root data path ran with QEMU `-snapshot`, so guest writes could not reach the base image. Benchmark workloads used a copied kernel image, QEMU snapshots, and freshly generated disposable GUI data disks. No before/after hash was captured for the root data image.

## PollikMark full-run baseline

Commands ran in the isolated temporary build, with `--headless --accel whpx --cpu qemu64`. The host deadline is 600 seconds. The earlier `PARTIAL` result was caused by the guest's 10-second `PM_LEVEL_MAX_US` deadline ending levels before they accumulated enough samples; the earlier 300-second host deadline was also too short for the full WHPX suite. The per-level deadline is now 30 seconds and the harness waits up to 600 seconds for all eight workloads and verifies guest serial results against the QMP-read result table.

Earlier incomplete output:

```text
INCOMPLETE test=1 level=3 status=2 samples=2 mean_us=4277206 work_us=5966 units=10000 rate=1676164
INCOMPLETE test=4 level=3 status=2 samples=2 mean_us=4288510 work_us=5953 units=10000 rate=1679825
INCOMPLETE test=4 level=4 status=0 samples=0 mean_us=0 work_us=0 units=0 rate=0
```

1920×1080 command result, from the guest serial log:

```text
POLLIKMARK_RESULT test=0 rate=2855138550 unit=px/s completed=1 levels=1
POLLIKMARK_RESULT test=1 rate=1843101 unit=shapes/s completed=4 levels=4
POLLIKMARK_RESULT test=2 rate=50354 unit=tris/s completed=1 levels=1
POLLIKMARK_RESULT test=3 rate=321431 unit=tris/s completed=3 levels=3
POLLIKMARK_RESULT test=4 rate=1635509 unit=tris/s completed=5 levels=5
POLLIKMARK_RESULT test=5 rate=52904879 unit=texpx/s completed=3 levels=3
POLLIKMARK_RESULT test=6 rate=73 unit=fps completed=1 levels=1
POLLIKMARK_RESULT test=7 rate=28327792673 unit=B/s completed=30 levels=30
POLLIKMARK_FULL_END
1920x1080 full PollikMark PASS: accel=whpx cpu=qemu64
1920x1080 FULL_RUN_SECONDS=350.9 (limit=600)
EXIT_CODE=0
```

1024×768 command result:

```text
POLLIKMARK_RESULT test=0 rate=1846059752 unit=px/s completed=1 levels=1
POLLIKMARK_RESULT test=1 rate=1848481 unit=shapes/s completed=4 levels=4
POLLIKMARK_RESULT test=2 rate=50187 unit=tris/s completed=1 levels=1
POLLIKMARK_RESULT test=3 rate=324957 unit=tris/s completed=3 levels=3
POLLIKMARK_RESULT test=4 rate=1444151 unit=tris/s completed=5 levels=5
POLLIKMARK_RESULT test=5 rate=50904415 unit=texpx/s completed=3 levels=3
POLLIKMARK_RESULT test=6 rate=71 unit=fps completed=1 levels=1
POLLIKMARK_RESULT test=7 rate=29165532034 unit=B/s completed=30 levels=30
POLLIKMARK_FULL_END
1024x768 full PollikMark PASS: accel=whpx cpu=qemu64
1024x768 FULL_RUN_SECONDS=350.8 (limit=600)
EXIT_CODE=0
```

Both memory rates exceed the old 32-bit ceiling `4294967295 B/s`, so saturation was not present in these runs. `MarkResult.rate` and `units` are 64-bit, the rate calculation uses `gfx_ratio64`, and serial/UI formatting is 64-bit. Native evidence:

```text
Command: python tests\pollikmark_native.py
RAW memory units=17179869184 rate=17179869184 formatted=17.1G
pollikmark native: PASS (rotation, raster counters/ABI, mixed shapes, clipping, alpha, resize/failure/leaks, detailed info text bounds)
```

## Guest frame intervals

The guest records presentation intervals from its calibrated TSC clock into `g_frame_interval_history` and advances `g_interval_idx` at frame completion. `Guest.capture()` briefly stops the vCPU through QMP, reads the ring and write index, then resumes it. The harness extracts the newest samples in chronological order, capped at 128, and reports nearest-rank p50/p95 plus the maximum. The idle scenario waits 10 seconds, then moves the cursor one pixel and waits for the wake frame; idle can still contain periodic guest frames, so its sample count is small.

Command: `python tests\benchmark_gui.py --headless --accel whpx --cpu qemu64 --resolution 1920x1080 --resolution 1024x768`.

```text
1920x1080 idle_10s detail: dirty=794829 px/frame; phase_us input/app/layout/draw/compose/LFB=34/0/1/153/273/67; frame mean/p95/max=10089/29770/29770 us; guest interval p50/p95/max=1770772/5368547/5368547 us (3 samples; WM TSC presentation intervals)
1920x1080 drag_held_10s detail: dirty=157374 px/frame; phase_us input/app/layout/draw/compose/LFB=11342/0/1/0/3/1; frame mean/p95/max=316/732/903 us; guest interval p50/p95/max=1188/27739/28736 us (128 samples; WM TSC presentation intervals)
1920x1080 window_open_animation detail: dirty=628611 px/frame; phase_us input/app/layout/draw/compose/LFB=3/0/1/0/564/41; frame mean/p95/max=2637/10372/10496 us; guest interval p50/p95/max=12954/26677/31536 us (25 samples; WM TSC presentation intervals)
1920x1080 [PERF OVERLAY PASS] F12 on/off; backbuffer panel pixel restored
1024x768 idle_10s detail: dirty=154932 px/frame; phase_us input/app/layout/draw/compose/LFB=64/0/0/153/444/80; frame mean/p95/max=340/677/677 us; guest interval p50/p95/max=682/5401436/5401436 us (2 samples; WM TSC presentation intervals)
1024x768 drag_held_10s detail: dirty=158583 px/frame; phase_us input/app/layout/draw/compose/LFB=32/0/0/0/1507/136; frame mean/p95/max=325/758/1643 us; guest interval p50/p95/max=1647/28602/42712 us (128 samples; WM TSC presentation intervals)
1024x768 window_open_animation detail: dirty=445019 px/frame; phase_us input/app/layout/draw/compose/LFB=4/0/0/0/1329/90; frame mean/p95/max=1730/2434/2644 us; guest interval p50/p95/max=13187/17875/25849 us (25 samples; WM TSC presentation intervals)
1024x768 [PERF OVERLAY PASS] F12 on/off; backbuffer panel pixel restored
EXIT_CODE=0
```

Timing summary (microseconds):

| Resolution | Scenario | Samples | p50 | p95 | Max |
|---|---|---:|---:|---:|---:|
| 1920×1080 | Idle, 10 s plus cursor wake | 3 | 1,770,772 | 5,368,547 | 5,368,547 |
| 1920×1080 | Held window drag, 10 s | 128 | 1,188 | 27,739 | 28,736 |
| 1920×1080 | Window-open animation | 25 | 12,954 | 26,677 | 31,536 |
| 1024×768 | Idle, 10 s plus cursor wake | 2 | 682 | 5,401,436 | 5,401,436 |
| 1024×768 | Held window drag, 10 s | 128 | 1,647 | 28,602 | 42,712 |
| 1024×768 | Window-open animation | 25 | 13,187 | 17,875 | 25,849 |

## Checks run and limits

```text
Command: python tests\gui_metrics_test.py
PASS GUI timing metrics: wrapped history order; nearest-rank p50/p95/max; idle uses PIT elapsed and records wake-frame interval

Command: python -m py_compile tests\pollikmark.py tests\benchmark_gui.py tests\gui_metrics.py
No output; exit status 0.
```

NOT RUN:

- TCG benchmarks. WHPX figures above are not substituted with TCG results.
- Full guest boots for all 12 display × CPU × irqchip combinations; the complete 12-case backend-only initialization matrix was run, while full guest boot probes covered the SDL crash, GTK, display-none, and SDL with GL disabled as detailed above.
- Pixel-by-pixel visual inspection of the saved QMP screenshots.
- A before/after hash comparison of the workspace's persistent PollikData image.

The measurement gate is complete. No performance optimization was started in this gate.
