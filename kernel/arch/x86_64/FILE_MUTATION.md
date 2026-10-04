# PollikOS C5 userspace filesystem mutation

C5 exposes safe regular-file creation, writing, truncation, append, directory
creation/removal, unlink and rename to x86_64 userspace through the existing
per-process descriptor and VFS/PollikFS architecture. The PollikFS on-disk
format is unchanged; no migration is required and i386 keeps its own behavior.

## Syscalls and open flags

| Operation | RAX | RDI | RSI | RDX | Success |
| --- | --- | --- | --- | --- | --- |
| open (extended) | 0x504f0010 | path | flags | unused | fd >= 3 |
| write (extended) | 0x504f0018 | fd | readable bytes | count | bytes written |
| mkdir | 0x504f0021 | path | unused | unused | 0 |
| unlink | 0x504f0022 | path | unused | unused | 0 |
| rmdir | 0x504f0023 | path | unused | unused | 0 |
| rename | 0x504f0024 | old path | new path | unused | 0 |
| rename_replace | 0x504f003e | old path | new path | unused | 0 |

VFS flags are PollikOS-defined (`file.c` static-asserts their values):
`O_RDONLY=0x0001`, `O_WRONLY=0x0002`,
`O_RDWR=0x0003`, `O_CREAT=0x0100`, `O_TRUNC=0x0200`, `O_APPEND=0x0400`. Any
other bit is `EINVAL`; a missing access mode is `EINVAL`; `O_TRUNC`/`O_APPEND`
without write access is `EACCES`. The SDK open ABI additionally accepts
`O_CLOEXEC=0x1000`, stores it on the returned descriptor, and strips it before
the VFS call. Unknown bits remain `EINVAL`. Paths use the single existing
cwd-aware resolver, so absolute, relative, `.` and `..` forms all behave as
before.

## Semantics

- **Write**: stdout/stderr keep their existing behavior. VFS writes require a
  writable descriptor; read-only, stdin and directories return `EACCES`.
  Counts above `USER_WRITE_MAX` (65536) return `E2BIG`; zero-length writes
  return 0 without touching the buffer; the whole user range is validated
  (cross-page buffers are copied through a fixed 4-KiB kernel buffer) before
  any filesystem call. File offsets advance by the number of bytes written;
  short writes are returned to the caller.
- **Create**: `O_CREAT` on a missing regular path allocates an inode and a
  directory entry; an existing file is opened without being overwritten.
  Failed directory insertion rolls the inode back, so a failed create leaks
  neither blocks nor inodes. Opening a directory requires exactly `O_RDONLY`.
- **Truncate**: `O_TRUNC` releases every direct and indirect data block,
  zeroes `size` and stamps the inode. Arbitrary-size `truncate`/`ftruncate`
  is not implemented (documented limitation).
- **Append**: `O_APPEND` re-reads the inode and positions each write at the
  current EOF, not only at open time, so appends remain correct while other
  processes extend the same file.
- **Growth/limits**: files grow through 8 direct, 256 single-indirect and
  65,536 double-indirect data-block pointers. The inode can represent up to
  67,379,200 bytes (~64.24 MiB), but this PollikFS volume has only 32,768
  1-KiB blocks (32 MiB gross, including metadata); actual file data is bounded
  by the smaller remaining free-block count and therefore cannot reach the
  inode's representational limit. Writes beyond available blocks return
  `ENOSPC`. Every block index is bounds checked by inode validation in
  `read_inode`; failed allocations never alter metadata that was not committed.
- **mkdir**: rejects existing targets with `EEXIST`, requires an existing
  directory parent, allocates one directory block and rolls back the inode and
  block on failure. Directory growth past one block is supported by
  `dir_add_entry` (up to eight direct blocks = 128 entries).
- **unlink**: only regular files; directories return `EISDIR`. The entry is
  cleared, all direct/indirect blocks are freed, the inode is zeroed and the
  free-inode counter is restored. Removing a file that another process still
  has open is permitted: the open descriptor observes EOF on reads and
  `EACCES` on writes because the inode mode is zero. If the slot is later
  reused, an in-memory allocation generation makes the old descriptor continue
  to return EOF on reads and `EACCES` on writes without touching the new file.
  The generation is not persisted and does not change the on-disk format.
  The single-CPU, IF=0 kernel makes unlink and inode reuse atomic with respect
  to other processes.
- **rmdir**: only empty directories (no live entries); non-empty returns
  `ENOTEMPTY`, regular files return `ENOTDIR`. Never recursive.
- **rename**: no data copy, inode preserved, destination paths that already
  exist are rejected with `EEXIST` (no implicit overwrite). Moving a directory
  beneath its own descendant is refused with `EACCES` by walking the
  destination path component by component. Destination insertion happens
  before source removal so a failure cannot lose the only directory entry.
- **rename_replace**: x86_64-only; moves a regular source onto a regular-file
  destination, replacing the destination inode and reclaiming its blocks. A
  directory source or destination returns `EISDIR`; a missing destination uses
  the existing rename path. It updates existing directory records without
  allocation, leaves the source name/data in place if validation fails, and
  runs as one non-preemptible filesystem operation. This is atomic with respect
  to other processes, but it is not crash-atomic and does not promise an all-or-
  nothing result after power loss. Existing `rename` keeps its `EEXIST` behavior.

## Errors and accounting

New stable codes extend the existing ABI: `ENOSPC=0x1010`, `ENOTEMPTY=0x1011`,
`EEXIST=0x1012`. Existing codes (`ENOENT`, `EISDIR`, `ENOTDIR`, `EACCES`,
`EINVAL`, `EFAULT`, `E2BIG`, `ENAMETOOLONG`) are unchanged. The free block and
inode counters are persisted in the superblock after every mutation via
`superblock_sync`, and the bitmap is updated together with block allocation.

## Failure injection and disk full

Self-test builds gate every block and inode allocation through
`pollikfs_fail_after(budget)`. The C5 kernel test exhausts every successful
allocation prefix of create+write and of a 17th directory entry, remounts
PollikFS after each failure, and requires free blocks/inodes to return exactly
to the pre-call baseline. A dedicated disposable image with eight free blocks
(`tools/make_full_image.py`, derived from a valid image, never formatted) is
used to verify that ENOSPC is returned cleanly, committed bytes remain
readable and the filesystem remounts afterwards.

Read-side I/O injection (`fs64_io_fail_after`) remains unchanged and covers
the existing VFS error paths; allocation injection covers block, inode and
directory-entry creation. `rename_replace` is exercised with allocation budgets
0, 1 and 2 across separate source/target directories; each run remounts the
filesystem, verifies the committed contents and requires exact block/inode
baselines after cleanup. A sector-I/O failure injected before the first read is
also required to preserve both names and accounting. The operation does not
allocate directory space.

## Verification

`/bin/mutation_c` is an SDK-built C program with modes for basic create/write/
read, many small writes, a 40-KiB direct/indirect file, append, truncate,
mkdir/rename/unlink/rmdir, relative paths, bad pointers/flags/access modes,
two concurrent writers and 100 lifecycle repetitions. `c5_test.c` runs each
mode through the scheduler, then a reboot-persistence pair boots the same
disposable image twice and requires the first boot's committed file to read
back exactly, and finally runs the ENOSPC image. Every check asserts exact PMM,
VFS handle, descriptor, PollikFS block and inode balance.

Limitations: no arbitrary truncate, no hard links or `O_SYNC`, plain rename
never overwrites (`rename_replace` is a separate x86_64 operation), the inode's
representational size ceiling is 67,379,200 bytes but the 32-MiB gross volume
capacity limits actual file data to less than 32 MiB, one 4-KiB write buffer per call,
no file timestamps beyond the existing legacy tick counters, and no concurrent
multi-CPU guarantees (single BSP, non-preemptible IF=0 filesystem calls).
