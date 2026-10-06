# PollikOS

A hobby operating system written from scratch in C and NASM. No Linux, no GRUB, no host libc: custom bootloader, kernel, filesystem, network stack, windowing system, C library and a self-hosted C compiler.

> **Status: alpha, QEMU only.** It has never been verified on physical hardware. Expect bugs, breaking changes and unfinished parts. See [Known limitations](#known-limitations).

## Two targets

| | i386 (legacy desktop) | x86_64 (the future) |
|---|---|---|
| Boot | BIOS, own boot sector + stage 2 | BIOS, 64-bit long mode |
| Userspace | ELF32 in Ring 3; GUI apps still run inside the kernel | ELF64 in Ring 3 under a preemptive scheduler |
| Desktop | Full desktop, dock, windows, browser, USB installer | Native `.pol` apps (desktop, terminal, files, browser) |
| Memory | Up to 1 GiB direct-mapped | 4-level paging, tested up to 32 GiB RAM |
| Storage | ATA PIO, AHCI/SATA | ATA PIO |

The long-term plan is to make x86_64 the main target and retire the i386 desktop.

## Features

**Kernel (both targets)**
- Own bootloader, GDT/IDT, paging, physical memory manager (E820), kernel heap
- PollikFS v2: a small own filesystem with directories, inodes and a persistent account database
- Local login with salted, iterated SHA-256 password hashing

**i386 desktop**
- Software-rendered compositor, rounded windows, dock, animations, own vector font (Pollik Sans)
- PollikOS Web: HTTPS via BearSSL, HTML/CSS layout, a subset of JavaScript (Elk)
- Intel AC'97 audio, RTL8139 networking (ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP), AHCI
- Bootable USB installer image (ATA and AHCI targets verified in QEMU)
- PollikMark: a built-in 2D/3D software-rendering benchmark

**x86_64**
- SYSCALL entry, static ELF64 loader, per-process address spaces, guarded stacks
- Preemptive round-robin scheduler, `spawn`/`waitpid`, pipes, process groups, basic signals
- Isolated x87/SSE2 state per process
- C SDK: crt0, a small libc (`stdio`, `malloc`, `math`, ...), `pollikcc` driver
- **Native TinyCC** running inside PollikOS: compile, link and run C programs in the guest
- `pollish` shell with line editing, history, pipes and redirection

## Quick start (Windows host)

Requirements on `PATH`: `nasm`, `clang`, `ld.lld`, `llvm-objcopy`, `llvm-ar`, `qemu-system-x86_64`, Python 3.

```powershell
# i386 desktop
.\build.ps1
.\run.ps1                       # use -Accel whpx if Windows Hypervisor Platform is enabled

# x86_64 target
.\build-x86_64.ps1
```

Compile a C program for PollikOS:

```powershell
.\sdk\tools\pollikcc.ps1 sdk\examples\hello.c -o hello.pol
```

## Tests

Tests boot real images in QEMU on disposable disks and never touch your data disk.

```powershell
python tests\x86_64_boot.py        # 16 MiB - 32 GiB guests, scheduler, files, console, TinyCC
python tests\x86_64_storage.py     # damaged / unsupported disks are refused
python tests\process_stress.py --ram 64 256
python tests\smoke.py --notes-only
```

The x86_64 suites check that physical memory, file handles, process slots and zombies return exactly to baseline after every phase, including failure injection on allocation paths.

## Known limitations

- Only tested in QEMU; physical hardware is unverified
- Single CPU (no SMP); the kernel is non-preemptible
- PollikFS: one local user, no permissions, small fixed geometry, **not crash-consistent** (lost sectors can leave counters and bitmap inconsistent)
- GUI is software-rendered without GPU acceleration or VSync; performance is limited, especially under pure emulation (TCG)
- x86_64 target does not yet have AHCI, a graphical installer or all i386 desktop apps
- No `fork`/`exec`, threads, dynamic linking, AVX or `long double`
- BIOS/legacy boot only (no UEFI, NVMe or GPT)
- Browser: subset of HTML/CSS/JS, no HTTP/2, no multi-process isolation

## Repository layout

```
boot/               BIOS boot sector and stage 2
kernel/             i386 kernel, desktop, network stack, browser
kernel/arch/x86_64/ 64-bit kernel, scheduler, memory manager, syscalls
sdk/                C SDK for x86_64 (crt0, libc, headers, pollikcc)
third_party/        BearSSL, Elk, stb_image, TinyCC (see licences)
tests/              QEMU and host-native test suites
tools/              image and filesystem builders
fonts/ assets/      Pollik Sans generator, UI assets
```

Design notes and ABI contracts live next to the code (`ARCHITECTURE.md`, `kernel/arch/x86_64/*.md`).

## Licence

Choose a licence for the PollikOS code and add a `LICENSE` file. Third-party components keep their own licences:
BearSSL (MIT), Elk (see its repository), stb_image (public domain / MIT), TinyCC (LGPL-2.1).