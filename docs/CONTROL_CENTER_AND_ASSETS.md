# System assets, clearer controls and native release image

Date: 2026-10-06. This checkpoint keeps the working i386 desktop and prepares
the separate native x86-64 system; it does not claim complete desktop migration.

## What changed

| Item | State | Implementation / limitation |
| --- | --- | --- |
| Uniform icons | Implemented | 12 PNGs, 64×64 canvas, 56×56 alpha bounding box; same optical tile size within Desktop/Dock contexts |
| Icons outside the runtime kernel | Implemented | `/usr/share/icons/*.png`, decoded once at boot; native desktop uses its own userspace PNG cache |
| Installer assets | Verified | ATA and AHCI installs include all 12 exact icon files and both wallpapers |
| Control Center | Implemented | Click centered `Pollik OS`; 200ms cubic slide, reversible during animation; bounded overlay damage |
| Brightness | Implemented | Software framebuffer dimming, 20–100%; not a physical backlight driver |
| Audio | Implemented | Volume, PC speaker / detected AC'97 routing; unavailable outputs rejected |
| Wi-Fi / Bluetooth | Not implemented | No supported wireless/Bluetooth drivers; panel displays `No adapter` |
| Airplane mode | Unavailable | No supported radio devices; no fake toggle or fake connection state |
| Settings / About / context menu | Implemented | Larger primary text, fewer descriptions; matching rounded cards; menu rows increased 26→34px with hit tests updated |
| Separate applications | Native x86-64 only | 9 ELF64 files on PollikFS; rich i386 clients still run in Ring0 and remain in its kernel |
| Cleanup | Completed for generated assets and native release disk | Removed obsolete Calculator header; moved frozen indexed sprite tables into test fixtures; omitted malformed/test files from release disk |

Raster app/file icons are filesystem assets. Small UI action glyphs and cursors
remain renderer code/generated cursor data. Fonts remain compiled resources.
No syscall numbers, existing structures or PollikFS format changed.

## Files and cache

`assets/build_system_icons.py` reproducibly generates stock PNGs from the
existing supplied artwork and the original Calculator/PollikMark shapes.
The runtime cache validates signature, type, encoded size and 64×64 dimensions
before decoding. Missing/corrupt files get a small procedural fallback and one
serial summary. Drawing and theme changes do not decode PNGs again.

Normal runtime builds no longer reference `icons_index`, `desktop_icons_index`,
`calculator_icon` or `pollikmark_icon`. Installer payloads contain the PNGs
because they must install them; they are not used as compiled runtime icons.
`tests/fixtures/ui_data.h` preserves the original indexed-sprite golden tests.
Source/tests and personal files were not deleted as part of cleanup.

## Real data disk: non-destructive install

Commands actually executed:

```powershell
python tools/sync_system_files.py build/PollikData.img --manifest > build/icons-real-before.txt
python tools/sync_system_files.py build/PollikData.img
System sync: installed=12 changed_blocks=53 image_bytes=33587200; tail untouched
python tools/sync_system_files.py build/PollikData.img --manifest > build/icons-real-after.txt
```

The before/after manifest comparison actually reported:

```text
PASS real image manifest: only /usr/share/icons changed; unrelated file sizes and SHA256 identical
CHANGED 13 entries
```

Those entries are the icon directory and its 12 files. No format/migration was
performed. The existing validator and exclusive lock cover all writes. Tests
on copies retain the ENOSPC, damaged-tail, invalid-reference, legacy-geometry,
idempotence, live-QEMU lock and unrelated-file preservation assertions.

```powershell
python tests/system_file_sync.py
PASS parent directory growth: 16 existing entries and timestamp/reserved bytes preserved
PASS system-file sync preservation and refusal cases
```

Detailed logs: `build/icons-sync-evidence.txt`, `build/icons-real-before.txt`,
`build/icons-real-after.txt`.

## Actual framebuffer / controls evidence

```powershell
python tests/system_icons_gui.py --resolution 1920x1080 --accel whpx
PASS 12 filesystem icons: 196608 decoded RGBA bytes identical to PNG oracle; no embedded bitmap symbols
PIXEL brightness: at=(1870, 540) before=(254, 242, 212) after=(101, 96, 84) expected=(102, 97, 85)
PASS Control Center: PS/2 open/close, audio slider, brightness pixels, themed context menu/About

python tests/system_icons_gui.py --resolution 1024x768 --accel tcg
PASS 12 filesystem icons: 196608 decoded RGBA bytes identical to PNG oracle; no embedded bitmap symbols
PIXEL brightness: at=(974, 384) before=(253, 235, 225) after=(101, 94, 90) expected=(101, 94, 90)
PASS Control Center: PS/2 open/close, audio slider, brightness pixels, themed context menu/About

python tests/control_center_native.py
PASS Control Center: slide/reversal/scissor, sliders/clamps/deferred saves, unavailable hardware, Settings/About

python tests/held_drag.py
PASS brightness: 62208 channel vectors, max channel error 1, alpha preserved
PASS rect legacy/primitive pixel parity scene=29cdb7eb565afb73 hardware=17e73028b77840f0
```

Screenshots are actual QMP screendumps converted to PNG:
`build/control-center-1920x1080.png`, `build/control-center-1024x768.png`,
`build/context-menu-1920x1080.png`, `build/about-system-1920x1080.png`.
Brightness deliberately permits maximum error 1 per channel relative to
rounded `channel * percent / 100`. At 100%, the original copy path is unchanged.
The cursor uses the same output transform; saved-under pixels remain unmodified.
The PC speaker supports tone/mute, not continuous hardware gain or PCM audio.

## Native production profile

```powershell
./build-x86_64.ps1 -Production
Native system disk: PollikData-system.img; stock_files=65; user files preserved
Built build/x86_64/system/PollikOS-x86_64.img (424067 kernel bytes)
./run-x86_64.ps1
```

The launcher uses `build/x86_64/system/PollikData-system.img` and snapshots by
default. Normal `build-x86_64.ps1` and `-SelfTest` retain their previous test
variants. Existing release disks are validated and updated only at explicitly
listed stock paths; they are never automatically reformatted or purged.

Applications: `/bin/desktop.pol`, `files.pol`, `terminal.pol`, `notes.pol`,
`browser.pol`, `calculator.pol`, `windowdemo.pol`, `/bin/pollish`, `/bin/tcc`.
Window Demo is retained because the existing native desktop has that shortcut.
Headers/libraries required for compiling programs, browser welcome content,
wallpapers and icons are included. Broken ELF samples and `testdir` are omitted.

Cleaning the disk exposed an existing boot dependency: the ordinary kernel
halted at `[LAUNCH64] /bin/hello: not found`. The new release variant excludes
demo execution/modules and collects unreachable functions/data, including the
synthetic user payload. Existing test builds retain the demos.

```text
PASS native production apps: ELF64 x86-64 files on PollikFS 9
PASS clean profile: no malformed ELF, oversized fixture or testdir; filesystem invariants valid
```

```powershell
python tests/calculator_x64.py --kernel build/x86_64/system/PollikOS-x86_64.img --data build/x86_64/system/PollikData-system.img
PIXELS result 5 and 63: 5248 RGB pixels each identical to SDK font oracle
PASS native x86-64 calculator: shell, floating point, PS/2 keys/buttons, framebuffer, clean exit status 0
```

This harness now starts QEMU paused and attaches serial/QMP before continuing;
its initial unpaused attempt lost the early boot diagnostics. The captured
paused attempt exposed the actual missing-demo dependency rather than hiding it.

## Build, installation and regression evidence

```powershell
./build.ps1 -NoSync
USB installer built: build/PollikOS-USB-Installer.img (3952718 kernel bytes)
PollikOS built: build/PollikOS-Alpha.img (734588 kernel bytes, 1435 sectors loaded at 1 MiB)
./build.ps1 -NoSync -ImageName PollikOS-Surface.img
PollikOS built: build/PollikOS-Surface.img (734588 kernel bytes, 1435 sectors loaded at 1 MiB)

python tests/installer_wallpapers.py --backend ata
PASS ATA installer completed, stock files exact, installed target boots and decodes wallpaper
python tests/installer_wallpapers.py --backend ahci
PASS AHCI installer completed, stock files exact, installed target boots and decodes wallpaper

python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 5807 / [PROC] [TEST] PMM free pages after:  5807
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 54911 / [PROC] [TEST] PMM free pages after:  54911

python tests/smoke.py
PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock
PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping

python tests/resize_layout.py --skip-native
PASS 1024x768: all eight apps, 56 viewport-content stages, external guards
PASS 1920x1080: all eight apps, 56 viewport-content stages, external guards
python tests/surface_bounds.py --resolution 1920x1080
PASS: 1920x1080, all 8 surfaces maximize/restore, runtime LFB and external guards
python tests/corners_guards.py
PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption
```

Actual i386 binary: `build/kernel.bin`, **829368 → 734588 bytes** (-94780).
Actual installer binary: `build/install/kernel.bin`, **3952718 bytes**.
These are not ELF or disk-image sizes.

```powershell
./build-x86_64.ps1
Built build/x86_64/kernel/PollikOS-x86_64.img (440451 kernel bytes)
./build-x86_64.ps1 -SelfTest
Built build/x86_64/selftest/PollikOS-x86_64.img (530675 kernel bytes)
python tests/x86_64_boot.py
PASS: selftest-qemu64-16
PASS: selftest-qemu64-64
PASS: selftest-qemu64-256
PASS: selftest-qemu64-5120
PASS: selftest-qemu64-32768
PASS: selftest-qemu64-64-reboot1
PASS: selftest-qemu64-64-reboot2
PASS: selftest-qemu64-64-full
python tests/x86_64_storage.py
PASS: missing/corrupt/unsupported/truncated storage refused without image changes
```

Actual guest serial lines from `build/x86_64/selftest-qemu64-16.log`:

```text
[MM64] balance before=0x0000000000000d7b after=0x0000000000000d7b
[USER64] balance before=0x0000000000000d7b after=0x0000000000000d7b
[ELF64] balance before=0x0000000000000d6a after=0x0000000000000d6a
[SCHED64] balance before=0x0000000000000d6e after=0x0000000000000d6e
[X64] SELFTEST PASS
```

32GiB serial: `[MM64] balance before=0x00000000007ff635 after=0x00000000007ff635`
and `[X64] SELFTEST PASS`. The full boot command exited 0, including its native
interactive console/TinyCC workflow. Logs: `build/system-x64-boot.txt` and
`build/system-x64-storage.txt`.

```powershell
python tests/gui_registry.py
PASS: gui registry (19393 checks)
python tests/app_layout.py
PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping, Calculator (201711 checks)
python tests/browser_cooperative.py
PASS: 972 cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input
python tests/browser_responsive.py --resolution 1024x768
PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies
python tests/browser_responsive.py --resolution 1920x1080
PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies
```

The real `PollikData.img` was also booted with a read-only QEMU snapshot,
without login: `[ICONS] PollikFS loaded=12 fallback=0`.
Production refresh on a copy containing a personal file reported:
`PASS production refresh on copy: user file preserved; second update byte-identical SHA256=dbc7912ad85edf686a96f75345038ae4eb280176c15d3fc45bf32acb5bd74824`.

## Assertion updates and limits

- Registry bitmap-byte assertions moved from compiled palette arrays to the
  filesystem PNG/cache oracle. The registry instead requires all icon pointers
  null and every canvas exactly 64×64; routing/body/minimum/ABI tests remain.
- Indexed-sprite compositor tests keep their original frozen tables, inputs and
  pixel hashes. No assertions were removed from `held_drag`.
- System-sync expected additions include `/usr/share/icons/` and the 12 files;
  original-file/inode/directory-entry/tail preservation checks remain exact.
- The previous Calculator addition made `/bin` contain 55 fixtures. The old
  `path_test.c` assertion required 54 (its diagnostic still expected 50), causing
  `[X64] FAIL: disk files visible through VFS`. Both generated disk manifests
  prove exactly 55 entries including `calculator.pol`; the assertion/diagnostic
  now require 55, not a lower bound. This is a corrected outdated assertion.
- No smoke assertions or existing app pixel expectations were weakened.
- Wi-Fi connection/scanning, Bluetooth pairing, radio airplane mode, physical
  backlight/audio validation and the complete rich desktop userspace migration
  are **NOT DONE / NOT RUN**. There are no simulated success paths for them.
