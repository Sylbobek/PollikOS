# PollikOS overnight performance report

Date: 2026-10-04. Branch: `perf-overnight`; start tag: `perf-start`.
Host CPU command output: `12th Gen Intel(R) Core(TM) i5-12400F`.

All guest builds and tests ran in the disposable copy
`C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn`; no command
opened or changed the real `build\PollikData.img` or a `*.backup*.img`.
`run.ps1` was not used to launch a guest because it attaches the persistent
data image. SDL/TCG and SDL/WHPX guest probes both caused QEMU to exit during
startup. TCG measurements below therefore use the test harness's QMP,
`-display none` mode and are not comparable with SDL results.

## Phase status

| Step | Status | Evidence | Commit | NOT RUN |
|---|---|---|---|---|
| 5b.1 freestanding primitives | Done | Native reference/fast builds; rectangle fill/blit, constant-alpha and masked fill, premultiplied rectangle blend, rounded-mask generation in `kernel/gfx/gfx_primitives.h` | current follow-up | Production compositor integration and cache reuse remain under 5b.4/5b.6 |
| 5b.2 reference/fuzz gate | Done | `PASS gfx rectangle fuzz: 100000 cases seed=0x6d2b79f5 pitch=148..159 align=0..3 overlap=all guards=checked`; `PASS gfx rounded-mask fuzz: 10000 cases seed=0xc001d00d coverage=8x8 guards=checked` | current follow-up | Broader GUI scene golden tests are listed under 5b.4 |
| 5b.3 target compiles | Done | `clang` compile commands for i386, x86_64, SDK and host in `PERF_LOG.md` all exited 0 | `add1308` | — |
| 5b.4 rounded corners/shadows/dock | Partial | `python tests/held_drag.py`; all cached LUT radii 1..29 and full-repaint oracle passed | `add1308` | Old 8x8 pixel-by-pixel comparison, fresh cache implementation, dock-glass cache proof |
| 5b.5 microbenchmarks | Partial | Host fill/copy/blend medians: 9,287.5/5,064.9/1,123.4 -> 9,853.4/5,249.1/1,487.5 Mpx/s | `add1308`, `3f13c63` | Rounded-fill benchmark and in-guest rates |
| 5b.6 production integration | Partial | `build.ps1 -NoSync`: `PollikOS built: build/PollikOS-Alpha.img (818296 kernel bytes, 1599 sectors loaded at 1 MiB)`; held-drag pixel oracle passed | `add1308`, `3f13c63` | Only `app_host_blit` uses the new library; no guest PollikMark before/after data |
| 5c.1 rasterizer profile | Partial | `soft3d_bench.py` golden writes and `llvm-nm -u build/soft3d.o` (only `U gfx_target_valid`) recorded in `PERF_LOG.md` | `397c43b` | Exact inner-loop instruction count and count of x87 divide instructions within each pixel loop |
| 5c.2 integer rasterizer | Not done | 8- and 4-pixel perspective-interpolation experiments changed pixels (max RGB error 3) and were slower; reverted | `fa97b75` | Fixed-point rasterizer, top-left coverage, perspective error bound |
| 5c.3 raster tests | Partial | `python tests/soft3d_native.py`: `soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)`; exact golden SHA outputs in `PERF_LOG.md` | `397c43b`, `fa97b75` | New watertight-edge, huge-triangle, fixed-point coverage and texture-error tests |
| 5c.4 generated-code gate | Partial | `llvm-nm -u build/soft3d.o` showed no `__divdi3`, `__muldi3`, or `__udivdi3` | `397c43b` | x87 divide count per hot pixel loop |
| 5c.5 PollikMark 3D | Not done | TCG full run logged `AssertionError: all eight PollikMark workloads complete` after its 300-second deadline | `1ce299e` | Triangle/Cube/Geometry/Texture before/after scores at both resolutions and accelerators |
| 5d.1 guest timing histories | Partial | `tests/test_perf.py` passed both sizes; drag/open-animation intervals are in `PERF_LOG.md` | `3f13c63` | WHPX/SDL histories and guest idle histogram |
| 5d.2 damage accounting | Partial | `tests/damage_accounting.py` records RTC, idle, Notes caret and Dock at both resolutions under TCG; raw output in `PERF_LOG.md` | `3f13c63` + this follow-up | WHPX/SDL matrix and full-screen damage-path audit |
| 5d.3 open-animation reduction | Not done | Fresh TCG headless baseline: 540,708 pixels/frame and 104.049 ms mean at 1024x768; 753,888 and 47.900 ms at 1920x1080 | `3f13c63` + this follow-up | Cached transform, pixel-identical end-state comparison |
| 5d.4 frame pacing | Not done | Existing timer reports `source=1 resolution_us=1`; measured TCG open-animation interval max was 122.669 ms / 82.186 ms | `3f13c63` | Fixed 16.67 ms deadlines, WHPX p50/p95/max, idle `hlt` verification |
| 5d.5 PollikMark compositor metric | Done for explanation; measurements partial | Source uses `frames * 1,000,000 / elapsed_us`; single-workload TCG probes completed at both sizes below | `3f13c63` | WHPX/SDL measurement; no score or denominator change was needed |
| 5d.6 PollikMark memory rate | Done before this session | `docs/MILESTONE5_FOLLOWUP.md`: `RAW Memory accel=whpx ... rate=93597604855 B/s`; native 64-bit test also passes | Prior 5a commit | Full eight-workload PollikMark suite under either accelerator in this phase |

The strict native graphics, cursor, surface, GUI-layout, process, smoke and
cooperative-browser regression outputs are recorded in
[PERF_LOG.md](PERF_LOG.md). `smoke.py --notes-only` failed once with
`AssertionError: PS/2 packet was not consumed exactly`, then passed unchanged
on its immediate rerun. No assertion or expected pixel was weakened.

## i386 kernel size

| Checkpoint | Actual `kernel.bin` | Evidence |
|---|---:|---|
| `perf-start` baseline | 818,004 bytes | `docs/PERF_BASELINE.txt` |
| Final isolated build | 818,296 bytes | `Get-Item C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn\build\kernel.bin` returned `Length : 818296` |
| Change | +292 bytes | Difference of the two binary sizes above |

The final size refers to the object code binary produced for the i386 kernel,
not an ELF or disk image. The repository's existing `build\kernel.bin` was
not rebuilt or modified by the final isolated build.

## PollikMark before/after

The user-provided performance note gave 1920x1080 rates of 28.1M px/s fill,
136.9K shapes/s, 336 tris/s, 2.9K cube tris/s, 17.1K geometry tris/s,
165K texpx/s, 14 compositor fps and 1.4 GB/s memory. That note did not label
the accelerator, so these are retained as an unlabeled historical baseline.
The task context separately attributed 336 tris/s and 165K texpx/s to TCG, and
29.5K tris/s / 29M texpx/s to WHPX. Those historical values were not
independently rerun here.

| Workload | TCG before | TCG after | WHPX before | WHPX after |
|---|---|---|---|---|
| Fill Rate | 28.1M px/s (user note; accel unspecified) | NOT RUN; full suite timed out | Unspecified | NOT RUN; guest startup failed |
| 2D Shapes | 136.9K shapes/s (user note; accel unspecified) | NOT RUN; full suite timed out | Unspecified | NOT RUN; guest startup failed |
| Triangle | 336 tris/s (historical TCG context) | NOT RUN | 29.5K tris/s (historical context) | NOT RUN |
| Cube | 2.9K tris/s (user note; accel unspecified) | NOT RUN | Unspecified | NOT RUN |
| Geometry | 17.1K tris/s (user note; accel unspecified) | NOT RUN | Unspecified | NOT RUN |
| Texture | 165K texpx/s (historical TCG context) | NOT RUN | 29M texpx/s (historical context) | NOT RUN |
| Compositor | 14 fps (user note; accel unspecified) | 38 fps at 1024x768; 42 fps at 1920x1080, single test, TCG/QMP display-none | Historical report: 39 fps and 939 us mean frame time | NOT RUN |
| Memory | 1.4 GB/s (user note; accel unspecified) | NOT RUN in full suite | Earlier 5a memory-only run: 93,597,604,855 B/s | NOT RUN in this phase; see prior WHPX memory-only evidence |

The compositor fps values are per-workload elapsed-time throughput, not
`1,000,000 / mean_frame_us`. The benchmark's completion path computes
`frames * 1,000,000 / (app_host_time_us() - level_start)`; its separate frame
histogram measures only completed compositor-frame duration. The 939-us/39-fps
pair is not itself evidence of a bad denominator. Current single-test data
instead measured 11.391 ms mean frame duration and 42 fps at 1920x1080 TCG.

## Tests NOT RUN and limitations

- Full PollikMark under SDL/TCG and SDL/WHPX at 1024x768 or 1920x1080. QEMU
  closed during startup with exit code `3221225477` (0xC0000005) on the SDL
  probe. The TCG display-none full run timed out after 300 seconds at 1024x768.
- Full PollikMark at 1920x1080 on TCG or either resolution on WHPX.
- Guest primitive Fill Rate, 2D Shapes, rounded-fill, and per-primitive before/
  after comparisons.
- Integer fixed-point rasterizer, its expanded edge/texture tests, full x87
  hot-loop instruction count, and measured WHPX 3D scores.
- WHPX/SDL damage accounting matrix and full-screen damage-path audit.
- Window-open animation optimization and golden/screendump end-state compare.
- 60-Hz/25-ms WHPX frame-pacing target. SDL/WHPX measurements are blocked by
  QEMU startup termination, and the measured TCG open-animation maxima exceed
  25 ms.
- Full regression after each individual primitive integration; the full
  requested baseline suite was run once after the completed integration.

Final source/log commit: `3f13c63` (`perf: finish 5d telemetry and record measurements`).
The final report itself is documentation-only and is committed separately.
The pre-existing `add1308` commit groups the 5b primitive library, its initial
native tests and production row-blit integration; those substeps do not have
separate commit IDs in the preserved history.

## Rectangle primitive integration follow-up (2026-10-05)

The i386 compositor `rect()` path now calls `gfx_fill_rect()` by default, with
the old assembly path retained behind `-DGFX_RECT_LEGACY=1`. The 8x8 rounded
corner coverage output remains exact while only corner patches are calculated.
Native legacy-versus-primitive scene and hardware hashes match:

```text
PASS rect legacy/primitive pixel parity scene=29cdb7eb565afb73 hardware=b7f00ff433c10c0c
```

In the same native harness, the median time for four 320x200 masks fell from
12.836 ms to 0.109 ms (about 118x). Fill and blit rate measurements were noisy
and showed no consistent improvement, so no broader speedup is claimed.

The latest isolated i386 build was 819656 bytes (`build\kernel.bin`), compared
with 819592 bytes before this integration (+64 bytes). `gfx_primitives_native.py`,
`held_drag.py`, `smoke.py --notes-only`, full `smoke.py`, and
`corners_guards.py` passed in the isolated copy. Broader PollikMark TCG/WHPX
measurements and the remaining requested regression matrix were NOT RUN in
this follow-up.
