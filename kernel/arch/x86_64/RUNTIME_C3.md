# PollikOS C3 userspace C runtime

C3 is the first PollikOS userspace C runtime: a small PollikOS-native libc and
crt0 on the verified C2 memory primitives. It is cross-built on the host; no
native compiler or libc port exists. C4 packages these artifacts as an SDK.

## Startup path

1. The kernel loads a static ELF64 and builds the startup ABI v1 stack
   (`RUNTIME_C1.md`): `RDI=argc`, `RSI=argv`, `RDX=envp`, `RCX=version`.
2. `crt0.asm` (`sdk/crt/crt0.asm`) forwards those four registers to
   `__libc_start`; it contains no other logic.
3. `__libc_start` (`sdk/lib/start.c`) publishes `char **environ`, validates
   version 1 and the argument bounds, then calls `main(argc, argv)`.
4. Returning from `main` calls `exit(return_value)`, which maps to the existing
   process exit syscall; the kernel keeps the low 8 bits as the exit status.

`int main(void)` and `int main(int argc, char **argv)` are both supported;
`envp` is available through `environ` and `getenv`. Applications never define
`_start` and contain no assembly.

## Syscall layer and errno

`sdk/include/pollikos/syscall.h` is the only place with inline `syscall`
instructions. It provides 0/1/2/3-argument wrappers that follow the PollikOS
register ABI (RAX number/result, RDI/RSI/RDX arguments, RCX/R11 clobbered) and
returns the raw signed result. `sdk/lib/errno.c` translates negative `USER_E*`
results to POSIX errno numbers for the POSIX-style wrappers; `errno` is a
single per-process global until userspace threads exist. Raw `pollikos_*`
functions return negative kernel codes directly and never touch `errno`.

## Allocator

`sdk/lib/malloc.c` is a small process-private allocator over `brk` plus
anonymous `mmap`:

- 64-byte boundary-tag headers with a magic value, INUSE bit and previous-size
  link; 16-byte aligned payloads.
- Doubly-linked free list, first fit, block splitting, forward and backward
  coalescing; `free(NULL)` is safe and double frees abort with a diagnostic.
- Blocks at or above 128 KiB are mapped anonymously and released with
  `munmap`; larger requests fall back to `brk` when a single bounded mapping
  is not possible. `malloc(0)` returns a unique freeable block.
- `calloc` checks multiplication overflow; `realloc` handles `NULL` (malloc),
  zero (free + `NULL`, documented), in-place growth/shrink with splitting, and
  allocate-copy-free otherwise; it never copies beyond the old block.
- On kernel `ENOMEM`, `malloc`/`calloc`/`realloc` return `NULL` with
  `errno=ENOMEM`; the kernel reclaims all pages at process exit regardless of
  userspace state.

## Supported API

- Memory/strings: `memcpy memmove memset memcmp memchr`, `strlen strnlen
  strcmp strncmp strcpy strncpy strchr strrchr strcat strncat`.
- stdlib: `malloc calloc realloc free`, `exit _exit abort`, `atoi strtol
  strtoul`, `getenv`, `EXIT_SUCCESS/EXIT_FAILURE`.
- stdio: `putchar fputc puts fputs printf fprintf vprintf vfprintf snprintf
  vsnprintf fflush` over unbuffered fd 1/2/0; integer-only formatter with
  `%s %c %d %i %u %x %X %p %%`, `l`/`z` length modifiers, width, `-` and `0`.
- Read-only filesystem: `open read lseek close stat fstat`, `DIR` streams with
  `opendir readdir rewinddir closedir dirfd`, `chdir getcwd` and `getpid`.
- Time: `pollikos_clock_ticks`, `pollikos_monotonic_ms`, `pollikos_sleep_ms`,
  `sleep_ms` (100 Hz PIT ticks; no calendar time).
- Memory/runtime: `pollikos_sdk_version`, `pollikos_page_size`,
  `pollikos_heap_bytes`.

## Build configuration

`sdk/tools/pollikos_sdk.ps1` is the single source of compiler flags:
`--target=x86_64-none-elf`, `-ffreestanding -fno-builtin -fno-pic -fno-pie`,
`-fno-stack-protector -mno-red-zone -msse2 -mno-avx`, `-mcmodel=large`
(userspace text/data load above 4 GiB), `-Wall -Wextra -Werror`. Link order is
`crt0.o`, application objects, user archives, `libpollikc.a`, using
`sdk/linker/pollik.ld` (RX text/rodata, RW data/bss, no PT_INTERP or dynamic
sections).

No host CRT, host libc, compiler-rt or shared library is linked; every built
ELF is checked for undefined symbols, `PT_INTERP`/`PT_DYNAMIC`/`NEEDED` and a
defined `_start`.

## Limitations

This historical C3 checkpoint has since gained scalar float/double with
per-process x87/SSE2 state, a starter `<math.h>`, software signal groups and
stop/continue. AVX, `long double`, threads, TLS and dynamic linking remain
unavailable. Synchronous hardware-fault delivery and `SA_RESTART` are not
supported.
The current user stack is 256 KiB plus a guard page, so deep unbounded recursion can still fault.
Filesystem mutation (C5, see `FILE_MUTATION.md`)
and spawn/wait process creation (C6, see `PROCESS_MODEL.md`) extend the
wrappers documented here; there is still no `fork` or `exec`.
