# Generic input and administrator dialog checkpoint (2026-10-09)

Implemented in the existing i386 and x86_64 input paths:

- Shared PS/2 relative/IntelliMouse decoder and protocol-based Synaptics new-absolute detection. Native Synaptics motion, tap, pressure/palm rejection, two-finger scrolling and advertised pass-through; other PS/2 devices retain relative compatibility.
- PCI UHCI/OHCI/EHCI/xHCI and hubs from pinned BSD libpayload sources. PollikOS supplies kernel-owned DMA32 allocation, firmware ownership handoff, PCI/MMIO access and bounded HID descriptor/report decoding. USB mouse, keyboard, absolute tablet and HID touchpad reports feed the existing input consumers. Disconnect releases buttons; reconnect enumerates again. Composite alternate-zero HID interfaces are collected together.
- USB enumeration runs outside the x64 PIT handler. Interrupt queues are polled on the kernel dispatcher/authentication path. Storage support is disabled.
- Administrator capture preserves the old presented frame and cursor-free scene while constructing the cache. Half-size blur expands in place; the expanded cache is reused for edits/blinks. x64 cursor rendering is suspended across nested overlays, including concurrent pointer movement.
- The dock cache now uses PMM memory, keeping the installer below its existing image/BSS limits. Builds use `-NoSync`; the user data disk is untouched.

## Verification actually run

- `python tests/pointer_protocols.py`: production PS/2 and HID decoding, motion/buttons/wheel, Synaptics tap/scroll/palm, report IDs/contact lift, malformed/truncated/out-of-range reports.
- `python tests/cursor_framebuffer.py`: all cursor kinds/scales, screen corners, moving windows, and successful administrator capture without modifying the live scene/frame.
- `python tests/console_cursor_native.py`: real x64 save-under/overlay code, movement during nested painting, padded pitch/edges; in-place blur matches a separate-buffer reference across 1209 odd/even dimensions.
- `python tests/held_drag.py`: legacy/optimized graphics pixel parity and existing drag/dock/clip/framebuffer regressions.
- `python tests/control_center_native.py` and `python tests/desktop_menus.py --arch both`: search/pins, administrator cancellation and authorization, protected i386 write, x64 native administrator launch, logout/sign-in, shutdown/restart.
- `python tests/usb_input_guest.py --controller <uhci|ohci|ehci|xhci>`: actual i386 QEMU USB mouse movement/buttons, independent USB keyboard scan queue/search, unplug/replug. `--tablet` also passed for xHCI.
- `python tests/usb_input_x64_guest.py --controller <uhci|ohci|ehci|xhci>`: actual x64 USB pointer/buttons, keyboard and unplug/replug. xHCI absolute tablet also passed.
- i386 runtime/installer built with the official build path. x64 kernel/image rebuilt and linked against the existing genuine native userspace assets; the entire browser/toolchain build was not rerun.

The QMP timing probe measures host wall time until the password layout is ready, including observer overhead. At 1024x768 the earlier probe was 893.6 ms; optimized runs ranged from 365.7 to 847.5 ms. A concurrent-VM 1920x1080 run was 2490.7 ms, with the full password/Cancel screen visually inspected. These samples were affected by concurrent builds/VMs and are not a controlled speedup benchmark or physical-laptop timings. Reproduce with `python tests/admin_latency_guest.py --resolution 1920x1080`.

Tests used disposable data fixtures and snapshot boot media. Independent copies of ELF/boot media avoided races with other workspace builds; `POLLIK_GUI_BUILD` and `POLLIK_X64_BUILD` can select those directories. The GUI helper verifies ELF load bytes against the actual boot image.

## Remaining limits

- HID over I2C/ACPI and model-specific nonstandard PS/2 protocols are not implemented. This is generic PS/2/USB support, not a claim that every laptop works. Native Synaptics/HID touchpad behavior has decoder tests, but no physical touchpad was available for runtime validation.
- USB boot-keyboard reports are supported; arbitrary keyboard NKRO report formats are not decoded.
- Real hardware, arbitrary controller firmware quirks, precision-touchpad feature-mode negotiation and a full-system regression suite were not verified.
- The old asynchronous i386 spawn PMM-total probe can report a decrease while desktop resources are allocated concurrently. This checkpoint does not treat that result as a completed system-wide leak audit.

Protocol references: [Linux Synaptics definitions](https://github.com/torvalds/linux/blob/master/drivers/input/mouse/synaptics.h); host stack provenance: [coreboot libpayload at the pinned commit](https://github.com/coreboot/coreboot/tree/1fe735b7cefcc14060b41b629406fbdf1d7eed01/payloads/libpayload/drivers/usb). Original upstream licenses remain in the vendored files.
