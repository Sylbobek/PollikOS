# Native devices and Control Center

Checkpoint: 2026-10-06. This is native PollikOS code, not Windows/Linux drivers.
The rich desktop remains i386; the separate x86-64 `.pol` desktop now also has
a userspace Control Center. No disk format, existing operation number or ABI
structure size was changed. No optimisation milestone was started.

## Implemented

- Administrative airplane mode closes TCP connections, clears DHCP/ARP/DNS
  state, blocks receive delivery and blocks RTL8139 transmission even through
  an old cached interface. Ethernet is included. Disabling airplane mode starts
  DHCP again. This is an Internet traffic block, not a claim of physical RF power
  control for unsupported radios. Its state is currently session-local.
- Wi-Fi/Bluetooth have separate tiles and disclosure buttons. Their lists
  report the actual lack of supported adapters. Unsupported radio operations
  return `USER_ENOTSUP`; tiles never pretend to enable a missing driver.
- Brightness in the existing i386 desktop retains its slider and rendering.
  Sound is below a separator, with an output-list button at the right. The list
  has PC Speaker and detected Intel ICH AC97; unavailable PCM cannot be selected.
- Native x86-64 Intel ICH AC97 reuses `kernel/audio.c`. DMA pages are allocated
  from PMM_DMA32 and mapped into dedicated kernel virtual slots; physical
  addresses are never cast to virtual pointers. Allocation/map failures roll
  back. Tone DMA is asynchronous, stereo channel counts are correct and reset
  waits are bounded. The native PC speaker uses PIT channel 2 with a scheduled
  stop; it does not change the scheduler's PIT channel 0 frequency.
- PCI discovery and native output/volume/device capabilities are exposed by
  new `USER_DEVICE_CONTROL = 0x504f0040`, with scalar operations in the public
  SDK `pollikos/devices.h`. Invalid values/operations are rejected. No user
  pointer grants access to driver memory.
- Native framebuffer validation now checks exact 24/32-bit BGR masks, pitch,
  dimensions and memory model. Borrowed mappings are rolled back if mapping
  fails. Width, height, bpp and pitch can be queried from userspace. This is
  software VBE/LFB rendering, not GPU acceleration or an Intel/NVIDIA driver.
- Native Control Center runs in the Desktop process: separate radio lists,
  airplane tile, audio footer/output selector, held volume dragging, reversible
  slide and Escape closing the panel. The existing rich i386 panel has the
  corresponding layout. It is not a complete port of the rich desktop.

## Hardware boundary: NOT DONE / NOT RUN

Windows host inventory is not a guest driver test:

```powershell
Get-CimInstance Win32_NetworkAdapter | Where-Object {
  $_.PNPDeviceID -match 'VID_0B05|VEN_8086&DEV_1A1D'
} | Select-Object Name,PNPDeviceID | Format-List
```

```text
Name        : ASUS Wireless USB adapter
PNPDeviceID : USB\VID_0B05&PID_18F0\C87F548F89AB
Name        : Intel(R) Ethernet Connection (17) I219-V
PNPDeviceID : PCI\VEN_8086&DEV_1A1D&SUBSYS_1A1D1849&REV_11\3&11583659&0&FE
```

`0B05:18F0` is ASUS USB-N10 Nano B1 / RTL8188EUS according to the
[Linux USB-ID table](https://github.com/torvalds/linux/blob/v5.14/drivers/staging/rtl8188eu/os_dep/usb_intf.c).
PollikOS x86-64 does not yet have a USB host-controller stack, this WLAN driver,
802.11 association/security, or USB Bluetooth/HCI. QEMU's RTL8139 is Ethernet,
not Wi-Fi. USB Wi-Fi scanning/connection, Bluetooth pairing, physical I219-V,
Realtek/NVIDIA HDA, USB audio, physical backlight and physical speaker playback
are **NOT RUN and not implemented by this checkpoint**. The output selector
enumerates supported outputs; it does not claim to enumerate every physical
speaker. Native streaming PCM from arbitrary userspace applications is also
not exposed by this device-control operation; its sound proof uses test tones.

## Run the native target

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build-x86_64.ps1 -Production
.\run-x86_64.ps1 -Audio
```

Default native networking is QEMU user networking with RTL8139. `-NoNetwork`
disables it. `-Audio` exposes AC97 with the Windows `dsound` backend and routes the emulated PC speaker to that backend too. Both native
disks use snapshots. This launcher does not open `build/PollikData.img`.
Live DirectSound playback in an interactive Windows session: **NOT RUN**;
the native DMA audio proof uses QEMU's WAV capture backend.

## Found and reproduced before repair

```text
clang -D_CRT_SECURE_NO_WARNINGS -O2 -fno-builtin -Wall -Wextra -Werror tests/network_gate_native.c -o build/network-gate.exe
build/network-gate.exe
FAIL disabled cached-interface send: result=1 port_writes=2 tx=1

clang -O2 -fno-builtin -Wall -Wextra -Werror tests/audio_driver_native.c -o build/audio-driver.exe
build/audio-driver.exe
FAIL DMA rollback allocated=1 freed=0
FAIL unsupported audio class accepted as AC97
FAIL native stereo sample count=960 expected=1920
```

The audio fixture additionally exposed an unbounded stuck-reset loop (the
test process was stopped) and covers a one-sample waveform period without
division by zero. An earlier mock incorrectly counted freeing NULL; that mock
was corrected to match both real adapters' no-op NULL-free contract.

The first GUI harness attempt incorrectly sent `exit` to UART, which is the
separate TTY shell. It now closes the graphical terminal via its PS/2 shortcut.
Frame assertions wait for the unchanged expected pixels; they no longer sample
a partly restored LFB after an assumed fixed delay. Existing assertions were
not removed or weakened.

## Capture limitation

This Windows QEMU build leaves RIFF/data length fields at zero even after QMP
quit and process exit 0. The original captures are retained unmodified. The
test validates RIFF/fmt/data tags, PCM format, alignment, positive/negative
nonzero samples and the actual payload length. A separately named
`*-playback.wav` wraps those same PCM bytes for listening. This is a host
container limitation, not evidence that the original zero-length WAV is valid.
[QEMU's WAV source](https://github.com/qemu/qemu/blob/master/audio/wavaudio.c)
initialises these fields to zero and writes lengths in its finaliser.

## Existing assertions

```powershell
git diff --numstat -- tests/ sdk/tests/
```

```text
8  0  tests/control_center_native.c
```

Only eight checks/mock lines were added to that existing fixture: opening the
output/radio lists and switching airplane mode. Existing checks and coordinates
are retained. New driver fixtures are new files. No existing golden RGB value,
geometry assertion, syscall ABI assertion or filesystem assertion was changed.

## Evidence and remaining verification

Raw command output, marker counts, kernel sizes and any suite failures are in
`NATIVE_DEVICES_EVIDENCE.md`. Physical-radio/physical-audio/USB/controller tests,
real Wi-Fi scan/association and the native rich-desktop migration are **NOT RUN**.
The logical next driver stage is a native USB host controller, before the
RTL8188EUS and USB Bluetooth drivers can be implemented and exercised.

## Build dependency

The required missing `kernel/certs/anchors.h` was restored byte-for-byte from the existing `pre-followup2` worktree. No other files were restored or reverted:

```text
COMMAND git hash-object kernel/certs/anchors.h
8c7f79f5fbf3feaf0565b5bc72a22cac574da8a5
```
