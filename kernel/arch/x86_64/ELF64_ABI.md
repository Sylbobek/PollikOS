# PollikOS x86_64 static executable and startup ABI v1

Current C1 transport, stdio and path rules: [RUNTIME_C1.md](RUNTIME_C1.md).
SYSCALL is primary; INT 0x81 is transitional. Cwd/relative paths and explicit
standard streams supersede earlier absolute-only/reserved-fd statements below.

The normal x86_64 boot demo now loads a real, separately linked ELF64 file.
The old opcode payload remains only as a Ring 3 regression fixture. No i386
executable ABI changes are made.

## Supported image subset

`elf64_validate` and `elf64_load` accept a stable, bounded kernel byte image.
The caller owns that buffer for the duration of the call; it must not be a raw
user pointer or transient physical-aperture pointer. The VFS launch adapter reads
a bounded file into such a buffer and uses the same parser/loader.

Accepted: little-endian ELFCLASS64, EM_X86_64, EV_CURRENT, ET_EXEC, OSABI/ABI-version
zero, ELF flags zero, 64-byte ELF header and 56-byte program headers. At most
16 program headers, 2 MiB of input bytes (`ELF64_MAX_IMAGE`) and 1,024 mapped
image pages (4 MiB) are allowed. Header/program-table and all used file ranges are bounded before
reads. Section headers and `p_paddr` are not used for loading.

Only PT_LOAD and ignored PT_NULL headers are accepted. Other headers—including
PT_INTERP, PT_DYNAMIC, PT_TLS, GNU extensions and metadata headers—fail cleanly.
ET_DYN/PIE/shared objects and runtime relocations are unsupported. This is a
deliberately narrow static subset, not general upstream ELF compatibility.

Each nonempty segment must lie wholly in `[MM_USER_START, USER_STACK_BASE)`.
This excludes kernel, noncanonical and reserved stack addresses. File bytes may
be zero but cannot exceed memory size. Alignment must be 0, 1 or a power of two
at most 2 MiB; virtual address and file offset must be congruent modulo alignment
and 4 KiB. Even disjoint byte segments sharing a rounded page are rejected.
All range calculations use subtraction bounds before addition/rounding. Entry
must fall inside file-backed bytes of an executable segment, not BSS or padding.

PF_R is required, unknown flag bits and PF_W+PF_X are rejected:

| ELF flags | Final user mapping |
| --- | --- |
| R | read-only, NX |
| R+W | writable, NX |
| R+X | read-only, executable |

Pages are allocated zeroed and receive final PTE permissions immediately. File
bytes are installed through the trusted supervisor physical aperture. No writable
user text alias is created. BSS and unused page padding stay zero.

Wire-format reference: [ELF gABI program headers](https://gabi.xinuos.com/elf/07-pheader.html).
PollikOS's supported subset and startup convention are defined here, not by a
Linux executable or syscall ABI.

## Initial process state

Entry RIP is `e_entry`; CS/SS and sanitized flags use the existing Ring 3 contract.
The guarded user stack is USER/RW/NX with 64 mapped pages (256 KiB usable). Initial
RSP is 16-byte aligned and points at this five-qword PollikOS header:

| RSP offset | uint64 value |
| --- | --- |
| 0 | startup version = 1 |
| 8 | argc |
| 16 | user pointer to argv |
| 24 | envc |
| 32 | user pointer to envp |

The `argv[argc+1]` array follows the header; `envp[envc+1]` follows argv. Each
ends with a null pointer. NUL-terminated strings sit above the arrays, entirely
inside the stack. Padding is zero. Every supplied pointer is a userspace VA;
no kernel pointer or inherited kernel stack content is exposed.

Entry registers: RDI=argc, RSI=argv, RDX=envp, RCX=version 1. Other GPRs are zero.
There is no return address at entry. An assembly `_start` can `call` a future
C entry routine with the usual x86_64 call alignment (callee RSP mod 16 = 8).
That future runtime must deliberately recognize this header/version rather than
assume a Linux argc-at-RSP stack or auxiliary vector. Scheduled userspace has IF
set; the retained synchronous regression runner keeps IF clear. See
[SCHEDULER.md](SCHEDULER.md) for preemption and register-state policy.

Arguments and environment are implemented, not placeholders. Limits: 16 args,
16 environment strings, at most 255 content bytes plus NUL per string, and
4,096 total string bytes including NULs. Zero args/environment produce empty
terminated arrays. No shell parsing occurs. Inputs are bounded trusted kernel
strings; invalid counts, null entries, missing terminators or excess bytes
produce ELF64_ARGUMENTS and complete process rollback.

Temporary syscall convention remains INT 0x81: RAX operation, RDI/RSI arguments,
RAX result. `0x504f0001` writes a bounded user debug buffer; `0x504f0002` exits.
The standalone assembly program isolates invocation in macros for later migration.
There is no SYSCALL/SYSRET or libc change. The later [file ABI](FILE_ABI.md)
adds four read-only file calls without changing these existing operations.

## Ownership and failures

Validation allocates no frames. Loading preflights conflicts and maps only new,
owned pages. Allocation failure rolls back every page installed by that call,
reclaiming empty intermediate tables without touching pre-existing mappings.
`process64_create_elf` reuses common process/stack setup; READY is published only
after image loading and stack construction succeed. Any failure destroys all
partial resources. Exit/fault cleanup uses the already verified Ring 3 teardown.

## Build and verified behavior

`apps/x86_64/hello.asm` plus `user.ld` build an actual ET_EXEC with RX text and
RW/NX data+BSS. Outputs are separate from kernel objects under
`build/x86_64/{kernel,selftest}/userspace/hello.elf`. The build installs the file
as `/bin/hello` in generated PollikFS v2 storage. No ELF bytes are embedded in
either kernel; see [VFS launch](VFS_LAUNCH.md).
The kernel reads its ELF header/program headers; it does not jump into raw bytes.

The real program verifies `hello.elf first second`, `TEST=pollikos`, version,
alignment, null vector terminators, initialized data and 8,193 zero BSS bytes.
It writes private BSS, prints `Hello from ELF64 PollikOS`, and exits 42.

QEMU tests at 16/64/256/5120 MiB include all previous memory/Ring 3 checks plus:
26 malformed/unsupported ELF cases, startup bounds, same-VA distinct data/stack
frames, text-write/NX/kernel-access faults, unaffected peer processes, allocation
failure sweeps, standalone loader rollback/conflicts and 100 complete ELF runs.
In the 5 GiB guest, free frames remain `0x13fd79` before and after 100 ELF runs.

i386 regression: build and 64/256 MiB boots pass; 100 process runs return to
56,448 free frames. The earlier ELF checkpoint kernel binary was identical
to its predecessor; the current VFS milestone includes shared filesystem safety
fixes and therefore changes the i386 binary. The focused GUI test passes on a standalone rerun. Its first run
failed exact screenshot equality because the Notes status changed from
`SAVED TO DISK` to `Unsaved`; document pixels were unchanged. This remains a
timing-sensitive GUI assertion, not a change to the i386 executable ABI.

Remaining limits: fixed-address static images, bounded byte input, one BSP,
non-preemptible kernel and no FPU/TLS process state. The kernel launches files
through read-only VFS access; processes now have bounded
[open/read/seek/close calls](FILE_ABI.md) with private descriptor tables.
Scheduled userspace is now timer-preemptible and can be killed safely; see
[SCHEDULER.md](SCHEDULER.md). No dynamic linker, libc or native development
toolchain is implemented.
