# Media, SDK and 8-GiB/30-GiB checkpoint

> MP4 status in this historical checkpoint is superseded by [MP4_CHECKPOINT.md](MP4_CHECKPOINT.md).

This checkpoint implements native x86-64 WAV/MP3 playback, reusable SDK media
APIs, native browser image loading and an 8-GiB VM profile. It does not claim all
codecs, every website, every programming language or every hardware driver.
The existing rich i386 desktop remains intact.

## Run

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build-x86_64.ps1 -Production
.\run-x86_64.ps1 -Audio
```

The native launcher defaults to 8192 MiB RAM and
`build/x86_64/system/PollikData-system-30g.img`. Both drives are snapshot-backed.
The generated profile is a new disk: existing `build/PollikData.img` and backup
images are protected. The native compact source system image remains available
for disposable tests. New profile creation preserves its entire prefix and
opaque tail, checks the source did not change, and never formats an existing
target. An existing profile is preserved and receives only the known stock
system-file updates.

**30 GiB is the virtual disk size. PollikFS v2 still has 32768 1-KiB blocks,
about 32 MiB total filesystem capacity. The other disk space is not allocatable
by v2.** Expanding usable filesystem capacity requires a separate geometry/
filesystem milestone with v2 compatibility; no on-disk format was changed here.

On NTFS, sparse marking alone initially allocated the entire extended file.
The creator now punches the freshly-created zero range before copying data.
Only the newly-created profile's known zero tail was repaired; its prefix
SHA-256 was unchanged. The fixture tests verify sparse allocation, exact copied
bytes, a personal file, an opaque tail, idempotence and protected-name refusal.

## Audio

- WAV: RIFF PCM 8/16/24/32-bit and IEEE float32; mono/stereo; standard PCM/float
  WAVEFORMATEXTENSIBLE GUIDs. Source rates 8..48 kHz. Nonfinite float samples
  become silence. Unsupported metadata and truncated chunks are rejected.
- MP3: vendored [minimp3](https://github.com/lieff/minimp3), CC0-1.0, commit
  `ea99364f61c14656440e8d77e9c233ccf3124633`. Scalar decoder in **userspace**, with
  the existing per-process FPU state isolation. No decoder is linked into the
  kernel. MP3 encoder padding is retained, and mid-stream rate changes are not
  supported by this first adapter.
- Encoded input is bounded to 16 MiB; decoded output is streamed rather than
  allocating a whole PCM song. Resampling uses integer linear interpolation to
  fixed S16LE stereo at 48000 Hz. This is not a high-quality sinc resampler.
- Kernel AC97 uses a bounded 16-buffer DMA32 queue and 32 hardware descriptors.
  It supports backpressure, drain, pause/resume, stop, failure rollback and owner
  exit cleanup. DMA is quiesced before its buffers are unmapped; a halt timeout
  disables PCI bus mastering and retires the failing controller.
- New `USER_AUDIO_STREAM = 0x504f0041`; existing numbers and structures remain
  unchanged. Begin/submit/pending/stop/pause are new scalar operations. A second
  owner receives EAGAIN. Device switching/test tones are rejected while a PCM
  stream owns the controller.
- `/bin/media.pol` is a separate Ring 3 application with Play/Pause/Stop and a
  headless mode. Files opens `.wav`/`.mp3` through that application. A generated
  original demo sound lives at `/usr/share/sounds/demo.wav`, not inside the kernel.

```text
/bin/media.pol /usr/share/sounds/demo.wav
/bin/media.pol --headless /home/music.mp3
```

The SDK headers are `pollikos/media.h`, `pollikos/audio.h` and
`pollikos/image.h`. Decoder memory ownership and fixed PCM format are documented
there. Native libraries/headers are packaged into the system disk, including the
decoder headers required by native TinyCC libc preparation.

```powershell
.\sdk\tools\pollikcc.ps1 --runtime build/x86_64/system/sdk sdk/examples/audio.c -o build/audio.pol
```

Host FFmpeg only generates independent test inputs/oracles; it is not used to
execute playback for the guest. The proof path is native MP3/WAV decoding,
SYSCALL PCM submission, AC97 DMA and QEMU audio capture.

## Browser

The native browser now loads local images and sequential HTTP image resources
after CSS and JavaScript. Images resolve relative to the HTML document, even
when scripts are fetched from another directory. RGBA buffers belong to the
DOM and are freed with it. PNG/JPEG/BMP and the first GIF frame use the existing
stb decoder in userspace, with encoded-size and decoded-dimension limits.

The test fetches actual HTML, linked CSS, JavaScript, PNG and JPEG; it verifies
both request paths and framebuffer pixel colors. Existing HTML/CSS/JS rendering
is reused. No desktop screenshot or fake web content substitutes for rendering.

This remains the existing HTML/CSS/Elk subset. React/Vue/Angular compatibility,
modern full DOM/Web APIs, SVG/WebP/AVIF, full native GIF animation and "every
website" are **NOT implemented or verified** by this checkpoint.

## Bugs found while implementing

1. Initial streaming code wrote Run on every enqueue. QEMU's AC97 model fetches
   the next descriptor on each such write, so completed-frame counters could
   advance despite dropped audio. A register-write fixture and an actual
   captured-frame assertion reproduced it. Run is now written only to start
   the channel. Pause/resume preserves the unplayed descriptor portion by
   rebuilding the stopped descriptor list. See the
   [QEMU controller implementation](https://github.com/qemu/qemu/blob/master/hw/audio/ac97.c).
2. The first browser test used Escape to close the application; its existing
   contract uses Escape to leave address editing. The test now clicks the native
   titlebar close button. Browser behavior and existing assertions were retained.
3. BMP decoding required `abs`, absent from the SDK. Standard C `abs`, `labs`
   and `llabs` were added, preserving ISO C's minimum-signed-value limitation.

## Remaining scope

| Requested area | Current state |
|---|---|
| WAV/MP3 | Implemented and native playback tested |
| MP4/M4A/AAC/H.264/H.265/AV1/WebM/Opus/FLAC/Vorbis | NOT implemented / NOT RUN |
| 8 GiB RAM + 30 GiB device | Booted and identified in guest |
| 30 GiB usable filesystem | NOT implemented; v2 still about 32 MiB |
| Native browser PNG/JPEG, HTML/CSS/JS integration | Implemented; real HTTP and pixel checks |
| All websites/frameworks | NOT implemented / NOT RUN |
| SDK | Existing C SDK extended; not a claim of complete C/POSIX coverage |
| New Python/Rust/C++/Java runtimes | NOT implemented / NOT RUN |
| All missing hardware drivers | NOT implemented; USB/WLAN/BT/HDA/USB audio remain pending |
| Physical audio/boot/network hardware | NOT RUN |

The next media stage is MP4 demux plus AAC/H.264 decoding, using this PCM API
and userspace graphics path. Raw evidence and checkpoint test results are in
`MEDIA_SDK_EVIDENCE.md`.
