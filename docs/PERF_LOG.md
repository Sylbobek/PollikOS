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
