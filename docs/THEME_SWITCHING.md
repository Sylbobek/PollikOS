# Theme switching, Settings and reversible window animations (2026-10-05)

## Changes

- Both stock PNGs remain PollikFS files under `/usr/share/wallpapers`. After reserving WM surfaces, boot decodes and scales each theme once. Theme/accent invalidation selects an existing buffer; it does not re-read, decode or scale PNGs.
- Wallpaper follows theme. Legacy `wallpaper=` values are ignored and omitted on the next normal Settings save; no image migration or on-disk format change. The independent picker is removed.
- Appearance now has theme selection, readable accent labels and a selection ring, and a short explanation of the matching wallpaper. Removed misleading acrylic/hardware/120-FPS labels. Animations Off now suppresses window transitions and finishes pending minimize transitions.
- Open/close/minimize/restore reversals preserve displayed geometry and alpha. Duplicate requests do not restart the same transition. WindowAnim remains its existing size; no syscall changes.
- No edits to the real PollikData image by this work. Builds used `-NoSync`; runtime tests used disposable data disks or snapshots of the fixture below. No stash, revert or commit was performed by this agent. HEAD changed externally during work; this report describes the built working tree, not attribution to a single Git commit.

## Reproduction before the repair

Actual output from the first successful reproduction on the old binary:

```text
python tests/animation_continuity.py
COMMAND clang -O2 -fno-builtin -Wall -Wextra -Werror -fuse-ld=lld tests/animation_continuity.c -o build/animation_continuity.exe
FAIL line 16: after->cur_alpha==before.cur_alpha
```

The OPEN start alpha was 110, but its update at elapsed=0 assigned 140. CLOSE also restarted from authoritative geometry/alpha rather than the displayed animation frame.

```text
python tests/theme_switch.py --data FIXTURE --accel whpx
PIXEL theme=0 x=984 y=384 expected=385698 actual=385698
PIXEL theme=0 x=964 y=608 expected=0a1734 actual=0a1734
PIXEL theme=0 x=904 y=558 expected=061126 actual=061126
SWITCH 0 theme=0 observer_ms=2991.88 PNG_counts=(2, 2, 2, 2) baseline_counts=(1, 1, 1, 1) free_pages=56438
AssertionError: theme switch repeated disk/decode/scale work
```

`observer_ms` includes QMP/host polling, PS/2 button down/up and observing presentation. It is not a pure kernel phase timer or frame-rate promise. PNG_counts = (path, decode, scale, cache-ready). Before/after measurements use WHPX headless with 256 MiB; old reproduction at 1024x768.

The new test originally sampled PMM while boot's autonomous spawn/exit tests still owned ten transient pages. It now waits for the actual `[TEST] PMM no leak` marker before recording the baseline; the equality assertion remains exact. Its first draft also had a conflicting host memset prototype and the wrong PMM symbol; both harness errors were corrected before kernel fixes.

## Raw verification

FIXTURE = `C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img` (known test account; QEMU snapshot mode).

### Build normal

```text
.\build.ps1 -NoSync
USB installer built: build/PollikOS-USB-Installer.img (4081324 kernel bytes)
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (820680 kernel bytes, 1603 sectors loaded at 1 MiB)
```

### Build Surface

```text
.\build.ps1 -NoSync -ImageName PollikOS-Surface.img
USB installer built: build/PollikOS-USB-Installer.img (4081324 kernel bytes)
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Surface.img (820680 kernel bytes, 1603 sectors loaded at 1 MiB)
```

### Native animation

```text
python tests/animation_continuity.py
PASS animation continuity: 100 open/close reversals, alpha at t=0, minimize/restore reversal, duplicate close, final state (1823 checks)
```

### Theme and Settings 1024

```text
python tests/theme_switch.py --data FIXTURE --accel whpx
PIXEL theme=0 x=984 y=384 expected=385698 actual=385698
PIXEL theme=0 x=964 y=608 expected=0a1734 actual=0a1734
PIXEL theme=0 x=904 y=558 expected=061126 actual=061126
SWITCH 0 theme=0 observer_ms=176.58 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=55680 baseline=55680
PIXEL theme=1 x=984 y=384 expected=fefcf3 actual=fefcf3
PIXEL theme=1 x=964 y=608 expected=f1eae8 actual=f1eae8
PIXEL theme=1 x=904 y=558 expected=dbd9de actual=dbd9de
SWITCH 1 theme=1 observer_ms=168.70 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=55680 baseline=55680
PIXEL theme=0 x=984 y=384 expected=385698 actual=385698
PIXEL theme=0 x=964 y=608 expected=0a1734 actual=0a1734
PIXEL theme=0 x=904 y=558 expected=061126 actual=061126
SWITCH 2 theme=0 observer_ms=231.16 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=55680 baseline=55680
PIXEL theme=1 x=984 y=384 expected=fefcf3 actual=fefcf3
PIXEL theme=1 x=964 y=608 expected=f1eae8 actual=f1eae8
PIXEL theme=1 x=904 y=558 expected=dbd9de actual=dbd9de
SWITCH 3 theme=1 observer_ms=151.32 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=55680 baseline=55680
PIXEL theme=0 x=984 y=384 expected=385698 actual=385698
PIXEL theme=0 x=964 y=608 expected=0a1734 actual=0a1734
PIXEL theme=0 x=904 y=558 expected=061126 actual=061126
SWITCH 4 theme=0 observer_ms=181.08 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=55680 baseline=55680
PIXEL theme=1 x=984 y=384 expected=fefcf3 actual=fefcf3
PIXEL theme=1 x=964 y=608 expected=f1eae8 actual=f1eae8
PIXEL theme=1 x=904 y=558 expected=dbd9de actual=dbd9de
SWITCH 5 theme=1 observer_ms=172.92 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=55680 baseline=55680
PASS 6 theme switches: stock PNG pixels exact, no disk/decode/scale, PMM unchanged
PASS five accent choices; former wallpaper row is inert; Animations Off opens/closes instantly; PMM unchanged
```

### Theme 1920

```text
python tests/theme_switch.py --data FIXTURE --accel whpx --resolution 1920x1080
PIXEL theme=0 x=1880 y=540 expected=365da1 actual=365da1
PIXEL theme=0 x=1860 y=920 expected=040b1b actual=040b1b
PIXEL theme=0 x=1800 y=870 expected=050e22 actual=050e22
SWITCH 0 theme=0 observer_ms=156.88 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=43110 baseline=43110
PIXEL theme=1 x=1880 y=540 expected=fef2d3 actual=fef2d3
PIXEL theme=1 x=1860 y=920 expected=d7d8e4 actual=d7d8e4
PIXEL theme=1 x=1800 y=870 expected=e5e1e5 actual=e5e1e5
SWITCH 1 theme=1 observer_ms=176.55 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=43110 baseline=43110
PIXEL theme=0 x=1880 y=540 expected=365da1 actual=365da1
PIXEL theme=0 x=1860 y=920 expected=040b1b actual=040b1b
PIXEL theme=0 x=1800 y=870 expected=050e22 actual=050e22
SWITCH 2 theme=0 observer_ms=147.54 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=43110 baseline=43110
PIXEL theme=1 x=1880 y=540 expected=fef2d3 actual=fef2d3
PIXEL theme=1 x=1860 y=920 expected=d7d8e4 actual=d7d8e4
PIXEL theme=1 x=1800 y=870 expected=e5e1e5 actual=e5e1e5
SWITCH 3 theme=1 observer_ms=180.19 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=43110 baseline=43110
PIXEL theme=0 x=1880 y=540 expected=365da1 actual=365da1
PIXEL theme=0 x=1860 y=920 expected=040b1b actual=040b1b
PIXEL theme=0 x=1800 y=870 expected=050e22 actual=050e22
SWITCH 4 theme=0 observer_ms=176.33 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=43110 baseline=43110
PIXEL theme=1 x=1880 y=540 expected=fef2d3 actual=fef2d3
PIXEL theme=1 x=1860 y=920 expected=d7d8e4 actual=d7d8e4
PIXEL theme=1 x=1800 y=870 expected=e5e1e5 actual=e5e1e5
SWITCH 5 theme=1 observer_ms=206.03 PNG_counts=(2, 2, 2, 2) baseline_counts=(2, 2, 2, 2) free_pages=43110 baseline=43110
PASS 6 theme switches: stock PNG pixels exact, no disk/decode/scale, PMM unchanged
```

### Spam WHPX

```text
python tests/window_spam.py --data FIXTURE --accel whpx
PASS 20 real close/open reversals during active animations; final focus/surface/scene/LFB; free_pages 55680 -> 55680
```

### Spam TCG

```text
python tests/window_spam.py --data FIXTURE --accel tcg
PASS 20 real close/open reversals during active animations; final focus/surface/scene/LFB; free_pages 55680 -> 55680
```

### Process balance

```text
python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 6576 / [PROC] [TEST] PMM free pages after:  6576
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 55680 / [PROC] [TEST] PMM free pages after:  55680
```

### Registry

```text
python tests/gui_registry.py
PASS: open/close/resize
PASS: poll bitmask and reset
PASS: gui registry (64827 checks)
```

### App layout

```text
python tests/app_layout.py
PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping (37182 checks)
```

### Notes smoke

```text
python tests/smoke.py --notes-only
RAW Notes F11 window (0, 32, 1024, 640, 2)
RAW Notes F11 window (170, 125, 680, 410, 0)
PASS: cursor visible at four corners; stationary-cursor scene repaint
RAW wallpaper LFB changed pixels 33480
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override
PASS: pointer acceleration can be disabled and persists
```

### Full smoke

```text
python tests/smoke.py
RAW Notes F11 window (0, 32, 1024, 640, 2)
RAW Notes F11 window (170, 125, 680, 410, 0)
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock
PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping
```

### Corrupt stock PNG

```text
python tests/smoke.py --notes-only --corrupt-wallpaper
RAW Notes F11 window (0, 32, 1024, 640, 2)
RAW Notes F11 window (170, 125, 680, 410, 0)
PASS: cursor visible at four corners; stationary-cursor scene repaint
RAW wallpaper LFB changed pixels 33480
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override
PASS: pointer acceleration can be disabled and persists
```

### Missing stock PNGs

```text
python tests/smoke.py --notes-only --no-wallpapers
RAW Notes F11 window (0, 32, 1024, 640, 2)
RAW Notes F11 window (170, 125, 680, 410, 0)
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: missing wallpaper files use one procedural fallback log
```

### Corner guards

```text
python tests/corners_guards.py --resolution 1024x768
PASS boot: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS maximized: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=1024x640
PASS restored: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption
```

### Resize

```text
python tests/resize_layout.py --resolution 1024x768 --skip-native
PASS 1024x768 PollikMark3D maximized [0, 32, 1024, 640]
PASS 1024x768 PollikMark3D restored [8, 39, 640, 480]
PASS 1024x768: all seven apps, 49 viewport-content stages, external guards
```

### Surface bounds

```text
python tests/surface_bounds.py --resolution 1920x1080
PASS: 1920x1080, all 7 surfaces maximize/restore, runtime LFB and external guards
```

### Light screenshot

```text
$env:POLLIK_GUI_ACCEL="whpx"; python tests/ui_polish_probe.py --data FIXTURE --theme light --resolution 1920x1080
PIXEL title-highlight 810,126 expected=f8f7fa screenshot=f8f7fa
PIXEL close-control 188,142 expected=ef4444 screenshot=ef4444
PIXEL dock-outline 960,984 expected=f4f5ff screenshot=f4f5ff
PASS UI polish light 1920x1080: Settings focus, completed animation, exact scene/LFB/screenshot chrome and dock pixels
```

### Dark screenshot

```text
python tests/ui_polish_probe.py --data boot-dark.img --theme dark --resolution 1920x1080 (POLLIK_GUI_ACCEL=whpx)
PIXEL title-highlight 810,126 expected=2a2e3a screenshot=2a2e3a
PIXEL close-control 188,142 expected=ef4444 screenshot=ef4444
PIXEL dock-outline 960,984 expected=3d465c screenshot=3d465c
PASS UI polish dark 1920x1080: Settings focus, completed animation, exact scene/LFB/screenshot chrome and dock pixels
```

### Boot

```text
python tests/boot_loading.py --data FIXTURE --accel whpx --runs 1 --label theme-boot
RUN 1 HOST_MS 4972.05 [WALLPAPER] decoder=ok dimensions=1672x941
RUN 1 HOST_MS 4977.75 [WALLPAPER] scale_ticks=0
RUN 1 HOST_MS 4977.75 [WALLPAPER] cache=ready
RUN 1 HOST_MS 4994.62 [WALLPAPER] path=/usr/share/wallpapers/dark.png mount=ready
RUN 1 HOST_MS 7553.23 [WALLPAPER] read_ticks=300
RUN 1 HOST_MS 7648.19 [WALLPAPER] decode_ticks=12
RUN 1 HOST_MS 7648.19 [WALLPAPER] decoder=ok dimensions=1672x941
RUN 1 HOST_MS 7726.51 [WALLPAPER] scale_ticks=9
RUN 1 HOST_MS 7726.51 [WALLPAPER] cache=ready
RUN 1 HOST_MS 7771.23 VBE ready; PS/2 ready; desktop ready
PASS boot run=1 accel=whpx resolution=1920x1080 ready_ms=7771.23
BOOT_MEAN_MS 7771.23 runs=1 accel=whpx
```

### Native compositor oracle

Executed before the final low-RAM branch and Settings-only adjustment; compositor drawing/damage code did not change afterward:

```text
python tests/held_drag.py
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
PIXEL_HASH scene=29cdb7eb565afb73 hardware=dcec3595897d7888
PASS rect legacy/primitive pixel parity scene=29cdb7eb565afb73 hardware=dcec3595897d7888
```

```text
python tests/window_restore.py --data FIXTURE
MINIMIZE window=(2, 0, 0, 32, 1024, 640, 1, 1, 1, 0, 0, 1, 480, 280, 1024, 768, 210, 145, 680, 410, 1505234) prior=2
PASS maximized Terminal minimize/restore: state and authoritative rectangle retained; focus handed off; full surface/scene/LFB restored
```

The new spam harness initially observed `open=1` before the client callbacks and OPEN animation setup finished under TCG (type was still CLOSE). It now waits for both the logical open and OPEN type before asserting the visual state. The active/type/bounds/alpha assertions remain. The corrected run is above. This was an observation race in the new test, not a removed existing assertion.

The corrupt-PNG fixture initially attempted install_file over an existing stock name, which the installer correctly rejected. The harness now removes/replaces light.png only inside its freshly generated disposable disk.

## Existing assertion changes

Only smoke's independently selected wallpaper contract changed, as explicitly requested:

| Before | After / reason |
|---|---|
| Click wallpaper filename row with relative offsets | Click Light theme tile using actual Settings window coordinates; picker was intentionally removed |
| Wait for cache-ready count to increase | Require count unchanged across switch; both caches are prepared at boot |
| Require `wallpaper=light.png` or `wallpaper=corrupt.png` persisted | Require `theme=1` and no independent `wallpaper=` override |
| Corrupt arbitrary selectable corrupt.png | Corrupt stock light.png in disposable fixture, because Light now owns this asset |
| Relative offsets to Dock tab / pointer toggle | Absolute coordinates relative to the measured Settings window; these controls stay in the same layout |

The >1000 changed desktop pixels assertion remains (>33480 observed). No Notes editing, wheel, cursor, corner, scheduler, networking, memory balance, geometry or golden-pixel assertion was weakened. Other existing test bodies were unchanged; held_drag gained two painter stubs for the added internal API.

## Size, boot cost, limitations

Before is the already-built preceding checkpoint, recorded in BOOT_LOADING_UI.md (not rebuilt or reverted here):

```text
.\build.ps1 -NoSync
PollikOS built: build/PollikOS-Alpha.img (820656 kernel bytes, 1603 sectors loaded at 1 MiB)
```

Actual final binary and image payload check (Python read_bytes and comparison from byte offset 4608):

```text
KERNEL_BINARY build/kernel.bin bytes=820680
EMBEDDED Background_LightTheme.png=False
EMBEDDED Background_BlackTheme.png=False
IMAGE_KERNEL_MATCH PollikOS-Alpha.img=True
IMAGE_KERNEL_MATCH PollikOS-Surface.img=True
```

Delta +24 B; PNG payloads are not linked into the normal kernel. Installer payload still includes wallpaper installation assets by design.

Second full-resolution cache costs 768 pages = 3,145,728 B at 1024x768, or 2025 pages = 8,294,400 B at 1920x1080. Compared to preceding default-resolution process baselines, 7344 -> 6576 and 56448 -> 55680 reflect this permanent 768-page allocation; within each stress run before/after remain exactly equal.

Boot now prepares both themes before exposing the desktop. The recorded 1920x1080 WHPX run was 7771.23 ms including QEMU/BIOS startup; the preceding checkpoint recorded 4523.05 ms mean across three dark-theme runs (different single-theme warmup, not a controlled same-theme timing comparison). This trades longer boot and RAM for avoiding first-switch I/O/decode stalls. No boot-speed improvement is claimed for this patch.

If the optional second allocation fails, both modes use procedural backgrounds; switches perform no disk I/O or PNG decode but do generate a gradient in the shared buffer. Forced allocation-failure test: NOT RUN. Settings persistence still uses the existing synchronous VFS write. Sub-25-ms presentation under theme changes is not demonstrated; measured observer latencies are above. Stock-pixel tolerances are zero.

## Checkpoint

| Item | Status | Evidence |
|---|---|---|
| Theme and matching wallpaper | Implemented and verified | 6 switches at each resolution; exact stock pixels; PNG counts unchanged |
| Accent / Settings / Animation Off | Implemented and verified | Five accents, former picker row inert, immediate open/close with Off |
| Spam animations | Implemented and verified | 1823 native checks; 20 actual reversals under each WHPX and TCG |
| i386 regression checkpoint | Verified for commands above | Full/Notes/corrupt/missing smoke, registry/layout/resize/surface/corners, process balance |
| User image / syscall / filesystem compatibility | Preserved by scope | NoSync builds, snapshot/disposable runtime disks, no syscall/format changes |

NOT RUN in this patch: x86_64 build/selftest/boot/storage suites; browser_cooperative/browser_responsive/browser_js suites; installer completion on ATA/AHCI; physical hardware; forced second-cache allocation failure; no-data-disk smoke variant; fresh PollikMark and frame-time benchmark. Older results are not new verification.

Screenshots: build/ui-polish-light-1920x1080.png and build/ui-polish-dark-1920x1080.png (QMP screendump, exact chrome/dock scene/LFB/screenshot checks above).
