# PollikOS x86_64 read-only directory ABI, version 1

> Superseded C1 checkpoint: the fd ranges, 13-descriptor limit and “C2 not started” scope below are historical; current process limits are in [PROCESS_MODEL.md](PROCESS_MODEL.md).

Current C1 transport, stdio and path rules: [RUNTIME_C1.md](RUNTIME_C1.md).
SYSCALL is primary; INT 0x81 is transitional. Cwd/relative paths and explicit
standard streams supersede earlier absolute-only/reserved-fd statements below.

Real ELF64 programs loaded from PollikFS enumerate through the existing per-process
VFS descriptor table. INT 0x81, startup ABI v1 and all prior file/metadata operations
remain intact. No userspace writes or directory mutation are introduced.

| Operation | RAX | RDI | RSI | Result |
| --- | --- | --- | --- | --- |
| opendir | `0x504f0016` | user absolute NUL-terminated path | exactly 1 (USER_O_RDONLY) | process-local fd 3..15 |
| readdir | `0x504f0017` | directory fd | writable UserDirent64 pointer | 1 entry, 0 EOF, negative error |
| close | existing `0x504f0013` | directory fd | unused | 0 |
| seek | existing `0x504f0012` | directory fd | offset exactly 0 | only RDX=SEEK_SET=0 succeeds, returning 0 |

RDX is unused for opendir/readdir. Other flags, including write/create/truncate,
are rejected. Ordinary open retains its regular-file-only semantics and rejects
a directory with EISDIR. Opener paths use the same 128-byte safe-copy bound and
absolute-path parsing as ordinary open. No new handle namespace or allocator exists.

## Stable entry layout

The dedicated `UserDirent64` in `dir_abi.h` is exactly **96 bytes**, little-endian,
with explicit fixed-width values and compile-time size/offset assertions. Unaligned
user result addresses and results crossing two writable pages are supported.

| Offset | Width | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 4 | version | 1 |
| 4 | 4 | struct_size | 96 |
| 8 | 4 | type | centralized UNKNOWN=0, REGULAR=1, DIRECTORY=2 |
| 12 | 4 | name_length | bytes excluding NUL, 1..55 |
| 16 | 8 | inode | filesystem-scoped identifier, zero-extended |
| 24 | 8 | reserved | zero |
| 32 | 64 | name | exact VFS name, NUL-terminated, remaining bytes zero |

The current operations permanently copy exactly 96 bytes. A larger future layout
requires a new operation or explicitly negotiated caller capacity/version; adding
fields must never silently enlarge an existing application's output write.
Reserved fields remain zero in v1. No internal pointers, raw PollikFS records,
record lengths, physical locations or directory byte offsets are exposed.

The shared PollikFS backend already rejects malformed live records: invalid inode,
record length, type, zero/overlong name length, missing terminator, embedded NUL or
slash. The syscall additionally bounds and checks the VFS name before translation.
Names longer than 55 bytes or impossible names fail with EIO, never truncate.
Names are opaque bytes with the backend's existing restrictions; no new encoding
or hidden-file policy is imposed. `.layout` and `.trashinfo` are visible when
stored in VFS. GUI filtering remains separate. PollikFS does not normally create
`.` or `..`; this API fabricates neither. Any actual valid stored entry is returned
as VFS provides it. Unsupported internal types translate explicitly to UNKNOWN;
current PollikFS rejects unsupported on-disk type tags as corrupt.

## Position, copies and errors

Each directory fd owns its VFS object and independent visible-entry position.
Separate opens, including the same numeric fd in different processes, never share
iterator state. The backend scans bounded direct directory blocks and skips empty
slots. Its position is private: arbitrary seeks, CUR and END are rejected with
EINVAL. Only seek(fd, 0, SET) rewinds. It does not return an internal byte offset.
Fstat accepts directory descriptors and reports their metadata. Ordinary read
rejects them with EACCES, preventing access to raw directory blocks.

Readdir checks the full-width descriptor, directory type, and complete writable
96-byte user range before VFS I/O, even at EOF. It builds a zero-initialized kernel
result and uses copy_to_user64. Valid results return 1. EOF returns 0 without
changing the output. Errors leave output and iterator position unchanged. There
is no ambiguity between EOF and I/O failure. No persistent allocation or extra
handle is created by readdir.

| Error | Positive code | Meaning |
| --- | --- | --- |
| ENOTDIR | `0x100d` | regular file passed to opendir/readdir |
| EBADF | `0x1004` | negative, reserved, unused, closed, high-bit or out-of-range fd |
| EFAULT | `0x1001` | invalid path/output user range |
| ENOENT | `0x1005` | missing directory |
| EINVAL | `0x1006` | invalid path syntax or unsupported directory seek |
| EACCES | `0x1007` | invalid open flags or raw directory read |
| EIO | `0x1008` | storage read failure or malformed metadata/name |
| EMFILE | `0x1009` | all 13 ordinary descriptor slots occupied |
| ENOMEM | `0x100a` | VFS object pool allocation failure |
| ENAMETOOLONG | `0x100b` | no path terminator within 128 bytes |

Errors are returned negated in RAX. Readdir checks fd before type before output.
Opens use the existing file-open validation order. Current single-BSP, IF=0 kernel
serialization keeps validation, VFS iteration and copy atomic against scheduling
and mapping changes. Userspace execution remains preemptible. Concurrent mutation
semantics and SMP locking are outside this read-only milestone.

Close clears the slot and releases the VFS object; lowest-slot reuse applies to
both regular files and directories. Existing process destruction closes all owned
directories on exit, fault, kill and partial-construction cleanup, on the safe
scheduler/reaper stack.

## Guest verification

`apps/x86_64/dirtest.asm` is a separately built ELF installed at `/bin/dirtest` and
launched with process64_launch_path, never embedded in the kernel. The generated
`/testdir` holds alpha.txt, beta.txt, subdir, empty.txt, .layout, .trashinfo, a 55-byte
name and omega.txt. Subdir is empty. Omega occupies a second directory block after
unused slots. The image format and shared VFS/PollikFS implementation are unchanged.

Normal boot enumerates and verifies all eight names, exact padding, inode presence,
regular/directory types, EOF twice without buffer writes, rewind, close/slot reuse
and empty-directory EOF, then exits 42. Output crosses writable page boundaries.
Fstat works on the directory; raw read and arbitrary seeks are rejected.

Self-tests add:

- Concurrent same-directory fd 3 opens with distinct VFS objects and offsets 1/0
  across at least three timer ticks each; each receives its expected first/next
  entry. Closing one fd cannot select or affect the peer's fd 3.
- Ten rounds of bad paths/fds/output pointers and full-table churn; regular-file
  readdir, negative/reserved/unused/closed/high-bit/oversized descriptors; kernel,
  noncanonical, unmapped, RX, overflowing and partially mapped output ranges.
  Partial-range sentinel bytes remain unchanged and iteration still starts at 0.
- Full 13-directory tables, EMFILE, close and lowest-slot reuse.
- 200 injected readdir I/O failures and 200 opendir I/O failures, followed by
  restored I/O and complete enumeration from the unchanged starting position.
- 100 VFS object-allocation failures, with no leaked handles or memory.
- Five repeats each of exit, protection fault and timer kill with all 13 directory
  slots open; every handle reaches the existing safe reaper and is reclaimed.
- 100 complete open/enumerate/close/exit/destroy lifecycles in four-process batches,
  checking PMM, VFS handles and process removal after every batch.

Host verification uses both x86_64 builds, the full 16/64/256/5120-MiB boot matrix,
normal reboot, unsupported CPUs and existing damaged-storage regressions. Each
attached generated data image is hashed before/after guest execution. Earlier
memory, ELF/startup, scheduler, launch, file and stat/fstat suites remain required.
i386 uses its existing build and disposable-disk boot/stress/focused desktop tests.

## Verified checkpoint

Both x86_64 builds and the full 16/64/256/5120-MiB matrix passed, including all
previous suites, normal boot/reboot, unsupported CPUs and damaged-storage tests.
The 5-GiB directory suite reports `0x13fd79` free frames before and after 100
lifecycles, zero remaining VFS handles and zero surviving descriptors. Every
attached generated guest data image retained its SHA-256. Neither kernel embeds
the userspace ELF.

i386 build, 64/256-MiB desktop boots, 100-process stress and focused cursor,
Terminal and Notes editing/wheel smoke passed. Stress returned to 56,448 free
pages. The kernel SHA-256 remains
`cc167c6de16bd44e389c65410ecb1984ba8ea516a7a5be9cba7fec3e09970af9`.
The existing desktop PollikData image was locked by another process and was left
alone; its hash could not be independently checked in this run. For the i386
build, a temporary copy of the existing build script redirected only its data
image inspection to a generated disposable fixture; compilation and image
assembly were unchanged, and the temporary script was removed. Guest regression
tests also used disposable data fixtures.

## Changed files

- `user_abi.h`, new `dir_abi.h`: centralized syscall/errors/constants and entry layout.
- `file.c`, `file.h`: directory open, safe readdir, rewind policy and declarations.
- New `dir_demo.c`, `dir_test.c`, `apps/x86_64/dirtest.asm`: real userspace verification.
- `kernel.c`, `path_test.c`, `build-x86_64.ps1`, `tools/build_x64_data.py`:
  demo/test/build wiring, deterministic disk fixture and directory entry count.
- `tests/x86_64_boot.py`: required directory markers and ownership balance.
- `DIRECTORY_ABI.md`, `FILE_ABI.md`, `STAT_ABI.md`, `SELF_HOSTING.md`,
  `SCHEDULER.md`, `VFS_LAUNCH.md`: current contracts and milestone boundary.

Existing workspace changes are preserved. No shared filesystem format or i386
source changes are needed for this milestone.

## Limits and next milestone

Four processes, 13 total ordinary descriptors per process, absolute paths,
55-byte names, serialized PIO and read-only directory contents remain the limits.
No arbitrary directory seeks, mutation, libc or language runtime support is added.

Working directories and relative paths are implemented in C1. Next: C2 crt0/C
wrapper layer, subject to the user specification; not started.
