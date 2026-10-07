# Media/SDK raw evidence

2026-10-07. These are executed commands and their actual output lines. No existing syscall number, ABI structure size or disk format was changed.

## powershell -File build-x86_64.ps1 -Production

```text
COMMAND powershell -File build-x86_64.ps1 -Production
PROFILE preserved PollikData-system-30g.img bytes=32212254720 filesystem_bytes=33554432
Built build/x86_64/system/PollikOS-x86_64.img (432275 kernel bytes)
```

Full log: `build/media-x64-build.log`.

## powershell -File build-x86_64.ps1

```text
COMMAND powershell -File build-x86_64.ps1
Built build/x86_64/kernel/PollikOS-x86_64.img (448659 kernel bytes)
```

Full log: `build/media-normal-build.log`.

## powershell -File build-x86_64.ps1 -SelfTest

```text
COMMAND powershell -File build-x86_64.ps1 -SelfTest
Built build/x86_64/selftest/PollikOS-x86_64.img (538883 kernel bytes)
```

Full log: `build/media-selftest-build.log`.

## powershell -File build.ps1 -NoSync

```text
COMMAND powershell -File build.ps1 -NoSync
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (736024 kernel bytes, 1438 sectors loaded at 1 MiB)
```

Full log: `build/media-i386-build.log`.

## python tests/media_audio_native.py

```text
COMMAND python tests/media_audio_native.py
ORACLE MP3 source_rate=22050 frames=32601 frequency_hz=440.7127
ORACLE MP3 source_rate=44100 frames=31347 frequency_hz=440.2298
ORACLE MP3 source_rate=48000 frames=31104 frequency_hz=439.9847
PASS media decoder: 19 cases, PCM golden samples exact, float/clamps, malformed rejection, MP3 3 rates and independent tone-frequency oracle
```

Full log: `build/media-decoder-test.log`.

## python tests/image_sdk_native.py

```text
COMMAND python tests/image_sdk_native.py
PIXELS png count=3072 max_channel_error=0
PIXELS jpg count=3072 max_channel_error=0
PIXELS bmp count=3072 max_channel_error=0
PIXELS gif count=3072 max_channel_error=0
PASS SDK images: PNG/BMP/GIF exact RGBA, JPEG max error 1, malformed rejection
```

Full log: `build/image-sdk-test.log`.

## python tests/media_x64.py --ram 8192

```text
COMMAND python tests/media_x64.py --ram 8192
PROFILE created data-30g.img bytes=32212254720 allocated_bytes=6291456 source_bytes=41943040 source_sha256=6bec23444f02e0fb6a036eb709d4d578ab31cd495be935fcdc415cb994bfdc94 filesystem_bytes=33554432
COMMAND qemu-system-x86_64 -S -accel tcg -cpu qemu64 -m 8192 -vga std -display none -nic none -no-reboot -drive file=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\PollikOS-x86_64.img,format=raw,if=ide,index=0,snapshot=on -drive file=C:\Users\syltu\AppData\Local\Temp\pollik-media-6b63tsxh\data-30g.img,format=raw,if=ide,index=1,snapshot=on -serial tcp:127.0.0.1:54615,server=on,wait=off -qmp tcp:127.0.0.1:54616,server=on,wait=off -audiodev wav,id=a,path=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\media-8192.wav,out.frequency=48000 -device AC97,audiodev=a
[AUDIO64] stream PMM before=0x00000000001ff3a9 after=0x00000000001ff3a9
[AUDIO64] stream PMM before=0x00000000001ff40a after=0x00000000001ff40a
[AUDIO64] stream PMM before=0x00000000001ff40a after=0x00000000001ff40a
[AUDIO64] stream PMM before=0x00000000001ff40a after=0x00000000001ff40a
PROFILE disk_bytes=32212254720 framebuffer=1024x768
PASS native pause/resume queued_frames=16384->14336; exiting with live DMA for owner cleanup
[AUDIO64] stream PMM before=0x00000000001ff44e after=0x00000000001ff44e
PMM_BALANCES [('0x00000000001ff3a9', '0x00000000001ff3a9'), ('0x00000000001ff40a', '0x00000000001ff40a'), ('0x00000000001ff40a', '0x00000000001ff40a'), ('0x00000000001ff40a', '0x00000000001ff40a'), ('0x00000000001ff44e', '0x00000000001ff44e')]
FRAME_COUNTS completed=191052 captured=207412
CAPTURE real DMA rate=48000 samples=414824 min=-6000 max=6000
PASS native WAV/MP3: RAM=8192 MiB disk=32212254720 B, 3 MP3 rates, playback, pause/resume, owner-exit cleanup, exact PMM baselines
```

Full log: `build/media-guest-test.log`.

## python tests/media_x64.py --ram 64

```text
COMMAND python tests/media_x64.py --ram 64
PROFILE created data-30g.img bytes=32212254720 allocated_bytes=6291456 source_bytes=41943040 source_sha256=6bec23444f02e0fb6a036eb709d4d578ab31cd495be935fcdc415cb994bfdc94 filesystem_bytes=33554432
COMMAND qemu-system-x86_64 -S -accel tcg -cpu qemu64 -m 64 -vga std -display none -nic none -no-reboot -drive file=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\PollikOS-x86_64.img,format=raw,if=ide,index=0,snapshot=on -drive file=C:\Users\syltu\AppData\Local\Temp\pollik-media-7zx_1wnm\data-30g.img,format=raw,if=ide,index=1,snapshot=on -serial tcp:127.0.0.1:56795,server=on,wait=off -qmp tcp:127.0.0.1:56796,server=on,wait=off -audiodev wav,id=a,path=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\media-64.wav,out.frequency=48000 -device AC97,audiodev=a
[AUDIO64] stream PMM before=0x00000000000034d2 after=0x00000000000034d2
[AUDIO64] stream PMM before=0x0000000000003533 after=0x0000000000003533
[AUDIO64] stream PMM before=0x0000000000003533 after=0x0000000000003533
[AUDIO64] stream PMM before=0x0000000000003533 after=0x0000000000003533
PROFILE disk_bytes=32212254720 framebuffer=1024x768
PASS native pause/resume queued_frames=16384->14336; exiting with live DMA for owner cleanup
[AUDIO64] stream PMM before=0x0000000000003577 after=0x0000000000003577
PMM_BALANCES [('0x00000000000034d2', '0x00000000000034d2'), ('0x0000000000003533', '0x0000000000003533'), ('0x0000000000003533', '0x0000000000003533'), ('0x0000000000003533', '0x0000000000003533'), ('0x0000000000003577', '0x0000000000003577')]
FRAME_COUNTS completed=191052 captured=204709
CAPTURE real DMA rate=48000 samples=409418 min=-6000 max=6000
PASS native WAV/MP3: RAM=64 MiB disk=32212254720 B, 3 MP3 rates, playback, pause/resume, owner-exit cleanup, exact PMM baselines
```

Full log: `build/media-guest-64.log`.

## python tests/browser_images_x64.py

```text
COMMAND python tests/browser_images_x64.py
COMMAND qemu-system-x86_64 -S -accel tcg -cpu qemu64 -m 8192 -vga std -display none -no-reboot -drive file=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\PollikOS-x86_64.img,format=raw,if=ide,index=0,snapshot=on -drive file=C:\Users\syltu\AppData\Local\Temp\pollik-web-images-_2v0vk5x\data.img,format=raw,if=ide,index=1,snapshot=on -netdev user,id=n -device rtl8139,netdev=n -serial tcp:127.0.0.1:63436,server=on,wait=off -qmp tcp:127.0.0.1:63437,server=on,wait=off
HTTP_REQUESTS ['/index.html', '/styles/site.css', '/scripts/app.js', '/pictures/p.png', '/pictures/p.jpg']
PIXELS PNG=3072 RGB=(18,171,52); JPEG=3072 expected=(21, 64, 218) maxerr=1; JS-styled=5163 RGB=(193,123,208)
PASS native browser: real HTTP HTML/CSS/JS, document-relative PNG/JPEG after nested script URL, framebuffer colors, clean close
```

Full log: `build/browser-images-test.log`.

## python tests/vm_profile.py

```text
COMMAND python tests/vm_profile.py
PROFILE created vm.img bytes=32212254720 allocated_bytes=7340032 source_bytes=41943040 source_sha256=f77ff4c79ddc4e80e46c9da23d63b3be162900c6e420e3dc998b7af1ccbee23d filesystem_bytes=33554432
PROFILE preserved vm.img bytes=32212254720 filesystem_bytes=33554432
PASS profile: virtual_bytes=32212254720 allocated_bytes=7340032, exact prefix/personals/opaque-tail, idempotence, protected names, no replacement
```

Full log: `build/vm-profile-test.log`.

## python tests/process_stress.py --ram 64 256

```text
COMMAND python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 5807 / [PROC] [TEST] PMM free pages after:  5807
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 54911 / [PROC] [TEST] PMM free pages after:  54911
```

Full log: `build/media-process-stress.log`.

## python tests/smoke.py --notes-only

```text
COMMAND python tests/smoke.py --notes-only
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override
PASS: pointer acceleration can be disabled and persists
```

Full log: `build/media-smoke.log`.

## python tests/x86_64_boot.py

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
CONSOLE PASS
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
```

Full log: `build/media-x64-boot.log`.

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

Full log: `build/media-x64-storage.log`.

## .\run-x86_64.ps1 -Audio -Headless -NoLaunch

```text
COMMAND .\run-x86_64.ps1 -Audio -Headless -NoLaunch
Guest RAM: 8192 MiB. Disk profile: 30 GiB; PollikFS v2 usable filesystem remains about 32 MiB.
qemu-system-x86_64 "-name" "PollikOS native x86-64" "-machine" "pc,pcspk-audiodev=native_audio" "-accel" "tcg" "-cpu" "qemu64" "-smp" "1" "-m" "8192" "-vga" "std" "-display" "none" "-drive" "file=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\PollikOS-x86_64.img,format=raw,if=ide,index=0,snapshot=on" "-drive" "file=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\PollikData-system-30g.img,format=raw,if=ide,index=1,snapshot=on" "-serial" "stdio" "-netdev" "user,id=native_net" "-device" "rtl8139,netdev=native_net" "-audiodev" "dsound,id=native_audio" "-device" "AC97,audiodev=native_audio"
```

Full log: `build/media-launcher.log`.

## Queue unit test

```text
COMMAND clang -O2 -Wall -Wextra -Werror tests/audio_stream_native.c -o build/audio-stream.exe
COMMAND build/audio-stream.exe
PASS stream: 17 allocation failures, reset failure, ownership/pointers, depth 16, 3 wraps, no duplicate Run, exact partial pause/resume, underrun restart, owner cleanup baseline
```

## Final guest marker counts and balances

```text
COMMAND Python: saved_guest_text.count("PASS"), saved_guest_text.count("[X64] SELFTEST PASS")
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

## Actual objcopy binary sizes

```text
COMMAND Get-Item build/kernel.bin,build/x86_64/kernel/kernel.bin,build/x86_64/selftest/kernel.bin,build/x86_64/system/kernel.bin | Select FullName,Length
C:\Users\syltu\Desktop\PollikOS\build\kernel.bin 736024
C:\Users\syltu\Desktop\PollikOS\build\x86_64\kernel\kernel.bin 448659
C:\Users\syltu\Desktop\PollikOS\build\x86_64\selftest\kernel.bin 538883
C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\kernel.bin 432275
```

i386 before/after this media work: **736024 -> 736024 B**. The rebuilt binary is identical to the original driver-checkpoint binary:

```text
COMMAND Get-FileHash -Algorithm SHA256 build/kernel.bin
E89C202FBEB4894AE59BED94E2AFF6EDF5C48739CF60E3437306C5D3574F84E1
```

## Reproduction that prompted the Run-register fix

```text
FAIL stream line=39 run_writes==1 live=17 produced=2 consumed=0
REPRO expected_frames 191052 captured_frames 102107 clip_counts [96000, 32601, 31347, 31104]
AssertionError: FAIL DMA capture shorter than submitted completed audio
```

The final test additionally checks actual recorded frames >= completed frames. Existing tests were not weakened; the initial new capture test was insufficient and was strengthened.

## Other encountered failures

An intermediate host decoder invocation could not open its output file (test exit 4 for pcm-8-1.raw). A later diagnostic run succeeded without a code change; the host I/O failure cause is **unconfirmed**, not claimed fixed by retries. The initial browser test timed out waiting for a shell prompt after Escape; the screenshot already showed the correct images. Its existing Escape contract leaves address editing. The new harness now uses the window close button.

## Assertions

No existing assertion was removed/modified by this media stage. The eight added lines in control_center_native.c belong to the preceding driver stage.

```text
COMMAND git diff --numstat -- tests/ sdk/tests/ kernel/arch/x86_64/*_test.c
8	0	tests/control_center_native.c
```

## NOT RUN / not implemented

MP4/AAC/H.264/H.265/AV1/WebM/Opus/FLAC/Vorbis, full native GIF animation, SVG/WebP/AVIF, every website/framework, new Python/Rust/C++/Java runtimes, USB/WLAN/Bluetooth/HDA/USB-audio drivers, physical hardware playback/boot, 30 GiB usable filesystem: **NOT RUN / not implemented**. Existing C SDK was extended; full C/POSIX compatibility is not claimed. Installer ATA/AHCI end-to-end, full smoke beyond notes-only, browser_js legacy repeated-flake campaign and performance benchmarks in this media checkpoint: **NOT RUN**.

## Table

| Item | Status | Evidence | NOT RUN |
|---|---|---|---|
| Native WAV/MP3 player and SDK | Implemented | Media guest at 64/8192 MiB; real AC97 PCM capture, pause/resume, owner cleanup and exact PMM balances | Physical audio; other codecs |
| 8 GiB RAM / 30 GiB disk profile | Booted/identified | Guest disk_bytes=32212254720; sparse clone exact-prefix/tail test | 30 GiB usable FS (v2 is still ~32 MiB) |
| Native browser images | Implemented | HTTP request order, 3072 PNG pixels, 3072 JPEG pixels, 5163 JS-styled pixels | Full web compatibility, animated native GIF |
| SDK extensions | Implemented | media.h/audio.h/image.h, native-library packaging, audio example compiled | Complete language runtimes/C/POSIX |
| Regressions | Executed | Full x64 boot/storage, i386 build/process stress/notes smoke | Physical hardware, installer end-to-end in this checkpoint |
| i386 binary | Preserved | 736024 B before/after; identical SHA-256 | None for size |
