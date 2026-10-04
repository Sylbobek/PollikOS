# PollikOS x86_64 read-only file syscall ABI

> Superseded C1 checkpoint: its descriptor limits and “C2 not started” scope below are historical; current process and mutation contracts are in [PROCESS_MODEL.md](PROCESS_MODEL.md) and [FILE_MUTATION.md](FILE_MUTATION.md).

Current C1 transport, stdio and path rules: [RUNTIME_C1.md](RUNTIME_C1.md).
SYSCALL is primary; INT 0x81 is transitional. Cwd/relative paths and explicit
standard streams supersede earlier absolute-only/reserved-fd statements below.

Real ELF64 applications loaded from PollikFS can now open, read, seek and close
regular files through INT 0x81. Startup ABI v1 and the existing debug_write/exit
operations are unchanged. No libc, filesystem writes or descriptor inheritance
are implemented.

## Descriptor ownership

Every `Process64` contains 16 native VFS-object pointers, initially zero.
Slots 0/1/2 are reserved for future stdin/stdout/stderr and currently reject file
operations. Ordinary opens allocate the lowest free slot in **3..15**: at most
13 files per process. A VFS object owns the access flags, offset, inode, type and
reference count; no userspace pointer is retained. There is no dynamic table growth.

Descriptor numbers are process-local. Two processes' fd 3 can refer to different
files or independent opens of the same file. The shared VFS object pool is an
internal allocator, not a userspace descriptor namespace. User arguments are
checked at full 64-bit width before conversion, preventing high-bit truncation
from selecting another descriptor.

`file64_dispatch` temporarily binds VFS lookup to the current process table and
restores the prior context through one return path. This happens with IF=0 on one
BSP; preemption cannot expose another table halfway through a syscall. Outside
the file dispatcher, the default VFS context is the separate kernel table used
by `process64_launch_path`. Executable handles are closed before launch returns
and never enter the new process's table.

Close releases one object and clears its slot. Double close is EBADF. The safe
process destructor closes every remaining table entry on exit, fault, kill and
partial-construction teardown, before freeing the process control page. Reaping
still occurs on the dispatcher stack. No descriptor survives destruction.

## Transport and return values

RAX is the operation number on entry and signed result on return. RDI, RSI and
RDX carry arguments below. The gate is **INT 0x81**, not Linux syscall numbering.
Other saved registers and the documented user flag policy remain unchanged.
Success is non-negative. Failure is the negative of a stable PollikOS error code;
wrappers must interpret RAX as `int64_t`. There is no errno variable or TLS yet.

| Operation | RAX | RDI | RSI | RDX | Success |
| --- | --- | --- | --- | --- | --- |
| debug_write | `0x504f0001` | user bytes | count, at most 256 | unused | bytes printed |
| exit | `0x504f0002` | exit status | unused | unused | does not return |
| open | `0x504f0010` | user NUL-terminated path | flags, exactly **1** (`USER_O_RDONLY`) | unused | fd 3..15 |
| read | `0x504f0011` | fd | user destination | count | bytes read, zero at EOF |
| seek | `0x504f0012` | fd | signed offset | whence 0/1/2 | resulting byte offset |
| close | `0x504f0013` | fd | unused | unused | zero |
| stat | `0x504f0014` | user path | user metadata result | unused | zero |
| fstat | `0x504f0015` | fd | user metadata result | unused | zero |
| opendir | `0x504f0016` | user directory path | flags exactly 1 | unused | fd 3..15 |
| readdir | `0x504f0017` | directory fd | user directory entry | unused | 1 entry / 0 EOF |

Metadata layout and semantics are specified in [STAT_ABI.md](STAT_ABI.md).
Directory enumeration, shared descriptor limits and rewind-only seek are defined
in [DIRECTORY_ABI.md](DIRECTORY_ABI.md).

`USER_O_RDONLY=1` deliberately follows existing PollikOS VFS flags; do not assume
Linux's O_RDONLY value. Write-only, read-write, create, truncate, append, zero and
unknown flag combinations are rejected with EACCES, never silently ignored.

| Positive code | Symbol | Meaning |
| --- | --- | --- |
| `0x1001` | EFAULT | invalid user path or destination |
| `0x1002` | ENOSYS | unknown syscall |
| `0x1003` | E2BIG | read count exceeds the bounded syscall limit |
| `0x1004` | EBADF | reserved, unused, closed or out-of-range descriptor |
| `0x1005` | ENOENT | path not found |
| `0x1006` | EINVAL | invalid path/seek arguments or offset range |
| `0x1007` | EACCES | unsupported open/access mode |
| `0x1008` | EIO | filesystem unavailable, corrupt metadata or read I/O failure |
| `0x1009` | EMFILE | all 13 ordinary descriptor slots are occupied |
| `0x100a` | ENOMEM | underlying VFS file-object allocation failed |
| `0x100b` | ENAMETOOLONG | no path terminator within 128 bytes |
| `0x100c` | EISDIR | path is not a regular file |

## Path, read and seek bounds

Paths are copied into a 128-byte kernel buffer with `copy_string_from_user64`
before VFS sees them: at most 127 content bytes plus NUL. No raw userspace pointer
reaches path parsing. Existing VFS absolute-path rules are reused, including the
55-byte component limit and rejection of dot/dot-dot components. No separate
syscall path parser, relative path support or cwd is added. Directories are rejected;
raw directory blocks cannot be read through these calls.

A read accepts at most **4096 bytes**; larger unsigned counts return E2BIG.
There is no count-sized heap allocation. The kernel prevalidates the complete
writable user range, reads into a fixed 4096-byte kernel-stack buffer and copies
only the successful byte count. Cross-page writable ranges work; kernel,
noncanonical, unmapped, overflowing and read-only ranges fail before file I/O.
Bad pointers do not advance the offset or partially overwrite the destination.
An I/O error also leaves the offset unchanged and copies no bounce-buffer bytes.
Short successful reads are normal; EOF returns zero without touching the buffer.

A zero-length read still requires a valid readable descriptor, but ignores the
unused destination and returns zero. Nonzero reads validate their requested
destination even at EOF. Callers must loop to read more than 4096 bytes.

Seek supports SET=0, CUR=1 and END=2. RSI is interpreted as a signed 64-bit value
and must fit a signed 32-bit delta before calling the current VFS. The resulting
offset must be between 0 and INT32_MAX. Invalid whence, negative results and
overflow return EINVAL without changing the old offset. Seeking past EOF is
allowed; subsequent reads return zero. The wire format and VFS offset width have
not changed.

## Verification

`apps/x86_64/readtest.asm` is a separately assembled ELF installed as
`/bin/readtest`; its bytes are not embedded in the kernel. The deterministic
PollikFS fixture also contains `/etc/read_test.txt` (`Alpha file data\n`) and
`/etc/other.txt` (`Bravo file data\n`). Normal boot launches the program by path;
its CPL3 code opens, verifies bytes, checks short read/EOF, seeks SET/CUR/END,
reads across writable pages, closes/reuses fd 3 and exits 42.

The self-test covers:

- Concurrent fd 3 opens of different files, private offsets across repeated timer
  preemption, and one process closing while its peer still owns fd 3. A third
  executable is loaded while a user handle remains open, testing kernel-context
  separation.
- Negative/high-bit/reserved/unused/closed descriptors, all unsupported open modes,
  missing/directory/relative paths, bad path pointers and missing/overlong NUL
  terminators. Read tests include unmapped page crossings, RX destinations,
  overflow, zero/oversize counts and no partial destination change on failure.
- Seek underflow/overflow and invalid origins; full tables, EMFILE, close and
  lowest-slot reuse. Twenty error-suite launches must return to baseline.
- One hundred injected VFS-object allocation failures and one hundred injected
  read failures; failed reads preserve both destination and offset. File syscalls
  allocate no PMM buffers; existing launch-allocation failure regressions remain.
- Five repeats of exit, fault and timer kill, each with **all 13 files open**.
  Kill timing starts only after userspace reports its table is full, avoiding a
  disk-timing assumption. The deferred reaper closes every object.
- **100 complete open/read/seek/close/exit lifecycles**, in four-process batches,
  checking PMM, VFS handles and process teardown after every batch.

Run both x86_64 builds, `tests/x86_64_boot.py` and `tests/x86_64_storage.py`.
The full matrix retains memory/ELF/path/scheduler tests at 16/64/256/5120 MiB.
The host hashes each attached data image before and after boot, proving these
read-only operations do not mutate PollikFS. No user data image is formatted.

Verified at all four RAM sizes: the 5-GiB file suite reports `0x13fd79` free
frames before and after, zero VFS handles and zero surviving descriptors.
i386 build, 64/256-MiB boots, 100-process stress and focused desktop smoke pass.
Its kernel binary is unchanged from the preceding VFS checkpoint (SHA-256
`cc167c6de16bd44e389c65410ecb1984ba8ea516a7a5be9cba7fec3e09970af9`).

## Limits and next milestone

Four processes, 13 ordinary FDs per process, one BSP, non-preemptible kernel PIO,
absolute paths, 4096-byte reads, signed-32-bit VFS offsets and read-only files.
No writes, dup, inheritance, pipes, fork, exec FD
semantics or terminal-backed stdio. FPU/SIMD state remains unsupported and traps.

Stat/fstat metadata access is now implemented; see [STAT_ABI.md](STAT_ABI.md).
Directory enumeration is implemented; see [DIRECTORY_ABI.md](DIRECTORY_ABI.md).
Working directories and relative paths are implemented in C1. Next: C2 crt0/C
wrapper layer, subject to the user specification; not started.
