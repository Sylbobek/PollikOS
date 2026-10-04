# Controlled x86_64 user execution

Current C1 transport, stdio and path rules: [RUNTIME_C1.md](RUNTIME_C1.md).
SYSCALL is primary; INT 0x81 is transitional. Cwd/relative paths and explicit
standard streams supersede earlier absolute-only/reserved-fd statements below.

This document describes the CPL3 foundation and its retained synthetic regression
payload. Normal boot now uses the separately documented [static ELF64 loader and
startup ABI](ELF64_ABI.md), followed by the [preemptive scheduler demo](SCHEDULER.md).
The synchronous IF=0 runner described below remains for regression coverage. No
public application SDK is implied. Later [read-only file syscalls](FILE_ABI.md)
extend the gate while retaining these original regression checks. The default i386 desktop and its ABI are unchanged.

## Layout and frame

`user_abi.h` is the source for C and generated NASM constants. `memory.h` retains
the overall virtual layout; compile-time assertions keep the user boundary in sync.

| Range | Policy |
| --- | --- |
| PML4 slot 0 | Shared supervisor bootstrap region; first 2 MiB mapped |
| `0x8000000000`–`0x800000000000` exclusive | Canonical lower-half user region |
| `USER_CODE` = `0x8000000000` | One embedded RX code page; start of future executable region |
| `USER_DATA` = `0x8000200000` | One private RW/NX data page |
| `USER_PRIVATE` = `0x8000400000` | Optional private isolation-test page |
| `USER_STACK_BASE` = `0x7ffffffbb000` | One absent guard, then 64 USER/RW/NX pages (256 KiB) |
| PML4 slots 256–510 | Reserved, unmapped |
| Slot 511, from `0xffffff8000000000` | Shared supervisor kernel mappings |
| `PROCESS_KERNEL_BASE` = `0xffffff8010000000` | Up to 1024 process slots, each with a two-page control record and guarded kernel stack |

All other user pages stay unmapped. Initial user RSP is the aligned end of the
stack (`USER_STACK_BASE + 65*4096`). No argc/argv/startup ABI is implied for the synthetic payload.

`UserFrame` in `user.h` contains all 15 non-RSP GPRs, normalized vector/error,
RIP, CS, RFLAGS, RSP and SS, in interrupt-stack order. Offsets/size used by assembly
have compile-time assertions. GDT user selectors are CS `0x33`, SS `0x2b`;
the existing kernel selectors and 16-byte TSS descriptor retain their positions.
Initial entry restores the frame with IRETQ. RSP0 is set to the selected process's
own guarded kernel stack before switching CR3 and entering CPL3.

## Safe copies

`user_range_check`, `copy_from_user64`, `copy_to_user64` and
`copy_string_from_user64` take an explicit address-space handle and numeric user
address. They reject kernel/noncanonical pointers and overflow using subtraction
against the exclusive user limit, without unchecked rounding. Every level must
be present and USER; writes also require WRITE at every level. Page zero, guards,
missing pages and read-only destinations fail with `USER_COPY_FAULT`.

Buffer copies validate the complete range first: ordinary invalid input cannot
partially alter the destination. The copy then uses the supervisor physical
aperture, never an unchecked user pointer, and can operate on inactive spaces.
Single-BSP IF=0 serialization prevents mapping changes between validation and copy.
Unexpected kernel faults remain fatal diagnostics; no broad fault swallowing is
used to simulate safe copies.

A zero-length buffer succeeds only with an in-range canonical user pointer and
a valid user space, without requiring a mapped page or kernel buffer. String
capacity includes NUL; the maximum must fit the user numeric range. The string
helper checks each visited page, stops at NUL without requiring later pages,
and returns `USER_COPY_TOO_LONG` if capacity is exhausted. Failure leaves an
empty destination when capacity is nonzero. Kernel destinations are trusted
internal pointers, not another user-provided buffer.

## Temporary PollikOS gate ABI

Only interrupt `0x81` is DPL3-callable. All other gates remain DPL0. This temporary
transport will be replaced during a dedicated syscall/per-CPU entry milestone;
do not expose it as the stable SDK ABI. SYSCALL/SYSRET and EFER.SCE remain disabled.

| RAX number | Operation | Arguments/result |
| --- | --- | --- |
| `0x504f0001` | debug_write | RDI user buffer, RSI length (0–256); RAX bytes or negative error |
| `0x504f0002` | exit | RDI low 8 bits are status; does not return |

Errors are `-0x1001` invalid user buffer, `-0x1002` unknown operation and `-0x1003`
oversized debug buffer. These are PollikOS bootstrap numbers, not Linux numbers.
All GPRs other than RAX are preserved on returning calls. RFLAGS retains only
CF/PF/AF/ZF/SF/DF/OF plus fixed bit 1; IF/IOPL/TF/NT/VM/AC and other privileged
state are cleared. Kernel entry clears DF before C. No user-return address is
used until canonical mapped USER/EXEC RIP and writable mapped RSP-1, valid CS/SS
and canonical RSP have been checked. A bad return state terminates the payload.

## Process lifetime and containment

`Process64` owns a dynamically allocated two-page control record, independent address space,
guarded user and kernel stacks, saved frame, PID/state and exit status. Four
slots bound this test execution facility; a fifth creation fails. Create only
publishes READY after all resources exist. Every allocation failure destroys
partial mappings and tables, including the control record. No process becomes
runnable after failure. It reuses PMM/VMM rather than duplicating i386 scheduling.

`process64_run` is synchronous and consumes a ready process. The interrupt frame
is checked against the active process's kernel-stack bounds. Normal exit or a
user exception records the result, restores kernel CR3 and prior TSS.RSP0, and
resumes the saved kernel continuation stack. Only then are user tables/frames,
both stacks and the control record destroyed. Results survive in a caller-owned
`Process64Result`; there is no zombie list.

User faults report PID, CPL, vector/error, RIP and fault address (CR2 for #PF).
Kernel faults still panic even while a user process is active. NMI, double fault
and machine check remain platform/kernel handling, not ordinary process exits.
The existing emergency IST behavior is preserved.

## Verified scope and remaining limits

The full QEMU TCG self-test suite runs at 16/64/256/5120/32768 MiB; the normal CPL3 demo is verified at 64 MiB.
Tests exercise two simultaneous private spaces, actual USER stack reads/writes,
kernel/RO/NX/unmapped/guard violations, forbidden interrupt/CLI, real gate errors,
register/DF preservation, unsafe interrupt-return RSP, and bad initial RIP/RSP.
Safe-copy tests cover page boundaries, ancestor permissions, overflow, empty
buffers, missing mappings, read-only destinations and bounded strings. Failure
injection covers every construction allocation prefix. One hundred real CPL3
processes each make multiple returning gate calls and exit; free PMM counts
return to baseline after every process, including faults.

The synchronous regression runner remains one BSP, IF=0 and non-preemptive,
and is now compiled only for SELFTEST. Normal ELF demos use the
[preemptive scheduler](SCHEDULER.md), including safe infinite-loop termination.
The later scheduler milestone adds isolated x87/SSE2 state through a separate
per-process FXSAVE64 image; AVX and TLS remain unsupported. Threads, SMP and
general executable launching are not provided by this execution layer.
Bounded static ELF parsing is now provided by the separate ELF64 layer.
The CPU execution record is isolated in `process.c` for later per-CPU conversion.
The physical aperture also still requires serialization. No later runtime or
toolchain work is included.
