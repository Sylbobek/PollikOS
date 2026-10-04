# PollikOS Setup and local login

The graphical i386 target now contains a first-run setup flow. On a valid
PollikFS v2 data disk without an account it appears before the desktop and:

1. asks for a local user name;
2. asks for a password and confirmation;
3. creates `/home/<user>`;
4. writes the local account database to `/etc/account.db`;
5. writes `/etc/pollikos-installed` only after setup completes.

Every later boot is held at the sign-in screen until the stored password is
accepted. The session can also be locked from **Settings > General > Lock**.

Passwords are never stored as text. The account record contains a unique
16-byte salt, an 8,192-round SHA-256 derived value, and the KDF round count.
Comparison is constant-time. The first implementation supports one local
account; PollikFS does not yet contain per-file owners or permission bits, so
the login protects access to the desktop session but is not yet a multi-user
file-permission boundary.

## Bootable USB installation

The build produces `build/PollikOS-USB-Installer.img`. Write this raw image to
a USB drive with Rufus in DD/raw-image mode, enable Legacy BIOS/CSM boot on the
target computer, and boot from the USB drive. The installer kernel carries a
separate normal-runtime payload in memory; it does not need to read files from
the USB device after the BIOS has loaded it.

The installer prefers a detected AHCI/SATA disk and falls back to the primary
ATA/IDE disk. It writes the PollikOS BIOS boot sector, stage 2 and normal
kernel, then creates PollikFS on the same disk at the fixed 8 MiB offset. It
creates the local account before reporting completion. After **Installation
complete**:

1. choose **Shut down**;
2. remove the USB drive;
3. start the computer from the installed disk;
4. sign in with the password created during installation.

The regular QEMU layout remains compatible: `PollikOS-Alpha.img` can continue
to use the separate `PollikData.img` data disk. At boot PollikOS checks that
legacy layout first, then the single-disk installed layout.

## Disk safety

Installation is destructive and requires two separate confirmations. It does
not silently format a failed mount. The removable-media installer only accepts
a detected AHCI/SATA disk or the primary ATA target with enough sectors for the
runtime, PollikFS and legacy desktop storage. Existing build-time backup and
explicit `-FormatData` guards remain in place for `PollikData.img`.

Do not use the installer on a computer containing data you need. It currently
uses one SATA disk exposed by an AHCI controller on PCI bus 0, or one ATA target,
and has no partition-preserving mode. UEFI-only boot, NVMe, Secure Boot and GPT
installation are not implemented. Physical USB boot depends on firmware Legacy
BIOS support. The complete install, remove-media and reboot flow is verified in
QEMU for both ATA and ICH9 AHCI/SATA targets; physical hardware is not yet
verified.

## Build and run

Build and start PollikOS normally:

```powershell
.\build.ps1
.\run.ps1
```

For Rufus use `build/PollikOS-USB-Installer.img`, not the normal QEMU image.
Use a disposable target disk when testing the destructive path.

## Terminal and C compiler

The graphical i386 desktop terminal includes file and directory commands
(`pwd`, `cd`, `ls`/`dir`, `cat`/`type`, `stat`, `mkdir`, `touch`, `rm`/`del`,
`rmdir`, `mv`/`rename`, `cp`), note commands (`new`, `open`, `save`), system,
network and desktop tools (`ps`, `tasks`, `free`, `mem`, `uptime`, `time`,
`date`, `lspci`, `beep`, `net`, `ping`, `publicip`, `perf`, `theme`, `anim`,
`reboot`, `shutdown`) and a 32 KiB scrollback. Up/Down recalls up to 64
commands for the current session; `history` lists them oldest to newest and
`history -c` clears that list.

The x86_64 console additionally includes native TinyCC as `/bin/tcc`. It can
compile, link and run C programs stored in PollikFS. The graphical installer
currently installs the i386 desktop, so it cannot run the ELF64 TinyCC binary;
the GUI's `tcc --help` shows examples, but compilation requires booting the
x86_64 console. The PollikOS port currently supports integer C and does not
support floating-point operations or `-m32`/`-m64` cross-compilation.
