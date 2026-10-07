# PollikOS C SDK

Cross-hosted C development for PollikOS x86_64: crt0, a PollikOS-native mini
libc (`libpollikc.a`), public headers, one linker script and the `pollikcc`
driver. Applications are ordinary C with `main`; the result is a static
PollikOS ELF64 that runs in Ring 3 under the PollikOS scheduler.

Scalar `float` and `double` are supported with x87/SSE2. Each process has an
isolated FPU context, and the SDK provides common scalar `<math.h>` functions
for `float` and `double`, including exponentials, logarithms, powers and
trigonometry. AVX, `long double` and complete, high-accuracy `libm` coverage
are not included yet; trig range reduction is intended for ordinary input sizes.

C5 adds writable files: `open` with `O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`/
`O_APPEND`, `write`, `mkdir`, `unlink`, `rmdir` and `rename` are available
through `<pollikos/fs.h>` and the POSIX-style wrappers. See
`kernel/arch/x86_64/FILE_MUTATION.md` for the exact semantics.

`time()` and `gettimeofday()` return UTC seconds from the RTC through the new
`USER_CLOCK_REALTIME` syscall. `pollikos_clock_ticks()` and
`pollikos_monotonic_ms()` continue to report monotonic scheduler time. PollikFS
stat timestamps remain boot-relative tick counters; the disk format is unchanged.

C6 adds the spawn/wait process model in `<pollikos/process.h>`:

```c
long child = pollikos_spawn("/bin/tool", argv, NULL); /* envp NULL = inherit */
pollikos_wait_t status;
if (pollikos_waitpid(child, &status) == child &&
    status.kind == POLLIKOS_WAIT_EXITED)
    return status.code;
```

`pollikos_spawnp` searches `PATH` (default `/bin`) when the name has no slash,
`pollikos_run` is a spawn+wait convenience, and `setenv`/`unsetenv` provide a
process-local environment. See `kernel/arch/x86_64/PROCESS_MODEL.md` for
parent/child ownership, zombie/orphan policy, descriptor inheritance and the
typed termination status.

C7 completes the compiler-facing runtime. `<stdio.h>` now provides a real
buffered `FILE` layer (`fopen`/`fclose`/`fread`/`fwrite`/`fseek`/`ftell`/
`fgets`/`printf`/`snprintf`/`perror`/`remove`), `<ctype.h>`, `<assert.h>`,
`<unistd.h>` with `access`, and POSIX-style `<sys/stat.h>`, `<sys/types.h>`
and `<dirent.h>` aliases over the PollikOS ABI. libc adds `strstr`/`strspn`/
`strcspn`/`strpbrk`/`strdup`/`strndup`, `strtoll`/`strtoull`/`atol`, `qsort`,
`bsearch`, `mkstemp` and `putenv`/`setenv`/`unsetenv`. Limits are raised for
toolchain workloads: 256 KiB user stack, 128 descriptor slots per process,
64 argv/envp entries and 2 MiB images. The inode pointer tree can represent
~64.24 MiB, but PollikFS has only 32 MiB gross capacity including metadata, so
actual file data is bounded by the remaining volume space. See
`kernel/arch/x86_64/TINYCC_READINESS.md` for the compiler-readiness audit.

C8-C10 add native TinyCC 0.9.27 as
`/bin/tcc`, the runtime sysroot lives in PollikFS (`/usr/include`,
`/usr/lib/crt0.o`, `/usr/lib/libc.a`, `/usr/lib/tcc/libtcc1.a`,
`/usr/src/libc`), and the self-host driver rebuilds `libc.a` with native
`tcc -c` before compiling and linking C inside the guest. See
`kernel/arch/x86_64/TINYCC_PORT.md` for the port contract and limitations.
The same `pollikcc` driver also cross-compiles with host Clang, so Clang is a
second compiler for PollikOS programs; it is not installed inside the guest.

The interactive console milestone adds `/bin/pollish` (`sdk/apps/pollish.c`),
the x86_64 shell: cwd prompt, line editing, command history (persisted to
`/home/.pollik_history`), `$?`/`$NAME` expansion, file builtins
(`ls`/`dir`, `cat`/`type`, `mkdir`, `rm`/`del`, `rmdir`, `mv`/`rename`,
`touch`) and real external execution through `spawnp`/`spawn` + `waitpid`.
The normal kernel boots into it, so `tcc hello.c -o hello.pol` and
`./hello.pol` can be typed by hand. A `.pol` file is a static PollikOS ELF64
executable. See `kernel/arch/x86_64/CONSOLE_TTY.md`.
`.pol` is the executable file format and extension; there is no `.app` manifest
format. The i386 graphical desktop's Files app can launch 32-bit ELF `.pol`
programs as Ring 3 processes. TinyCC currently emits ELF64 `.pol` files, which
run in the x86_64 shell but need the x86_64 graphical desktop before Files can
launch them. `/Applications` is the location for installed `.pol` executables.
The built-in graphical clients still run inside the kernel and have not yet
been converted into standalone `.pol` programs with a user-mode GUI API.

## Requirements (Windows host)

- LLVM/Clang with `ld.lld`, `llvm-ar`, `llvm-nm`, `llvm-readobj` on `PATH`
- NASM on `PATH` (only for building `crt0.o`)
- Python 3 (only for `pollikinstall.py`)
- QEMU for running (tests use `qemu-system-x86_64`)

## Layout

```
sdk/
  include/        public headers (std*.h and pollikos/*)
  lib/            libc sources -> libpollikc.a
  crt/crt0.asm    startup object
  linker/pollik.ld  canonical userspace linker script
  tools/          pollikos_sdk.ps1, pollikcc.ps1, pollikinstall.py, pollikfs_install.py
  examples/       hello, files, dirs, memory, cwd, time, errno, multifile, library
  tests/          C3-C7 fixtures (argv, libc/allocator, filesystem, runtime, ABI,
                  faults, stdio, tool chains, allocator stress, large ELF)
  VERSION         SDK version (1.0.0)
```

## Quick start

```powershell
# 1. Build an application (builds crt0 + libpollikc.a on first use)
.\sdk\tools\pollikcc.ps1 sdk\examples\hello.c -o hello.pol

# Writable-file example
.\sdk\tools\pollikcc.ps1 sdk\examples\write.c -o write.elf

# 2. Install it into a disposable PollikFS test image (never formats)
python sdk\tools\pollikinstall.py build\x86_64\selftest\PollikData-test.img /bin/hello hello.pol

# 3. Boot the self-test kernel and run it
python tests\x86_64_boot.py
```

`pollikcc` options: `-o <file>`, `-c` (compile only), `-O0/-O1/-O2/-O3/-Os`,
`-g`, `-D<name>[=value]`, `-I<dir>`, `-L<dir>`, `-l<name>`, `--runtime <dir>`,
`--rebuild-runtime`, `--verbose`, `--version`.

```powershell
# Multi-file
.\sdk\tools\pollikcc.ps1 main.c utils.c parser.c -o program.elf
# Objects then link
.\sdk\tools\pollikcc.ps1 -c main.c; .\sdk\tools\pollikcc.ps1 -c utils.c
.\sdk\tools\pollikcc.ps1 main.o utils.o -o program.elf
# Static library
.\sdk\tools\pollikcc.ps1 -c libexample.c -o libexample.o
llvm-ar rcs libexample.a libexample.o
.\sdk\tools\pollikcc.ps1 app.c -L. -lexample -o app.elf
```

Builds are reproducible for identical inputs and flags; the build script
verifies this by building `hello.c` twice and comparing SHA-256.

## Compiler flags and why

| Flag | Reason |
| --- | --- |
| `--target=x86_64-none-elf` | PollikOS target; no host libc/CRT is linked |
| `-ffreestanding -fno-builtin` | freestanding runtime, no implicit libc semantics |
| `-fno-pic -fno-pie -mcmodel=large` | static images above 4 GiB need 64-bit addressing |
| `-fno-stack-protector` | no TLS/guard runtime in PollikOS; explicitly disabled |
| `-mno-red-zone` | safe against the current kernel entry/user ABI |
| `-msse2 -mno-avx` | use the process-isolated x87/SSE2 ABI; AVX state is not saved |
| `-Wall -Wextra -Werror` | keep the small runtime warning-clean |

Every linked application is checked for undefined symbols (catches
compiler-rt/division/stack-protector helpers), `PT_INTERP`, `PT_DYNAMIC`,
`NEEDED`, the `_start` entry and the x86_64 machine type.

## ABI versions targeted

- startup ABI v1 (`kernel/arch/x86_64/RUNTIME_C1.md`)
- SYSCALL ABI, opcode/error numbers generated into
  `pollikos/abi_numbers.h` from `user_abi.h` at build time
- stat ABI v1 and dirent ABI v1 (`STAT_ABI.md`, `DIRECTORY_ABI.md`)
- C runtime details: `kernel/arch/x86_64/RUNTIME_C3.md`

`pollikos_sdk_version()`, `POLLIKOS_SDK_VERSION_*` and the ABI version macros
in `<pollikos/version.h>` let applications check what they were built against.

## CMake

Not provided in this milestone. A host CMake cross-toolchain file was attempted
but the Windows Clang driver kept selecting the host (MinGW) linker for CMake's
link rule, and a verified configuration would need more work than C4 allows.
`pollikcc` is the supported workflow; no CMake exists inside PollikOS.

## Limitations

Threads, `fork`, dynamic libraries, AVX, `long double` and complete POSIX/libm
coverage are not implemented. The math library covers common scalar functions,
but does not promise correctly rounded results across the full IEEE-754 range.
Basic software signals are available through
`<signal.h>` (`sigaction`, `sigprocmask`, `sigpending`, `kill`, `raise`), with
`getpgid`, `getpgrp` and `setpgid` from `<unistd.h>`. `kill` accepts a PID, zero
for the caller's group, or a negative process-group ID. `SIGSTOP` and `SIGCONT`
also stop and resume scheduled processes. Sessions, restart flags and
hardware-fault delivery are not supported. The
current user stack is 256 KiB and each process supports 128 descriptors.
Filesystem mutation is available but bounded: arbitrary truncate, `O_SYNC` and
hard links are not implemented. Plain `rename()` never overwrites; the separate
`rename_replace()` operation replaces regular files. Although the inode pointer
tree represents ~64.24 MiB, actual file data is constrained below the volume's
32 MiB gross capacity by metadata and other allocated files.

## Media extensions (x86-64)

`pollikos/media.h` decodes WAV/MP3 in userspace to 48000-Hz stereo S16LE.
`pollikos/audio.h` submits asynchronous PCM to the AC97 driver with explicit
ownership, queue backpressure, pause/resume and stop. `pollikos/image.h` decodes
bounded PNG/JPEG/BMP/GIF-first-frame inputs to owned RGBA8 buffers.

Build the public API example:

```powershell
.\sdk\tools\pollikcc.ps1 --runtime build/x86_64/system/sdk sdk/examples/audio.c -o build/audio.pol
```

Encoded audio is limited to 16 MiB; decoded PCM is streamed. MP3 encoder
padding is retained. The initial resampler is linear. This audio/image checkpoint does not add full POSIX or additional language
runtimes. MP4 requires the optional library described below. See
[the media checkpoint](../docs/MEDIA_SDK_CHECKPOINT.md) and
[executed test evidence](../docs/MEDIA_SDK_EVIDENCE.md).

## MP4 Baseline extension

`pollikos/movie.h` and `/usr/lib/libpollikvideo.a` support the documented
unfragmented H.264 Baseline/AAC-LC subset. `/bin/video.pol` is the native
software player. For host SDK clients link the optional archive, for example
`--runtime build/x86_64/system/sdk -Lbuild/x86_64/system/codecs -lpollikvideo`.
The library is never linked into the kernel. Limits and executed proof are in
[MP4_CHECKPOINT.md](../docs/MP4_CHECKPOINT.md).
