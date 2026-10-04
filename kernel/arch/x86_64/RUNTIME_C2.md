# PollikOS C2 userspace memory and runtime foundation

C2 supplies the kernel primitives a future libc needs for `malloc`/`free`:
an isolated per-process heap grown by `brk`, bounded anonymous `mmap`/`munmap`,
`getpid`, a monotonic PIT clock and blocking sleep. The kernel provides memory;
allocator policy stays in userspace. No crt0, libc, allocator, filesystem write
or toolchain work is part of C2, and no FPU/SIMD context exists yet.

## Address layout

`user_abi.h` centralizes the deterministic userspace layout; no scattered magic
addresses. All regions are canonical, page aligned, supervisor-inaccessible and
disjoint from the reserved stack region. ELF segments overlapping the reserved
runtime regions are rejected with `ELF64_RANGE`, so a loaded image can never
collide with a heap or anonymous mapping.

| Region | Base | Limit (exclusive) | Growth | Purpose |
| --- | --- | --- | --- | --- |
| ELF images | `0x8000000000` | `0x400000000000` | image-defined | loaded PT_LOAD pages |
| Anonymous mmap | `0x400000000000` | `0x600000000000` | bump upward | 16 bounded records |
| Heap (brk) | `0x700000000000` | `0x780000000000` | upward | one per process |
| Guard gap | `0x780000000000` | `0x7ffffffe0000` | never mapped | heap/stack separation |
| User stack | 256 KiB bounded region below `USER_LIMIT` | USER/RW/NX | none | 64 pages + 1 guard |

Relationships: `brk` grows upward from the heap base and is rejected outside
`[USER_HEAP_BASE, USER_HEAP_LIMIT]`, so it can never reach the stack or the
kernel. The mmap bump cursor never rewinds; each allocation is page aligned and
capped at `USER_MMAP_MAX_PAGES` (1024) pages with at most 16 live records.
Although the reserved virtual address window spans 32 TiB, at most 16 live
mmap records of up to 4 MiB each are allowed per process (64 MiB at once); each
mapped page needs physical RAM.
This is virtual address space, not multi-terabyte RAM or swap backing.
`brk` and mmap regions are far below the stack; the unreserved gap is kept
unmapped. Anonymous allocations and heap pages are always RW/NX user pages.

## Process state and ownership

`Process64` owns the complete runtime state in its two-page kernel control record:

- `heap_base`, `heap_break`, `heap_limit`: the current program break and the
  allowed range. `heap64_reset` initializes them (and the mmap cursor) when the
  process is built; no global heap is shared between processes.
- `mmap[USER_MMAP_MAX]`: explicit `{base, pages}` ownership records. Exact
  base/length matching means `munmap` is unambiguous and never splits or
  partially unmaps a region. A full table returns `ENOMEM`.
- `wake_tick`: BLOCKED deadline in scheduler ticks; zero means no deadline.

Every heap and anonymous page is allocated with `vmm64_alloc_page`, which
transfers sole ownership to the process address space. Process exit, fault or
kill therefore reclaim all runtime pages through the existing
`vmm64_destroy` path with no extra teardown list, and no runtime page can
survive process destruction.

## brk contract

`USER_BRK` (`0x504f001b`, RDI = requested break):

- RDI 0 queries the current break.
- A non-zero request must lie in `[heap_base, heap_limit]`, otherwise `EINVAL`
  (kernel, noncanonical, stack, below base and overflow requests all fail here).
- Growth rounds to pages, preflights every page in the range (any existing
  mapping yields `ENOMEM`/`EINVAL` before any change), then maps zeroed
  `VM_USER|VM_WRITE` (NX) pages. If any allocation fails after the preflight,
  all pages mapped by that call are unmapped, the previous break is retained
  and the process stays fully valid.
- Shrink unmaps whole pages above the new break; the partial final page is
  retained, so a break inside a page preserves that page's data.
- Success returns the exact new break; the break may be non-page-aligned.

## Anonymous mmap and munmap

`USER_MMAP` (`0x504f001f`, RDI = length, RSI = flags): only anonymous private
pages exist, so any non-zero flags (including future `MAP_FIXED`-style values)
are rejected with `EINVAL`. Length 0 is `EINVAL`; over 1024 pages is `E2BIG`.
The returned page-aligned address is inside the mmap region, starts zeroed
(`vmm64_alloc_page` zeroes the frame) and is RW/NX user memory. No physical
address is ever exposed. There is no file-backed, shared or protection-changing
mapping.

`USER_MUNMAP` (`0x504f0020`, RDI = address, RSI = length): the address must be
page aligned, the length non-zero and the exact `{address, length-in-pages}`
record must be owned by this process. Ownership of every page is verified
before the first unmap, so a rejected call changes nothing. Heap pages, ELF
images, the stack and kernel mappings have no mmap record and are rejected with
`EINVAL`; they cannot be unmapped through this ABI.

## getpid, clock and sleep

- `USER_GETPID` (`0x504f001c`) returns the stable current process PID.
- `USER_CLOCK` (`0x504f001d`, RDI = kind) returns `USER_CLOCK_TICKS` (PIT ticks
  since the scheduler started, 100 Hz) or `USER_CLOCK_MS` (ticks * 10). This is
  the monotonic timer source.
- `USER_CLOCK_REALTIME` (`0x504f003f`) returns validated UTC Unix seconds from
  the CMOS RTC, or `EIO` when the RTC reports invalid or unstable calendar data.
  SDK `time()` and `gettimeofday()` use this one-second-resolution clock;
  `pollikos_clock_ticks()` and `pollikos_monotonic_ms()` remain monotonic.
- `USER_SLEEP` (`0x504f001e`, RDI = milliseconds, max `USER_SLEEP_MAX_MS`):
  reuses the existing `PROCESS_BLOCKED` state and runnable-queue model. The
  process is removed from the runnable queue, `wake_tick` is set and the
  dispatcher continues with other processes or `HLT`. The timer tick scans the
  existing process slots and re-enqueues processes whose deadline passed. This
  is the only wake-up source; there is no second wait-queue structure and no
  POSIX `nanosleep` rem/restart semantics. Zero milliseconds succeeds
  immediately; synchronous regression builds without a timer return `ENOTSUP`.
  A blocked sleeper may be killed like any other blocked process.

## Errors, ABI and C3 readiness

C2 uses the existing PollikOS error codes and sign convention: `EINVAL` for
invalid range/alignment/flags, `ENOMEM` for exhausted ranges or allocation
failure, `E2BIG` for counts over the bounded caps, `ENOTSUP` for sleep without a
timer. New operation numbers continue the `0x504f00xx` sequence in `user_abi.h`,
which is also translated to NASM for fixtures.

For C3, the kernel ABI already provides exactly what `malloc/free/calloc/
realloc` require: a page-aligned growable RW/NX heap whose bytes survive
shrink/regrow, zeroed expansion pages, bounded anonymous mappings, monotonic
time and stable process identity. No kernel redesign is expected; libc will own
allocator policy on top of `brk`/`mmap`.

## Verification

`/bin/memtest` is a real disk-loaded ELF64 fixture using SYSCALL exclusively:
heap grow/write/shrink/regrow with retained data and zero checks, a test-only
bump allocator (unique patterns, non-overlap, shrink and regrow zeroing),
invalid `brk`/`mmap`/`munmap` inputs, execute-from-heap NX fault, injected
allocation failure with rollback, and two peer processes with identical virtual
heap addresses. `c2_test.c` orchestrates it and additionally:

- injects PMM exhaustion at every successful prefix of a four-page growth
  (three page tables plus four frames) and of the first anonymous mapping,
  verifying `ENOMEM`, an unchanged break, no partial mappings and an exact PMM
  return to the pre-call count;
- checks via `vmm64_lookup` that identical virtual heap addresses in two
  processes map distinct RW/NX owned frames, and via `copy_from_user64` that
  each process reads back only its own PID-tagged pattern across preemption and
  blocking sleeps;
- runs 100 full lifecycles (launch, grow, pattern write/read, mmap/munmap,
  getpid, clock, sleep, exit, destroy) with the PMM free count returning
  exactly to baseline.

C2 is verified together with the whole prior x86_64 suite at 16/64/256/5120/32768 MiB;
the i386 build, 100-process stress and focused desktop smoke are unchanged.

## Known limitations

Single BSP, IF=0 kernel memory APIs, RAM-scaled process cap up to 1024, one heap range per process,
bump-only mmap allocation (freed holes are not reused by this bounded version),
exact-record `munmap`, 60-second sleep cap, and no FPU/SIMD context: compiled
programs using floating point remain unsupported until a later milestone. There
is no regular-file write, create, truncate, mkdir, unlink or rename, no crt0,
libc, malloc or toolchain, and no process creation syscall. PollikFS's on-disk
format and all i386 sources are unchanged.
