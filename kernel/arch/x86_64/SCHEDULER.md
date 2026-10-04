# x86_64 preemptive process checkpoint

> Superseded checkpoint status: C2 is complete; see [RUNTIME_C2.md](RUNTIME_C2.md), [RUNTIME_C3.md](RUNTIME_C3.md) and [TINYCC_PORT.md](TINYCC_PORT.md).

Current C1 transport, stdio and path rules: [RUNTIME_C1.md](RUNTIME_C1.md).
SYSCALL is primary; INT 0x81 is transitional. Cwd/relative paths and explicit
standard streams supersede earlier absolute-only/reserved-fd statements below.

The isolated x86_64 target now schedules real static ELF64 processes with a
single-CPU round-robin dispatcher. The i386 implementation/ABI is unchanged.
ELF startup ABI v1, mappings and INT 0x81 debug_write/exit remain unchanged.
Both normal ELF demos use this scheduler; the old synchronous runner is compiled
only into SELFTEST to preserve previous memory/ELF regression coverage.

The implementation boundary is `scheduler.c`: it owns the runnable queue,
timer accounting, active execution context, deferred reaper loop and dispatch.
`process.c` owns PID slots, process construction/exit, descriptors, signals and
syscall dispatch; its private handoff is declared in `process_internal.h` and
`scheduler_internal.h`. Each `Process64` currently embeds one `Thread64` TCB
with its own TID; the scheduler queue and active CPU context hold TCB pointers.
Creating multiple threads per process and reaping their lifetimes independently
are not implemented yet.

## Timer and critical sections

The previous x86_64 checkpoint masked both PICs and never enabled IF. i386
`kernel/process.c` already programs the legacy 8259 PIC and PIT channel 0.
`timer.c` uses the same hardware approach, with PIC offsets 32/40 and PIT mode 3,
divisor 11931 (1193182/100), about 10 ms per quantum. Only IRQ0 is unmasked.
No APIC, HPET, SMP or independent competing timer source is introduced.

Every interrupt/syscall gate is an interrupt gate: the CPU clears IF on entry.
Kernel code never enables IF except the idle `sti; hlt; cli` sequence. Scheduler,
PMM/VMM, physical aperture, copy and callback APIs require BSP/IF=0. Thus timer
preemption cannot interrupt a syscall or mutate scheduler data recursively.
A pending IRQ becomes deliverable after IRETQ restores user IF; no syscall
changes another process's live kernel continuation. IRQ0 EOI happens once,
before leaving the interrupted process's stack. Idle IRQs only account/wake;
they never switch in the middle of kernel work.

`scheduler64_run(maximum_ticks, boundary, completion)` is a bounded dispatcher.
It masks IRQ0 and returns with IF=0 when its tick budget expires or all owned
processes are reaped. Survivors stay queued for a later run. Pending masked
hardware ticks are not counted as elapsed process CPU time. If only blocked
processes remain, the dispatcher uses interruptible HLT and runs its boundary
callback after wakeup. Empty runs return to their caller rather than inventing
an immortal idle process. Normal checkpoint boot deliberately stops after its demo.

## State, queue and ownership

`BUILDING -> READY -> RUNNING`; a timer quantum returns RUNNING to READY.
READY can become BLOCKED; kernel wake returns BLOCKED to READY. Exit/fault
take RUNNING to EXITED/FAULTED. Kill takes READY/BLOCKED to KILLED immediately;
kill of RUNNING records a pending request consumed at the safe trap boundary.
Terminal states never reenter the queue. `transition()` checks legal edges.

The runnable FIFO is a circular queue backed by the fixed 1024-slot upper bound.
Its active limit scales with managed RAM (32/64/128/256/512/1024 slots), as documented in
[PROCESS_MODEL.md](PROCESS_MODEL.md). Normal dispatch removes the head in
constant time; arbitrary kill or block removal compacts only the remaining
queue. It allocates no frames. Successful submit transfers ownership to the
scheduler; rejection leaves ownership with the caller.
Direct destruction rejects managed or active processes. Block/wake and kill
accept managed PIDs only; invalid, reaped and already terminal PIDs fail safely.
PIDs are not reused. Basic software signals are pending per process and
delivered at safe user-return boundaries. Process groups use the leader PID and
are inherited on spawn; priorities and session IDs are not implemented.

`process64_tick_limit(pid, ticks)` optionally sets an absolute consumed-CPU-tick
limit (zero disables it). Timer expiry calls the same kernel kill API for the
currently running process, recording reason 124. This bounds uncooperative tests
without adding a userspace syscall or cooperative yield.

## Exact frame and switch path

The existing NASM entry is retained; C does not infer compiler stack layout.
`UserFrame` offsets are statically asserted against generated ABI constants:

| Byte offset | Saved field |
| --- | --- |
| 0, 8, 16, 24, 32, 40, 48, 56 | R15, R14, R13, R12, R11, R10, R9, R8 |
| 64, 72, 80, 88, 96, 104, 112 | RDI, RSI, RBP, RBX, RDX, RCX, RAX |
| 120, 128 | normalized vector, error code |
| 136, 144, 152, 160, 168 | RIP, CS, RFLAGS, RSP, SS |

The total is 176 bytes. Hardware supplies return state, stubs normalize error
codes, then explicitly push all 15 non-RSP GPRs. CLD permits C execution while
the saved user DF survives. Scheduled returns retain arithmetic flags and DF,
set IF and fixed bit 1, and clear privileged/unsupported flags. IRETQ restores
the complete GPR/return context. FPU state is saved separately in each
process's aligned 512-byte FXSAVE64 image. Segment-base/TLS and debug-register
state are not part of this frame.

Timer entry asserts the live TSS RSP0 and frame bounds match the active process's
guarded kernel stack. It copies the frame into that process's supervisor control
page, updates state/queue, clears active ownership and returns through the saved
assembly continuation onto the dispatcher stack. No interrupted C stack is
transplanted. The dispatcher selects FIFO head, validates its return context,
sets current process, TSS RSP0 and CR3, then enters with IRETQ.

All dispatcher/kernel mappings are shared supervisor mappings. A timer park
keeps the outgoing CR3 until selection, so `vmm64_switch` can avoid a reload when
resuming the same root. Different roots reload CR3; PCID/PGE remain disabled,
so stale user translations cannot survive a switch.

Exit/fault leave the user stack permanently and return to the dispatcher.
The reaper asserts it is outside the victim kernel stack, switches to kernel
CR3, reports the read-only completion record and destroys mappings, page tables,
both stacks and the control page. Only then can a boundary callback create a
replacement. Completion pointers are valid only during the callback. Boundary
callbacks may submit/block/wake/kill but run non-preemptibly and must be bounded.

## Floating-point policy and accounting

Boot verifies FPU, FXSR, SSE and SSE2, then enables x87/SSE2 and initializes a
clean 512-byte FXSAVE64 image for each process. User state is saved on syscall,
exception and timer entry, then restored before returning to that process or
resuming it after a timer switch. AVX is deliberately disabled. Kernel C keeps
`-mgeneral-regs-only` so interrupt and scheduler code cannot corrupt user FPU
state. A scheduler fixture verifies x87 and XMM values survive a sleep while a
healthy peer runs. Signal handlers use a kernel-saved GPR/FPU frame and a
validated user restorer. Threads, FS/GS/TLS context and AVX remain unsupported.

Each process records PID/state, dispatches, consumed user ticks and exit/fault
diagnostics. Global accounting records hardware ticks, user/idle ticks,
dispatches, changes between different PIDs within a run, and completed reaps.
These are debugging counters, not wall-clock or CPU percentage accounting.

## Verification

Build both variants and run `python tests/x86_64_boot.py`. The real separately
assembled `apps/x86_64/schedule.asm` produces `userspace/schedule.elf` and is
loaded through the unchanged ELF loader. Three instances spin without yielding,
repeatedly validate all 15 GPRs, RSP, CF/DF/IF, unique stack cookies and private
data at identical VAs. Kernel-stack canaries and every timer-entry RSP0 are checked.

The suite exercises 1203 user timer ticks and at least 1200 different-process
switches, bounded stop/resume, killing the active process, runnable/blocked kills,
duplicate enqueue/wake rejection, idle HLT/wake, exit and page/GP/UD/NM faults
with healthy peers, allocation-prefix failures through all creation stages,
injected insertion rejection, full process capacity and 100 mixed replacements
created while the dispatcher runs. PMM must return to baseline. Existing memory,
Ring 3 and ELF tests remain enabled at 16/64/256/5120/32768 MiB.

The verified 5 GiB run reports 1,201 different-process switches, 1,203 user timer
ticks and `0x13fd79` free frames both before and after the scheduler suite. The
i386 checkpoint build, 64/256 MiB desktop boots and 100-process stress pass;
its binary was unchanged at the scheduler checkpoint. The subsequent VFS
milestone changes shared filesystem code and verifies i386 regressions again.

Limits: one BSP, up to 1024 control slots, non-preemptible kernel/callbacks, legacy
PIC/PIT PC hardware and non-preemptible disk I/O. The later
[VFS launch milestone](VFS_LAUNCH.md) replaces embedded ELF sources with real
PollikFS files for both demos and regression fixtures.
No libc, dynamic linker, signals, fork, full exec or language runtime was started.
VFS launch and [read-only userspace file operations](FILE_ABI.md) are now
implemented. Process destruction also closes all owned descriptors. Stat/fstat metadata
is also implemented; see [STAT_ABI.md](STAT_ABI.md). Next exact milestone:
C2 crt0/C wrappers (not started). C1 working directories and directory enumeration are
implemented; see [DIRECTORY_ABI.md](DIRECTORY_ABI.md).
