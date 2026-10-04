# GUI smoke fixture

`tests/smoke.py` creates a disposable 40 MiB secondary disk and formats it as
PollikFS v2. It completes first-run setup with the test-only account `smoke` /
`smokepass`; it does not read or modify the user's saved filesystem. After the
reboot phase, it logs into that same disposable account again.

The Notes framebuffer check accepts both supported active body colors: dark
`0x121520` and the historical light `0xfaf9fc`. It also checks editing,
Backspace restoration, wheel scrolling and the reverse-scroll round trip.
`--notes-only` stops after those focused UI checks. The full run additionally
checks Settings accent selection, dock input, shell commands, PollikFS file
save/open/delete across reboot, process isolation, and RTL8139 ARP/ICMP.

Verification: i386 `build.ps1` succeeds; `smoke.py --notes-only` and the full
`smoke.py` both pass on the current build. When the base image is locked by a
running QEMU, the build script leaves a complete `.pending` image; the full
smoke test was run against that image using `POLLIK_TEST_IMAGE`.
