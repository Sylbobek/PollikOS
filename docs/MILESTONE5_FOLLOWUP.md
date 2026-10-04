# Milestone 5 follow-up: correctness and harness repair

No compositor, rasterizer, animation or timer performance changes were made in
this follow-up. The production changes are the PollikMark count/rate widening
and `run.ps1` acceleration selection, plus cancellation of the open animation
when maximizing. User data was not formatted or synced.
All guest tests use disposable disks and TCG unless explicitly stated otherwise.

## Baseline actually executed

```
git worktree add --detach ../PollikOS-pre-followup2 pre-followup2
HEAD is now at ee6b7c7 Snapshot: GUI work + Luna A-H, before follow-up 2
```

The first build invocation from the parent directory failed at a relative .NET
runtime-prefix path. Running the command with the worktree as the working
directory completed:

```
.\build.ps1 -ImageName PollikOS-Surface.img
PollikOS built: build/PollikOS-Surface.img (3232332 kernel bytes, 6314 sectors loaded at 1 MiB)
```

The tag is a later snapshot, not the September 17/18 tree. That historical tree
was NOT RUN. The build regenerated its tracked `compile_commands.json`; no
baseline kernel/test source was edited. Raw logs are `build/m5-base-*.log`.

| Tag command | Actual result |
|---|---|
| `python tests/gui_registry.py` | `PASS: gui registry (64827 checks)` |
| `python tests/held_drag.py` | Compile failure: undeclared `minimizing_app`; three calls supply two arguments to a three-argument `render_window_to_surface` |
| `python tests/surface_bounds.py --resolution 1024x768` | `AssertionError: normal: surface/scene/LFB=(0, 10591195, 10591195)` |
| `python tests/surface_bounds.py` (default 3440x1440) | `AssertionError: normal: surface/scene/LFB=(0, 11314660, 11314660)` |
| `python tests/corners_guards.py --resolution 1024x768` | `AssertionError: canary/preblended RGB in drawable corners` |
| `python tests/resize_layout.py --resolution 1024x768 --skip-native` | `AssertionError: top-left placement` |

## Classifications and controlled copy experiments

Experiments were performed in `../PollikOS-m5-copy`, never by reverting hunks in
the working tree. Scripts, commands and output are saved under `build/m5_*`.

### Registry: (c), deliberate minimum-height change

Reverting only `.min_height = 520` to `410` in the copy changes registry failure
to `PASS: gui registry (64827 checks)`. Settings now has wallpaper controls down
to y=498 and cursor controls down to y=490. A 410-pixel window clips them. The
exact registry assertion now requires 640x520 for Settings, 480x280 for others.
The guest screenshot shows Settings at 680x520; the cursor button is white and
the theme outline is `(37,99,235)`. No production minimum was reduced.

### Held drag: (c), artwork and geometry oracle out of date

Both the tag and current compositor contain inactive neutral control colors
`b8adb5/b8b2ad/adb8b2`. The test expected the previous red/amber/green dim colors.
Actual native cache output:

```
RAW inactive RGB=b8adb5,b8b2ad,adb8b2
SCREENDUMP inactive Welcome controls [(184, 173, 181), (184, 178, 173), (173, 184, 178)]
```

The three assertions were updated to those exact tag colors. The corner test
marked its fixture MAXIMIZED while independently computing radius-12 coverage.
Reverting only the compositor's square-maximized-corner policy in the copy makes
that corner assertion pass. MAXIMIZED square geometry is already present in the
tag. The fixture now uses SNAPPED (rounded, no normal-window shadow), retains
every old coverage/color equality and additionally tests all 10,000 square
maximized pixels. NORMAL alone would include a shadow; the measured mismatch
was `actual=e4eaf4 expected=e4ebf4`, so its shadow was not silently tolerated.

Default damage handling passes the 29-position full-repaint oracle. The optional
legacy damage path was also checked in the copy and still fails:

```
clang ... -DPOLLIK_COMPOSITOR_LEGACY_DAMAGE=1 tests/held_drag.c kernel/graphics.c ...
FAIL cursor-free scene vs full repaint at 510,672: 00ebedf1 != 00e7ebef
```

That optional path was not repaired in this no-performance-work step.

### Surfaces and guards: (a) fixture setup, plus (c) geometry/input assumptions

The tag's guard test attaches no data disk and never completes authentication.
Adding a valid disk and PNGs alone still leaves the cache at zero. Adding account
setup in the copy makes the boot guard check pass. Exact screenshot evidence:

```
RAW tag cache corner ('0x0', '0x0')
SCREENDUMP png-only titlebar@(830,155)=(161, 155, 219)
RAW tag cache corner ('0xf2eff6', '0xf2eff6')
PASS boot: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
SCREENDUMP png-and-account-setup titlebar@(830,155)=(242, 239, 246)
```

Thus this titlebar failure is not a wallpaper-dependent expected color. Its
expected straight RGB remains `0xf2eff6`. PNG fixtures are explicit light theme.
The current surface driver also assumed raw PS/2 deltas bypass acceleration;
bounded packets and exact consumption now account for the configured behavior.

The tag already maximizes to `(0,32,width,height-128)`, rather than the old inset
work area. Exact geometry checks now require the actual full-width policy;
prefix/suffix canary assertions and all surface/scene/LFB RGB checks remain.

### Resize layout: (c) placement/layout, (a) Notes fixture

Reverting only the drag top-limit hunk (39 to 36) in the copy restores the old
placement/resize expectation:

```
current: RAW drag target=(8,36) actual= (8, 39, 680, 410)
current: RAW resize requested=1008x626 actual= (8, 39, 1008, 623)
copy: RAW drag target=(8,36) actual= (8, 36, 680, 410)
copy: RAW resize requested=1008x626 actual= (8, 36, 1008, 626)
SCREENDUMP titlebar (242, 239, 246)
```

The tag already reserves those extra three pixels for the top shadow. Placement
is now exactly `(8,39)`. Normal resize's bottom remains `height-106`, so the
maximum fixture height is `height-145`. Settings still enforces its 520 minimum.

Welcome's current renderer hides cards below height 340. The old compact probes
read `faf9fc/2563eb/faf9fc`, not card color `f0ecf6`. Below 340 the test checks
those exact body/button colors; above 340 it checks all three actual cards with
their current y/height (height-146,72; compact height-128,62).

Notes' clipboard now copies a selection. The old Ctrl+C without a selection did
not duplicate the fixture: at 1920x1080 only rows through 27 had ink, while the
unchanged assertion requires ink at row 30 or below. The fixture selects all,
copies, clears the selection and appends, verifying byte count 149 -> 298.
Both resolutions retain all 49 viewport-content stages and external guards.

No failing default-path rendering assertion was attributed to a new milestone
4/5 renderer defect; controlled fixture/oracle repairs suffice for these five.

## Browser flake

Two original-harness copy runs reproduced the page timeout with:

```
RAW ORIGINAL input_url b'about:home' typing 0 pending 0 loading 0
RAW ORIGINAL mx 448
RAW ORIGINAL my 120
```

The old driver landed at (448,120), above the browser's address field; the
consumed absolute address-field target is (370,175). The URL did not reach the
browser; no HTTP navigation was started. Another
original run reached the loopback server and decoded both PNG/GIF, then sampled
the same GIF frame twice. The failure is unchecked readiness/input and periodic
sampling, not demonstrated network transport or cursor corruption.

The harness waits for the home document and loading flags, derives i386 offsets
from the actual header, verifies each entered character, and uses absolute
consumed PS/2 coordinates. The 35-second HTTP deadline is unchanged. Existing
JS/PNG/GIF/click assertions remain. GIF progress additionally requires a change
inside its actual 32x32 magenta/cyan rectangle within three seconds, so a clock
change elsewhere cannot satisfy it. It uses unique disposable disks and an
ephemeral loopback port. Ten final runs have ten actual success markers.

The native cooperative harness was missing newer service symbols. It now links
the real clipboard implementation and updates its explicit HTTP/raster/media
mocks. It asserts its mocked transaction document contains no GIF; animated
media is verified by QEMU, not this native mock.

Windows denied some fixture image replacements even with unique paths; the
external lock owner was not identified. Appearance is now written in place only
to newly formatted disposable fixtures before QEMU starts. This changes no
user-data update API and introduces no retry.

Smoke's F11 driver now uses a bounded key press before waiting for state, and
wallpaper checking waits for actual LFB pixels after cache completion. Its
`changed > 1000`, Notes equality, wheel round-trip and cursor assertions remain.

The final smoke checkpoint exposed a real existing animation race, also present
in `pre-followup2`: `ui_anim_update` completes OPEN/RESTORE by writing NORMAL and
the original geometry, even after an F11 maximize. The real animation code and
the extracted actual `desktop.c` toggle routine reproduce it deterministically:

```
python tests/window_animation_race.py  # before fixing desktop.c
RAW maximize during open: state=0 rect=170,125,680,410 animation_active=0
FAIL opening completion overwrote maximize
python tests/window_animation_race.py  # after canceling the UI animation
RAW maximize during open: state=2 rect=0,32,1024,640 animation_active=0
PASS maximize survives opening animation deadline
```

`toggle_maximize` now calls the existing `ui_anim_cancel` before changing geometry.
This is a state correctness fix, with no animation timing/visual changes. The
native harness mocks only surrounding window services; it executes the actual
animation implementation and actual toggle routine. No smoke assertion was
removed to hide this race.

## Memory and acceleration

The original native reproduction produced `units=0 rate=4294967295 formatted=4.2G`.
Frame/accumulated units, stored results, average rate and UI formatting are now
64 bits. `gfx_ratio` retains its old saturation behavior; new `gfx_ratio64`
returns the complete quotient. The internal packed MarkResult probe is 68 bytes
(formerly 60); its native assertion and read-only QEMU parser were updated.
This is not a syscall or on-disk ABI. Native tests cover 17,179,869,184 units/B/s
and formatting through UINT64_MAX (`18446744073.7G`).

WHPX was executed with CPU `qemu64`; the Memory test measures a repeated 1-MiB
clear's slice time and excludes cooperative wait/painting, so its B/s is cache
work throughput, not a DRAM-bandwidth claim. Initial WHPX attempts with a 40s
boot deadline failed; the new WHPX-only test allows 180s. Existing TCG deadlines
were not increased. CPU `max` WHPX failed to reach QMP in an initial attempt;
the final `max` retry also failed: `AssertionError: QMP unavailable; QEMU exit=0`.
Only `qemu64` is claimed as guest-verified here.

`run.ps1` defaults to `-Accel auto`: queries Windows Hypervisor Platform and
starts a disk-free, paused QEMU probe with the selected CPU. It prints the chosen
accelerator/reason. Both unavailable-platform (injected host capability) and
actual QEMU rejection branches were tested. Existing harnesses default to TCG;
only the Memory command explicitly selects WHPX.

Without an explicit `-Cpu`, WHPX uses the guest-verified `qemu64`; TCG retains
`max`. Explicit CPU arguments are preserved. The first fallback unit attempt
after this change did not mark its invalid CPU as explicitly bound and therefore
correctly probed the new qemu64 default; the test now sets that bound argument.
The unavailable/rejected assertions themselves remain unchanged.

## Evidence and scope

Full output and commands: `build/m5-evidence.txt`. Assertions and original lines:
`build/m5-assertion-changes.diff`. Screenshots: `build/m5-probe-app*.png`,
`build/m5-drag-proof.ppm`, `build/m5-tag-fixture-*.ppm`, and
`build/memory64-whpx-1920x1080.png`.

NOT RUN: the September source tree, physical-machine testing,
actual hardware with WHP unavailable, x86_64
suites, and a new complete eight-workload performance benchmark. The legacy
damage experiment failed and was left unchanged; it is not a passing regression.

## Final executed checkpoint

Kernel at follow-up start: 817,588 bytes (copy build log). Current actual
`build/kernel.bin`: 818,004 bytes (+416). Tag kernel: 3,232,332 bytes.

The source change for the F11 fix adds eight bytes to the preceding 817,996-byte
build. Alpha and Surface both published successfully with `-NoSync`.

```text
COMMAND: python tests/gui_registry.py
PASS: metadata and optional callbacks
PASS: invalid IDs are no-ops
PASS: initialization
PASS: render adapters
PASS: key mapping (all 256 codes, Shift/Control)
PASS: click, scroll and cursor routing/bounds
PASS: open/close/resize
PASS: poll bitmask and reset
PASS: gui registry (64827 checks)
COMMAND: python tests/held_drag.py
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
COMMAND: python tests/surface_bounds.py
PASS: 3440x1440, all 7 surfaces maximize/restore, runtime LFB and external guards
COMMAND: python tests/corners_guards.py --resolution 1024x768
PASS boot: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS maximized: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=1024x640
PASS restored: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption
COMMAND: python tests/resize_layout.py --skip-native
RAW Notes fixture bytes 149 -> 298
PASS 1024x768: all seven apps, 49 viewport-content stages, external guards
RAW Notes fixture bytes 149 -> 298
PASS 1920x1080: all seven apps, 49 viewport-content stages, external guards
COMMAND: python tests/app_layout.py
PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping (37182 checks)
COMMAND: python tests/soft3d_native.py
soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)
COMMAND: python tests/pollikmark_native.py
RAW memory units=17179869184 rate=17179869184 formatted=17.1G
pollikmark native: PASS (rotation, raster counters/ABI, mixed shapes, clipping, alpha, resize/failure/leaks, detailed info text bounds)
COMMAND: python tests/browser_cooperative.py
PASS: 972 cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input
COMMAND: python tests/smoke.py --notes-only
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: Settings lists PollikFS wallpapers and persists the selected name
PASS: pointer acceleration can be disabled and persists
COMMAND: python tests/smoke.py
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock
PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping
COMMAND: python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 7344 / [PROC] [TEST] PMM free pages after:  7344
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 56448 / [PROC] [TEST] PMM free pages after:  56448
COMMAND: python tests/browser_responsive.py --resolution 1024x768
PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies
COMMAND: python tests/browser_responsive.py --resolution 1920x1080
PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies
COMMAND: POLLIK_GUI_CPU=qemu64 python tests/pollikmark_memory.py --accel whpx
RAW Memory accel=whpx resolution=1920x1080 units=1110441984 work_us=11864 rate=93597604855 B/s samples=1059
PASS Memory rate equals full 64-bit units * 1000000 / measured work_us; no UINT32 saturation
COMMAND: .\run.ps1 -Accel auto -Resolution 1024x768 -NoLaunch
Acceleration selected: whpx; reason: Windows Hypervisor Platform present; QEMU WHPX initialization accepted; default CPU qemu64 (WHPX-compatible); explicit -Cpu is preserved
COMMAND: python tests/run_accel.py
RAW selection=tcg reason=Windows Hypervisor Platform unavailable (HRESULT=0, present=0)
PASS auto fallback selects tcg
RAW selection=tcg reason=QEMU rejected WHPX (exit=1): C:\Users\syltu\scoop\apps\qemu\current\qemu-system-x86_64.exe: unable to find CPU model 'pollikos-invalid-probe-cpu'
PASS auto fallback selects tcg
COMMAND: python tests/browser_js.py (ten consecutive runs)
run=1: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=2: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=3: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=4: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=5: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=6: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=7: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=8: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=9: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
run=10: PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
BROWSER 818 run_files=10 PASS_markers=10
RESIZE viewport PASS markers=98
```

The completed WHPX Memory screen reads `93.5G B/s`, matching truncating UI
formatting of 93,597,604,855 B/s. `build/memory64-whpx-1920x1080.png` is the
actual QEMU screendump converted to PNG. It was visually inspected.

Existing expectation changes and their raw original/current lines are in
`build/m5-assertion-changes.diff`; the explanations above cover minimum size,
inactive colors, corner-state fixture, work-area geometry, compact Welcome
layout, Notes selection fixture and the explicitly widened internal probe.
No RGB tolerance, guard check, content-ink requirement or full repaint oracle
was relaxed.
