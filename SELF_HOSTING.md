# Self-hosting checkpoint

## Current milestone: x86_64 C runtime, scalar floating point and console

PollikOS is **not self-hosting**. The default usable desktop remains i386.
`build-x86_64.ps1` builds a separate native 64-bit kernel foundation using the
existing `boot/boot.asm` and `boot/stage2.asm`, without modifying their disk or
handoff contracts. This target boots, parses a separately built static ELF64 hello executable,
runs a timer-preempted three-process ELF demo, reports its state on COM1, and
halts. It mounts PollikFS v2 through shared VFS/ATA reads; it does not yet run
the x86_64 desktop or general device drivers.
Its test ELF applications run in CPL3 under a bounded round-robin scheduler.
Host LLVM/NASM perform bootstrap compilation; QEMU executes PollikOS machine code.

The x86_64 target now has an E820-backed 64-bit PMM, explicit DMA32 allocation,
dynamic page tables, independent mapping spaces, map/unmap/destruction and
dynamic guarded kernel/IST stacks. See the [memory API and ownership contract](kernel/arch/x86_64/MEMORY.md).
Safe user copies and a minimal synchronous process lifecycle now run real CPL3
code through a temporary PollikOS interrupt ABI. See the
[user execution contract](kernel/arch/x86_64/USER_EXECUTION.md). The new
[ELF64/startup ABI](kernel/arch/x86_64/ELF64_ABI.md) supports bounded ET_EXEC
images, PT_LOAD, argc/argv/envp and rollback. The [scheduler contract](kernel/arch/x86_64/SCHEDULER.md)
adds PIT preemption, explicit states, kill, idle wake and deferred cleanup.
[Launch by path](kernel/arch/x86_64/VFS_LAUNCH.md) now reads actual files from
PollikFS without embedding ELF bytes. [Read-only file syscalls](kernel/arch/x86_64/FILE_ABI.md)
now give each process its own descriptors with safe open/read/seek/close and
automatic termination cleanup. [Metadata syscalls](kernel/arch/x86_64/STAT_ABI.md)
provide a dedicated versioned 64-byte stat/fstat result with 64-bit size fields
and explicit legacy tick-counter semantics. [Directory enumeration](kernel/arch/x86_64/DIRECTORY_ABI.md)
adds opendir/readdir on process-owned descriptors, a versioned entry ABI and
rewind-only directory seek. [C1 runtime ABI](kernel/arch/x86_64/RUNTIME_C1.md)
adds validated SYSCALL/SYSRET, console stdout/stderr, an explicit unsupported-input
stdin endpoint, chunked write, per-process cwd and bounded relative paths.
[C2 runtime memory](kernel/arch/x86_64/RUNTIME_C2.md) adds an isolated per-process
brk heap, bounded anonymous mmap/munmap, getpid, monotonic clock and blocking
sleep on the existing scheduler states. [C3 runtime](kernel/arch/x86_64/RUNTIME_C3.md)
adds crt0, normal `main`, the PollikOS syscall wrapper layer, errno, a
brk/mmap-backed allocator, memory/string/stdlib/stdio and read-only filesystem,
directory, cwd, process and time wrappers in a static `libpollikc.a`. C4 packages
it as the [PollikOS C SDK](sdk/README.md): one public include tree, one linker
script, the `pollikcc` host driver and the safe `pollikinstall` image installer.
C5 adds writable regular files, creation, truncation, append, mkdir, unlink,
rmdir and rename through the same descriptor/VFS/PollikFS stack
([mutation contract](kernel/arch/x86_64/FILE_MUTATION.md)), verified by SDK-built
C programs including disk-full safety, failure injection and reboot
persistence. C6 adds the spawn/wait process model
([process model](kernel/arch/x86_64/PROCESS_MODEL.md)): 32 bounded process
slots, monotonic PIDs, parent/child ownership, blocking `waitpid` with typed
termination status, zombies, orphan auto-reaping, environment/cwd/descriptor
inheritance and libc `spawn`/`spawnp` wrappers. Pipe descriptors, `dup`/`dup2`,
shell pipelines and file redirection also exist. There is no `fork`/`exec`,
thread API or dynamic linking. Basic software signal handling is added at C12.
C7 completes the
compiler-facing runtime for a future TinyCC port
([readiness audit](kernel/arch/x86_64/TINYCC_READINESS.md)): a buffered `FILE`
layer in `<stdio.h>`, `<ctype.h>`, `<assert.h>`, `<unistd.h>`, POSIX-style
`<sys/stat.h>`/`<dirent.h>` headers, string/conversion/`qsort`/`bsearch`/
`mkstemp`/environment libc additions, a 256 KiB user stack, 128 descriptor
slots per process, 64 argv/envp entries and 2 MiB images. PollikFS's inode tree
represents ~64.24 MiB, but its 32 MiB gross volume capacity (including metadata)
bounds actual file data below 32 MiB. C8-C12 port a pinned upstream TinyCC
0.9.27 snapshot ([port contract](kernel/arch/x86_64/TINYCC_PORT.md)): the host
cross-builds `/bin/tcc`, the guest rebuilds `/usr/src/libc/*.c` with native
TinyCC into `/usr/lib/libc.a`, and then native `/bin/tcc` compiles, links and
runs normal C on PollikFS (`/home/hello.c` -> `/home/hello` printing the
expected text and exiting 42). `setjmp`/`longjmp` now exist at the SysV AMD64
integer ABI. x87/SSE2 state is isolated per process; scalar `float`/`double`
and common scalar `<math.h>` functions work. Basic software signal handlers are also
available through `<signal.h>`. AVX, `long double`, `alloca`, threads, `fork`
and TinyCC's own self-rebuild remain out of scope. The interactive console milestone
([console/TTY contract](kernel/arch/x86_64/CONSOLE_TTY.md)) makes the COM1
console a real terminal: a polled TTY input queue feeds blocking `read(0)`, and
the SDK-built userspace shell `/bin/pollish` provides the prompt, line editing,
`$?`/`$NAME` expansion, history (persisted to `/home/.pollik_history`), file
builtins and PATH/`./` execution through `spawnp`+`waitpid`. The normal kernel
boots straight into the shell, so `tcc hello.c -o hello` and `./hello` can be
typed by hand; scrollback, mouse-wheel scrolling and cursor rendering come from
the host terminal emulator on COM1. Ctrl+C sends SIGINT to the foreground process group.
Signal groups, stop/continue, SA_RESTART and CPU-fault handlers remain future work.

### C support status

| Area | Status |
| --- | --- |
| C language execution on x86_64 | SUPPORTED (scalar float/double, x87/SSE2 in CPL3) |
| C applications | SUPPORTED as static PollikOS ELF64 |
| C runtime | PollikOS mini-libc (`libpollikc.a`, crt0, errno, buffered stdio) |
| Compiler-facing libc | READY (FILE/ctype/assert/unistd/sys.stat/dirent, strto*, qsort/bsearch, mkstemp, setenv) |
| C compilation | Native TinyCC in guest; host Clang via `pollikcc` as second cross-compiler |
| Filesystem mutation from C | SUPPORTED (create/write/truncate/append/mkdir/unlink/rmdir/rename) |
| Process creation from C | SUPPORTED (spawn/spawnp + blocking waitpid, 32 slots, no fork/exec) |
| Pipes / input-output redirection | SUPPORTED (`pipe`, `dup`, `dup2`, pipelines and `<`, `>`, `>>`) |
| Job control / complete POSIX signals | PARTIAL (`setpgid`, `getpgid`, group `kill`, foreground Ctrl+C; no sessions or restart flags) |
| `setjmp`/`longjmp` | SUPPORTED (integer register state) |
| Software signals | PARTIAL (`sigaction`, masks, `sigpending`, PID/group `kill`, `raise`, `SIGSTOP`, `SIGCONT`) |
| threads, `alloca` | NOT YET |
| Native C compiler inside PollikOS | SUPPORTED (TinyCC 0.9.27, stage 0; compiles/links/runs C natively) |
| TinyCC self-rebuild | NOT VERIFIED (stretch goal, not attempted) |
| Interactive x86_64 console/TTY | SUPPORTED (COM1 input queue, blocking stdin, userspace shell) |
| x86_64 shell | `/bin/pollish`: prompt/cwd, line editing, history, builtins, PATH/`./` execution |
| C++ | NOT YET |
| Floating point / SIMD | SUPPORTED (x87/SSE2 scalar; AVX and `long double` unavailable) |
| `math.h` | COMMON SCALAR FLOAT/DOUBLE FUNCTIONS; complete POSIX libm remains unavailable |
| Dynamic libraries | NOT YET |

### Build and verification

```powershell
.\build-x86_64.ps1
.\build-x86_64.ps1 -SelfTest
python tests/x86_64_boot.py
python tests/x86_64_storage.py
```

Artifacts are under `build/x86_64/kernel/` and `build/x86_64/selftest/`, each with
`kernel.elf`, `kernel.bin`, `PollikOS-x86_64.img`, separate userspace ELF files
and generated `PollikData-test.img`. Neither x86_64 build opens user
`PollikData.img` or overwrites the desktop image. Tests use snapshot boot disks
and their generated PollikFS data disk, hash it before/after each boot, and keep
serial logs under `build/x86_64/`.
Run the normal target manually with:

```powershell
qemu-system-x86_64 -accel tcg -m 64 -display none -serial stdio -monitor none -nic none -no-reboot -drive file=build/x86_64/kernel/PollikOS-x86_64.img,format=raw,snapshot=on -drive file=build/x86_64/kernel/PollikData-test.img,format=raw,if=ide,index=1
```

Verified in QEMU TCG:

- Self-test boots at 16, 64, 256 and 5120 MiB: 64-bit GPRs and IRET/flags survive two
  breakpoint round trips; real page faults enforce null/stack guards, read-only
  text/rodata and NX data; real double fault uses the TSS emergency stack.
- Memory tests exercise allocation and active page tables above 4 GiB, DMA32,
  dynamic mapping/protection/guard faults, independent spaces, ownership,
  failure rollback and 100 lifecycles without a PMM page leak.
- Real `/bin/runtime` uses SYSCALL for stdout/stderr, chdir/getcwd and relative
  file operations; tests cover register/stack preservation, return-state attacks,
  preemption/isolation, bad arguments and 100 combined lifecycles.
- Real `/bin/dirtest` verifies bounded names, file/directory types, hidden-name
  visibility, empty/normal EOF, rewind, independent iterators, bad arguments,
  failure churn, termination with 13 directories open and 100 balanced lifecycles.
- Real `/bin/stattest` verifies the versioned 64-byte metadata ABI, file/directory
  types, stored tick values, bad paths/pointers/fds, concurrency, I/O errors,
  termination cleanup and 100 lifecycles with PMM/handles/descriptors balanced.
- Real `/bin/readtest` uses CPL3 open/read/seek/close on deterministic `/etc`
  files. Tests cover descriptor/offset isolation across preemption, bad pointers
  and descriptors, full tables, injected failures, termination with 13 files open
  and 100 full file lifecycles without PMM or handle leaks. Disk hashes stay fixed.
- VFS launches `/bin/hello`, `/bin/argvtest`, `/bin/spin` and `/bin/faulttest`.
  One hundred mixed path launches preserve PMM and open-handle baselines; short
  reads, EOF/I/O errors, allocation/queue failures and damaged disks fail cleanly.
  Hello runs after two boots of the same unchanged PollikFS image.
- Normal target reaches its ready diagnostic after three infinite-loop ELFs are
  preempted and killed at their configured CPU-tick limits.
- Scheduler tests cover at least 1200 process switches, integer-register/flag and
  stack preservation, CR3 isolation, idle wake, exit/fault/kill, failure injection
  and 100 mixed process replacements without a PMM leak.
- Real ELF64 hello validates startup arguments/environment, initialized data and
  BSS, prints and exits 42. All four sizes pass malformed ELF rejection, protection
  faults, allocation rollback, independent images and 100 ELF lifecycles without leaks.
- Ring 3 demo prints its message and exits with status 42. All four memory sizes
  pass safe-copy edge cases, process isolation/fault containment, construction
  failure rollback, and 100 CPL3 lifecycle tests with stable PMM accounting.
- Pentium III and qemu64 with LM, NX, PAE or MSR individually removed stop with
  an unsupported-CPU diagnostic, before enabling paging/long mode.
- C7 compiler-readiness fixtures run from PollikFS: buffered FILE modes,
  multi-block seek/overwrite, EOF/error state, the 128-slot per-process
  descriptor-table limit, deep
  stack frames, ctype/string/conversion/qsort/bsearch, allocator symbol-table
  and growth stress, three-stage spawn/wait tool chains, eight concurrent
  helpers, temporary-file isolation, 61-entry argv arrays and a 300 KiB ELF.
  All C7 selftests plus the C7 demo (tool chain, temp files, large ELF) pass
  at 16/64/256/5120 MiB, on the almost-full ENOSPC image and across the
  reboot-persistence pair, with PMM/handles/blocks/inodes balanced.
- C8-C12 native TinyCC, floating point and signals: `/bin/tcc` (0.9.27-pollikos) boots as a CPL3 ELF64, the
  driver rebuilds `/usr/src/libc/*.c` into `/usr/lib/libc.a` with native
  `tcc -c` + `tcc -ar`, then native `tcc /home/hello.c -o /home/hello`
  produces a static ELF64 that prints "Hello from self-hosted PollikOS C!" and
  exits 42. The same run passes `-E` preprocessing, `-c` object output, libc
  and filesystem programs, multi-file and object-link workflows, invalid
  source/missing header/unwritable output diagnostics and 25
  native compile/run churn cycles; the kernel verifies PMM, VFS handles,
  process slots and zombies all return to baseline. On the dedicated
  almost-full ENOSPC image the suite reports the documented skip marker. The
  C11 fixture also rebuilds `math.c` with native TinyCC and runs scalar
  float/double arithmetic plus the SDK math subset, exiting with status 42.
- Interactive console: `tests/x86_64_console.py` boots the normal kernel with a
  TCP serial link and types a full session. The shell prints a cwd prompt,
  `cd`/`pwd`, `ls`/`cat`, quoting and `$?`/`$PATH` expansion, file commands,
  then runs real `tcc hello.c -o hello` and `./hello` (printing "Hello from
  self-hosted PollikOS C!", exit 42 via `echo $?`), plus mid-line editing,
  Up/Down history recall, unknown-command reporting, retained output,
  `clear`, and exit/relaunch with persistent history. `tests/x86_64_boot.py`
  runs this check as its final step.
- Default i386 checkpoint build succeeds; desktop reaches ready at 64/256 MiB.
  Existing process stress passes 100 Ring 3 spawn/exit cycles at 256 MiB with
  unchanged PMM free-page counts and fault-isolation checks passing.
- The old i386 Notes background fixture is classified as a stale light-theme
  expectation. Source and saved-framebuffer evidence is in
  [the fixture investigation](tests/GUI_SMOKE_FIXTURE.md). No i386 source changes
  were part of the Ring 3 milestone. The current VFS milestone intentionally
  changes the i386 binary through shared filesystem bounds/error/close fixes;
  build, boot, process stress and focused GUI regression pass.
- Focused `smoke.py --notes-only` now passes cursor/Terminal interaction, Notes
  editing and wheel scrolling/restoration. Later legacy full-smoke steps were
  not rerun and are not claimed as passing.

No physical-hardware or networking tests are claimed. Filesystem persistence
is verified in QEMU across repeated boots of the same generated data image.

### Boot and exception contract

Stage 2 still loads a flat image at physical 1 MiB and enters 32-bit protected
mode. The kernel ELF64 artifact is a link/debug artifact; stage 2 does not parse it.
The separate userspace ELF64 files are parsed by the native kernel loader. Its first bytes are a 32-bit entry trampoline. The trampoline checks
CPUID/MSR/PAE/LM/NX, then requires a valid E820 usable entry covering the kernel
through BSS; overlapping reserved entries are rejected. It clears BSS, creates
the initial four-level tables, enables PAE, EFER.LME/NXE and CR0.PG/WP, and far
jumps to 64-bit code. E820/VBE addresses remain the existing boot contracts.

Initial mappings cover only the first 2 MiB with 4 KiB supervisor pages. Page
zero and three stack guards are absent. Text is RX, rodata is R/NX, and other
mapped pages are RW/NX. Tables and bootstrap stacks are linker-owned BSS,
bounded by a linker assertion, rather than arbitrary physical scratch areas.
These are bootstrap allocations only. After PMM initialization every active
page-table level and the running kernel/IST stacks are dynamically allocated;
the old bootstrap region stays reserved. The i386 PMM is not called under the
new ABI.

GDT selectors are kernel code 0x08, data 0x10 and TSS 0x18. TSS supplies RSP0,
IST1 for double fault and IST2 for NMI; its absent I/O bitmap denies
Ring 3 port access. Only the temporary 0x81 gate is user-callable. Exception
stubs normalize vector/error fields, preserve all 15 non-RSP general registers,
clear DF for C, align the C call stack and return with IRETQ. Hardware saves
RSP/SS in the frame. Unexpected exceptions report vector, error, RIP, CR2 and
RSP and halt for kernel faults; ordinary CPL3 faults terminate the process.
Only the self-test build permits narrowly matched kernel fault recovery.
Kernel compilation disables red-zone and floating-point register use. User
x87/SSE2 state is saved and restored per process; AVX is disabled.
PIC interrupts stay masked during bootstrap and synchronous regression checks.
The scheduler unmasks PIT IRQ0 and enables user IF; kernel work remains IF=0.
Early faults before the 64-bit IDT is installed still lack panic reporting.

Architecture reference: [AMD64 System Programming Manual, Volume 2](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf).

### Process/syscall migration boundary

The existing ABI remains ELF32/EM_386, INT 0x80 and 32-bit pointers in
`include/pollikos.h`. No published application ABI changes in this checkpoint.
The x86_64 target enables EFER.SCE with validated STAR/LSTAR/FMASK state. Initial user execution
uses IRETQ; temporary INT 0x81 offers debug_write, exit, bounded read-only file
operations and stat/fstat. Safe copies,
user selectors, process stacks and RSP0 integration exist. Per-CPU stack switching
is still needed before SYSCALL, which does not supply a kernel stack automatically.
Specify native PollikOS syscall numbers/argument registers, RCX/R11 clobbers,
error results and a versioned startup stack before publishing SDK headers.
Validate user RIP/RSP and return flags; initially use validated IRETQ returns
and only add SYSRET when canonical-address and privilege constraints are proven.
The full syscall migration remains future work; the temporary ABI is documented separately.

## Code-based compatibility audit

| Area | Current status | Evidence / immediate gap |
| --- | --- | --- |
| Desktop, Dock, compositor, input | Native i386 | Existing default target retained; no x86_64 integration |
| x86_64 kernel foundation | Partial, native guest execution | Long mode, page protections, GDT/TSS/IDT; no device IRQs |
| PMM/VMM | Native i386; native x86_64 foundation | Sparse 64-bit bitmap PMM, DMA32, dynamic four-level tables, independent spaces, ownership/destruction and guarded stacks; verified with 5 GiB RAM; BSP, kernel memory APIs require IF=0 |
| ELF/processes/scheduler | Native i386; bounded native x86_64 ELF64 processes | i386 ELF32 unchanged; static ET_EXEC/PT_LOAD with startup ABI v1, argc/argv/envp, isolated CPL3 execution and clean teardown; timer-driven round-robin, kill and deferred reaping; no dynamic linking |
| File descriptors/VFS | Native i386; x86_64 per-process descriptors | Shared VFS and path launch; CPL3 open/read/seek/close/dup/dup2, 128 descriptors, pipes, safe copies, stat/fstat, directory enumeration, mutation and teardown; POSIX coverage remains partial |
| PollikFS v2 | Shared i386/x86_64 read/write | Stable wire format; x86_64 ATA PIO mount, directories, direct/indirect/double-indirect inode pointer range (~64.24 MiB), 32 MiB gross volume and ENOSPC safety; i386 unchanged |
| Terminal/shell | Native i386 windowed Terminal; native x86_64 serial console | i386 `gui/terminal.c` built-in dispatcher retained; x86_64 `/bin/pollish` has TTY editing/history, PATH execution, pipelines and file redirection; no PTY/job control |
| Network/TLS | Native i386, partial | `net/`: RTL8139, ARP/IPv4/UDP/TCP/DNS and BearSSL integration; syscall sockets use a small global table; not a portable per-process POSIX API |
| C runtime / SDK | PollikOS SDK 1.0.0 | `libpollikc.a` (buffered stdio, malloc, spawn/wait, filesystem), crt0, public headers; i386 has only the kernel-side runtime |
| C/C++ compiler, assembler, linker | Native TinyCC 0.9.27 for scalar float/double; host Clang via SDK | `/bin/tcc` compiles and links C inside PollikOS; no C++, assembler or native linker beyond TinyCC |
| Dynamic linker and pthreads | Not implemented for x86_64 | Anonymous mmap exists; thread support needs shared address spaces and thread-group lifecycle |
| POSIX software signals | Partial for x86_64 | Handlers, masks, pending delivery, `SIGSTOP`/`SIGCONT`, process groups; hardware-fault signals remain unavailable |
| Native build tools and Git | Partial | TinyCC runs natively; no make, binutils, Git or image construction yet |
| Rust core/alloc/std, rustc, Cargo | Not started / blocked | Native target, allocator, ABI, then full OS services/toolchain |
| CPython, pip, venv, extensions | Not started / blocked | libc/process/filesystem foundations and native compiler |
| Node.js, npm, npx, Corepack, TypeScript | Not started / blocked | V8/libuv, threads, virtual memory, sockets, libc and native C++ toolchain; remains a required final target |
| Go, Java/OpenJDK | Not started / blocked | OS services and runtime/toolchain ports |
| Packages / Base and Developer profiles | Future work | No new packaging support claimed by this target |
| PollikOS rebuilding itself | Blocked | Native compiler/build tools/Git/image construction do not exist yet |

## Remaining C runtime work

The current C1-C12 checkpoints and interactive console are complete, including
the FPU state needed for scalar float/double, basic software signals, a starter
math library, pipes and redirection. Host Clang already cross-compiles PollikOS programs through
`pollikcc`, alongside the native guest TinyCC. The remaining user-requested
runtime work is broader POSIX/libc coverage and threads; these need separate
ABI and scheduler milestones. AVX, `long double`, dynamic
linking, job control and TinyCC self-rebuild are also not available.
