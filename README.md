# PollikOS

PollikOS is an independent experimental **x86_64-only** operating system with
its own C/NASM kernel, legacy BIOS boot, Ring 3 ELF64 `.pol` applications and
PollikFS v2. It does not use Linux or BSD. Required 16/32-bit BIOS transition
code remains; there is no separate 32-bit OS, ELF32 loader or application API.

## Build and run (Windows)

Required on PATH: NASM, Clang/LLVM (`ld.lld`, `llvm-ar`, `llvm-objcopy`,
`llvm-readobj`, `llvm-nm`), QEMU x86_64, Python 3, and Perl (Git for Windows
supplies it). Python image generators use Pillow and fontTools. Third-party
sources, including the PollikOS TinyCC port, are checked into this repository.

```powershell
# Production kernel and curated disk; run uses snapshots by default.
.\build.ps1 -Production
.\run.ps1

# Developer kernel with regression fixtures.
.\build.ps1
.\run.ps1 -Variant kernel

# Kernel self-tests.
.\build.ps1 -SelfTest
python tests\kernel_checkpoint.py

# Complete CI, including the high-memory boot matrix.
.\tools\ci.ps1 -Extended
```

`build.ps1` delegates to `build-x86_64.ps1`; both build only ELF64/x86_64.
Artifacts live under `build/x86_64/{kernel,selftest,system}/` with the canonical
boot name `PollikOS-x86_64.img`. Existing `PollikData-system*.img` are preserved
without sync, format or migration. Updating files in an existing private disk
is a separate explicit operation. `run.ps1` delegates to the x86_64 launcher;
`-PersistData` explicitly enables persistent writes for that VM session.

## Implemented components

- PMM/VMM with 64-bit physical addresses, high RAM, DMA32, NX/WP and guarded stacks.
- Static ELF64 loader, preemptive user scheduler, spawn/wait, pipes, signals,
  process-owned descriptors, capability checks and cleanup after exit.
- Native Desktop, Terminal, Files, Notes, Settings, Browser, Calculator and media
  applications. Window composition remains in the kernel; applications run in Ring 3.
- C SDK/crt0/libc, `pollish`, native TinyCC compile/link/run and a writable sysroot.
- PollikFS v2 with compatible geometry, metadata journal/CRC/recovery, read-only
  corruption handling, persistent accounts, Argon2id, sessions and administration.
- ATA PIO, RTL8139 IPv4/TCP/TLS, AC'97/PC speaker, PS/2 and USB HID adapters.
  HTTP is implemented in userspace. Browser uses QuickJS and NetSurf parser/libcss
  libraries with a PollikOS renderer; it is not a complete modern Web platform.

## Limits

One CPU, a non-preemptible kernel and one thread per process; no SMP, UEFI,
NVMe, GPU acceleration, dynamic linker, fork/exec or multi-user ACLs. The current
filesystem has 32 MiB gross capacity even in the sparse 30 GiB VM disk profile.
The legacy-only AHCI driver and physical installer were retired with the old OS;
no x86_64 functionality depended on them. Physical hardware is not certified.

[Architecture](ARCHITECTURE.md), [migration and real test results](docs/migration/X64_ONLY.md),
[inventory](docs/migration/X64_INVENTORY.json), [SDK](sdk/README.md).
Historical checkpoint documents are evidence of earlier versions, not current
build instructions or a second supported target.

## Licence

PollikOS code is AGPL-3.0-only; see [LICENSE](LICENSE).
[Third-party licences](third_party/README.md) remain unchanged.
