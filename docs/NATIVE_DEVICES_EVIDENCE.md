# Native devices: raw evidence

2026-10-06. No existing test assertion was removed. Physical USB/WLAN/Bluetooth/HDA/USB-audio tests: **NOT RUN**.

## powershell -File build.ps1 -NoSync

```text
COMMAND powershell -File build.ps1 -NoSync
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (736024 kernel bytes, 1438 sectors loaded at 1 MiB)
```

Full log: `build/drivers-i386-build.log`.

## powershell -File build.ps1 -NoSync -ImageName PollikOS-Surface.img

```text
COMMAND powershell -File build.ps1 -NoSync -ImageName PollikOS-Surface.img
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Surface.img (736024 kernel bytes, 1438 sectors loaded at 1 MiB)
```

Full log: `build/drivers-surface-build.log`.

## powershell -File build-x86_64.ps1 -Production

```text
COMMAND powershell -File build-x86_64.ps1 -Production
Built build/x86_64/system/PollikOS-x86_64.img (428179 kernel bytes)
```

Full log: `build/drivers-x64-build.log`.

## powershell -File build-x86_64.ps1

```text
COMMAND powershell -File build-x86_64.ps1
Built build/x86_64/kernel/PollikOS-x86_64.img (444563 kernel bytes)
```

Full log: `build/drivers-normal-build.log`.

## powershell -File build-x86_64.ps1 -SelfTest

```text
COMMAND powershell -File build-x86_64.ps1 -SelfTest
Built build/x86_64/selftest/PollikOS-x86_64.img (538883 kernel bytes)
```

Full log: `build/drivers-selftest-build.log`.

## python tests/devices_x64.py --ram 64 5120

```text
COMMAND python tests/devices_x64.py --ram 64 5120
DEVICES caps=57 output_mask=3 framebuffer=1024x768 bpp=24 pitch=3072
AIRPLANE packets tx=15->15 rx=13->13; new/old HTTP EIO
PASS native device ABI: Ethernet reconnect/HTTP, airplane, audio selection/tone, bounds, framebuffer, unsupported radios
PIXEL airplane enabled (342,321)=(104, 84, 191)
PIXEL volume dragged to 100 (622,454)=(167, 151, 240)
PIXEL native panel (330,389)=(37, 42, 59); output row (342,489)=(52, 57, 77); screenshot=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\devices-64-ac97.png
CAPTURE original RIFF/data sizes=0/0; PCM payload_bytes=1634880
WAV devices-64-ac97.wav: channels=2 frames=408720 nonzero_bytes=41952
PASS devices guest RAM=64 MiB audio=True: real HTTP, airplane packet balance, native panel pixels
DEVICES caps=57 output_mask=3 framebuffer=1024x768 bpp=24 pitch=3072
AIRPLANE packets tx=15->15 rx=13->13; new/old HTTP EIO
PASS native device ABI: Ethernet reconnect/HTTP, airplane, audio selection/tone, bounds, framebuffer, unsupported radios
PIXEL airplane enabled (342,321)=(104, 84, 191)
PIXEL volume dragged to 100 (622,454)=(167, 151, 240)
PIXEL native panel (330,389)=(37, 42, 59); output row (342,489)=(52, 57, 77); screenshot=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\devices-5120-ac97.png
CAPTURE original RIFF/data sizes=0/0; PCM payload_bytes=1536948
WAV devices-5120-ac97.wav: channels=2 frames=384237 nonzero_bytes=41952
PASS devices guest RAM=5120 MiB audio=True: real HTTP, airplane packet balance, native panel pixels
```

Full log: `build/devices-x64-test.log`.

## python tests/devices_x64.py --ram 128 --no-audio

```text
COMMAND python tests/devices_x64.py --ram 128 --no-audio
DEVICES caps=49 output_mask=1 framebuffer=1024x768 bpp=24 pitch=3072
AIRPLANE packets tx=14->14 rx=12->12; new/old HTTP EIO
PASS native device ABI: Ethernet reconnect/HTTP, airplane, audio selection/tone, bounds, framebuffer, unsupported radios
PIXEL airplane enabled (342,321)=(104, 84, 191)
PIXEL volume dragged to 100 (622,454)=(167, 151, 240)
PIXEL native panel (330,389)=(37, 42, 59); output row (342,489)=(104, 84, 191); screenshot=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\devices-128-speaker.png
PASS devices guest RAM=128 MiB audio=False: real HTTP, airplane packet balance, native panel pixels
```

Full log: `build/devices-noaudio.log`.

## python tests/x86_64_storage.py

```text
COMMAND python tests/x86_64_storage.py
PASS: neither kernel variant embeds userspace ELF files
PASS: kernel-qemu64-64-bad-signature
PASS: kernel-qemu64-64-old-geometry
PASS: kernel-qemu64-64-bad-directory-name
PASS: kernel-qemu64-64-bad-directory-inode
PASS: kernel-qemu64-64-bad-direct-block
PASS: kernel-qemu64-64-bad-indirect-entry
PASS: kernel-qemu64-64-overflow-inode-size
PASS: kernel-qemu64-64-small-disk
PASS: kernel-qemu64-64-no-disk
PASS: missing/corrupt/unsupported/truncated storage refused without image changes
```

Full log: `build/drivers-x64-storage.log`.

## python tests/gui_registry.py

```text
COMMAND python tests/gui_registry.py
PASS: metadata and optional callbacks
PASS: invalid IDs are no-ops
PASS: initialization
PASS: render adapters
PASS: key mapping (all 256 codes, Shift/Control)
PASS: click, scroll and cursor routing/bounds
PASS: open/close/resize
PASS: poll bitmask and reset
PASS: gui registry (19393 checks)
```

Full log: `build/drivers-registry.log`.

## python tests/app_layout.py

```text
COMMAND python tests/app_layout.py
PASS: real app geometry, hit bounds, file scrolling, Notes wrapping/cursor, Terminal prompt wrapping, Calculator (201711 checks)
```

Full log: `build/drivers-app-layout.log`.

## python tests/held_drag.py

```text
COMMAND python tests/held_drag.py
PASS brightness: 62208 channel vectors, max channel error 1, alpha preserved
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
PIXEL_HASH scene=29cdb7eb565afb73 hardware=17e73028b77840f0
PASS brightness: 62208 channel vectors, max channel error 1, alpha preserved
PASS held-drag frame pixels identical to full-repaint oracle (29 positions)
PASS accumulated held-drag frames, shadow-only cull, dock cache, resize, dim controls
PASS cursor union presents, kind changes, corners, cursor-free scene
PASS all primitive scissors and padded target stride
PASS 24/32-bpp framebuffer channel order, padded pitch, partial rows
PASS independent four-corner RGB/AA, no canary RGB, opaque black, all screen edges/scissors, resize-before-render
PASS independent LUT radii 1..29, rounded shadow/dock colors (18/29), clipping and padded stride
PIXEL_HASH scene=29cdb7eb565afb73 hardware=17e73028b77840f0
PASS rect legacy/primitive pixel parity scene=29cdb7eb565afb73 hardware=17e73028b77840f0
```

Full log: `build/drivers-held-drag.log`.

## python tests/soft3d_native.py

```text
COMMAND python tests/soft3d_native.py
soft3d native: PASS (matrices, depth, culling, clipping, bounds, texture)
```

Full log: `build/drivers-soft3d.log`.

## python tests/pollikmark_native.py

```text
COMMAND python tests/pollikmark_native.py
RAW live resize: changes=40 allocations=0 samples=2 running=1
pollikmark native: PASS (rotation, raster counters/ABI, mixed shapes, clipping, alpha, resize/failure/leaks, detailed info text bounds)
```

Full log: `build/drivers-pollikmark.log`.

## python tests/process_stress.py --ram 64 256

```text
COMMAND python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 5807 / [PROC] [TEST] PMM free pages after:  5807
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 54911 / [PROC] [TEST] PMM free pages after:  54911
```

Full log: `build/drivers-process-stress.log`.

## python tests/smoke.py --notes-only

```text
COMMAND python tests/smoke.py --notes-only
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override
PASS: pointer acceleration can be disabled and persists
```

Full log: `build/drivers-smoke-notes.log`.

## python tests/smoke.py

```text
COMMAND python tests/smoke.py
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock
PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping
```

Full log: `build/drivers-smoke.log`.

## python tests/network_ring.py

```text
COMMAND python tests/network_ring.py
PASS: 300 Ethernet ARP requests/replies, including two DMA ring wraps
```

Full log: `build/drivers-network-ring.log`.

## python tests/system_icons_gui.py --resolution 1024x768 --accel tcg

```text
COMMAND python tests/system_icons_gui.py --resolution 1024x768 --accel tcg
PASS 12 filesystem icons: 196608 decoded RGBA bytes identical to PNG oracle; no embedded bitmap symbols
PIXEL brightness: at=(974, 384) before=(253, 235, 225) after=(101, 94, 90) expected=(101, 94, 90)
PASS Control Center: PS/2 open/close, audio slider, brightness pixels, themed context menu/About
```

Full log: `build/devices-i386-gui.log`.

## python tests/system_icons_gui.py --resolution 1920x1080 --accel whpx

```text
COMMAND python tests/system_icons_gui.py --resolution 1920x1080 --accel whpx
PASS 12 filesystem icons: 196608 decoded RGBA bytes identical to PNG oracle; no embedded bitmap symbols
PIXEL brightness: at=(1870, 540) before=(254, 242, 212) after=(101, 96, 84) expected=(102, 97, 85)
PASS Control Center: PS/2 open/close, audio slider, brightness pixels, themed context menu/About
```

Full log: `build/drivers-i386-gui-1920.log`.

## python tests/surface_bounds.py --resolution 1920x1080

```text
COMMAND python tests/surface_bounds.py --resolution 1920x1080
PASS: 1920x1080, all 8 surfaces maximize/restore, runtime LFB and external guards
```

Full log: `build/drivers-surface-bounds.log`.

## python tests/corners_guards.py --resolution 1024x768

```text
COMMAND python tests/corners_guards.py --resolution 1024x768
PASS boot: ABI36, 8 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS maximized: ABI36, 8 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=1024x640
PASS restored: ABI36, 8 prefix/suffix guards, pixels=base+4 bytes, capacity=786432, window=680x410
PASS registry FULL WINDOW resize dimensions track maximize/restore; no guard corruption
```

Full log: `build/drivers-corners.log`.

## python tests/resize_layout.py --resolution 1024x768

```text
COMMAND python tests/resize_layout.py --resolution 1024x768
PASS 1024x768 Settings maximized [0, 32, 1024, 640]
PASS 1024x768 Settings restored [8, 39, 640, 520]
PASS 1024x768 Browser 480x280 [8, 39, 480, 280]
PASS 1024x768 Browser 640x480 [8, 39, 640, 480]
PASS 1024x768 Browser 800x600 [8, 39, 800, 600]
PASS 1024x768 Browser 1008x623 [8, 39, 1008, 623]
PASS 1024x768 Browser rapid-held-resize-final [8, 39, 640, 480]
PASS 1024x768 Browser maximized [0, 32, 1024, 640]
PASS 1024x768 Browser restored [8, 39, 640, 480]
PASS 1024x768 PollikMark3D 480x280 [8, 39, 480, 280]
PASS 1024x768 PollikMark3D 640x480 [8, 39, 640, 480]
PASS 1024x768 PollikMark3D 800x600 [8, 39, 800, 600]
PASS 1024x768 PollikMark3D 1008x623 [8, 39, 1008, 623]
PASS 1024x768 PollikMark3D rapid-held-resize-final [8, 39, 640, 480]
PASS 1024x768 PollikMark3D maximized [0, 32, 1024, 640]
PASS 1024x768 PollikMark3D restored [8, 39, 640, 480]
PASS 1024x768 Calculator 480x280 [8, 39, 480, 440]
PASS 1024x768 Calculator 640x480 [8, 39, 640, 480]
PASS 1024x768 Calculator 800x600 [8, 39, 800, 600]
PASS 1024x768 Calculator 1008x623 [8, 39, 1008, 623]
PASS 1024x768 Calculator rapid-held-resize-final [8, 39, 640, 480]
PASS 1024x768 Calculator maximized [0, 32, 1024, 640]
PASS 1024x768 Calculator restored [8, 39, 640, 480]
PASS 1024x768: all eight apps, 56 viewport-content stages, external guards
```

Full log: `build/drivers-resize.log`.

## python tests/browser_cooperative.py

```text
COMMAND python tests/browser_cooperative.py
PASS: 972 cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input
```

Full log: `build/drivers-browser-cooperative.log`.

## python tests/browser_responsive.py --resolution 1024x768

```text
COMMAND python tests/browser_responsive.py --resolution 1024x768
PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies
```

Full log: `build/drivers-browser-responsive.log`.

## python tests/browser_responsive.py --resolution 1920x1080

```text
COMMAND python tests/browser_responsive.py --resolution 1920x1080
PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies
```

Full log: `build/drivers-browser-responsive-1920.log`.

## network-gate.exe

```text
COMMAND clang -D_CRT_SECURE_NO_WARNINGS -O2 -fno-builtin -Wall -Wextra -Werror tests/network_gate_native.c -o build/network-gate.exe
COMMAND build/network-gate.exe
PASS Ethernet gate: disabled cached interface writes=0 tx=0; enabled tx=1
```

## audio-driver.exe

```text
COMMAND clang -O2 -fno-builtin -Wall -Wextra -Werror tests/audio_driver_native.c -o build/audio-driver.exe
COMMAND build/audio-driver.exe
PASS AC97: DMA rollback exact, unsupported PCI rejected, high-frequency tone, bounded stuck reset
```

## audio-dma.exe

```text
COMMAND clang -O2 -Wall -Wextra -Werror tests/audio_dma_native.c -o build/audio-dma.exe
COMMAND build/audio-dma.exe
PASS x64 DMA32: PMM failure, borrowed-map rollback, two-slot limit, free frames/mappings exactly baseline
```

## framebuffer-mode.exe

```text
COMMAND clang -O2 -Wall -Wextra -Werror tests/framebuffer_mode_native.c -o build/framebuffer-mode.exe
COMMAND build/framebuffer-mode.exe
PASS framebuffer geometry: 8 valid/pitch vectors; reject 31bpp and swapped RGB masks
```

## framebuffer-rollback.exe

```text
COMMAND clang -O2 -Wall -Wextra -Werror tests/framebuffer_rollback_native.c -o build/framebuffer-rollback.exe
COMMAND build/framebuffer-rollback.exe
PASS framebuffer failure injection: 12 borrowed-map failures, mappings returned exactly to baseline
```

## control-center.exe

```text
COMMAND clang -O2 -fno-builtin -Wall -Wextra -Werror tests/control_center_native.c -o build/control-center.exe
COMMAND build/control-center.exe
PASS Control Center: slide/reversal/scissor, sliders/clamps/deferred saves, unavailable hardware, Settings/About
```

## Actual kernel binary sizes

```text
COMMAND Get-Item build/kernel.bin,build/x86_64/kernel/kernel.bin,build/x86_64/selftest/kernel.bin,build/x86_64/system/kernel.bin | Select FullName,Length
C:\Users\syltu\Desktop\PollikOS\build\kernel.bin 736024
C:\Users\syltu\Desktop\PollikOS\build\x86_64\kernel\kernel.bin 444563
C:\Users\syltu\Desktop\PollikOS\build\x86_64\selftest\kernel.bin 538883
C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\kernel.bin 428179
```

i386 before this work: `build/kernel.bin` **734588 B**, measured before the first build; after **736024 B**, increase **1436 B**. This is the objcopy binary, not an ELF or disk image. Both the already-tested i386 build and its final rebuild have the same SHA-256:

```text
E89C202FBEB4894AE59BED94E2AFF6EDF5C48739CF60E3437306C5D3574F84E1
```

## Initial failures retained

The first full `python tests/x86_64_boot.py` run passed 16/64/256/5120 MiB and stopped at 32768 MiB:

```text
[X64] FAIL: full queue drains
```

Raw logs: `build/drivers-x64-boot-first.log`, `build/x86_64/selftest-qemu64-32768-first-driver-failure.log`. Scheduler code and its assertion/budget were not changed. An isolated 32768 MiB boot subsequently completed with `[X64] SELFTEST PASS`; the cause of the first failure is **unconfirmed**, and it is not claimed fixed by a retry. Polling the audio driver was scoped to network initialisation; uninitialised self-test boots no longer poll it.

One intermediate host build invocation also stopped while llvm-nm inspected windowdemo.elf:

```text
pollikcc: undefined symbols in build/x86_64/selftest/userspace/windowdemo.elf:
C:\Users\syltu\scoop\apps\llvm\current\bin\llvm-nm.exe: build/x86_64/selftest/userspace/windowdemo.elf:
```

The final standalone PowerShell build completed. Cause of that intermediate host-tool failure: **unconfirmed**; no validation assertion was removed.

## Data safety

All i386 builds used `-NoSync`. Tests used disposable/snapshot disks; no test attached `build/PollikData.img`. The initial whole-image hash read was **NOT RUN** because a running process locked it. Two later unlocked read-only observations produced:

```text
COMMAND Get-FileHash -Algorithm SHA256 build/PollikData.img
BE3C79F3F1E2EE87F0C4F2C184161F4C332F74FA0B0EDB3EBD0FBE7547824C59
```

This is not a claim of a full-session before/after hash while the user was using the disk. Installer ATA/AHCI end-to-end runs in this driver checkpoint: **NOT RUN** (installer source was not changed).

## Final x86-64 suite

The final standalone `python tests/x86_64_boot.py` run completed with process exit code **0**, without parallel OS builds. The first failed run remains separately recorded above. The final run does not establish the cause of that earlier failure.

```text
COMMAND python tests/x86_64_boot.py
PASS: first-run account saved to the disposable PollikFS disk
PASS: shell prompt reached
PASS: nonblocking pipe read reports empty and returns queued bytes
PASS: cwd prompt and cd
PASS: ls and cat
PASS: quoting and variable expansion
PASS: file commands (touch, mv, mkdir, rmdir, rm)
PASS: output and input redirection (> and >>, <)
PASS: redirection errors report failure
PASS: pipelines (|) between builtins and programs
PASS: native TinyCC compile, link and run
PASS: mid-line editing, delete, home and end
PASS: command history recall
PASS: history and unknown command reporting
PASS: Ctrl+C kills the foreground child
PASS: output retained for scrollback
PASS: clear screen sequence
PASS: exit, relaunch and persistent history
PASS: persisted account rejects a wrong password and logs in after reboot
PASS: selftest-qemu64-16
PASS: selftest-qemu64-64
PASS: selftest-qemu64-256
PASS: selftest-qemu64-5120
PASS: selftest-qemu64-32768
PASS: selftest-qemu64-64-reboot1
PASS: selftest-qemu64-64-reboot2
PASS: selftest-qemu64-64-full
PASS: kernel-qemu64-64
PASS: kernel-qemu64-64-reboot
PASS: kernel-pentium3-64
PASS: kernel-qemu64_-lm-64
PASS: kernel-qemu64_-nx-64
PASS: kernel-qemu64_-pae-64
PASS: kernel-qemu64_-msr-64
PASS: kernel-qemu64_-syscall-64
EXIT_CODE 0
```

Serial marker counts are literal occurrences of `PASS` and `[X64] SELFTEST PASS` in each saved guest log:

```text
COMMAND Python: text.count("PASS"), text.count("[X64] SELFTEST PASS")
selftest-qemu64-16.log PASS_MARKERS=168 SELFTEST_PASS=1
[MM64] balance before=0x0000000000000d7b after=0x0000000000000d7b
[USER64] balance before=0x0000000000000d7b after=0x0000000000000d7b
[ELF64] balance before=0x0000000000000d6a after=0x0000000000000d6a
[SCHED64] balance before=0x0000000000000d6e after=0x0000000000000d6e
[X64] SELFTEST PASS
selftest-qemu64-64.log PASS_MARKERS=168 SELFTEST_PASS=1
[MM64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[USER64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[ELF64] balance before=0x0000000000003d6a after=0x0000000000003d6a
[SCHED64] balance before=0x0000000000003d6e after=0x0000000000003d6e
[X64] SELFTEST PASS
selftest-qemu64-256.log PASS_MARKERS=168 SELFTEST_PASS=1
[MM64] balance before=0x000000000000fd4c after=0x000000000000fd4c
[USER64] balance before=0x000000000000fd4c after=0x000000000000fd4c
[ELF64] balance before=0x000000000000fd3b after=0x000000000000fd3b
[SCHED64] balance before=0x000000000000fd3f after=0x000000000000fd3f
[X64] SELFTEST PASS
selftest-qemu64-5120.log PASS_MARKERS=169 SELFTEST_PASS=1
[MM64] balance before=0x000000000013fca7 after=0x000000000013fca7
[USER64] balance before=0x000000000013fca7 after=0x000000000013fca7
[ELF64] balance before=0x000000000013fc96 after=0x000000000013fc96
[SCHED64] balance before=0x000000000013fc9a after=0x000000000013fc9a
[X64] SELFTEST PASS
selftest-qemu64-32768.log PASS_MARKERS=169 SELFTEST_PASS=1
[MM64] balance before=0x00000000007ff635 after=0x00000000007ff635
[USER64] balance before=0x00000000007ff635 after=0x00000000007ff635
[ELF64] balance before=0x00000000007ff624 after=0x00000000007ff624
[SCHED64] balance before=0x00000000007ff628 after=0x00000000007ff628
[X64] SELFTEST PASS
```

Raw final console/self-hosting result from the same command:

```text
CONSOLE PASS
```

## Assertions diff

```text
COMMAND git diff --numstat -- tests/ sdk/tests/
8	0	tests/control_center_native.c
```

## Checkpoint table

| Item | Status | Evidence | NOT RUN |
|---|---|---|---|
| x64 Ethernet and airplane | Implemented, guest verified | devices_x64.py, RAM 64/5120/128, HTTP before/after, zero packet-counter change while blocked | Physical I219-V |
| x64 AC97, speaker control and selector | Implemented, DMA audio captured | devices_x64.py, nonzero stereo PCM; host DMA/rollback/stereo tests | Physical HDA, USB audio, real speakers, interactive DirectSound |
| Wi-Fi/Bluetooth drivers | Not implemented | Guest capabilities/ENOTSUP; lists do not fabricate devices | USB host controller, RTL8188EUS scan/connect, Bluetooth HCI/pairing |
| Radio tiles, lists, audio footer | Implemented | control_center_native.c; system_icons_gui.py at 1024 TCG/1920 WHPX; native PS/2 screenshots/drag/Escape checks | Physical radio toggling |
| Native framebuffer validation/rollback | Implemented | framebuffer_mode_native.c, framebuffer_rollback_native.c; native 1024x768 24bpp pixels | Physical GPUs/backlight; native framebuffer at 1920x1080 |
| i386 regression | Completed | listed GUI/native/browser/process/smoke/network tests; final rebuilt kernel hash identical to tested kernel | Installer ATA/AHCI end-to-end in this checkpoint |
| x64 builds/storage/full boot | Completed final run | final build lines, storage refusal tests, full boot exit 0 and guest marker counts | Physical boot; first 32 GiB failure root cause remains unconfirmed |
| i386 kernel.bin | 734588 -> 736024 B | actual objcopy binary, final SHA-256 and build output above | None for size measurement |

The full rich desktop/app migration to x86-64 is not complete. No Wi-Fi or
Bluetooth feature is claimed ready merely because its UI tile exists.

## Launcher audio machine configuration

```text
COMMAND qemu-system-x86_64 -S -machine pc,pcspk-audiodev=native_audio -audiodev none,id=native_audio -device AC97,audiodev=native_audio -nic none -display none -serial none -qmp tcp:127.0.0.1:55676,server=on,wait=off
{'return': {}}
{'return': {}}
QEMU_CONFIG_EXIT 0 STDERR
```

This is a configuration/startup proof only; it does not claim speaker playback
or interactive DirectSound. An initial attempt to feed QMP commands through
Windows stdio timed out after 20 seconds; the equivalent TCP-QMP proof above
completed. The native harness uses TCP QMP.

```text
COMMAND git diff --check
EXIT_CODE 0
```

Git emits CRLF conversion warnings on this Windows checkout; no whitespace
error is reported by its normal configured check. A separate check with
`core.autocrlf=false` misinterpreted the existing CRLF checkout as wholesale
changes; that override is not used and no line-ending rewrite was performed.
