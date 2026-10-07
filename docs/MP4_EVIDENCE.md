# MP4 executed evidence

2026-10-07. This verifies the documented Baseline/AAC-LC subset, not arbitrary MP4 files.

## python tests/movie_native.py

```text
COMMAND python tests/movie_native.py
COMMAND python tools/build_mp4_codecs.py --output C:\Users\syltu\Desktop\PollikOS\build\mp4-native\codecs
COMMAND clang -D_CRT_SECURE_NO_WARNINGS -O2 -Wall -Wextra -Werror -idirafter sdk/include -Ithird_party/h264bsd/src -Ithird_party/faad2/include tests/movie_native.c sdk/media/movie.c C:\Users\syltu\Desktop\PollikOS\build\mp4-native\codecs\libpollikvideo.a -o C:\Users\syltu\Desktop\PollikOS\build\mp4-native\movie.exe
COMMAND ffmpeg -v error -y -f lavfi -i testsrc2=size=160x96:rate=12:duration=2 -f lavfi -i sine=frequency=440:sample_rate=48000:duration=2 -c:v libx264 -profile:v baseline -pix_fmt yuv420p -bf 0 -x264-params ref=1:scenecut=0 -c:a aac -ac 2 -shortest -movflags +faststart C:\Users\syltu\Desktop\PollikOS\build\mp4-native\clip.mp4
MOVIE 160x96 frames=24 audio_rate=48000 stereo_frames=96256
GOLDEN YUV bytes=552960 frames=24 pixel-identical to FFmpeg
AUDIO_BYTES ours 385024 reference 385024
GOLDEN AAC frames=96256 max_error=5 rms_lsb=0.274851
MOVIE 160x90 frames=5 audio_rate=0 stereo_frames=0
GOLDEN cropped 160x90: 5 frames exact; silent video supported
PASS MP4 host: native C demux/H264 Baseline/AAC LC, 24 golden frames, malformed atom bounds
```

Full log: `build/mp4-native-test.log`.

## python tests/movie_x64.py

```text
COMMAND python tests/movie_x64.py
COMMAND qemu-system-x86_64 -S -accel tcg -cpu qemu64 -m 8192 -vga std -display none -nic none -no-reboot -drive file=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\PollikOS-x86_64.img,format=raw,if=ide,index=0,snapshot=on -drive file=C:\Users\syltu\AppData\Local\Temp\pollik-mp4-5tb68q30\data.img,format=raw,if=ide,index=1,snapshot=on -serial tcp:127.0.0.1:63773,server=on,wait=off -qmp tcp:127.0.0.1:63774,server=on,wait=off -audiodev wav,id=a,path=C:\Users\syltu\Desktop\PollikOS\build\x86_64\system\mp4-native.wav,out.frequency=48000 -device AC97,audiodev=a
PASS native MP4 decoded_frames=24 PCM_frames=96256
PROCESS_PMM before=2094248 after=2094248
PIXELS MP4 moving region=640x384 distinct_screendumps=2 exact FFmpeg-YUV RGB oracle
GUI_PROCESS_PMM before=2094248 after=2094248
CAPTURE AAC native DMA frames=509866 min=-2977 max=2977
PASS native MP4: 24 FFmpeg-identical YUV frame hashes, timestamps, AAC PCM/DMA, exact PMM baseline, moving framebuffer
```

Full log: `build/mp4-guest-test.log`.

## powershell -File build-x86_64.ps1 -Production

```text
COMMAND powershell -File build-x86_64.ps1 -Production
Built build/x86_64/system/PollikOS-x86_64.img (432275 kernel bytes)
```

Full log: `build/mp4-x64-build.log`.

## powershell -File build-x86_64.ps1

```text
COMMAND powershell -File build-x86_64.ps1
Built build/x86_64/kernel/PollikOS-x86_64.img (448659 kernel bytes)
```

Full log: `build/mp4-normal-build.log`.

## powershell -File build-x86_64.ps1 -SelfTest

```text
COMMAND powershell -File build-x86_64.ps1 -SelfTest
Built build/x86_64/selftest/PollikOS-x86_64.img (538883 kernel bytes)
```

Full log: `build/mp4-selftest-build.log`.

## powershell -File build.ps1 -NoSync

```text
COMMAND powershell -File build.ps1 -NoSync
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (736024 kernel bytes, 1438 sectors loaded at 1 MiB)
```

Full log: `build/mp4-i386-build.log`.

## python tests/process_stress.py --ram 64 256

```text
COMMAND python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 5807 / [PROC] [TEST] PMM free pages after:  5807
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 54911 / [PROC] [TEST] PMM free pages after:  54911
```

Full log: `build/mp4-process-stress.log`.

## python tests/smoke.py --notes-only

```text
COMMAND python tests/smoke.py --notes-only
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override
PASS: pointer acceleration can be disabled and persists
```

Full log: `build/mp4-smoke.log`.

## Resource interpretation

The first probe decodes reference pictures before starting PCM. Its audio-start/stop PMM counters match. The actual player allocates one additional userspace reference buffer during playback, so its intermediate audio-only global PMM line differs by 16 frames. The process-level read-only QMP check verifies that the entire player is reaped and all allocations return to the same baseline:

```text
PROCESS_PMM before=2094248 after=2094248
GUI_PROCESS_PMM before=2094248 after=2094248
```

## Initial failures and repairs

FAAD LC-only configuration initially retained the library default MAIN object type; SetConfiguration rejected it. The adapter now explicitly requests LC. A second test found the MP4 edit offset being trimmed twice: FAAD already suppresses its first 1024 priming samples. It is now counted against the edit offset.

```text
AUDIO_BYTES ours 380928 reference 385024
GOLDEN AAC frames=96256 max_error=5 rms_lsb=0.274851
```

The first presentation test only looked for changing colourful pixels. It was strengthened to require complete regions equal to the independent FFmpeg-YUV RGB oracle. The independent cursor is moved outside the region; no assertion is masked. Initial strong sampling failed before that test setup was corrected; final exact-region checks pass. A packaging attempt referenced LICENSE rather than the actual LICENSE.md; the path was fixed. One intermediate probe link used a stale optional archive before it was rebuilt for movie_error; the final production build and probe use the current archive. No missing symbol was stubbed.

## Limits / NOT RUN

H264 Main/High/CABAC/B-frame reordering, HE-AAC/SBR, fragmented MP4, DRM, H265/AV1, subtitles, seeking, network video, physical playback, GPU acceleration, files >16 MiB and video >640x480: **NOT RUN / unsupported**. High profile is actually rejected by the host test. Installer end-to-end, full smoke beyond notes-only and performance benchmarking in this MP4 checkpoint: **NOT RUN**.

## Actual i386 binary

```text
COMMAND Get-Item build/kernel.bin | Select Length
736024
COMMAND Get-FileHash -Algorithm SHA256 build/kernel.bin
E89C202FBEB4894AE59BED94E2AFF6EDF5C48739CF60E3437306C5D3574F84E1
```

Before/after MP4 work: **736024 -> 736024 B**, identical binary. No decoder is linked into the kernel.

## Data and assertions

No MP4 test mounts build/PollikData.img. Native test disks are disposable/snapshot images; i386 builds used -NoSync. The first observed legacy-data hash in this phase was 183E1A03ED2113EDAFB0A93AB05569480A1386E1C487B18D78DC5C0F310206DF; this differs from the older media-phase observation. The writer between phases is unconfirmed, so no full-session unchanged-data hash is claimed.

No existing assertion was removed or weakened in this MP4 phase. New tests were strengthened during implementation; the eight added control_center_native.c lines are from the preceding driver phase.

## Full x64 suite

```text
COMMAND python tests/x86_64_boot.py
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
EXIT_CODE 0
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
EXIT_CODE 0
```

Full logs: `build/mp4-x64-boot.log`, `build/mp4-x64-storage.log`.

### Raw guest marker counts and free-frame balances

The command used to produce the counts below was this PowerShell here-string piped to Python (the final report also includes reboot/full-image labels):

```powershell
@'
from pathlib import Path
for label in ('16','64','256','5120','32768','64-reboot1','64-reboot2','64-full'):
    p = Path(f'build/x86_64/selftest-qemu64-{label}.log')
    t = p.read_text(errors='replace')
    print(f'{p}: PASS: count={t.count("PASS:")} SELFTEST_PASS={t.count("[X64] SELFTEST PASS")}')
    for line in t.splitlines():
        if ('balance before=' in line and any(x in line for x in ('[MM64]','[USER64]','[ELF64]','[SCHED64]'))) or line == '[X64] SELFTEST PASS':
            print(line)
'@ | python -
```

```text
COMMAND python (read saved selftest logs; count PASS: and SELFTEST PASS; print balances)
build\x86_64\selftest-qemu64-16.log: PASS: count=154 SELFTEST_PASS=1
[MM64] balance before=0x0000000000000d7b after=0x0000000000000d7b
[USER64] balance before=0x0000000000000d7b after=0x0000000000000d7b
[ELF64] balance before=0x0000000000000d6a after=0x0000000000000d6a
[SCHED64] balance before=0x0000000000000d6e after=0x0000000000000d6e
[X64] SELFTEST PASS
build\x86_64\selftest-qemu64-64.log: PASS: count=154 SELFTEST_PASS=1
[MM64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[USER64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[ELF64] balance before=0x0000000000003d6a after=0x0000000000003d6a
[SCHED64] balance before=0x0000000000003d6e after=0x0000000000003d6e
[X64] SELFTEST PASS
build\x86_64\selftest-qemu64-256.log: PASS: count=154 SELFTEST_PASS=1
[MM64] balance before=0x000000000000fd4c after=0x000000000000fd4c
[USER64] balance before=0x000000000000fd4c after=0x000000000000fd4c
[ELF64] balance before=0x000000000000fd3b after=0x000000000000fd3b
[SCHED64] balance before=0x000000000000fd3f after=0x000000000000fd3f
[X64] SELFTEST PASS
build\x86_64\selftest-qemu64-5120.log: PASS: count=155 SELFTEST_PASS=1
[MM64] balance before=0x000000000013fca7 after=0x000000000013fca7
[USER64] balance before=0x000000000013fca7 after=0x000000000013fca7
[ELF64] balance before=0x000000000013fc96 after=0x000000000013fc96
[SCHED64] balance before=0x000000000013fc9a after=0x000000000013fc9a
[X64] SELFTEST PASS
build\x86_64\selftest-qemu64-32768.log: PASS: count=155 SELFTEST_PASS=1
[MM64] balance before=0x00000000007ff635 after=0x00000000007ff635
[USER64] balance before=0x00000000007ff635 after=0x00000000007ff635
[ELF64] balance before=0x00000000007ff624 after=0x00000000007ff624
[SCHED64] balance before=0x00000000007ff628 after=0x00000000007ff628
[X64] SELFTEST PASS
build\x86_64\selftest-qemu64-64-reboot1.log: PASS: count=154 SELFTEST_PASS=1
[MM64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[USER64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[ELF64] balance before=0x0000000000003d6a after=0x0000000000003d6a
[SCHED64] balance before=0x0000000000003d6e after=0x0000000000003d6e
[X64] SELFTEST PASS
build\x86_64\selftest-qemu64-64-reboot2.log: PASS: count=155 SELFTEST_PASS=1
[MM64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[USER64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[ELF64] balance before=0x0000000000003d6a after=0x0000000000003d6a
[SCHED64] balance before=0x0000000000003d6e after=0x0000000000003d6e
[X64] SELFTEST PASS
build\x86_64\selftest-qemu64-64-full.log: PASS: count=141 SELFTEST_PASS=1
[MM64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[USER64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[ELF64] balance before=0x0000000000003d6a after=0x0000000000003d6a
[SCHED64] balance before=0x0000000000003d6e after=0x0000000000003d6e
[X64] SELFTEST PASS
```

### Packaged sizes

```text
COMMAND python (stat actual build artifacts; SHA256 legacy binary)
build\kernel.bin: 736024 bytes
build\x86_64\system\kernel.bin: 432275 bytes
build\x86_64\kernel\kernel.bin: 448659 bytes
build\x86_64\selftest\kernel.bin: 538883 bytes
build\x86_64\system\userspace\video_player.elf: 457544 bytes
build\x86_64\system\codecs\libpollikvideo.a: 361974 bytes
assets\media\demo.mp4: 60162 bytes
build/kernel.bin SHA256 E89C202FBEB4894AE59BED94E2AFF6EDF5C48739CF60E3437306C5D3574F84E1
```

## Checkpoint table

| Item | Status | Command + raw evidence above | NOT RUN |
|---|---|---|---|
| MP4 demux/H264 Baseline/AAC-LC | implemented and verified | `python tests/movie_native.py`: `PASS MP4 host` | Other codecs/profiles; seeking |
| Native video/PCM/player | implemented and verified | `python tests/movie_x64.py`: `PASS native MP4`; both PMM lines 2094248 -> 2094248 | Physical hardware; WHPX |
| SDK archive + system packaging | implemented and built | `build-x86_64.ps1 -Production`: 432275 kernel bytes; native player executes | Self-hosted linking against the new archive |
| x64 regression | verified | `python tests/x86_64_boot.py`: 16 host boot PASS lines + CONSOLE PASS; raw guest counts above | Not all GUI suites rerun |
| storage regression | verified | `python tests/x86_64_storage.py`: missing/corrupt/unsupported/truncated storage refused without image changes | Crash-consistency replay in this phase |
| i386 regression | verified | `build.ps1 -NoSync`: 736024 kernel bytes; `process_stress.py --ram 64 256`: exact 5807/54911 balances; `smoke.py --notes-only`: four PASS lines | Full smoke; installer ATA/AHCI end-to-end |
| i386 size | unchanged | actual `build/kernel.bin`: 736024 -> 736024 bytes, same SHA256 | None |

No failure appeared in this completed boot/storage run. Initial adapter/test setup failures remain documented above; this does not resolve the earlier-phase intermittent audio queue timing failure.

```text
COMMAND git diff --check
git diff --check exit=0
```
