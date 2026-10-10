# PollikOS architecture — x86_64 only

The repository contains one operating system: the native x86_64 C/NASM kernel
and static ELF64 userspace. See [migration report](docs/migration/X64_ONLY.md)
and [source inventory](docs/migration/X64_INVENTORY.json) for removed components
and verification. Older checkpoint documents describe historical versions.

## Boot and memory

`boot/boot.asm` and `boot/stage2.asm` retain BIOS real-mode/protected-mode loading.
`kernel/arch/x86_64/entry.asm` checks LM/NX/PAE/MSR/SYSCALL and enters long mode.
The ELF64 kernel is linked at physical/bootstrap address 0x100000; its early
text is in the low supervisor mapping. Dynamic kernel allocations start at
`MM_KERNEL_START=0xffffff8000000000`. Private userspace occupies
`[0x8000000000,0x800000000000)`. PMM/VMM support frames and page tables above
4 GiB, DMA32, NX/WP, ownership/rollback and guard pages. Physical aperture access
and kernel memory APIs are serialized on one BSP with interrupts disabled.

## Processes and ABI

`process.c` owns PID lifecycle/resources; `scheduler.c` owns execution queues.
Each Process64 embeds one Thread64 with guarded stacks and x87/SSE2 state.
There are up to 1024 RAM-scaled process slots and 128 descriptors per process.
Static ELF64/ET_EXEC images are bounded to 4 MiB and 1024 mapped image pages.
Spawn/wait, pipes, signals, cwd, descriptor rights and deterministic reaping
remain implemented. No fork, image replacement, SMP or additional user threads.

SYSCALL is primary; INT 0x81 remains a transitional x86_64 test transport.
`include/pollikos_abi.h` describes ABI 1.1; operation numbers come from
`kernel/arch/x86_64/user_abi.h`. Reserved historical IDs are not executable APIs.
There is no ELF32 loader, int 0x80 dispatcher or separate application SDK.
Public ABI numbers and layouts were retained during removal of the old target.

## Storage, security and devices

The single storage stack uses `kernel/storage.c`, VFS, PollikFS and BlockDevice.
The native adapter mounts an ATA primary-slave data disk; it never falls back
to writing the boot disk. PollikFS v2 keeps its 32 MiB/512-inode geometry and
60-byte inode/64-byte dirent formats. The metadata redo journal validates CRC
and recovery ranges; corruption falls back to read-only. File data replacement
is not made wholly atomic by the metadata journal.

`account.c` provides the compatible 128-byte account record, Argon2id and legacy
password verification. Kernel-owned credentials/session/capabilities protect
VFS, windows, networking and administration. `auth64.c` handles login, lock,
logout and authenticated elevation. There is one local account, not per-inode ACLs.

Shared HAL, RTL8139/TCP/TLS, audio, USB and filesystem modules use native x86_64
adapters only. AC'97/USB require DMA32 allocations. AHCI and the old physical
installer had no native caller and were removed rather than leaving a second OS.
No destructive physical-disk install operation is provided by the launchers.

## Desktop and toolchain

`window.c`/`console_fb.c` compose process-owned surfaces. Desktop, Terminal,
Files, Notes, Settings, Browser, Calculator and media run as `.pol` ELF64 programs.
Browser parser/CSS/layout/render sources are retained in `kernel/browser/` but
compiled into userspace; the Ring 0 Browser host and GUI were removed. HTTP is
in SDK userspace; TCP/TLS remain in the kernel. Rendering is software-based.

The SDK has crt0/libc and an explicit PollikOS linker/ABI. TinyCC is the existing
native port, now vendored from its pinned source commit so clean checkouts contain
it. Upstream compiler/codec architecture definitions are library implementation
code, not additional PollikOS OS targets.

## Build and tests

`build.ps1` is the x86_64 wrapper; developer, SELFTEST and Production variants
remain separate. `run.ps1` launches only x86_64 with snapshots by default.
Existing private system disks are never synchronized or formatted during build.
CI runs the same commands as `tools/ci.ps1`; `-Extended` adds the full RAM boot
matrix, sessions, concurrent networking, console and USB regressions.
Test coverage and limits are stated by actual outcomes in X64_ONLY.md.
