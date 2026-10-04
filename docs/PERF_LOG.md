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
