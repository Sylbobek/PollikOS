USB host controller and hub sources from coreboot libpayload.
Pinned commit: 1fe735b7cefcc14060b41b629406fbdf1d7eed01.
https://github.com/coreboot/coreboot/tree/1fe735b7cefcc14060b41b629406fbdf1d7eed01/payloads/libpayload/drivers/usb

The BSD license and attribution are retained at the top of every source file.
PollikOS supplies its own platform and HID input adapters. Mass storage and
firmware disk registration are disabled; this integration never mounts,
formats, or writes a USB disk.
