# PollikOS C6 process model (spawn and wait)

C6 gives x86_64 userspace a spawn-first process model: a C program launches
another static PollikOS ELF64 by path, receives the child PID, may keep running,
blocks in `waitpid` without busy-spinning and collects a typed termination
record. There is no `fork` and no image replacement (`exec`) yet.

## Process table and identifiers

The x86_64 process table has an upper bound of `PROCESS_MAX = 1024` slots; each
live slot owns a two-page control record plus its kernel stack. The active limit
scales with usable managed RAM: 32 processes below 96 MiB, 64 below 1 GiB, 128
below 8 GiB, 256 below 16 GiB, 512 below 28 GiB, and 1024 above that. These are
slot limits, not reserved memory guarantees; process creation can still fail
cleanly when RAM is occupied.
Each process has `USER_FD_LIMIT = 128` descriptor slots (fd 0 through 127,
including the three standard streams); regular file/directory opens use fd 3
through 127. Its user stack has 64 mapped pages, or 256 KiB usable, plus a guard
page. These are the current x86_64 limits; i386 has separate tables and limits.
The runnable queue is a circular FIFO, so normal dispatch removes the head in
constant time. The VFS descriptor pool scales with this active limit and is
allocated from normal RAM during mount, rather than growing the boot image.
PIDs are allocated monotonically from 1 and never reused, so a stale PID can never be confused with
a new process; `next_pid == 0` (64-bit wrap) stops new creation. A zombie keeps
its slot (and thus its PID) until the parent waits, exactly like a bounded
Unix-style table.

## Spawn

`USER_SPAWN` (`0x504f0025`): RDI path, RSI argv array, and RDX envp array.
Other argument registers are ignored. The kernel:

1. resolves the path through the existing cwd-aware bounded resolver;
2. copies argv/envp into bounded kernel storage
   (`STARTUP_MAX_ARGS`/`STARTUP_MAX_ENV` entries, `STARTUP_MAX_STRING` bytes
   each, no NULL terminator inside the arrays is `E2BIG`, invalid pointers
   `EFAULT`);
3. loads the ELF through the shared `launch64_create` primitive (the same
   loader used by kernel launches; no duplicate ELF code) under the kernel
   descriptor context;
4. sets `parent_pid`, inherits the parent's process group, copies the cwd and clones descriptors except those marked close-on-spawn;
5. inserts the child into the scheduler and returns its PID.

`USER_SPAWN_GROUP` (`0x504f0033`) takes the same path/argv/envp arguments and
an R10 process-group ID. A zero ID makes the child the leader of a new group;
a positive ID joins the caller's group or a direct-child-led group. The group
is assigned before scheduler insertion, so short-lived pipeline stages cannot
race the shell's group setup. An unreaped zombie group leader remains valid
while its parent is still assembling that pipeline.

`envp == NULL` in the raw syscall means an **empty** environment; the libc
`pollikos_spawn` wrapper passes the caller's current `environ` when `envp` is
NULL, so ordinary C code inherits the environment by default. All strings are
deep-copied; nothing in the child points into parent memory. Every failure path
reclaims the slot, address space, stacks, descriptor references and the
temporary image mapping through the existing launch rollback.

## Parent/child and zombies

A child stores `parent_pid`; a process is a child of exactly the process whose
PID matches, and PIDs are unique, so ownership cannot drift. Kernel-launched
processes use `parent_pid = 0` and are never waitable by userspace.

When a child reaches a terminal state, the deferred reaper immediately releases
its heavy resources (address space, user stack, kernel stack, descriptors,
cwd) and keeps only the lightweight control-page record: PID, parent, terminal
state, exit status and fault information. If no live parent exists, the record
is destroyed at once.

## Orphan policy

If a parent exits (or is killed) before its children, each live child is
re-parented to the kernel (`parent_pid = 0`) and auto-reaped on termination;
already-collected zombie children of that parent are destroyed immediately.
There is no init process and no recursive adoption in C6.

## Process groups and signals

Each process has a process-group ID (`pgid`); a new kernel-launched process
starts its own group, and `spawn` inherits the caller's group. The SDK exposes
`getpgid`, `getpgrp` and `setpgid`. A process may create its own group or join
its parent's group; a parent may assign a live direct child to its own group or
to a group led by one of its children. This gives the shell one process group
per pipeline. There is no separate session ID yet, so group membership checks
are bounded to the caller's own group and direct children.

`kill(pid, sig)` targets a positive PID, `kill(0, sig)` targets the caller's
group, and `kill(-pgid, sig)` targets that group. Signal zero checks whether a
PID or group exists. The TTY sends Ctrl+C to the selected foreground group.
Since PollikOS `spawn` loads a fresh image in one operation and has no `exec`, a
parent may regroup a live direct child after spawn; this differs from POSIX's
pre-exec restriction. Hardware-fault signal delivery and `SA_RESTART` remain
unsupported.

## Wait and termination status

`USER_WAITPID` (`0x504f0026`): RDI is a signed PID selector: a positive PID
selects that child, a value below -1 selects children whose process-group ID is
the negated value, and 0/-1/`UINT64_MAX` select any child (0 and -1 retain the
raw PollikOS API's legacy any-child meaning). RSI points to a 24-byte
`UserWait64`, or is zero to discard status. RDX accepts `USER_WAIT_NOHANG`
(`0x1`); other option bits are rejected. With WNOHANG, a live matching child
returns 0. Otherwise the call blocks and returns the collected child PID.
No matching child or waiting again after collection returns `ECHILD`. One
outstanding blocking wait per process returns `EAGAIN` if re-entered.

The SDK's POSIX `waitpid` wrapper implements positive PID, any-child (`-1`),
current process group (`0`), and explicit group (`<-1`) selectors, WNOHANG, and
the standard exit/signal status macros in `<sys/wait.h>`. Other options,
including `WUNTRACED` and `WCONTINUED`, return `EINVAL`; stopped children are
not waitable events.

Blocking reuses `PROCESS_BLOCKED`: the parent is removed from the runnable queue
with no deadline. The child's terminal transition (`exit`, fault, kill at a
safety boundary or `process64_kill`) delivers the status, writes the child PID
into the parent's saved `RAX` and re-enqueues it; there is no polling.

Status record v1 (`[C6]` ABI, mirrored by `pollikos_wait_t`):

| Field | Meaning |
| --- | --- |
| `version` | 1 |
| `kind` | 0 exited, 1 faulted, 2 killed |
| `code` | exit status (0/2), fault vector (1) or kill reason (2) |
| `address` | faulting address for kind 1, else 0 |

A fault is never reported as a normal exit, and a kill is never reported as a
fault.

## Inheritance

- **Environment**: deep-copied at spawn; the libc layer passes `environ` by
  default and the kernel never points a child at parent memory. Userspace
  `setenv`/`unsetenv` mutate a private malloc'd copy, so a child's mutations
  never affect the parent.
- **CWD**: copied into the child's own 128-byte buffer; a later child `chdir`
  cannot alter the parent.
- **Stdio**: descriptors 0/1/2 are inherited as fresh per-process stream
  endpoints unless marked close-on-spawn. A skipped stream is closed in the
  child. This lets callers close selected standard descriptors as well as
  ordinary files and pipes.
- **Other descriptors**: every open VFS descriptor is inherited by reference
  (shared open file description, shared offset, refcounted) unless marked
  close-on-spawn. Pipe ends and console aliases follow the same per-descriptor
  rule. A skipped descriptor stays closed in the child; parent flags and
  references are unchanged. Closing an inherited descriptor decrements its
  reference; the underlying handle is destroyed only when no descriptor
  remains.
- **Close-on-spawn ABI**: SDK `O_CLOEXEC` (`0x1000`) sets the descriptor flag
  during `open`. `pollikos_set_cloexec(fd, set)` sets or clears it on any open
  descriptor, including a pipe end or fd 0/1/2, and returns the previous
  value (`0` or `1`); invalid fds return `-EBADF`. The raw syscall requires
  `set` to be exactly 0 or 1 and returns `-EINVAL` otherwise; the SDK helper
  treats any nonzero `set` as true. `USER_CLOEXEC` is the new operation
  `0x504f003d` (`RDI=fd`, `RSI=set`).
  Unknown open flag bits continue to return `EINVAL`. `dup` and the target of
  `dup2` start with the flag clear, so a caller can mark raw pipe copies and
  then map only the intended ends onto child stdin/stdout.
- **PATH lookup** lives in libc (`pollikos_spawnp`): names containing `/` are
  used as paths, otherwise the `PATH` environment variable is searched in
  order (default `/bin`). The kernel never parses shell syntax.

## Limits and errors

Stable errors: `ENOENT` missing executable, `ENOEXEC` invalid/unsupported ELF,
`E2BIG` argument-count/length bound exceeded, `EFAULT` invalid user pointer,
`EINVAL` malformed request, `EAGAIN` full process table or one-wait-at-a-time,
`ECHILD` no such child, `ESRCH` PID outside the 32-bit range, `ENOTSUP`
reserved options. The table can be filled completely; the 33rd process fails
cleanly and slots become available again after children exit and are waited or
auto-reaped.

## Pipes

The later bounded pipe layer is implemented in `pipe.c`: anonymous unidirectional
pipes use fixed kernel slots and ring buffers, block readers/writers when the
buffer state requires it, signal EOF after the final writer closes, and report
broken pipes after the final reader closes. `pipe()` returns unflagged ends;
call `pollikos_set_cloexec` when parent copies must not keep a peer waiting for
EOF. Named FIFOs and `pipe2` flags are not implemented.

## Verification

`c6_test.c` runs SDK-built C parents (`spawn_parent_c`) that exercise: basic
spawn/wait/exit-42, child exit before wait, blocking wait timing, four children
with distinct statuses, faulting child, kernel-killed child, invalid spawn/wait
requests, environment and cwd inheritance, shared descriptor offset and
refcounts, `O_CLOEXEC`, the fcntl-like flag operation, rejection of unknown open
bits, close-on-spawn standard streams, and a three-process/two-pipe pipeline
that keeps the parent's pipe copies open until all spawns complete. The pipeline
must still drain through EOF. C6 also checks `spawnp` PATH search, a
four-child/four-grandchild tree, orphans, the 32-slot limit, allocation-failure
injection across the whole spawn path and 100 spawn/wait lifecycles. Every phase
requires PMM, VFS handles, process slots and zombie counts to return exactly to
baseline.
