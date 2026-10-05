# Boot loading and UI polish (2026-10-05)

## Measured cause and changes

The dominant delay was reading the wallpaper through scalar ATA PIO, not PNG
decoding or scaling. The i386 read path called the HAL for each of 256 words
per sector. It now uses `cld; rep insw` with exactly 256 words, preserving device
selection, DRQ/completion checks, bounded reset/retry and all write paths.
`build.ps1 -LegacyAtaRead` selects the original scalar read. The x86_64 ATA
path is unchanged.

The shared PollikFS reader also reread the same indirect pointer blocks for
every data block. One 1-KiB local cache per synchronous read now retains the
current pointer block and double-indirect outer entry. It expires at return;
the next read observes new metadata. No persistent cache, on-disk layout,
descriptor layout or syscall changes were introduced. EOF, sparse holes and
I/O error/offset behavior are checked against the actual shared reader.

The normal kernel still excludes the PNG payloads. All guest runs use
disposable data images or snapshots. Builds used `-NoSync`; this work did not
write the persistent user data image or its backups.

Window controls are circular within their existing hit rectangles. Active
title bars have a subtle one-row inset highlight. Buttons have an inset
highlight and distinct pressed/disabled states, including default and danger
buttons; previously those categories ignored the pressed/disabled state.
The Dock has a one-pixel rounded outline over the existing cached glass.
These are deliberate visual changes; no new blur or per-frame gradient was
added. The splash now identifies wallpaper loading and desktop preparation.

An existing full interaction test exposed a separate animation bug:
minimize completion wrote state flags directly, bypassing WM's saved state,
rectangle and focus handoff. Restore completion then forced NORMAL state and
geometry. Completion now uses `wm_minimize` and leaves the authoritative
geometry/state established by `wm_open`/`wm_unminimize` intact.

## Boot evidence

Host timings measure from QEMU process creation to the serial `desktop ready`
marker, including BIOS startup. They exclude `run.ps1` accelerator/audio
preflight and entering the user's password. The marker denotes a prepared
desktop/login screen. Log polling requests 5 ms intervals; Windows scheduling
and serial coalescing limit precision. WHPX, headless, qemu64, one vCPU,
256 MiB, 1920x1080, IDE snapshots, no NIC/audio were used throughout.

Initial baseline used a snapshot of the actual disk. Its selected theme changed
between boots; the source was not an immutable fixture and the writer was not
attributed. Report light/dark
runs separately, not a mixed-theme speedup. Later measurements use a stable
disposable dark copy of the repaired disk, with the same stock PNG bytes and
layout and a fixture account. This is not a claim of identical whole-disk
inputs or controlled host load across all runs.

```text
python tests\boot_loading.py --accel whpx --data build\PollikData.img --label boot-before --runs 3
RUN 1 HOST_MS 2669.95 [WALLPAPER] path=/usr/share/wallpapers/light.png mount=ready
RUN 1 HOST_MS 43781.85 [WALLPAPER] decoder=ok dimensions=1672x941
PASS boot run=1 accel=whpx resolution=1920x1080 ready_ms=44465.33
PASS boot run=2 accel=whpx resolution=1920x1080 ready_ms=39417.20
PASS boot run=3 accel=whpx resolution=1920x1080 ready_ms=39553.07

python tests\boot_loading.py --accel whpx --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-dark.img --image PollikOS-Alpha.img.pending --label boot-rep --runs 3
PASS boot run=1 accel=whpx resolution=1920x1080 ready_ms=10789.79
PASS boot run=2 accel=whpx resolution=1920x1080 ready_ms=8659.44
PASS boot run=3 accel=whpx resolution=1920x1080 ready_ms=8408.87
BOOT_MEAN_MS 9286.03 runs=3 accel=whpx

python tests\boot_loading.py --accel whpx --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-dark.img --label boot-cache --runs 3
PASS boot run=1 accel=whpx resolution=1920x1080 ready_ms=4399.49
PASS boot run=2 accel=whpx resolution=1920x1080 ready_ms=4286.41
PASS boot run=3 accel=whpx resolution=1920x1080 ready_ms=4384.24
BOOT_MEAN_MS 4356.71 runs=3 accel=whpx

python tests\boot_loading.py --accel whpx --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-dark.img --label boot-final --runs 3
PASS boot run=1 accel=whpx resolution=1920x1080 ready_ms=4853.23
PASS boot run=2 accel=whpx resolution=1920x1080 ready_ms=4370.08
PASS boot run=3 accel=whpx resolution=1920x1080 ready_ms=4345.83
BOOT_MEAN_MS 4523.05 runs=3 accel=whpx
```

The final timing cohort includes the visual polish; the later minimize/restore
fix was not retimed (it does not run before the ready marker). Its final kernel
was rebuilt. The normal launcher image and Surface image both contain it.
Guest wallpaper phase logs report PIT ticks, not milliseconds. The current
PIT divisor is 9943 at 1,193,180 Hz (approximately 120 Hz); zero ticks denotes
a phase shorter than its coarse observation interval. For example:

```text
RUN 3 HOST_MS 1241.55 [WALLPAPER] path=/usr/share/wallpapers/dark.png mount=ready
RUN 3 HOST_MS 3543.20 [WALLPAPER] read_ticks=267
RUN 3 HOST_MS 3739.33 [WALLPAPER] decode_ticks=24
RUN 3 HOST_MS 3762.25 [WALLPAPER] scale_ticks=3
```

Full QEMU commands and timelines are in `build/boot-{before,rep,cache,final}-whpx*.log`
and the matching `.json` files. This measures boot loading, not ongoing desktop
FPS or frame-time percentiles.

## Focused test evidence

Before the shared read optimization:

```text
python tests\pollikfs_read_native.py
READ sectors=2938 pointer_sectors=1776 bytes=593920
PASS PollikFS native read: full-byte golden pattern, three boundary crossings, EOF, sparse hole, per-call metadata refresh, eight I/O failures
```

After, with exact read-count assertions enabled:

```text
python tests\pollikfs_read_native.py --expect-cache
READ sectors=1172 pointer_sectors=10 bytes=593920
PASS PollikFS native read: full-byte golden pattern, three boundary crossings, EOF, sparse hole, per-call metadata refresh, eight I/O failures
```

Button test before the fix failed `fill_color!=normal`; after:

```text
python tests\ui_button_native.py
PASS button states: light/dark x normal/default/danger; pressed, disabled, hover; drawing bounds (84 checks)
python tests\app_layout.py
PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping (37182 checks)
python tests\gui_registry.py
PASS: gui registry (64827 checks)
python tests\held_drag.py
PASS rect legacy/primitive pixel parity scene=29cdb7eb565afb73 hardware=dcec3595897d7888
```

Actual framebuffer probes (each uses `--data` with the respective disposable
`boot-light.img`/`boot-dark.img` path above) passed at both resolutions:

```text
python tests\ui_polish_probe.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img --theme light
PIXEL title-highlight 810,126 expected=f8f7fa screenshot=f8f7fa
PIXEL close-control 188,142 expected=ef4444 screenshot=ef4444
PIXEL dock-outline 960,984 expected=f4f5ff screenshot=f4f5ff
PASS UI polish light 1920x1080: Settings focus, completed animation, exact scene/LFB/screenshot chrome and dock pixels
python tests\ui_polish_probe.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-dark.img --theme dark --resolution 1024x768
PASS UI polish dark 1024x768: Settings focus, completed animation, exact scene/LFB/screenshot chrome and dock pixels
python tests\ui_polish_probe.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img --theme light --resolution 1024x768
PASS UI polish light 1024x768: Settings focus, completed animation, exact scene/LFB/screenshot chrome and dock pixels
python tests\ui_polish_probe.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-dark.img --theme dark --resolution 1920x1080
PASS UI polish dark 1920x1080: Settings focus, completed animation, exact scene/LFB/screenshot chrome and dock pixels
```

Screenshots: `build/ui-polish-{light,dark}-{1920x1080,1024x768}.png`.
The light 1920x1080 screenshot was also visually inspected.

```text
python tests\wallpaper_probe.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img --resolution 1920x1080
PASS synced wallpaper light.png: five independent cache/scene pixel samples; three LFB samples; fallback absent
SNAPSHOT original SHA256 before=9c009d3baf3784fa9395798f0e7699f8e91080ed6c6a1bf0d355bb9c23dd3df0 after=9c009d3baf3784fa9395798f0e7699f8e91080ed6c6a1bf0d355bb9c23dd3df0 bytes=33587200
python tests\smoke.py --notes-only
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: Settings lists PollikFS wallpapers and persists the selected name
PASS: pointer acceleration can be disabled and persists
python tests\corners_guards.py --resolution 1024x768
PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption
python tests\resize_layout.py --resolution 1024x768 --skip-native
PASS 1024x768: all seven apps, 49 viewport-content stages, external guards
python tests\process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 7344 / [PROC] [TEST] PMM free pages after:  7344
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 56448 / [PROC] [TEST] PMM free pages after:  56448
```

The minimize regression reproduced before its fix with `prior=0` and a hidden
window still marked focused. After rebuilding:

```text
python tests\window_restore.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img
MINIMIZE window=(2, 0, 0, 32, 1024, 640, 1, 1, 1, 0, 0, 1, 480, 280, 1024, 768, 210, 145, 680, 410, 1505182) prior=2
PASS maximized Terminal minimize/restore: state and authoritative rectangle retained; focus handed off; full surface/scene/LFB restored
```

No existing assertion or expected pixel value was modified. The full
`surface_bounds.py --resolution 1024x768 --wm` now passes minimize/restore, but
still fails line 247, `active dock click minimized app`. Its assertion demands
that clicking an already focused app does not minimize it. Existing
`desktop.c:dock_activate_app` explicitly implements this toggle, and was not
changed by this work. The assertion remains unchanged; the run is a FAILURE,
not a passing interaction suite. Other resize/guard checks above remain separate
evidence.

## Builds and remaining verification

```text
.\build.ps1 -NoSync
PollikOS built: build/PollikOS-Alpha.img (820656 kernel bytes, 1603 sectors loaded at 1 MiB)
.\build.ps1 -NoSync -ImageName PollikOS-Surface.img
PollikOS built: build/PollikOS-Surface.img (820656 kernel bytes, 1603 sectors loaded at 1 MiB)
.\build-x86_64.ps1
Built build/x86_64/kernel/PollikOS-x86_64.img (440451 kernel bytes)
.\build-x86_64.ps1 -SelfTest
Built build/x86_64/selftest/PollikOS-x86_64.img (530675 kernel bytes)
python tests\x86_64_storage.py
PASS: missing/corrupt/unsupported/truncated storage refused without image changes
```

i386 `build/kernel.bin`: 820,000 bytes before, 820,656 bytes after (+656).
The currently enforced build cap is 4 MiB, as established in the earlier size
attribution. Physical hardware, AHCI performance, installer end-to-end and
new full PollikMark/frame-pacing measurements are NOT RUN in this work.
Additional completed checkpoint commands:

```text
python tests\surface_bounds.py --resolution 1920x1080
PASS: 1920x1080, all 7 surfaces maximize/restore, runtime LFB and external guards
python tests\smoke.py
PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock
PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping
```

The complete x86_64 boot suite finished with exit status 0:

```text
python tests\x86_64_boot.py
CONSOLE PASS
PASS: selftest-qemu64-16
PASS: selftest-qemu64-64
PASS: selftest-qemu64-256
PASS: selftest-qemu64-5120
PASS: selftest-qemu64-32768
PASS: selftest-qemu64-64-reboot1
PASS: selftest-qemu64-64-reboot2
PASS: selftest-qemu64-64-full
PASS: kernel-qemu64-64
PASS: kernel-qemu64-64-reboot
PASS: kernel-pentium3-64
PASS: kernel-qemu64_-lm-64
PASS: kernel-qemu64_-nx-64
PASS: kernel-qemu64_-pae-64
PASS: kernel-qemu64_-msr-64
PASS: kernel-qemu64_-syscall-64
```

The regular boot suite's native spawn churn is its ordinary short prefix, not
the separately configured 5,000-cycle stress. The dedicated stress and timer
perturbation modes are NOT RUN in this loading/UI change. Browser-specific
network suites and performance comparisons for ongoing desktop rendering are
also NOT RUN. No claim of unchanged frame-time percentiles is made.

Raw marker count and frame balance from the fresh 64-MiB selftest serial log:

```python
from pathlib import Path
import re
s = Path('build/x86_64/selftest-qemu64-64.log').read_text()
print(len(re.findall(r'\[[A-Z0-9]+\] PASS:', s)))
print(s.count('[X64] SELFTEST PASS'))
```

```text
144
1
[MM64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[C6] PASS: 100 spawn/wait lifecycles return PMM exactly to baseline
[X64] SELFTEST PASS
```

Final payload verification:

```text
python tests\wallpaper_payload.py
PASS: main kernel excludes PNGs; installer payload includes both (2439656 bytes)
```
