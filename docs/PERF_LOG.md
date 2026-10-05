# Performance experiment log

## Preflight: requested SDL guest benchmark display (2026-10-04)

Scope: PollikMark full-run baseline at 1024x768, CPU qemu64, first TCG then WHPX, `-display sdl`. No graphics-kernel changes were made.

Attempt 1:

```text
$ POLLIK_GUI_DISPLAY=sdl POLLIK_GUI_CPU=qemu64 POLLIK_GUI_ACCEL=tcg python tests/pollikmark.py --full-run --resolution 1024x768
TCG 1024x768 exit=1
ConnectionResetError: [WinError 10054] An existing connection was forcibly closed by the remote host
```

Attempt 2, isolated exact SDL guest boot with disposable PollikFS disk, TCG/qemu64, QMP, VGA:

```text
RAW exact SDL guest probe QEMU exit before QMP= None
RAW exact SDL guest probe greeting={"QMP":{"version":{"qemu":{"micro":0,"minor":0,"major":11},"package":"v11.0.0-12122-ga4bb4b10c9"},"capabilities":["oob"]}}
RAW exact SDL QEMU exit=3221225477 stderr=
```

Attempt 3:

```text
$ POLLIK_GUI_DISPLAY=sdl POLLIK_GUI_CPU=qemu64 POLLIK_GUI_ACCEL=whpx python tests/pollikmark.py --full-run --resolution 1024x768
WHPX 1024x768 exit=1
ConnectionResetError: [WinError 10054] An existing connection was forcibly closed by the remote host
```

Decision after three attempts: SDL-backed guest measurement is blocked by QEMU process termination during startup on this host. The temporary display/timeout changes to `tests/gui_metrics.py` were reverted exactly to `HEAD`; `git status --short` was empty after the revert. Continue independent native graphics work. PollikMark values collected with `-display none` must be labeled separately and cannot be compared with historical SDL results. SDL baseline and after numbers are NOT RUN.

Raw logs: `build/perf-base-pollikmark-tcg-1024x768.log`, `build/perf-base-pollikmark-whpx-1024x768.log`.

## PollikMark fallback TCG baseline timeout (2026-10-04)

After SDL guest startup failed, one separate TCG/qemu64 run used the harness's
default `-display none` as a diagnostic only. It is not comparable with the
historical SDL result.

```text
$ POLLIK_GUI_CPU=qemu64 POLLIK_GUI_ACCEL=tcg python tests/pollikmark.py --full-run --resolution 1024x768
GUI fixture: appearance installed before QEMU launch
PASS startup PMM check completed before full PollikMark
PollikMark timer: source=1 resolution_us=1 tsc_khz=4626310
AssertionError: all eight PollikMark workloads complete
```

The run reached the harness's 300-second full-suite deadline before producing
the eight workload results. TCG full-run numbers and all eight baseline scores
are NOT RUN. QEMU was no longer running when checked afterward. This is one
diagnostic attempt; the SDL failure decision above remains unchanged.

## Phase 5b: freestanding primitive prototype, before GUI integration

`kernel/gfx/gfx_primitives.c` now contains selectable reference loops and a
fast path: x86 `rep stosd`, i386 `rep movsd`, x86_64 `rep movsq`, and packed
premultiplied-alpha span blending. The library has no kernel or SDK headers.
These are isolated native measurements; the compositor does not use the new
library yet, so no PollikMark or guest-performance gain is claimed.

Native correctness and microbenchmark command:

```text
$ python tests/gfx_primitives_native.py
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
BENCH gfx mode=reference pixels=2073600 rounds=128 fill_ms=30.000 copy_ms=58.000 blend_ms=240.000 sink=009f803f
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
BENCH gfx mode=fast pixels=2073600 rounds=128 fill_ms=28.000 copy_ms=55.000 blend_ms=179.000 sink=009f803f
```

The initial low-resolution `clock()` readings above are retained as raw output,
but the QPC readings below supersede them for comparison. The first `rep movsl`
experiment was slower than the compiler's reference loop (reference 52 ms,
fast 60 ms), so x86_64 now uses `rep movsq`. Native host timing is not a guest
rendering result.

Freestanding compile checks after the final row-copy edit:

```text
$ clang --target=i386-none-elf -m32 -march=i386 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-sse -mno-mmx -Wall -Wextra -Werror -I kernel/include -I kernel/gfx -c kernel/gfx/gfx_primitives.c -o build/gfx-i386.o
exit=0 (no compiler diagnostics)
$ clang --target=x86_64-none-elf -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mgeneral-regs-only -Wall -Wextra -Werror -I kernel/include -I kernel/gfx -c kernel/gfx/gfx_primitives.c -o build/gfx-x64.o
exit=0 (no compiler diagnostics)
$ clang --target=x86_64-none-elf -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mgeneral-regs-only -Wall -Wextra -Werror -I sdk/include -I kernel/gfx -c kernel/gfx/gfx_primitives.c -o build/gfx-sdk.o
exit=0 (no compiler diagnostics)
```

These commands compile the primitive translation unit for the three targets.

The first clock-based benchmark showed large copy variance, so the harness now
uses Windows `QueryPerformanceCounter` (and C11 `timespec_get` elsewhere) and
prints three repetitions. The follow-up output is:

```text
$ python tests/gfx_primitives_native.py
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
BENCH gfx mode=reference pixels=2073600 rounds=128 fill_ms=29.756 copy_ms=62.865 blend_ms=260.488 sink=009f803f
BENCH gfx mode=reference pixels=2073600 rounds=128 fill_ms=28.066 copy_ms=61.955 blend_ms=253.364 sink=009f803f
BENCH gfx mode=reference pixels=2073600 rounds=128 fill_ms=31.108 copy_ms=58.367 blend_ms=248.099 sink=009f803f
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
BENCH gfx mode=fast pixels=2073600 rounds=128 fill_ms=27.940 copy_ms=62.163 blend_ms=192.032 sink=009f803f
BENCH gfx mode=fast pixels=2073600 rounds=128 fill_ms=28.429 copy_ms=70.174 blend_ms=183.576 sink=009f803f
BENCH gfx mode=fast pixels=2073600 rounds=128 fill_ms=26.777 copy_ms=56.932 blend_ms=181.519 sink=009f803f
```

Medians are fill 29.756->27.940 ms, copy 61.955->62.163 ms, and premultiplied
blend 253.364->183.576 ms. This shows no copy win on this host and a noisy
copy measurement; no row-copy performance gain is claimed. The blend path is
not substituted for the existing straight-alpha UI blend because the pixel
contract differs. `app_host_blit` uses the row-copy primitive after pixel
identity tests; its guest performance remains NOT RUN pending a usable
PollikMark/display benchmark.

The row-copy integration was validated by the existing native full-repaint
oracles:

```text
$ python tests/cursor_framebuffer.py
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS cursor framebuffer: all 10 kinds, four scales, four corners, dock, stationary cursor/window motion, scale erasure; pixel-identical full-repaint oracle
$ python tests/held_drag.py
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
$ clang -std=c11 -O2 -fno-builtin -Wall -Wextra -Werror -fuse-ld=lld -DGFX_REFERENCE=1 tests/held_drag.c kernel/graphics.c kernel/gfx/gfx_primitives.c -o build/held_drag_reference.exe; .\build\held_drag_reference.exe
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
```

The full i386 build was done in an isolated working copy without the original
`build` directory, so it created and formatted only a disposable PollikFS disk
there. Raw build evidence:

```text
$ .\build.ps1 -NoSync
Graphics primitives: optimized x86 path
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (818188 kernel bytes, 1599 sectors loaded at 1 MiB)
$ .\build.ps1 -NoSync -GfxReference
Graphics primitives: reference scalar path
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (818260 kernel bytes, 1599 sectors loaded at 1 MiB)
RAW_KERNEL_BINARY=C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn\build\kernel.bin bytes=818260
```

The branch baseline was 818004 bytes (`docs/PERF_BASELINE.txt`), so the fast
linked kernel is +184 bytes. The reference build is +256 bytes over baseline.
The actual current worktree's `build` folder was not used for this build.

## Phase 5c: software rasterizer baseline, before edits

The existing `soft3d.c` rasterizer already clips to a bounding box and steps
floating-point edge/barycentric values across each row. Perspective-correct
texturing still divides twice per covered pixel. Baseline native benchmark:

```text
$ python tests/soft3d_bench.py
SOFT3D res=1024x768 rounds=8 tri_writes=335826 tri_checksum=2de83b3c51c72a09 tri_ms=12.056,11.436,11.469 tex_writes=335826 tex_checksum=54d314bd1befcbbd tex_ms=11.163,13.097,12.653
SOFT3D res=1920x1080 rounds=8 tri_writes=886465 tri_checksum=3e523fca62730c89 tri_ms=32.320,30.638,31.664 tex_writes=886465 tex_checksum=f5b46f16cdeb1641 tex_ms=31.832,36.718,28.969
```

Current i386 `soft3d.o` code-generation inspection:

```text
$ llvm-nm -u build/soft3d.o
         U gfx_target_valid
$ llvm-objdump -d build/soft3d.o | Select-String -Pattern "fdiv|divss|__div|call" | Select-Object -First 30
     1bf: dc f1                         fdiv %st, %st(1)
     1d2: dc f9                         fdivr %st, %st(1)
     1e3: de f1                         fdivp %st, %st(1)
     226: e8 fc ff ff ff                calll 0x227 <soft3d_triangle+0xb>
     267: e8 0c 00 00 00                calll 0x278 <clip_and_raster>
     504: de fc                         fdivrp %st, %st(4)
     977: dc f1                         fdiv %st, %st(1)
     b70: de f2                         fdivp %st, %st(2)
    1125: dc f9                         fdivr %st, %st(1)
    1131: de f1                         fdivp %st, %st(1)
    17d9: e8 fc ff ff ff                calll 0x17da <soft3d_triangle_textured+0xe>
    1896: e8 dd e9 ff ff                calll 0x278 <clip_and_raster>
```

No `__divdi3`, `__muldi3`, or `__udivdi3` references appear in the unresolved
symbol list. The object still contains x87 floating-point divide instructions;
the native benchmark's host timings do not establish their QEMU cost.

Two PollikMark guest runs were attempted using the newly built isolated image.
The first used the harness default `PollikOS-Surface.img`, which that build
does not produce. Retrying with its explicit supported `POLLIK_GUI_IMAGE`
override reached the fixture but stopped at an unrelated dock-focus assertion
before running the triangle or compositor workloads:

```text
$ python tests/pollikmark.py --resolution 1024x768
FileNotFoundError: [Errno 2] No such file or directory: 'C:\\Users\\syltu\\AppData\\Local\\Temp\\PollikOS-perfbuild-ogiluhwn\\build\\PollikOS-Surface.img'
$env:POLLIK_GUI_IMAGE='PollikOS-Alpha.img'; $env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='qemu64'; python tests/pollikmark.py --resolution 1024x768
GUI fixture: appearance installed before QEMU launch
dock probe state: {'slot': 6, 'expected_app': 6, 'pointer': (716, 712), 'focused': 5, 'hover': 6, 'window': (6, 0, 170, 125, 680, 410, 0, 1, 0, 0, 1, 0, 480, 280, 1024, 768, 170, 125, 680, 410, 1490712)}
AssertionError: dock slot 6 -> app 6
```

The target window is already visible and hovered at failure, but the harness
does not observe focus moving from app 5. No rasterizer change has been made,
and guest PollikMark triangle/compositor data are NOT RUN.

One more run changed the emulated CPU type from `qemu64` to `max` and failed at
the same dock-focus assertion, so no further PollikMark harness retries are
being used for this rasterizer experiment:

```text
$env:POLLIK_GUI_IMAGE='PollikOS-Alpha.img'; $env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='max'; python tests/pollikmark.py --resolution 1024x768
dock probe state: {'slot': 6, 'expected_app': 6, 'pointer': (716, 712), 'focused': 5, 'hover': 6, 'window': (6, 0, 170, 125, 680, 410, 0, 1, 0, 0, 1, 0, 480, 280, 1024, 768, 170, 125, 680, 410, 1490712)}
AssertionError: dock slot 6 -> app 6
```

A second golden harness now keeps fixed SHA-256 checksums for the current
reference rasterizer and benchmarks both 1024x768 and 1920x1080 native scenes.
After the experimental source was removed, the baseline gate and existing
native regression output were:

```text
$ python tests/soft3d_bench.py
GOLDEN res=1024x768 scene=triangle writes=335826 sha256=8008a1c1d787cc84f03b475d30ef74f1191d0a3a6d21d8e92460f97c59c9cbf0
BENCH mode=current res=1024x768 scene=triangle rounds=8 ms=11.161,11.357,11.297
GOLDEN res=1024x768 scene=perspective_texture writes=335826 sha256=22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112
BENCH mode=current res=1024x768 scene=perspective_texture rounds=8 ms=10.849,11.008,10.774
GOLDEN res=1920x1080 scene=triangle writes=886465 sha256=42d3106651618ccb3cd0554e2c9297a32929da848ac774956dd7e81ef6fcdf89
BENCH mode=current res=1920x1080 scene=triangle rounds=8 ms=28.805,28.361,28.759
GOLDEN res=1920x1080 scene=perspective_texture writes=886465 sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a
BENCH mode=current res=1920x1080 scene=perspective_texture rounds=8 ms=35.851,28.894,28.431
$ python tests/soft3d_native.py
soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)
```

I tried linear perspective correction blocks of 8 and then 4 pixels, retaining
the previous per-pixel path under `SOFT3D_REFERENCE` during the experiments.
Both were discarded before commit. The reference goldens exposed changes up to
3 RGB levels; both also slowed down in the measured host runs. Raw failure
lines:

```text
GOLDEN res=1920x1080 scene=perspective_texture pixels=2073600 writes=886465 differing_pixels=4 max_rgb_error=3 ref_sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a fast_sha256=26a9b473bc7c68adf643d85ba727ab342542e40c8cf05401a71a9454007c895a
1920x1080 perspective_texture: max RGB error 3 > 1
GOLDEN res=1024x768 scene=perspective_texture pixels=786432 writes=335826 differing_pixels=1 max_rgb_error=3 ref_sha256=22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112 fast_sha256=16aa7e95fd45f87b8f2d9484d93d477c6006ab13dad69d572f17c09cb816a2da
1024x768 perspective_texture: max RGB error 3 > 1
```

This path was reverted because it failed the visual gate and the host timing
showed extra per-pixel branch/step overhead (for example, at 1024x768 triangle
rounds were 10.951,10.987,11.120 ms reference vs 15.183,14.492,15.176 ms
experimental). No rasterizer optimization is integrated. The current native
golden harness locks reference output exactly; the randomized and boundary
coverage in `soft3d_native.py` still passes.

## Phase 5b final primitive measurements and integration (2026-10-04)

The framebuffer code stores numeric `0x00RRGGBB` scene pixels; 32-bpp VBE LFB
copies their bytes directly (little-endian B,G,R,X). `gfx_primitives.h` now
states the premultiplied source-over formula and `+127` round-to-nearest rule.
The new library still intentionally exports only span fill, non-overlapping row
blit, and premultiplied-alpha span blend; rectangle, overlap-safe blit,
constant-alpha fill, and rounded-mask generation are not implemented here.
The existing rounded-corner LUT, shadow cache, and dock cache remain unchanged.

Four standalone target compiles (each command exited 0 with empty stdout):

```text
clang -target i686-elf -ffreestanding -fno-builtin -fno-stack-protector -std=c11 -Wall -Wextra -Werror -Ikernel -c kernel/gfx/gfx_primitives.c -o build/gfx-i386.o
clang -target x86_64-elf -mgeneral-regs-only -ffreestanding -fno-builtin -fno-stack-protector -std=c11 -Wall -Wextra -Werror -Ikernel -c kernel/gfx/gfx_primitives.c -o build/gfx-x64.o
clang -target x86_64-elf -mgeneral-regs-only -ffreestanding -fno-builtin -fno-stack-protector -std=c11 -Wall -Wextra -Werror -Isdk/include -Ikernel -c kernel/gfx/gfx_primitives.c -o build/gfx-sdk.o
clang -std=c11 -O2 -ffreestanding -fno-builtin -Wall -Wextra -Werror -Ikernel -c kernel/gfx/gfx_primitives.c -o build/gfx-native.o
```

The i386 build explicitly passes `-mno-sse -mno-mmx`; the fast paths use
`rep stosl` / `rep movsl`, not SIMD. The x86_64 compile uses
`-mgeneral-regs-only`. Native microbenchmark output (1920x1080, 128 full-span
operations per timed sample; MB/s counts nominal bytes touched: fill 4 B/px,
copy 8 B/px, blend 12 B/px):

```text
$ python tests/gfx_primitives_native.py
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
BENCH gfx mode=reference pixels=2073600 rounds=128 fill_ms=29.956 fill_Mpx_s=8860.4 fill_MB_s=35441.5 copy_ms=53.485 copy_Mpx_s=4962.5 copy_MB_s=39700.4 blend_ms=238.393 blend_Mpx_s=1113.4 blend_MB_s=13360.5 sink=009f803f
BENCH gfx mode=reference pixels=2073600 rounds=128 fill_ms=28.578 fill_Mpx_s=9287.5 fill_MB_s=37150.0 copy_ms=52.404 copy_Mpx_s=5064.9 copy_MB_s=40519.5 blend_ms=236.273 blend_Mpx_s=1123.4 blend_MB_s=13480.4 sink=009f803f
BENCH gfx mode=reference pixels=2073600 rounds=128 fill_ms=27.393 fill_Mpx_s=9689.4 fill_MB_s=38757.5 copy_ms=52.099 copy_Mpx_s=5094.5 copy_MB_s=40756.3 blend_ms=240.679 blend_Mpx_s=1102.8 blend_MB_s=13233.6 sink=009f803f
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
BENCH gfx mode=fast pixels=2073600 rounds=128 fill_ms=27.512 fill_Mpx_s=9647.5 fill_MB_s=38589.8 copy_ms=54.651 copy_Mpx_s=4856.7 copy_MB_s=38853.3 blend_ms=178.429 blend_Mpx_s=1487.5 blend_MB_s=17850.5 sink=009f803f
BENCH gfx mode=fast pixels=2073600 rounds=128 fill_ms=26.937 fill_Mpx_s=9853.4 fill_MB_s=39413.4 copy_ms=50.565 copy_Mpx_s=5249.1 copy_MB_s=41993.1 blend_ms=181.395 blend_Mpx_s=1463.2 blend_MB_s=17558.6 sink=009f803f
BENCH gfx mode=fast pixels=2073600 rounds=128 fill_ms=26.360 fill_Mpx_s=10069.0 fill_MB_s=40276.0 copy_ms=49.695 copy_Mpx_s=5341.0 copy_MB_s=42728.3 blend_ms=176.183 blend_Mpx_s=1506.5 blend_MB_s=18078.1 sink=009f803f
```

Median reference -> fast rates were fill 9,287.5 -> 9,853.4 Mpx/s, copy
5,064.9 -> 5,249.1 Mpx/s, blend 1,123.4 -> 1,487.5 Mpx/s. This is host-only;
it does not establish guest throughput. The only production integration is the
`app_host_blit` row copy. The full-repaint oracle output remains exact:

```text
$ python tests/held_drag.py
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
```

Isolated disposable-image i386 build after the integration:

```text
$ .\build.ps1 -NoSync
Graphics primitives: optimized x86 path
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (818296 kernel bytes, 1599 sectors loaded at 1 MiB)
```

The source kernel.bin was 818004 bytes at the `perf-start` checkpoint; the
isolated build's actual `build/kernel.bin` is 818296 bytes (increase 292 B).
No guest Fill Rate, 2D Shapes, or compositor before/after comparison was
available because the SDL QEMU process exits during startup and the full TCG
PollikMark run hits its 300-second deadline (see preflight above).

## Phase 5d guest timing and corrected PollikMark compositor probe (2026-10-04)

Guest-side frame/interval histories were read using the existing read-only
QMP symbol probe. The six stage measurements below ran on TCG/qemu64 with
`-display none`, 256 MiB, so they are not SDL/physical-display results. They
were collected before adding the appended partial-frame accounting field;
that field only completes frame-kind accounting, not the draw path.

```text
$env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='qemu64'; python tests/benchmark_gui.py --resolution 1024x768
drag_held_10s: 62 verified operations; 46.02 actual fps; 54.10 render/s
detail: dirty=157646 px/frame; phase_us input/app/layout/draw/compose/LFB=131/1/5/0/33/13; frame mean/p95/max=19040/50957/54915 us; interval mean/p95/max=22422/50964/54920 us
window_open_animation: 1 operation; 11.04 actual fps; 11.55 render/s
detail dirty=537946; phases=3/2/10/0/88182/535; frame=86552/106747/106747; interval=90568/122669/122669
[PERF OVERLAY PASS] F12 on/off; backbuffer panel pixel restored
$env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='qemu64'; python tests/benchmark_gui.py --resolution 1920x1080
drag_held_10s: 123 verified operations; 91.85 actual fps; 120.36 render/s
detail dirty=157659; phases=124/2/4/0/32/13; frame=8112/24079/39398 us; interval=10739/24162/39406 us
window_open_animation: 1 operation; 18.62 actual fps; 19.41 render/s
detail dirty=835272; phases=3/1/11/0/38335/714; frame=51526/69326/69326 us; interval=53699/82186/82186
[PERF OVERLAY PASS] F12 on/off; backbuffer panel pixel restored
```

The guest histories show the 1024x768 drag p95/max interval of 50.964/54.920
ms and open-animation 122.669 ms max. At 1920x1080 drag p95/max was
24.162/39.406 ms and open-animation max 82.186 ms. Idle interval histogram was
NOT RUN. QMP observer overhead and the `-display none` backend are included.
The open animation remains expensive: 537,946 composed pixels/frame at
1024x768 and 835,272 at 1920x1080. It was not optimized or visually compared.
Fixed 60-Hz frame deadlines and animation scheduling changes were NOT RUN.

PollikMark's single Compositor workload was probed directly with the existing
workload key 7, same guest workload definition, TCG/qemu64, disposable data
image, `-display none`:

```text
$env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='qemu64'; python build/pollikmark_compositor_probe.py 1024x768
POLLIMARK_COMPOSITOR res=1024x768 raw_result=(1, 77, 0, 0, 0, 0, 38, 38, 0, 0, 5114, 8354, 1257, 14726, 0)
POLLIMARK_COMPOSITOR_FRAME res=1024x768 samples=77 mean/p95/max_us=14726/28106/50132 fps=38 frames=78 dirty_px_per_frame=322905 phase_avg_paint/compose/LFB_us=5048/8626/1355
POLLIMARK_COMPOSITOR_DISTRIBUTION res=1024x768 {'count': 78, 'mean_us': 15029.884615384615, 'min_us': 3804, 'max_us': 50132, 'p95_us': 28486, 'p99_us': 50132, 'low_1pct_fps': 19.947339024974067}
$env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='qemu64'; python build/pollikmark_compositor_probe.py 1920x1080
POLLIMARK_COMPOSITOR res=1920x1080 raw_result=(1, 86, 0, 0, 0, 0, 42, 42, 0, 0, 3197, 5879, 2314, 11391, 0)
POLLIMARK_COMPOSITOR_FRAME res=1920x1080 samples=86 mean/p95/max_us=11391/16739/37600 fps=42 frames=87 dirty_px_per_frame=342466 phase_avg_paint/compose/LFB_us=3161/6067/2295
POLLIMARK_COMPOSITOR_DISTRIBUTION res=1920x1080 {'count': 87, 'mean_us': 11523.643678160919, 'min_us': 3326, 'max_us': 37600, 'p95_us': 22853, 'p99_us': 37600, 'low_1pct_fps': 26.595744680851062}
```

The PollikMark `fps` denominator is elapsed wall time from workload start to
completion (`frames * 1,000,000 / elapsed_us`), while frame mean is time spent
inside completed compositor frames. A ~939-us mean with 39 fps is therefore
not an arithmetic contradiction: the frame metric excludes the gaps between
poll/present calls. Current probes also show mean frame times 11.4-14.7 ms and
wall throughput 38-42 fps. The corrected denominator is unchanged and the new
figures are not comparable with historical SDL/WHPX numbers. WHPX whole-guest
boot failed before desktop ready; PollikMark WHPX measurements are NOT RUN.

The instrumentation test had first exposed that cursor framebuffer copies
were missing from `present_time_us`; the timed cursor-pair copy is now included.
It also exposed a non-exhaustive frame classification (`frame_count=89`,
`full_redraw_count=44`, `cursor_frames=28`, `dock_frames=0`, 17 partial frames).
The stats now append `partial_frames` after existing fields, preserving all
previous offsets, and classify all four frame kinds without weakening the
accounting assertion. Current-build guest runs:

```text
$env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='qemu64'; python tests/test_perf.py --resolution 1024x768
PASS native: exact percentiles/interval-low/history wrap/idle FPS/bounded summary
PASS 1024x768: 84 frames, clock=4685340 kHz; C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn\build\perf-1024x768.json
$env:POLLIK_GUI_ACCEL='tcg'; $env:POLLIK_GUI_CPU='qemu64'; python tests/test_perf.py --resolution 1920x1080
PASS native: exact percentiles/interval-low/history wrap/idle FPS/bounded summary
PASS 1920x1080: 86 frames, clock=4703210 kHz; C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn\build\perf-1920x1080.json
```

Both current test_perf guests used disposable PollikFS images and QMP display-
none mode. The full WHPX + SDL scenario histograms, idle histogram, zero-idle-
copy assertion, frame pacing changes, open-animation cache-transform, and
pixel-identical animation end-state comparison are NOT RUN.

## Final requested regression checkpoint

Post-change isolated build regression output:

```text
$ python tests/gui_registry.py
PASS: gui registry (64827 checks)
$ python tests/surface_bounds.py
PASS: 3440x1440, all 7 surfaces maximize/restore, runtime LFB and external guards
$ python tests/corners_guards.py
PASS boot: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS maximized: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=1024x640
PASS restored: ABI36, 7 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption
$ python tests/resize_layout.py
PASS 1024x768: all seven apps, 49 viewport-content stages, external guards
PASS 1920x1080: all seven apps, 49 viewport-content stages, external guards
$ python tests/app_layout.py
PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping (37182 checks)
$ python tests/cursor_framebuffer.py
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS cursor framebuffer: all 10 kinds, four scales, four corners, dock, stationary cursor/window motion, scale erasure; pixel-identical full-repaint oracle
$ python tests/held_drag.py
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
$ python tests/soft3d_native.py
soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)
$ python tests/pollikmark_native.py
RAW memory units=17179869184 rate=17179869184 formatted=17.1G
pollikmark native: PASS (rotation, raster counters/ABI, mixed shapes, clipping, alpha, resize/failure/leaks, detailed info text bounds)
$ python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 7344 / [PROC] [TEST] PMM free pages after:  7344
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 56448 / [PROC] [TEST] PMM free pages after:  56448
$ python tests/browser_cooperative.py
PASS: 972 cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input
```

`resize_layout.py` also printed both per-resolution success lines above.
`app_layout.py` was run separately and again inside `resize_layout.py`.
The first smoke invocation had one transient PS/2 consumption timeout:

```text
$ python tests/smoke.py --notes-only
  File "C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn\tests\smoke.py", line 141, in move_mouse_to
    assert time.monotonic()<deadline,'PS/2 packet was not consumed exactly'
AssertionError: PS/2 packet was not consumed exactly
```

An immediate independent rerun completed with:

```text
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: Settings lists PollikFS wallpapers and persists the selected name
PASS: pointer acceleration can be disabled and persists
```

No smoke assertion or expected value was changed. The intermittent QMP/TCG
input timeout remains a test flake; no cause was proven.

## Phase 5b primitive API follow-up (2026-10-05)

The primitive-only gap is closed: `gfx_fill_rect`, overlap-safe
`gfx_blit_rect`, `gfx_fill_rect_alpha`, `gfx_blend_premul_rect`,
`gfx_rounded_rect_mask`, and `gfx_fill_rect_masked` are freestanding APIs.
Rectangles accept padded byte pitches and unaligned base addresses. Same-pitch
blits use memmove ordering across both rows and pixels. Rounded masks use an
8x8 coverage grid and are intended to be cached by callers. The UI has not been
changed to use these new APIs.

The first test-first compile failed because the rectangle API symbols were
absent; adding the mask test then failed on the two absent mask symbols:

```text
$ python tests/gfx_primitives_native.py
tests/gfx_primitives_native.c:89:9: error: call to undeclared function 'gfx_fill_rect'
tests/gfx_primitives_native.c:101:9: error: call to undeclared function 'gfx_blit_rect'
tests/gfx_primitives_native.c:113:9: error: call to undeclared function 'gfx_fill_rect_alpha'
tests/gfx_primitives_native.c:126:9: error: call to undeclared function 'gfx_blend_premul_rect'
$ python tests/gfx_primitives_native.py
tests/gfx_primitives_native.c:153:9: error: call to undeclared function 'gfx_fill_rect_masked'
tests/gfx_primitives_native.c:190:9: error: call to undeclared function 'gfx_rounded_rect_mask'
```

After implementation, both reference and optimized binaries printed:

```text
$ python tests/gfx_primitives_native.py
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
PASS gfx rectangle fuzz: 100000 cases seed=0x6d2b79f5 pitch=148..159 align=0..3 overlap=all guards=checked
PASS gfx rounded-mask fuzz: 10000 cases seed=0xc001d00d coverage=8x8 guards=checked
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
PASS gfx rectangle fuzz: 100000 cases seed=0x6d2b79f5 pitch=148..159 align=0..3 overlap=all guards=checked
PASS gfx rounded-mask fuzz: 10000 cases seed=0xc001d00d coverage=8x8 guards=checked
```

Freestanding target compile commands (i386, x86_64, SDK, host) all exited 0
with empty stdout/stderr:

```text
$ clang --target=i386-none-elf -m32 -march=i386 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-sse -mno-mmx -Wall -Wextra -Werror -I kernel/include -I kernel/gfx -c kernel/gfx/gfx_primitives.c -o build/gfx-i386.o
$ clang --target=x86_64-none-elf -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mgeneral-regs-only -Wall -Wextra -Werror -I kernel/include -I kernel/gfx -c kernel/gfx/gfx_primitives.c -o build/gfx-x64.o
$ clang --target=x86_64-none-elf -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mgeneral-regs-only -Wall -Wextra -Werror -I sdk/include -I kernel/gfx -c kernel/gfx/gfx_primitives.c -o build/gfx-sdk.o
$ clang -std=c11 -O2 -ffreestanding -fno-builtin -Wall -Wextra -Werror -I kernel/gfx -c kernel/gfx/gfx_primitives.c -o build/gfx-native.o
$ llvm-nm -u build/gfx-i386.o build/gfx-x64.o build/gfx-sdk.o build/gfx-native.o

build/gfx-i386.o:

build/gfx-x64.o:

build/gfx-sdk.o:

build/gfx-native.o:
```

The native existing-scene regressions still passed:

```text
$ python tests/held_drag.py
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
$ python tests/cursor_framebuffer.py
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS cursor framebuffer: all 10 kinds, four scales, four corners, dock, stationary cursor/window motion, scale erasure; pixel-identical full-repaint oracle
```

The linked i386 build ran in the disposable performance copy after copying only
`gfx_primitives.c` and `.h` into it. The before and after file measurements are:

```text
PS C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn> Get-Item build\kernel.bin | Select-Object FullName,Length,LastWriteTimeUtc
FullName                                                                       Length LastWriteTimeUtc
--------                                                                       ------ ----------------
C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn\build\kernel.bin 818296 10/4/2026 9:21:11 PM
PS C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn> .\build.ps1 -NoSync
Graphics primitives: optimized x86 path
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (819592 kernel bytes, 1601 sectors loaded at 1 MiB)
PS C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn> Get-Item build\kernel.bin | Select-Object FullName,Length,LastWriteTimeUtc
FullName                                                                       Length LastWriteTimeUtc
--------                                                                       ------ ----------------
C:\Users\syltu\AppData\Local\Temp\PollikOS-perfbuild-ogiluhwn\build\kernel.bin 819592 10/5/2026 1:36:14 PM
```

> Superseded below by the rectangle primitive integration follow-up.

This is a +1296-byte i386 kernel delta in that copy. At that checkpoint no
rectangle-specific performance result was claimed; the APIs were not yet
integrated into the compositor.
Full PollikOS regression suites and QEMU visual goldens are NOT RUN in this
follow-up. The attempted Windows sanitizer commands and their environment
failures were:

```text
$ clang -std=c11 -O1 -g -fno-builtin -Wall -Wextra -Werror -DGFX_REFERENCE=1 "-fsanitize=address,undefined" tests/gfx_primitives_native.c kernel/gfx/gfx_primitives.c -o build/gfx_primitives_san.exe
$ & .\build\gfx_primitives_san.exe; Write-Output "SAN_EXIT=$LASTEXITCODE"
SAN_EXIT=-1073741515
$ clang -std=c11 -O1 -g -fno-builtin -Wall -Wextra -Werror -DGFX_REFERENCE=1 "-fsanitize=undefined" tests/gfx_primitives_native.c kernel/gfx/gfx_primitives.c -o build/gfx_primitives_ubsan.exe
lld-link: error: undefined symbol: __declspec(dllimport) CommandLineToArgvW
lld-link: error: undefined symbol: __declspec(dllimport) SymLoadModuleEx
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```

The combined sanitizer binary could not load the Windows sanitizer runtime;
the standalone UBSan link lacks Windows runtime symbols. Sanitizer validation
is NOT RUN.

## Rectangle primitive integration follow-up (2026-10-05)

The compositor `rect()` path now uses `gfx_fill_rect()` by default; the old
assembly path remains selectable with `-DGFX_RECT_LEGACY=1`. Rounded-rectangle
coverage keeps the exact 8x8 sample rule but computes only four corner patches
and fills the center directly. Aligned x86 rows use `rep stosl`; rectangle
blits reuse the row blitter only when corresponding row ranges are disjoint.

The before/after native benchmark used the same harness against saved
`3fa58eb` primitive source and the current source:

```text
$ & .\build\gfx_rect_before.exe | Select-String '^BENCH gfx_rect'
mask_ms=13.408
mask_ms=12.836
mask_ms=10.460
$ & .\build\gfx_primitives_native.exe | Select-String '^BENCH gfx_rect'
mask_ms=0.109
mask_ms=0.127
mask_ms=0.109
```

Median time for four 320x200 masks moved from 12.836 ms to 0.109 ms (about
118x in this native run). Fill and copy rates varied substantially and showed
no consistent gain, so no general rectangle-throughput improvement is claimed.

```text
$ python tests\gfx_primitives_native.py
PASS gfx correctness: fill/blit lengths 0..257; blend multipliers 65536; seeded pixels 100000 seed=0x6d2b79f5
PASS gfx rectangle fuzz: 100000 cases seed=0x6d2b79f5 pitch=148..159 align=0..3 overlap=all guards=checked
PASS gfx rounded-mask fuzz: 10000 cases seed=0xc001d00d coverage=8x8 guards=checked
$ python tests\held_drag.py
PIXEL_HASH scene=29cdb7eb565afb73 hardware=b7f00ff433c10c0c
PIXEL_HASH scene=29cdb7eb565afb73 hardware=b7f00ff433c10c0c
PASS rect legacy/primitive pixel parity scene=29cdb7eb565afb73 hardware=b7f00ff433c10c0c
```

Latest-source isolated i386 build and GUI checks (`build.ps1 -NoSync` in a
temporary repository copy; the persistent data image was not changed):

```text
PollikOS built: build/PollikOS-Alpha.img (819656 kernel bytes, 1601 sectors loaded at 1 MiB)
Length LastWriteTimeUtc
------ ----------------
819656 10/5/2026 2:00:27 PM
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: Settings lists PollikFS wallpapers and persists the selected name
PASS: pointer acceleration can be disabled and persists
PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption
PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock
PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping
```

The prior isolated build measured 819592 bytes; this update adds 64 bytes to
`kernel.bin`. Freestanding i386, x86_64, SDK, and native compiles all exited 0;
`llvm-nm -u` printed only object headings and no undefined symbols. PollikMark
under TCG/WHPX, frame pacing, and the broader GUI/browser regression matrix are
NOT RUN in this follow-up.

## Phase 5c.1 profile completion after WHPX headless gate (2026-10-05)

No rasterizer source changes were made for this profile. The supplied PollikMark screenshot is a separate user observation (`Compositor`, Grade B, 38 fps); its accelerator and resolution are not recorded, so it is not a comparable before/after number.

Native reference/golden coverage and current timings:

```text
$ python tests\soft3d_bench.py
GOLDEN res=1024x768 scene=triangle writes=335826 sha256=8008a1c1d787cc84f03b475d30ef74f1191d0a3a6d21d8e92460f97c59c9cbf0
BENCH mode=current res=1024x768 scene=triangle rounds=8 ms=12.042,11.855,12.054
GOLDEN res=1024x768 scene=perspective_texture writes=335826 sha256=22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112
BENCH mode=current res=1024x768 scene=perspective_texture rounds=8 ms=14.438,13.619,12.143
GOLDEN res=1920x1080 scene=triangle writes=886465 sha256=42d3106651618ccb3cd0554e2c9297a32929da848ac774956dd7e81ef6fcdf89
BENCH mode=current res=1920x1080 scene=triangle rounds=8 ms=32.738,37.984,48.251
GOLDEN res=1920x1080 scene=perspective_texture writes=886465 sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a
BENCH mode=current res=1920x1080 scene=perspective_texture rounds=8 ms=32.540,32.789,46.165
```

These one-triangle scenes write 335,826 of 786,432 pixels (42.7%) at 1024x768 and 886,465 of 2,073,600 pixels (42.7%) at 1920x1080. These are coverage counts for the native test scenes, not a per-triangle rate extrapolated from PollikMark.

For code generation, `soft3d.c` was compiled into a temporary object for the same scalar i386 target and `-Os` options as `build.ps1`:

```text
$ clang --target=i386-none-elf -m32 -march=i386 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-sse -mno-mmx -Os -Wall -Wextra -Werror -Ikernel/include -Ikernel/gfx -c kernel/soft3d.c -o %TEMP%\pollikos-soft3d-prof\soft3d-i386.o
$ llvm-nm -u %TEMP%\pollikos-soft3d-prof\soft3d-i386.o
         U gfx_target_valid
HOT_PIXEL_LOOP symbol=clip_and_raster range=0x0f9e..0x172c static_instructions_inclusive=511
fdiv count=2 addresses=1125,1131
fmul count=22 addresses=1042,1048,1050,1072,107f,108b,109f,10b2,10c4,10d7,10e4,10ea,10f8,110b,1111,115b,1266,12b8,14fc,1568,15c4,162e
fild count=1 addresses=114e
fist count=9 addresses=11e2,1230,128e,12d8,151e,158a,15e4,164f,1692
NO_LIBCALLS unresolved: U gfx_target_valid
```

The 511 count is the static instruction footprint between the pixel-loop head and back edge, including mutually exclusive flat/color, nearest-sample, and bilinear-sample paths; it is not a dynamic instruction count for one pixel. The perspective-texture path executes two x87 divisions per covered pixel, plus float/integer conversions for texture coordinates. The raster loop limits writes to the clipped triangle bounding box and performs no full-target copy or clear. PollikMark's caller does perform one full color-target clear before each triangle/frame, in 2,048-pixel chunks through `gfx_clear`; that cost is part of measured frame time and is outside the triangle raster loop.

The unchanged native regression passed:

```text
$ python tests\soft3d_native.py
soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)
```

## Phase 5c.2 raster optimization decision (2026-10-05)

Three perspective-interpolation approaches were measured: the prior 8-pixel and 4-pixel linear blocks (above), and a shared-reciprocal experiment. The float reciprocal version failed the unchanged golden: at 1024x768 perspective texture it changed one pixel, two channels, with maximum RGB error 3; the 1920x1080 vector happened to match. Replacing it with a double reciprocal restored exact bytes, but did not improve native throughput. The experiment remained behind `SOFT3D_RECIPROCAL_EXPERIMENT` and was removed; production `soft3d.c` is unchanged.

Float-reciprocal mismatch evidence:

```text
$ python tests\soft3d_bench.py
1024x768 perspective_texture: baseline golden mismatch writes=335826 sha256=16aa7e95fd45f87b8f2d9484d93d477c6006ab13dad69d572f17c09cb816a2da
RECIPROCAL res=1024x768 scene=perspective_texture ref_writes=335826 candidate_writes=335826 differing_pixels=1 differing_channels=2 max_rgb_error=3 ref_sha256=22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112 candidate_sha256=16aa7e95fd45f87b8f2d9484d93d477c6006ab13dad69d572f17c09cb816a2da
RECIPROCAL res=1920x1080 scene=perspective_texture ref_writes=886465 candidate_writes=886465 differing_pixels=0 differing_channels=0 max_rgb_error=0 ref_sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a candidate_sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a
```

Double-reciprocal candidate evidence (native host, 3 batches of 8 renders):

```text
RECIPROCAL res=1024x768 scene=triangle ref_writes=335826 candidate_writes=335826 differing_pixels=0 differing_channels=0 max_rgb_error=0 ref_sha256=8008a1c1d787cc84f03b475d30ef74f1191d0a3a6d21d8e92460f97c59c9cbf0 candidate_sha256=8008a1c1d787cc84f03b475d30ef74f1191d0a3a6d21d8e92460f97c59c9cbf0
RECIPROCAL_BENCH res=1024x768 scene=triangle rounds=8 reference_ms=12.415,13.146,14.250 candidate_ms=12.123,12.373,25.416
RECIPROCAL res=1024x768 scene=perspective_texture ref_writes=335826 candidate_writes=335826 differing_pixels=0 differing_channels=0 max_rgb_error=0 ref_sha256=22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112 candidate_sha256=22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112
RECIPROCAL_BENCH res=1024x768 scene=perspective_texture rounds=8 reference_ms=12.970,13.978,15.236 candidate_ms=16.488,18.052,16.627
RECIPROCAL res=1920x1080 scene=triangle ref_writes=886465 candidate_writes=886465 differing_pixels=0 differing_channels=0 max_rgb_error=0 ref_sha256=42d3106651618ccb3cd0554e2c9297a32929da848ac774956dd7e81ef6fcdf89 candidate_sha256=42d3106651618ccb3cd0554e2c9297a32929da848ac774956dd7e81ef6fcdf89
RECIPROCAL_BENCH res=1920x1080 scene=triangle rounds=8 reference_ms=37.166,38.907,38.222 candidate_ms=44.176,41.880,35.284
RECIPROCAL res=1920x1080 scene=perspective_texture ref_writes=886465 candidate_writes=886465 differing_pixels=0 differing_channels=0 max_rgb_error=0 ref_sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a candidate_sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a
RECIPROCAL_BENCH res=1920x1080 scene=perspective_texture rounds=8 reference_ms=36.035,39.960,33.259 candidate_ms=41.208,38.899,39.283
```

The perspective-texture medians were 13.978 ms reference versus 16.627 ms candidate at 1024x768 and 36.035 ms reference versus 39.283 ms candidate at 1920x1080. The exact variant was slower in both. No QEMU run was made for a native candidate that failed the required performance gate. After removing the experiment, focused baseline checks returned to their stored checksums:

```text
$ python tests\soft3d_bench.py
GOLDEN res=1024x768 scene=perspective_texture writes=335826 sha256=22beb925133fca077179b9081d0b9ce7b82403e60dc6f3d370de0e2145287112
GOLDEN res=1920x1080 scene=perspective_texture writes=886465 sha256=9a88176a8dac74a6faf0f001d165c82767d188ee72b6212641ae743a63423a6a
$ python tests\soft3d_native.py
soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)
```

Decision: stop the 5c.2 interpolation experiment after three approaches. The existing fixed-point rasterizer/top-left-rule design remains NOT DONE. Continue with the independent 5d damage-accounting work; no performance claim is made for the rejected reciprocal experiment.

## Phase 5d.2 focused damage accounting (2026-10-05)

Added `tests/damage_accounting.py`. It uses the existing QMP-only disposable GUI fixture, reads guest compositor counters, verifies the Notes caret pixel, and checks the dock hover path. It does not alter the compositor. Builds and guests ran in `C:\Users\syltu\AppData\Local\Temp\PollikOS-damage-cee1731d354f4148a4ccc74d9dfc01ab`; `build\PollikData.img` in the workspace was not opened by the test run. The temporary build used a newly created sparse PollikFS disk and `-NoSync`.

The isolated build and final TCG runs printed:

```text
PollikOS built: build/PollikOS-Alpha.img (820000 kernel bytes, 1602 sectors loaded at 1 MiB)
$ python tests/damage_accounting.py --resolution 1024x768
DAMAGE res=1024x768 stage=clock_minute_tick duration_s=32.30 frames=1 full=0 partial=1 dock=0 rects=1 composed_px=32768 lfb_px=32768
DAMAGE_CLOCK res=1024x768 old=17:51 new=17:52 partial_frames=1 full_redraws=0
DAMAGE res=1024x768 stage=idle_no_input_4s duration_s=4.00 frames=0 full=0 partial=0 dock=0 rects=0 composed_px=0 lfb_px=0
DAMAGE_NOTES_STATE res=1024x768 overlay=0 active_animations=[] pointer=(760, 500)
DAMAGE res=1024x768 stage=notes_static_caret_3s duration_s=3.00 frames=0 full=0 partial=0 dock=0 rects=0 composed_px=0 lfb_px=0
DAMAGE_CARET res=1024x768 pixel=(562, 278) row=3 x=328 pixel_before=0x9470bd pixel_after=0x9470bd clock_before=17:52 clock_after=17:52
DAMAGE res=1024x768 stage=dock_hover duration_s=1.50 frames=53 full=0 partial=0 dock=40 rects=93 composed_px=3650272 lfb_px=3663584
PASS damage accounting: zero-work idle, Notes caret, Dock hover; RTC minute
$ python tests/damage_accounting.py --resolution 1920x1080
DAMAGE res=1920x1080 stage=clock_minute_tick duration_s=47.73 frames=1 full=0 partial=1 dock=0 rects=1 composed_px=61440 lfb_px=61440
DAMAGE_CLOCK res=1920x1080 old=17:50 new=17:51 partial_frames=1 full_redraws=0
DAMAGE res=1920x1080 stage=idle_no_input_4s duration_s=4.00 frames=0 full=0 partial=0 dock=0 rects=0 composed_px=0 lfb_px=0
DAMAGE_NOTES_STATE res=1920x1080 overlay=0 active_animations=[] pointer=(760, 500)
DAMAGE res=1920x1080 stage=notes_static_caret_3s duration_s=3.00 frames=0 full=0 partial=0 dock=0 rects=0 composed_px=0 lfb_px=0
DAMAGE_CARET res=1920x1080 pixel=(562, 278) row=3 x=328 pixel_before=0x9470bd pixel_after=0x9470bd clock_before=17:51 clock_after=17:51
DAMAGE res=1920x1080 stage=dock_hover duration_s=1.45 frames=52 full=0 partial=0 dock=39 rects=91 composed_px=3559348 lfb_px=3572660
PASS damage accounting: zero-work idle, Notes caret, Dock hover; RTC minute
```

The clock's sole partial frame was exactly `width * 32` composed and presented pixels at both sizes (32,768 and 61,440). Four seconds of no input and three seconds with the stationary Notes caret both recorded zero frames and zero LFB pixels. The caret remained `0x9470bd` at its data-derived coordinate. Dock hover stayed partial (`full=0`); its measured totals include cursor damage and changing icon bounds. The dock stage is an interaction sample, not a steady per-frame throughput number.

The harness was corrected before accepting the caret result. A probe taken immediately after opening Notes counted five partial frames and 773,120 pixels; each frame was 154,624 pixels (`1024 * 151`), consistent with the Dock launch bounce. The code starts a Dock launch animation when opening the app (`ui_anim_dock_launch`) and `ui_anim_has_active()` includes that animation even when the per-window animation array is inactive. The final harness waits for `g_dock_launch_app == 0xffffffff` before starting the Notes-caret interval. An initial non-warmed clock interval also included one one-shot full redraw; adding a two-second desktop-settle period produced the single-partial-frame readings above. These were fixture timing errors, not compositor fixes.

Scope remains TCG with `-display none`; WHPX/SDL accounting and the full-screen damage-path audit are NOT RUN. No optimization or visual change was made in this phase.

## Phase 5d.3 open-animation current baseline (2026-10-05)

Before changing the animation compositor, reran the complete GUI benchmark against the same isolated 820,000-byte i386 kernel, TCG, qemu64, and `-display none`. Both benchmark reports ended with `status=PASS`; this is an emulator baseline, not a WHPX target result. The screenshot supplied by the user shows PollikMark's separate Compositor workload at 38 fps, but gives no accelerator/resolution and is not mixed into these measurements.

```text
$ python tests/benchmark_gui.py --headless --accel tcg --cpu qemu64 --resolution 1024x768
1024x768 window_open_animation: 1 verified operations; 9.33 actual fps; 9.61 render/s
1024x768 window_open_animation detail: dirty=540708 px/frame; phase_us input/app/layout/draw/compose/LFB=2/2/5/0/89978/780; frame mean/p95/max=104049/135032/135032 us; guest interval p50/p95/max=90792/147447/147447 us (4 samples; WM TSC presentation intervals)
$ python tests/benchmark_gui.py --headless --accel tcg --cpu qemu64 --resolution 1920x1080
1920x1080 window_open_animation: 1 verified operations; 19.09 actual fps; 20.88 render/s
1920x1080 window_open_animation detail: dirty=753888 px/frame; phase_us input/app/layout/draw/compose/LFB=4/3/12/0/32486/1128; frame mean/p95/max=47900/93722/93722 us; guest interval p50/p95/max=34388/108568/108568 us (6 samples; WM TSC presentation intervals)
$ Get-Content benchmark-1024x768.json -Raw | ConvertFrom-Json | Select-Object status
status
------
PASS
$ Get-Content benchmark-1920x1080.json -Raw | ConvertFrom-Json | Select-Object status
status
------
PASS
```

At 1024x768, the mean composed area is 68.7% of the screen; at 1920x1080, it is 36.4%. The measured bottleneck phase was composition (89.978 ms and 32.486 ms per completed animation frame, respectively). This is the baseline for the next experiment. Cached transform changes, pixel-identical window-region goldens, and before/after results are NOT RUN; no compositor optimization is claimed here.

A candidate packed constant-alpha blend was tested in isolation against the exact current per-channel formula before any production integration. It matched 514,000 seeded vectors over alpha 0..256, but native throughput was noisy and slower by median, so it was rejected without changing the compositor:

```text
$ python tests/anim_blend_native.py
PASS animation blend exact parity: alpha=0..256 vectors=514000 seed=0x5d3
BENCH animation_blend pixels=2073600 repeat=1 reference_ms=2.454 packed_ms=3.734 exact=1 sink=00e5f000
BENCH animation_blend pixels=2073600 repeat=2 reference_ms=2.912 packed_ms=2.743 exact=1 sink=00000000
BENCH animation_blend pixels=2073600 repeat=3 reference_ms=2.883 packed_ms=4.292 exact=1 sink=00e5f000
```

Reference median: 2.883 ms; candidate median: 3.734 ms. It is NOT integrated.
