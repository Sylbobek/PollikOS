# PollikOS C1 runtime foundation

> Superseded checkpoint: its four-process and 13-ordinary-descriptor limits below are historical; see [PROCESS_MODEL.md](PROCESS_MODEL.md) for current x86_64 limits.

C1 provides the kernel ABI needed before crt0/libc: architectural SYSCALL,
standard streams, console write, process-owned cwd and bounded relative paths.
All programs here remain real static ELF64 files on PollikFS, launched by path.
No libc, crt0, allocator, regular-file write or later runtime stage is implemented.

## Transport and return contract

`user_abi.h` centralizes the convention and numeric constants. PollikOS uses its
own operation numbers, independent of Linux. RAX carries the operation and signed
result. Arguments are RDI, RSI, RDX, R10, R8, R9; current operations use at most
three. SYSCALL clobbers RCX (return RIP) and R11 (sanitized return flags). Other
GPRs and RSP are preserved. Nonnegative results are success; negative results
are the negated stable USER_E* code. There is no TLS errno yet.

Startup ABI v1, argc/argv/envp and initial register meanings are unchanged.
The GDT's internal user descriptors are ordered data then code for SYSRET:
SS=0x2b, CS=0x33. Applications do not supply selectors to SYSCALL. Kernel CS=8,
SS=0x10. Both the initial IRET and subsequent traps use this centralized layout.

Bootstrap now requires CPUID SYSCALL support in addition to LM/NX/MSR/PAE.
`syscall64_init` enables EFER.SCE while retaining existing EFER bits, programs
STAR/LSTAR/FMASK, and checks every MSR by readback. FMASK masks all user-controlled
flags during entry, including IF, DF, TF, AC, NT and IOPL. Kernel code stays
non-preemptible. No instruction pushes onto or dereferences incoming user RSP.

An explicit BSP entry record contains the trusted process kernel-stack top and
one entry scratch user-RSP value. Kernel-GS-base selects this record; SWAPGS plus
LFENCE precedes its use. Entry saves user RSP there, loads the process kernel
stack, and constructs the existing normalized UserFrame. A second SWAPGS restores
the ordinary GS state before any C handler. The record is CPU-owned, not a
process-global continuation: future SMP must bind one record/MSR per CPU.
Unprivileged FSGSBASE remains disabled and no user GS/TLS setter exists.

`kernel64_set_rsp0` updates both TSS.RSP0 and the CPU entry stack pointer with IF=0.
The process stack remains authoritative for interrupt and SYSCALL entries. The
common trap layer checks frame bounds and TSS ownership. IRQ/NMI handlers never
use GS; NMI/DF retain their dedicated IST stacks, including during entry's short
SWAPGS interval. Existing fatal NMI/machine-check policy is unchanged.

Synthetic vector 256 identifies SYSCALL internally; it is not an IDT gate.
Both transports enter the same exception/process/file dispatcher. Exit abandons
the process stack through the existing continuation; faults and pending kills
use the same deferred reaper. Timer IRQs remain masked until return to userspace.

Before SYSRETQ, the common validator requires expected user selectors, a canonical
user-range executable RIP, and a canonical user-range RSP with writable mapped
memory immediately below it. Flags retain arithmetic bits and DF only; bit 1 is
set and IF follows scheduler policy. TF/IOPL/NT/RF/VM/AC and reserved privilege
state are removed. The final assembly restores registers, loads validated RCX,
R11 and RSP, then SYSRETQ with no further stack access. Invalid contexts terminate
the process before the return instruction; they are never passed to SYSRET.
IRETQ remains the controlled path for initial entry, interrupts and legacy calls.
No IRET fallback is needed for contexts which validation rejects outright.

INT 0x81 remains a **legacy transitional transport**, with the same operations and
error convention but its original RCX/R11 preservation. Earlier assembly stress
fixtures deliberately retain it for compatibility coverage. Normal hello/argv
ELFs now use SYSCALL and write; the scheduler fixture uses write through its
legacy transport to preserve its existing all-register interrupt test. Only the
old diagnostic foundation payload still depends on debug_write.

## Descriptor backends and standard streams

Each process starts with explicit types at 0=stdin, 1=stdout and 2=stderr.
`file64_kind` distinguishes closed, VFS, stdin, stdout and stderr. The existing
VFS object array still owns regular files/directories in 3..15; streams are small
process-owned endpoint records, not fabricated PollikFS files or allocated VFS
handles. There are still 13 ordinary file/directory slots, even if a stream closes.

Stdout and stderr are distinct descriptors using the existing polled COM1 output
sink. Closing one does not close the other or another process's endpoint. There
is no terminal/keyboard input backend in this x86_64 target: nonzero read(0)
returns ENOTSUP and consumes no buffer; valid zero-length read(0) returns 0. No
input or EOF is fabricated. Writes to stdin and reads from output streams return
EACCES. Seek/fstat on open streams return ENOTSUP; readdir returns ENOTDIR.
Closed streams return EBADF. Stream close succeeds once, then EBADF.

Process destruction clears all stream endpoints and cwd as well as closing all
VFS handles. `file64_count` deliberately counts VFS-owned slots for existing
regressions; `file64_stream_count` counts the three endpoints. No stream backend
allocation or external reference can survive process teardown.

## Added operations

| Operation | RAX | RDI | RSI | RDX | Success |
| --- | --- | --- | --- | --- | --- |
| write | 0x504f0018 | fd | readable user bytes | count | bytes consumed |
| chdir | 0x504f0019 | user path | unused | unused | 0 |
| getcwd | 0x504f001a | writable user buffer | capacity | unused | path bytes excluding NUL |

Write accepts stdout/stderr only. VFS files/directories and stdin return EACCES;
closed/out-of-range descriptors return EBADF. It accepts up to 65,536 bytes per
call, rejecting larger counts with E2BIG. Larger than 4-KiB writes use a fixed
4-KiB kernel-stack buffer repeatedly; no count-sized allocation exists. The entire
requested readable user range is checked before output, so invalid/noncanonical,
kernel, overflow or partially mapped buffers produce EFAULT without partial
console output. RX memory is readable and accepted. A valid zero-count write
returns 0 without touching its pointer. Current COM1 output waits for each byte
and consumes every byte; it has no short-write or asynchronous buffering backend.
The loop counts only bytes consumed, and retains a partial-result path if a later
copy fails. Kernel PIO remains serialized and bounded by the per-call byte cap.

Getcwd copies exactly the current path and NUL, only if capacity suffices. Success
is the length excluding NUL. ERANGE means insufficient capacity (including zero);
EFAULT means the actual output range is invalid. No partial output occurs. Unused
capacity beyond the actual path is not accessed or required to be mapped.

## One bounded path resolver

A process owns a fixed 128-byte canonical cwd buffer, initially `/`, with no inode
reference or caller-owned pointer. `path64_user` first performs the existing safe
bounded user-string copy, then `path64_resolve` normalizes against that cwd.
Open, stat, opendir and chdir all use this one helper. Fd-based operations do not
resolve paths. Kernel process64_launch_path retains its existing absolute-path
contract; no userspace spawn/exec API is introduced.

Normalization is **lexical before VFS lookup**: repeated slashes collapse, `.` is
removed, `..` pops one component and clamps at root. Thus `/../../bin` becomes
`/bin`; `/testdir/subdir/../` becomes `/testdir`. Components cancelled by `..` are
not looked up; this is an explicit bounded PollikOS convention, not a claim of
POSIX symlink/intermediate-component behavior. There are no symlinks or concurrent
filesystem mutation in the current read-only target. Trailing separators collapse
without imposing a separate file-type check; chdir/opendir still require directories.

Empty input is EINVAL. Request and result remain bounded to 127 bytes plus NUL,
with components at most 55 bytes. Composition is overflow-checked and returns
ENAMETOOLONG instead of allocating or truncating. A prefix exceeding the bound is
rejected even if a later `..` could shorten it. Chdir stats the final normalized
path, requires a directory, then commits the copied cwd; every failure preserves
the previous cwd. Independent processes never share these buffers.

## Stable errors

Existing codes and sign convention are retained. Internal VFS status values are
translated in the kernel; libc will only need to negate errors and set errno.

| Code | Symbol | Meaning |
| --- | --- | --- |
| 0x1001 | EFAULT | invalid user range |
| 0x1002 | ENOSYS | unknown operation |
| 0x1003 | E2BIG | bounded transfer count exceeded |
| 0x1004 | EBADF | missing/closed/out-of-range descriptor |
| 0x1005 | ENOENT | missing final normalized target |
| 0x1006 | EINVAL | invalid argument/path/seek |
| 0x1007 | EACCES | unsupported access mode |
| 0x1008 | EIO | filesystem corruption/unavailable storage/read failure |
| 0x1009 | EMFILE | all ordinary descriptor slots occupied |
| 0x100a | ENOMEM | object allocation failed |
| 0x100b | ENAMETOOLONG | path/component/composition bound exceeded |
| 0x100c | EISDIR | ordinary file open requires regular file |
| 0x100d | ENOTDIR | directory operation requires directory |
| 0x100e | ENOTSUP | recognized operation unsupported by endpoint |
| 0x100f | ERANGE | getcwd capacity insufficient |

## Verification design

`/bin/runtime` is a real disk-loaded ELF using SYSCALL exclusively. It exercises
stdout/stderr messages, initial cwd, chdir/getcwd, relative open/read/stat/opendir,
normalization/root clamping and unchanged cwd on failure. Its repeated-call loop
checks every preserved GPR, RSP, return RCX and arithmetic/DF flag behavior against
ENOSYS on an unknown operation. Each normal lifecycle makes at least 256 such calls.

Two runtime processes repeat calls across at least three timer ticks each with
different cwd values and different fd-3 objects; closing one stdout/file leaves
the peer intact. Internal return-check tests reject noncanonical/kernel/unmapped
RIP/RSP and kernel selectors and sanitize all unsafe flags. Three additional
actual SYSCALL processes set kernel, noncanonical or unmapped user RSP and are
contained before SYSRET. These test the production validator, not a mock return.

The error fixture checks stdin semantics, closed/bad/VFS fds, every requested bad
write range, zero write, an actual 8,193-byte three-page write delivered to COM1,
getcwd capacity/pointer failures and bounded path failures. Five rounds of exit,
fault and kill retain 13 open files and cwd until safe teardown. One hundred
combined runtime lifecycles verify PMM, VFS handles and complete process removal
per four-process batch. Previous reserved-descriptor tests now explicitly close
streams first; relative-path expectations change only where the new contract
makes them valid. Existing compatibility transport coverage remains.

## Verified checkpoint

Both builds passed. QEMU TCG self-tests passed at 16/64/256/5120 MiB, including
all prior suites and C1, normal boot/reboot, unsupported CPUs (now including
missing SYSCALL) and damaged-storage tests. Host checks confirmed at least 101
stdout and stderr messages and all 8,193 bytes of the chunked write in each
self-test log. Whole attached generated data-image hashes stayed unchanged;
no user data image was opened or formatted by these builds/tests.

At 5 GiB the 100-runtime-lifecycle test reports `0x13fd79` free frames before and
after, zero VFS handles and zero surviving process descriptors. i386 build,
64/256-MiB boots, 100-process stress and focused cursor/Terminal/Notes smoke pass.
Its stress baseline remains 56,448 free pages, and kernel SHA-256 is unchanged:
`cc167c6de16bd44e389c65410ecb1984ba8ea516a7a5be9cba7fec3e09970af9`.
The i386 build used a temporary copy of the existing script redirecting only its
data-image inspection to a disposable generated fixture; compilation was
unchanged and the temporary script was removed.

## Changed files

- `syscall.c`, `syscall.h`, `interrupts.asm`, `entry.asm`, `kernel.c`, `process.c`,
  `user.h`, `user_abi.h`: CPU entry state, MSRs, transport/return, process state.
- `path.c`, `path.h`, `file.c`, `file.h`: shared normalization, cwd, stream kinds,
  close/read/write behavior and stable errors.
- `runtime_demo.c`, `runtime_test.c`, `apps/x86_64/runtime.asm`: actual C1 fixture
  and guest verification, including production return-validator tests.
- `apps/x86_64/hello.asm`, `schedule.asm`, `readtest.asm`, `stattest.asm`,
  `dirtest.asm`: stdout migration and deliberate stdio/relative-path updates.
- `build-x86_64.ps1`, `tools/build_x64_data.py`, `path_test.c`,
  `tests/x86_64_boot.py`: build/disk/test integration.
- This document, `SELF_HOSTING.md`, and earlier x86_64 ABI documents: current
  contract references and checkpoint scope. Existing workspace changes retained.

## Scope and next milestone

Single BSP, non-preemptible kernel/PIO, 4 processes, 13 ordinary descriptors,
128-byte paths, fixed write cap and no genuine stdin backend remain limitations.
No FPU/SIMD context, TLS, libc, crt0, malloc, filesystem writes or process creation
syscalls have been added. PollikFS's on-disk format and i386 sources are unchanged.

Next milestones after this document were the C2 userspace memory foundation
(`RUNTIME_C2.md`), the C3 C runtime (`RUNTIME_C3.md`) and the C4 SDK
(`sdk/README.md`); all three are implemented.
