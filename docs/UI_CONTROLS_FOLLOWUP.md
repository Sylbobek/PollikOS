# Calculator, sliders and benchmark resize — 2026-10-06

## Implemented

- Calculator: right-aligned larger result, rounded gray keys and orange operators;
  shared allocation-free expression engine, GUI buttons and terminal `calc`.
- Own vector Calculator/PollikMark icons in `kernel/desktop.c`, rendered at the
  requested icon size with the existing graphics primitives.
- Volume (0–100) and cursor-size sliders (100/125/150/200%). The preview changes
  during dragging; the existing settings store is written on release or close.
  `audio_volume` is an optional text setting; older configurations remain readable.
  Existing cursor-size buttons remain available.
- Calculator keyboard/click dispatch lets the client damage its readout instead
  of invalidating every button. Clicking an already focused window no longer
  requests another full scene through `focus_app`.
- Interactive benchmark resize, snap, maximize and restore keep the current
  workload buffers and samples. Reallocation waits until the run ends or the
  next explicit start. Programmatic resize behavior and its allocation-failure
  tests remain covered. Completed results survive. Keep geometry fixed when
  comparing Compositor results: changing the visible window changes that load.
- The played-sound status pill was wider than Settings at its 640px minimum.
  Its width is now bounded by the card.

No syscall numbers, syscall structures or PollikFS disk format changed.
The host builds below used `-NoSync`. Guests used disposable disks or snapshots;
no command in this follow-up writes the real `build/PollikData.img`.
Its hash was not measured, so this is not a before/after hash claim.

## Calculator opening and repaint evidence

The currently built image did not reproduce a permanent opening freeze.
The initial Calculator implementation earlier in this session had invalid font
scale 0; `font_glyphs[scale - 1]` requires scales 1–5. The callers now use valid
scales, and the native layout test asserts that requirement for every glyph.
The earlier x87 power validation also failed `2^3^2` under TCG. Exact binary64
integer validation replaced the unsafe conversion path; its precise emulator/
generated-code interaction was not conclusively attributed.

The following measurements isolate ten typed digits after the Dock launch
animation has ended. QMP observes guest counters; no guest memory is written.
Before uses the existing 827576-byte image, after uses the 829368-byte image.

```powershell
python tests/calculator_gui.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img --accel whpx --resolution 1920x1080 --label calculator-before
RAW rapid Calculator: cycles=12 host_seconds=0.828 ticks=98 frames=47
RAW calculator editing: presents=10 composed_pixels=2040600 presented_pixels=2040600

python tests/calculator_gui.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img --accel tcg --resolution 1024x768 --label calculator-after
RAW keyboard model expression=b'2^3^2' result=b'512' focus=7
RAW rapid Calculator: cycles=12 host_seconds=3.219 ticks=387 frames=20
RAW calculator editing: presents=10 composed_pixels=340000 presented_pixels=340000
PASS calculator partial/full render: 180000 pixels identical
PASS calculator: terminal expressions/errors, GUI buttons/keyboard, real framebuffer

python tests/calculator_gui.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img --accel whpx --resolution 1920x1080 --label calculator-after
RAW rapid Calculator: cycles=12 host_seconds=0.906 ticks=105 frames=51
RAW calculator editing: presents=10 composed_pixels=340000 presented_pixels=340000
PASS calculator partial/full render: 180000 pixels identical
PASS calculator: terminal expressions/errors, GUI buttons/keyboard, real framebuffer
```

The measured pixel count falls by about 6× (83.3%). This is a pixel-work
comparison, **not** a claim of 6× higher FPS. The golden comparison reads the
cached surface after partial editing, then maximizes/restores to force a full
render, and requires every pixel to match with zero tolerance.

Screenshots: `build/calculator-after-1920x1080.png`,
`build/calculator-after-1024x768.png`, `build/settings-volume-1920x1080.png`,
`build/settings-cursor-1920x1080.png`.

## Reproduced bugs and runtime controls

```powershell
python tests/pollikmark_native.py # before fix; build/pollikmark-resize-before.txt
FAIL 159: target.color==color && allocations==allocated && level_start==start_time

python tests/pollikmark_native.py # after fix
RAW memory units=17179869184 rate=17179869184 formatted=17.1G
RAW live resize: changes=40 allocations=0 samples=2 running=1
pollikmark native: PASS (rotation, raster counters/ABI, mixed shapes, clipping, alpha, resize/failure/leaks, detailed info text bounds)

python tests/app_layout.py # before bounding the played-sound pill
RAW bounds: tab=3 x=548 y=309 w=100 h=24 window=640x520
FAIL 33: w >= 0 && h >= 0 && x >= 0 && y >= 34 && x + w <= sw && y + h <= sh (app 4, 640x520)

python tests/ui_controls.py --data C:\Users\syltu\AppData\Local\Temp\pollikos-wallpaper-repair-u3do0z55\boot-light.img --accel whpx --resolution 1920x1080
RAW volume slider: left=0 right=100 middle=50
RAW cursor slider: left=100 right=200
RAW guest live resize: changes=12 status=1 samples=3384 rate=1269735810 px/s window=(716, 434)
RAW benchmark maximize/restore: status=1 samples=81 rate=1025166900 px/s
RAW settings file: audio_volume=50 cursor_size=200 (guest VFS readback)
PASS PS/2 sliders: clamps/persistence; benchmark completes during resize/maximize/restore
```

The native slider tests also require no settings write during dragging, one
write on release, clamping outside the track, and flushing an interrupted drag
on window close. Volume tests verify state and persistence; audible output on
physical audio hardware was NOT RUN.

## Full PollikMark result — explicitly PARTIAL

```powershell
python tests/pollikmark.py --resolution 1920x1080 --full-run --headless --accel whpx
PollikMark timer: source=1 resolution_us=1 tsc_khz=2491070
GUEST_SERIAL Fill Rate: 1424451172 px/s (1/1 levels)
GUEST_SERIAL 2D Shapes: 987453 shapes/s (4/4 levels)
GUEST_SERIAL Triangle: 23640 tris/s (1/1 levels)
GUEST_SERIAL Cube: 172519 tris/s (3/3 levels)
GUEST_SERIAL Geometry: 855435 tris/s (4/5 levels)
GUEST_SERIAL Texture: 28188820 texpx/s (3/3 levels)
GUEST_SERIAL Compositor: 39 fps (1/1 levels)
GUEST_SERIAL Memory: 15559160000 B/s (30/30 levels)
1920x1080 full PollikMark PARTIAL (not every level completed): accel=whpx cpu=qemu64
1920x1080 FULL_RUN_SECONDS=277.3 (limit=600)
```

All eight results were printed, but the highest Geometry level reached the
existing 30s deadline before the required three completed iterations. Raw row:
`[2, 1, 15763539, 15763539, 15763539, 0, 0, 944733, 10585, 10000, 0, 0, 0, 0, 0]`.
Status 2 is excluded from the score. Its one complete interval was 15.763539s;
the reported measured slice work was 10585us. Cooperative scheduling overhead
needs a separate investigation; the timeout/assertions were not relaxed.

Compositor CPU frame durations: `[80, 1226, 1778, 2561]` = count, mean, p95,
max in microseconds. These are **frame durations, not presentation intervals**.
Presentation still measured 39 fps; this follow-up does not claim 60 Hz.

To send results, use the screenshot or attach
`build/pollikmark-full-1920x1080-whpx-qemu64.json`. It contains environment,
raw level rows, statuses and the serial summary; not just the displayed score.

## Regression checkpoint: actual commands and output

| Command | Actual output line |
| --- | --- |
| `python tests/calc_native.py` | `PASS calculator: precedence, decimals, powers, functions, percent, scientific notation, errors/depth/bounds, button/keyboard model` |
| `python tests/gui_registry.py` | `PASS: gui registry (68372 checks)` |
| `python tests/app_layout.py` | `PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping, Calculator (214114 checks)` |
| `python tests/held_drag.py` | `PASS rect legacy/primitive pixel parity scene=29cdb7eb565afb73 hardware=17e73028b77840f0` |
| `python tests/soft3d_native.py` | `soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)` |
| `python tests/browser_cooperative.py` | `PASS: 972 cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input` |
| `python tests/surface_bounds.py --resolution 1920x1080` | `PASS: 1920x1080, all 8 surfaces maximize/restore, runtime LFB and external guards` |
| `python tests/corners_guards.py --resolution 1024x768` | `PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption` |
| `python tests/resize_layout.py --skip-native` | `PASS 1024x768: all eight apps, 56 viewport-content stages, external guards` and `PASS 1920x1080: all eight apps, 56 viewport-content stages, external guards` |
| `python tests/browser_responsive.py --resolution 1024x768` | `PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies` |
| `python tests/browser_responsive.py --resolution 1920x1080` | `PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies` |
| `python tests/smoke.py` | `PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock` and `PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping` |
| `python tests/calculator_x64.py` | `PIXELS result 5 and 63: 5248 RGB pixels each identical to SDK font oracle`; `PASS native x86-64 calculator: shell, floating point, PS/2 keys/buttons, framebuffer, clean exit status 0` |
| `python tests/pollikmark.py --resolution 1920x1080 --headless --accel whpx` | `1920x1080 PASS triangle (1, 556, 3601, 20, 30242, 35, 277, 5158, 107775, 556, 0, 0, 0, 0, 0) compositor (1, 80, 0, 0, 0, 0, 39, 39, 0, 0, 493, 1746, 137, 2377, 0)` |

```powershell
python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 5807 / [PROC] [TEST] PMM free pages after:  5807
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 54911 / [PROC] [TEST] PMM free pages after:  54911

./build.ps1 -NoSync
USB installer built: build/PollikOS-USB-Installer.img (4097812 kernel bytes)
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (829368 kernel bytes, 1620 sectors loaded at 1 MiB)

./build.ps1 -NoSync -ImageName PollikOS-Surface.img
PollikOS built: build/PollikOS-Surface.img (829368 kernel bytes, 1620 sectors loaded at 1 MiB)
```

The actual i386 binary is `build/kernel.bin`: 827576 → 829368 bytes for this
UI follow-up (+1792). At the start of the larger Calculator session it was
820680 bytes. The installer binary is `build/install/kernel.bin`, 4097812 bytes;
neither value is a disk-image or ELF size.

## Test changes and limits

| Item | Status | Evidence / limitation |
| --- | --- | --- |
| Calculator styling and icons | Implemented | Real QEMU screenshots at both resolutions; native layout bounds |
| Calculator hang | No hang reproduced in corrected image | 12 launch/close cycles in WHPX and TCG; timers and frames progress; original user session trace unavailable |
| Calculator editing damage | Verified | 2040600 → 340000 pixels; exact 180000-pixel partial/full comparison |
| Settings sliders | Verified | PS/2 endpoints, midpoint, cursor sizes and guest VFS readback |
| Benchmark resize/maximize/restore | Verified | Native 40-change test and actual PS/2 completion test |
| Full benchmark | PARTIAL | All 8 results; Geometry 4/5, existing deadline preserved |
| Complete desktop x86-64 port | Not done | Native Calculator verified; rich desktop remains i386 |

- Exact registry counts changed 7 → 8 because Calculator is appended as ID 7.
  Window84/Surface36/Animation68 layout assertions remain exact. Resize tests
  extend from 49 to 56 stages with Calculator color/geometry probes; all original
  seven application pixel expectations remain.
- Dock test positions use the seven pinned entries, not the eight total slots.
  Calculator remains unpinned by default, preserving the existing Terminal
  center at `(456,710)` in the 1024px smoke test.
- Settings registry tests now require its real drag/close callbacks; native
  mocks track values/writes and add bounds tests for all six tabs in both themes.
- No `smoke.py` or browser pixel assertions/clock masks were changed here.
- The rich desktop remains i386. A real native x86-64 Calculator exists and was
  tested, but the complete desktop migration is **not done**. `run-x86_64.ps1`
  launches the experimental native desktop with disk snapshots.
- The earlier restart test preserved 1920×1080, pitch 7680 and 32bpp under WHPX
  headless and GTK. The user's host-window/resolution symptom was not reproduced;
  no restart kernel fix is claimed by this follow-up.
- NOT RUN here: physical hardware/audio, installer ATA/AHCI installation,
  x86-64 SelfTest/boot/storage suites, 5000-cycle spawn perturbation, crash-write
  consistency campaigns, browser_js ten-run campaign, and a complete 64-bit GUI
  migration. No PASS claim is made for them.
