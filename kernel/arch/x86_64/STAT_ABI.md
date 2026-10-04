# PollikOS x86_64 metadata ABI, version 1

Current C1 transport, stdio and path rules: [RUNTIME_C1.md](RUNTIME_C1.md).
SYSCALL is primary; INT 0x81 is transitional. Cwd/relative paths and explicit
standard streams supersede earlier absolute-only/reserved-fd statements below.

`stat(path, result)` and `fstat(fd, result)` query shared VFS/PollikFS metadata
from real CPL3 ELF64 programs. Both use INT 0x81, the existing saved-register
contract and signed PollikOS errors. No libc or Linux ABI is assumed.

| Operation | RAX | RDI | RSI | Success |
| --- | --- | --- | --- | --- |
| stat | `0x504f0014` | user NUL-terminated absolute path | writable UserStat64 pointer | 0 |
| fstat | `0x504f0015` | current process fd | writable UserStat64 pointer | 0 |

RDX is unused. Numbers are centralized in `user_abi.h`, which also generates
NASM constants. The dedicated C layout is in `stat_abi.h`, with compile-time
size and offset assertions. Existing startup v1, open/read/seek/close,
debug_write, exit, loader and scheduler contracts remain unchanged.

## Fixed layout and evolution

The little-endian result is exactly **64 bytes**, naturally aligned to 8 in C;
the kernel also accepts unaligned writable user addresses. It contains only
explicit uint32_t/uint64_t values, never native pointers or internal structures.

| Offset | Width | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 4 | version | 1 |
| 4 | 4 | struct_size | 64 |
| 8 | 4 | type | UNKNOWN=0, REGULAR=1, DIRECTORY=2 |
| 12 | 4 | timestamp_kind | POLLIK_TICKS=1; see below |
| 16 | 8 | size_bytes | unsigned byte size |
| 24 | 8 | inode | unsigned PollikFS inode number |
| 32 | 8 | created_ticks | stored creation counter, zero-extended |
| 40 | 8 | modified_ticks | stored modification counter, zero-extended |
| 48 | 16 | reserved | all zero |

Version 1 and these syscall numbers permanently write exactly 64 bytes. Future
larger results require a new syscall or explicit negotiated version/capacity;
the kernel must never enlarge these implicit copies. Reserved fields remain
zero in v1. Applications check version and size before interpreting results.

Type values are translated explicitly, even where values currently coincide
with internal constants. Unknown internal types map to UNKNOWN. Directory stat
succeeds for `/` and `/etc`; its size is the stored directory byte extent,
not an entry count, and no directory contents are returned. Ordinary open rejects
directories; opendir now supplies directory descriptors accepted by fstat. See
[DIRECTORY_ABI.md](DIRECTORY_ABI.md).

PollikFS exposes no permission bits: its `mode` is a type, and open flags are
handle access mode, not filesystem permissions. Neither is presented as permissions.
The inode identifier is scoped to this filesystem and mount; it is not a global
identifier or a physical/kernel address and must not imply identity across reuse.
All current source sizes and counters are uint32_t and widen exactly to uint64_t;
no signed intermediate or truncation occurs. The inode's pointer tree can
represent up to 67,379,200 bytes (~64.24 MiB), but the PollikFS volume contains
only 32,768 1-KiB blocks (32 MiB gross, including metadata), so actual file data
is limited by the smaller remaining free-block count. This ABI does not impose
an additional 32-bit application size limit.

## Timestamp convention

PollikFS stores the legacy writer's **unsigned 32-bit boot-relative `ticks`
counter**, not Unix time. `timestamp_kind=1` explicitly means those raw persisted
PollikOS tick units, zero-extended into the 64-bit fields. The filesystem does not
store the originating boot identity or tick frequency, so these values cannot
reliably be converted to seconds or compared across boots. They wrap at 2^32;
the ABI does not reconstruct lost high bits. Zero is passed through and may mean
an unset formatter field or a counter at boot. The x86_64 realtime clock does not
change this on-disk timestamp format or substitute Unix seconds in these fields.
The read-only x86_64 target never updates them. Future real-time timestamps must
use an explicitly different convention/version.

The generated `/etc` fixtures deliberately store `0x80000001` and `0xfedcba98`
as synthetic tick values to verify unsigned widening. They are test metadata,
not claimed dates. Production queries report the actual stored values.

## Validation and ownership

Stat copies at most 127 path bytes plus NUL into a fixed kernel buffer through
`copy_string_from_user64`. The existing absolute-path parser and component bounds
apply. VFS never receives a user pointer. Both calls prevalidate the full 64-byte
writable output range before filesystem I/O, build a completely zero-initialized
local result, then use `copy_to_user64`. There is no partial output on any failure.
Mapped page crossings work; read-only, kernel, noncanonical, unmapped, crossing
unmapped and overflowing ranges return EFAULT.

Fstat validates the full 64-bit descriptor in the current process's 3..127 table
before narrowing. Its VFS query reads the inode behind that owned object, without
reopening by path, seeking, reading file data, taking another reference, or
changing the offset. The existing temporary process FD context is restored on
all paths. Kernel executable loading retains its separate context. Serialization
is the existing single-BSP, IF=0 kernel contract; no SMP claim is made.

No metadata syscall allocates a handle or PMM memory. Local path, backend metadata
and result buffers live on the guarded kernel stack. Existing safe reaping closes
all owned descriptors on exit/fault/kill.

Errors use the existing negative codes in FILE_ABI.md: EBADF for unsupported,
unused, closed or out-of-range fds; EFAULT for bad user ranges; ENAMETOOLONG for
no NUL within 128 bytes; ENOENT for missing paths; EINVAL for invalid path syntax;
EIO for unavailable/corrupt storage or read failure. Fstat checks fd before output;
stat checks output before path. Failed operations leave output and file offsets
unchanged. Permissions/access errors from VFS retain EACCES.

## Verification

`apps/x86_64/stattest.asm` builds a separate ELF installed at `/bin/stattest`.
Normal boot uses `process64_launch_path`, verifies file metadata, opens/fstats the
same file, compares all 64 bytes, checks the preserved nonzero offset, stats
directories, checks a successful page-crossing output, closes and exits 42.
No executable bytes are embedded in the kernel.

`stat_test.c` additionally runs:

- Two simultaneous disk-loaded processes using fd 3 for different files, querying
  repeatedly across at least three timer ticks each; one closes and successfully
  stats by path while the other retains its own descriptor and offset.
- Ten full bad-argument runs: missing/relative/dot-dot/empty/overlong paths,
  kernel/noncanonical/unmapped/unterminated path pointers; all invalid output
  classes; negative/reserved/unused/closed/high-bit/above-limit descriptors.
  Poisoned buffers and reserved-field checks catch accidental output or leakage.
- Two hundred injected stat I/O failures and two hundred fstat I/O failures,
  preserving poisoned outputs and file offsets with balanced ownership.
- Five repeats each of exit, fault and timer kill after successful metadata calls
  with a file still open, verifying cleanup through the existing safe reaper.
- **100 stat/open/fstat/close/exit/destroy lifecycles**, in four-process batches,
  checking PMM, VFS handles and complete process removal after every batch.

Run both x86_64 builds, `tests/x86_64_boot.py` (16/64/256/5120 MiB, normal reboot
and unsupported CPUs), and `tests/x86_64_storage.py`. All earlier suites remain.
The host verifies whole data-image SHA-256 before and after every guest run.
The i386 build and disposable-disk boot/stress/focused desktop tests remain required.

## Verified checkpoint

Both x86_64 builds and the full 16/64/256/5120-MiB boot matrix passed, including
all previous suites, normal boot/reboot, unsupported CPU gates and damaged-disk
regressions. The 5-GiB metadata suite reports `0x13fd79` free frames before and
after, zero VFS handles and zero surviving descriptors. Every attached guest
data-image SHA-256 stayed unchanged. Neither kernel contains the userspace ELF.

i386 build, 64/256-MiB boots, 100-process stress and focused cursor/Terminal/Notes
smoke passed. The stress test returned to 56,448 free pages. The i386 kernel
SHA-256 remains `cc167c6de16bd44e389c65410ecb1984ba8ea516a7a5be9cba7fec3e09970af9`,
identical to the prior checkpoint. The existing `build/PollikData.img` hash also
remained unchanged during verification; tests use disposable data fixtures.

## Files changed for this milestone

- `user_abi.h`, new `stat_abi.h`: centralized operations and stable result layout.
- `file.c`, `file.h`: safe metadata dispatch and declarations.
- `kernel/vfs.c`, `kernel/vfs.h`, `kernel/pollikfs.c`, `kernel/pollikfs.h`:
  read-only-target fstat by owned inode and explicit type preservation.
- New `stat_demo.c`, `stat_test.c`, `apps/x86_64/stattest.asm`: actual guest checks.
- `kernel.c`, `path_test.c`, `build-x86_64.ps1`, `tools/build_x64_data.py`:
  boot/build/fixture integration and updated directory entry expectation.
- `tests/x86_64_boot.py`: required diagnostics and balance verification.
- `STAT_ABI.md`, `FILE_ABI.md`, `SCHEDULER.md`, `VFS_LAUNCH.md`,
  `SELF_HOSTING.md`: contract, checkpoint and scope documentation.

Existing workspace changes outside this list were preserved.

## Scope boundary

> Superseded checkpoint: the C2-not-started and 13-descriptor scope below was replaced by the current runtime; see [PROCESS_MODEL.md](PROCESS_MODEL.md) and [FILE_MUTATION.md](FILE_MUTATION.md).

Four processes, 13 ordinary descriptors each, absolute paths, read-only storage,
serialized kernel PIO and legacy tick counters remain the current limits.
No writes, libc, dynamic linking, SYSCALL/SYSRET, FPU/SIMD or language runtime work.

Directory enumeration is now implemented; see [DIRECTORY_ABI.md](DIRECTORY_ABI.md).
Working directories and relative paths are implemented in C1. Next: C2 crt0/C
wrapper layer, subject to the user specification; not started.
