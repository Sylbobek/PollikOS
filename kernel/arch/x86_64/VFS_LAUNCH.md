# x86_64 VFS executable launch checkpoint

> Superseded checkpoint: its 128-KiB executable cap, process limits and “C2 not started” status below are historical; current limits are in [ELF64_ABI.md](ELF64_ABI.md) and [PROCESS_MODEL.md](PROCESS_MODEL.md).

Current C1 transport, stdio and path rules: [RUNTIME_C1.md](RUNTIME_C1.md).
SYSCALL is primary; INT 0x81 is transitional. Cwd/relative paths and explicit
standard streams supersede earlier absolute-only/reserved-fd statements below.

`process64_launch_path("/bin/hello", argc, argv, envc, envp, &process)` reads a
real PollikFS v2 file through the existing VFS and creates a scheduler-owned
ELF64 process. The ELF parser is unchanged and takes only bounded bytes.
Neither normal nor self-test kernels link userspace ELF file bytes.

## Reused storage stack and width audit

The x86_64 build compiles the existing `kernel/vfs.c`, `kernel/pollikfs.c` and
ATA PIO functions in `kernel/storage.c`. `POLLIK_FS_READONLY` selects native
64-bit memory/string/port adapters, a separate kernel FD context and read-only build
sections. It excludes legacy snapshot storage, mutation, formatting and embedded
i386 executable installation. There is no second filesystem or ELF parser.
The i386 build retains its existing writable filesystem and desktop interfaces.

The primary IDE slave remains the data device; the primary master is the boot
image. The adapter uses ATA IDENTIFY to check LBA support/capacity, then the
shared bounded-polling PIO read routine. No DMA buffer/address conversion is
needed. LBA28 limits are explicit. Reads have no fallback to the boot device.
Device IRQs are disabled; kernel disk access is serialized on one BSP with IF=0.

Buffer pointers and `fs_private` remain native pointers. Memory primitive lengths
use native `size_t`. Disk blocks, file lengths and offsets remain `u32`: the
existing format has 32,768 1-KiB blocks, 512 inodes and only eight direct plus
256 single-indirect file blocks. Widening those fields would not provide large
file support and would break the disk ABI. Seek uses a wide intermediate and
rejects signed-result overflow. No on-disk format version changed.

Wire sizes are asserted: superblock 512, inode 60, directory record 64 bytes.
Superblock/inode words and indirect entries are explicitly decoded little-endian;
directory lengths and inode references are bounded before use. Current geometry
is inode table blocks 5..35 (17 inodes/block), data beginning at block 36, filesystem
starting at LBA 64. The known overlapping 30-block legacy geometry is rejected,
not migrated or reformatted by the guest.

Shared read-path fixes bound inode sizes, directory names, component lengths,
record/inode references, data blocks and indirect indexes. Lookup/read I/O errors
propagate instead of becoming missing files or zero-filled indirect data. VFS
owns the file reference count; backend close no longer decrements it a second time.
These safety fixes also apply to i386. Valid current PollikFS v2 images retain
their layout and readable contents. Sparse regular-file blocks remain supported.

## Launch ownership and errors

The kernel-only API requires trusted, bounded request pointers and IF=0. It:

1. Validates path/vector bounds and available process slots; stats a regular file.
2. Rejects images below 64 bytes or above **128 KiB** before buffer allocation.
3. Opens through VFS and allocates owned, zeroed kernel pages in a bounded scratch
   window. Reads in a loop until the exact stat size arrives; EOF/error aborts.
4. Calls the existing ELF64 process constructor, which validates untrusted bytes,
   maps PT_LOAD segments and copies startup ABI v1 strings/vectors.
5. Copies a bounded diagnostic basename into the process, then submits it once.
6. Releases all temporary file-buffer pages and closes the descriptor before
   returning. On failure it also destroys any unsubmitted process.

The scratch window is `MM_KERNEL_START+0x400000`, at most 32 pages, serialized by
the IF=0/non-reentrant launcher. It is empty outside calls. Scheduler dispatch
cannot happen until the launch call returns. A successful process owns only its
normal image/stacks/control resources, not its file handle or duplicate ELF bytes.
PID remains identity; the copied name is diagnostic. No caller string pointer is
retained. A failed submission leaves no queue entry and restores the caller's PMM
and FD baseline. The four-process limit is unchanged.

Errors distinguish bad request/path, missing file, directory/non-regular file,
denied access, I/O, corrupt or unmounted filesystem, short/oversize image,
invalid/unsupported ELF, memory exhaustion, full process slots and rejected queue
insertion. Current PollikFS has no executable permission bits; no additional
permission model is invented. The later [file syscall ABI](FILE_ABI.md) also exposes bounded read-only access
through separate per-process tables. Executable handles remain kernel-private.

## Artifacts and safe test installation

Both variants keep `kernel.bin`, the boot image, `userspace/*.elf`,
`PollikData-test.img` and `data-manifest.json` separate. The explicit build step
`tools/build_x64_data.py` reuses the existing PollikFS formatter/packing helpers
to construct a new 40-MiB test image and atomically replace only the designated
generated fixture under `build/x86_64/{kernel,selftest}`. It cannot target the
user's `build/PollikData.img`. At runtime x86_64 mounts PollikFS read/write:
first-run setup stores the shared `/etc/account.db` record and creates
`/home/<username>`. Login never formats, migrates or resets the data disk. Use a
copy of the data image for disposable tests.

The fixture has `/bin/hello`, `/bin/argvtest`, `/bin/spin`, `/bin/faulttest` plus
negative fixtures. Their bytes are assembled/linked separately on the host and
installed as filesystem files, including single-indirect data. Guest runtime
uses ATA and VFS, never host filesystem access. Old parser/scheduler regression
fixtures are read through VFS into self-test BSS buffers before their tests;
those buffers contain no executable bytes in the kernel image.

```powershell
.\build-x86_64.ps1
.\build-x86_64.ps1 -SelfTest
python tests/x86_64_boot.py
python tests/x86_64_storage.py
```

Manual normal boot needs both generated disks:

```powershell
qemu-system-x86_64 -accel tcg -m 64 -display none -serial stdio -monitor none -nic none -no-reboot -drive file=build/x86_64/kernel/PollikOS-x86_64.img,format=raw,snapshot=on -drive file=build/x86_64/kernel/PollikData-test.img,format=raw,if=ide,index=1
```

Normal boot authenticates (or creates the first local account), then starts
`/bin/desktop.pol` automatically. The Desktop opens `/bin/terminal.pol`; the
COM1 shell remains available for diagnostics and recovery. Login does not
format, migrate or reset the data disk.

## Verified checkpoint

The full suite passes at 16/64/256/5120/32768 MiB, retaining all memory, ELF and 1200+
scheduler-switch regressions. Disk tests verify mount, `/bin` enumeration,
manifest-matched file sizes, seek/read/close, independent caller-string lifetime,
113-byte partial reads, early EOF, every ATA-read failure prefix and allocation
prefix through launch, rejected insertion and full process capacity.

Each of 25 batches launches spin first, then hello, argvtest and faulttest. The
non-yielding spin requires timer preemption before its peers can run. Totals are
50 clean exits (42), 25 contained text faults and 25 timer-limit kills: **100 path
launches**. At 5 GiB, free frames are **0x13fd79 before and after**, and open file
structures return to **zero** after every batch and failure. Both ELF startup
arguments and `TEST=pollikos` remain verified in CPL3.

The host boots the same normal data image twice and observes `/bin/hello` exit 42
both times. SHA-256 of the attached data image must be unchanged after every
boot, including corrupted-image cases. Tests reject missing/small disks, invalid
signatures/legacy geometry, malformed directory names/references, out-of-range
direct blocks, metadata-targeting indirect entries and overflowing inode sizes
without formatting or panic. Neither kernel contains a complete userspace ELF.

i386 build, 64/256-MiB desktop boots, 100-process stress (56,448 free pages before
and after) and focused cursor/Terminal/Notes editing/scroll tests pass. Its binary
changes because the shared filesystem safety fixes are compiled for i386 too;
no i386 executable ABI or disk layout changes were made.

## Limits and next milestone

Read-only x86_64 VFS, fixed current PollikFS v2 geometry, LBA28 PIO, 128-KiB
executable cap, up to 1024 RAM-scaled processes, one BSP and non-preemptible kernel I/O. Bounded
read-only file syscalls now exist; filesystem mutation, demand paging and a
dynamic linker remain absent. FPU/SIMD
context remains unsupported and use traps. No libc or language ports began.

Per-process open/read/seek/close is now implemented in the [file milestone](FILE_ABI.md).
Stat/fstat metadata is implemented; see [STAT_ABI.md](STAT_ABI.md).
Next exact milestone: C2 crt0/C wrappers (not started). C1 working directories and directory enumeration are
implemented; see [DIRECTORY_ABI.md](DIRECTORY_ABI.md).
That work is not started here.
