# C7 compiler-readiness and TinyCC audit

> The C8-C10 TinyCC port that followed this audit is documented in
> [TINYCC_PORT.md](TINYCC_PORT.md). The readiness tables below record the C7
> starting point; `setjmp`/`longjmp` were added during the port and the native
> compile+link+run chain is verified there.
> Superseded C7 limits: see [PROCESS_MODEL.md](PROCESS_MODEL.md), [ELF64_ABI.md](ELF64_ABI.md) and [FILE_MUTATION.md](FILE_MUTATION.md) for current resource limits.

C7 completes the userspace C environment so the next milestone can port TinyCC
and compile C inside PollikOS. This document records what C7 added, maps the
TinyCC requirements onto the current ABI/libc surface, and lists the known gaps
the port must handle. It is an audit, not a claim that TinyCC runs today.

## What C7 adds

- A real `FILE` stream layer over descriptors (`sdk/lib/stdio.c`): 512-byte
  buffered `fopen` streams, unbuffered standard streams, a flush registry so
  `fflush(NULL)`/`exit` flush forgotten writers, and separate input/output
  cursors. `fopen` modes `r/w/a/r+/w+/a+` with `b` variants, `fread`, `fwrite`,
  `fseek`, `ftell`, `rewind`, `feof`, `ferror`, `clearerr`, `fflush`, `fgetc`,
  `getc`, `getchar`, `ungetc`, `fputc`, `putc`, `putchar`, `fgets`, `fputs`,
  `puts`, `printf`, `fprintf`, `snprintf`, `perror`, `remove`.
- Compiler-facing headers: `<ctype.h>`, `<assert.h>`, `<unistd.h>` (`access`,
  `F_OK/R_OK/W_OK/X_OK`, `close`, `unlink`, `rmdir`), `<sys/types.h>`,
  `<sys/stat.h>` (`struct stat`, `S_IFREG/S_IFDIR`, `stat`, `fstat`) and
  `<dirent.h>` (`DIR`, `struct dirent`, `readdir`) as POSIX-style aliases over
  the PollikOS ABI.
- libc completion: `strstr`, `strspn`, `strcspn`, `strpbrk`, `strdup`,
  `strndup`, `strtoll`, `strtoull`, `atol`, `qsort`, `bsearch`, `mkstemp`,
  `putenv`, `setenv`, `unsetenv`, `strerror`, `abs`/`labs`, `atoi`/`atol`.
- Raised bounded limits: user stack 32 pages + guard (128 KiB), per-process
  descriptors 64, `argc`/`envp` 64 each, 1024-byte strings, 16 KiB of total
  argv/envp bytes, 2 MiB executable image cap, 64 system VFS objects.
- `O_EXCL` for atomic create; PollikFS grows a double-indirect level inside the
  former inode reserve word (wire format unchanged) so a file can reach about
  64 MiB.
- New C7 fixtures (`stdio_c`, `libc_c`, `alloc_stress_c`, `tool_stage_c`,
  `tool_driver_c`, `big_elf_c`) exercise the FILE layer, conversions and
  qsort/bsearch, compiler-like allocation growth, multi-process tool chains,
  temporary files, large argv vectors and a 300 KiB ELF.

## TinyCC requirement audit

| Requirement | Status | Notes |
| --- | --- | --- |
| Integer C compiler sources build cross-hosted | READY | `pollikcc` builds multi-file C and static libraries; ABI tests pass at `-O0` and `-O2`. |
| Preprocessor I/O (`fopen`/`fgets`/`fprintf`/`fclose`) | READY | Buffered FILE layer with `r/w/a/+` modes and flush-on-exit. |
| `ctype` classification | READY | `<ctype.h>` with the standard functions. |
| String/memory utilities | READY | Includes `strstr`, `strspn`, `strcspn`, `strpbrk`, `strdup`/`strndup`. |
| Numeric conversions (`strtoll`, `strtoul`, `atol`) | READY | Shared base 0/8/10/16 parser with `ERANGE`. |
| `qsort`/`bsearch` | READY | Used by the C7 stress fixture. |
| `errno`/`strerror` coverage | READY | Stable `USER_E*` to `errno` translation. |
| Descriptor I/O (`open`/`read`/`write`/`lseek`/`close`) | READY | 64 descriptors per process, `O_EXCL`, append and mutation through VFS. |
| `stat`/`fstat` | READY | Versioned 64-byte ABI converted to `struct stat`. |
| Directory enumeration | READY | `opendir`/`readdir`/`closedir`. |
| Process creation (`spawn`/`wait`) | READY (as `spawn`) | TinyCC itself only needs to run; a driver/shell uses `spawn`+`waitpid`. No `fork`/`exec`. |
| Temporary files | READY | `mkstemp` plus `/tmp/c7` isolation tests. |
| Large inputs | READY (bounded) | 2 MiB image cap, ~64 MiB PollikFS files, 1 MiB per `read`/64 KiB per `write` syscall chunking in libc. |
| Deep recursion | PARTIAL | 128 KiB user stack; TinyCC compiles moderately nested expressions but deep recursion would need a bigger stack. |
| `setjmp`/`longjmp` | MISSING | TinyCC uses them for error recovery; must be added in C8 or compiled out. |
| FPU/SIMD state | MISSING | Integer-only. TinyCC can be built/used without FP codegen, but any floating-point source it compiles cannot run yet. |
| `alloca` | MISSING | Optional; avoid or implement with a stack bump. |
| Signals | MISSING | TinyCC uses `signal`/`SIGSEGV` guards optionally; must be compiled out. |
| `time`/`gettimeofday`/`utime` | PARTIAL | `time` and `gettimeofday` read the validated RTC-backed UTC clock; `utime` and sub-second precision are unavailable. |
| `isatty`, terminal control | MISSING | Return a stable answer or compile out. |
| File-backed `mmap` | MISSING | Only anonymous `mmap` exists; TinyCC's own allocator uses `malloc`, so this is optional. |
| Dynamic linking (`dlopen`) | NOT NEEDED | Self-hosted builds should be static ELF64. |
| Host linker equivalent | PARTIAL | `pollikcc` uses host `ld.lld`; an in-OS linker is future work. |

## Bounded resources the port must respect

| Resource | Limit |
| --- | --- |
| User stack | 32 pages + 1 guard (128 KiB usable) |
| Descriptors per process | 64 (3 standard streams plus 61 ordinary) |
| `argc` / `envp` entries | 64 each |
| argv/envp string | 1024 bytes |
| argv/envp total | 16 KiB |
| Executable image | 2 MiB |
| Process slots | 32 |
| PollikFS file size | `(8 + 256 + 256*256) * 1024` bytes (~64 MiB) |
| `read` syscall / `write` syscall | 4096 / 65536 bytes (libc splits larger FILE requests) |

## Porting risks

1. `setjmp`/`longjmp` are the biggest known blocker for an unmodified TinyCC
   build; they need a saved-register/stack implementation in the C8 port.
2. No FPU/SIMD means TinyCC must be configured for integer code generation and
   its own sources must avoid floating point; compiler-internal doubles would
   not execute.
3. The 128 KiB user stack caps recursion depth; a larger bounded stack is the
   natural C8 adjustment if the port needs it.
4. The in-OS toolchain story (a running `tcc` that emits a PollikOS static
   ELF64) still depends on an in-OS linker/loader step; C7 only guarantees the
   libc/runtime surface is sufficient.

## C7 verification

- `build-x86_64.ps1 -SelfTest` and `python tests/x86_64_boot.py` pass at
  16/64/256/5120 MiB, including 136 selftest markers, the almost-full image
  ENOSPC boot and the reboot pair.
- Focused C7 run: `python build/c7_run.py` (14 C7 markers plus
  `[X64] SELFTEST PASS`).
- i386 regressions unchanged: `build.ps1`, `tests/process_stress.py --ram 64
  256`, `tests/smoke.py --notes-only`.
