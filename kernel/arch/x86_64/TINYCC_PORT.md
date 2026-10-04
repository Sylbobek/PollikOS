# TinyCC PollikOS port (C8-C11)

Native C compilation inside PollikOS is provided by a pinned upstream TinyCC
0.9.27 snapshot (`third_party/tinycc`, upstream githash
`d348a9a51d32cece842b7885d27a411436d7887b`) with a small set of
`CONFIG_TCC_POLLIKOS`-guarded changes. The host cross-builds the compiler once;
after boot, `/bin/tcc` runs as a normal PollikOS userspace ELF64 process and
compiles C entirely inside the guest.

## Scope and honest status

| Item | Status |
| --- | --- |
| `/bin/tcc` runs natively | YES (bootstrap stage 0) |
| Native compile + link + run of normal C | YES (`/home/hello.c` -> `/home/hello`, exit 42) |
| Native libc objects for native linking | YES (`/usr/src/libc/*.c` rebuilt by `/bin/tcc`) |
| Multi-file and `-c`/object link workflows | YES |
| Preprocessor, diagnostics, bad output paths | YES |
| TinyCC rebuilding itself | NOT VERIFIED (stretch goal, not attempted) |
| Scalar `float`/`double` code generation | SUPPORTED (x87/SSE2; per-process FPU state) |
| `<math.h>` | SUPPORTED (documented starter subset; no full libm) |
| AVX, `long double` | NOT SUPPORTED |
| Basic software signals | SUPPORTED (PollikOS `<signal.h>` subset) |
| `alloca`, threads, `fork` | NOT PRESENT (TinyCC does not require them for this build) |

## Host bootstrap build

`build-x86_64.ps1` builds three artifacts that `tools/build_x64_data.py`
installs into the generated PollikFS image:

1. `/bin/tcc` from `third_party/tinycc/tcc.c` (single-translation-unit build:
   `-DONE_SOURCE=1 -DCONFIG_TCCBOOT=1 -D__pollikos__=1`, `--allow-fpu` for the
   host compiler only) — about 353-361 KiB, ET_EXEC, RX+RW segments, no
   `PT_INTERP`.
2. `/usr/lib/tcc/libtcc1.a` from `lib/libtcc1.c` and `lib/va_list.c`
   (`--allow-simd --small-code-model`).
3. `/usr/lib/libc.a` (initial, Clang-built `libc-native.a`) plus
   `/usr/src/libc/*.c` sources, `/usr/include` SDK headers,
   `/usr/lib/crt0.o` and `/usr/lib/tcc/include` TinyCC headers.

At run time the self-host driver rebuilds the libc archive with native TinyCC
(see below), so the final application link uses objects produced by the same
compiler that links them.

## Sysroot layout inside PollikFS

```
/bin/tcc                     natively running compiler
/usr/include                 PollikOS SDK headers (stdio, stdlib, ...)
/usr/include/tcc/include     TinyCC internal headers (stdarg.h, ...)
/usr/lib/crt0.o              PollikOS startup object (startup ABI v1)
/usr/lib/libc.a              native libpollikc objects (rebuilt at run time)
/usr/lib/tcc/libtcc1.a       TinyCC runtime helpers
/usr/src/libc/*.c            libc sources compiled natively at run time
/home                        self-host workspace written by the driver
```

`config.h` points every path at PollikFS: `CONFIG_TCCDIR /usr/lib/tcc`,
`CONFIG_TCC_SYSINCLUDEPATHS "/usr/include:{B}/include"`,
`CONFIG_TCC_LIBPATHS "/usr/lib:{B}/lib"`, `CONFIG_TCC_CRTPREFIX /usr/lib`,
no `PT_INTERP`, static link only.

## PollikOS-specific TinyCC changes

| File | Change |
| --- | --- |
| `config.h` | PollikOS target configuration, sysroot paths, `CONFIG_TCC_POLLIKOS`, clang warning suppressions |
| `libtcc.c` | static linking; scalar SSE2 enabled; fixed `__pollikos__` define outside the per-target chain; `__TINYC__` from the PollikOS version; `crt0.o` instead of `crt1.o`/`crti.o` |
| `tcc.c` | "PollikOS" in the version banner; benchmark timing diagnostic; `-E` output to `-o` file |
| `tccelf.c` | resolve `R_X86_64_PLT32` to `R_X86_64_PC32` for statically defined symbols (static executables, no PLT); no `crtn.o` |
| `x86_64-link.c` | `ELF_START_ADDR 0x8000000000`, 4 KiB pages (matches `USER_CODE`) |
| `tccpp.c`, `tccgen.c` | support scalar float/double literals and operations; reject `long double` |
| `cctools.c` | `-m32`/`-m64` cross mode rejected on PollikOS |

## Native libc bootstrap

`/bin/selfhost_driver_c` (kernel-driven, cross-compiled) performs the final
chain:

1. writes the proof source to `/home/hello.c`;
2. runs `tcc -c` over `/usr/src/libc/*.c` and `tcc -ar rcs /usr/lib/libc.a ...`
   so native TinyCC produces the libc objects it will link against;
3. runs `tcc /home/hello.c -o /home/hello` and validates the result;
4. launches `/home/hello` and checks the printed text and exit status 42;
5. runs the compiler checks (preprocessor `-E`, `-c` object, libc program,
   filesystem and float/double math programs, multi-file, object link, invalid
   source, missing header, unwritable output) and 25 compile/run
   churn cycles;
6. the kernel then verifies PMM/handles/slots/zombies are back at baseline.

Required markers (all emitted by the run):

```
[SELFHOST] compiler=/bin/tcc
[SELFHOST] source=/home/hello.c
[SELFHOST] native compile begin
[SELFHOST] tcc exit=0
[SELFHOST] output=/home/hello
[SELFHOST] output ELF valid
Hello from self-hosted PollikOS C!
[SELFHOST] program exit=42
[SELFHOST] PASS
[SELFHOST] balance pmm=0x... handles=0x0 slots=0x0 zombies=0x0
```

On the dedicated almost-full ENOSPC image the self-host suite reports
`[SELFHOST] skipped: dedicated full image` (there is no space to rebuild libc).

## Generated output

Native `tcc hello.c -o hello` produces a static ET_EXEC ELF64, EM_X86_64,
entry `0x80000000b0`, two `PT_LOAD` segments (RX code, RW/NX data), no
`PT_INTERP` and no dynamic dependencies. It links `/usr/lib/crt0.o` and
`/usr/lib/libc.a`, enters `__libc_start` (startup ABI v1), calls `main` and
turns the return value into the exit status.

## Known limits and open items

- Scalar x87/SSE2 float and double work with isolated process state. AVX,
  `long double` and full transcendental `libm` remain unavailable.
- The kernel `write` syscall caps a single request at 64 KiB; the SDK `write()`
  splits larger requests, which is what lets TinyCC write large objects.
- User stack is 256 KiB; very deep recursion can still overflow the guard.
- `time()`/`gettimeofday` use the RTC-backed UTC clock. The `/bin/tcc` build
  defines `POLLIKOS_TCC_DETERMINISTIC_DATE=1`, which fixes `__DATE__` and
  `__TIME__` to `Jan  1 1970` and `00:00:00` for repeatable native builds.
  Omitting that compile-time flag makes TinyCC's date macros use libc time.
- `alloca`, threads and `fork` are unavailable; the port does not need them.
- Signal support includes per-process handlers/masks, stop/continue, process
  groups and group-directed `kill`; `SA_RESTART` and synchronous hardware-fault
  delivery remain unavailable.
- Native TinyCC self-rebuild is not attempted; only stage-0 native compilation
  is claimed.
- Host Clang is a second cross-compiler through `pollikcc`; Clang is not part of
  the guest image.
- A rare intermittent native `spawn` failure was observed twice early in
  bring-up, but no failing return was captured. `python tests/x86_64_spawn_stress.py`
  runs 5,000 mixed exit/fault/kill spawn+`waitpid` cycles at 64 MiB and 5 GiB,
  compiling and running a TinyCC output every 200 cycles. The 2026-10-03 run and
  2026-10-04 rerun each completed all 5,000 cycles and 25 compiler rounds at both
  profiles, with PMM, handle, slot and zombie counts back at their baselines.
  An extended 2026-10-04 run (`--cycles 10000 --tcc-interval 400`) also passed
  at both profiles: 10,000 cycles and 25 compiler rounds per guest. This adds
  20,000 mixed child lifecycles without reproducing the failure.
  The root cause remains unconfirmed: the failure did not recur, so the evidence
  does not implicate reaping, descriptor references, process groups, or PID/slot
  reuse. The stress driver reports raw spawn/wait results and does not retry.
