# Native MP4 checkpoint — 2026-10-07

The x86-64 system now has a separate Ring 3 MP4 player and an optional SDK
library. Demux and software decoding happen in userspace. FFmpeg only generates
test assets and independent reference outputs; it never plays the guest's video.
No kernel decoder, existing syscall number, structure layout or disk format
was changed. The rich i386 desktop is not migrated by this change.

## Use

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build-x86_64.ps1 -Production
.\run-x86_64.ps1 -Audio
```

In the native terminal:

```text
/bin/video.pol
/bin/video.pol /home/clip.mp4
/bin/video.pol --headless /home/clip.mp4
```

Without arguments the player opens `/usr/share/videos/demo.mp4`. Files opens
`.mp4` through `/bin/video.pol`. Space pauses/resumes; Escape or the window close
button closes playback. Audio uses the existing PCM queue; video presentation
uses its consumed-audio clock, skipping late presentation while continuing
reference-frame decoding. Silent tracks use the monotonic clock.

## Supported subset

- Unfragmented MP4 with `avc1` H.264 Baseline (profile 66), no B-frame timestamp
  reordering, 8-bit planar 4:2:0; even dimensions from 16 pixels up to 640×480.
- Optional `mp4a` AAC-LC mono/stereo, 8–48 kHz source audio; output is 48-kHz
  stereo S16LE. FAAD's priming discard is counted against the MP4 edit offset
  instead of trimming the same samples twice.
- `stts`, `stsz`, `stsc`, `stco`/`co64`, `avcC` and nested `esds` descriptors.
  Atom sizes, recursion, sample/chunk counts and sample bounds inside `mdat`
  are checked. Input is limited to 16 MiB and 8192 samples per track.
- Cropped pictures are returned as contiguous YUV planes. RGB conversion uses
  integer BT.601/BT.709 coefficients and nearest chroma expansion; display
  scaling is nearest-neighbour. No colour-management/HDR implementation is claimed.

**This is not universal MP4 support.** H.264 Main/High, B frames, fragmented
MP4, HE-AAC/SBR, multiple audio/video tracks, DRM, H.265, AV1, subtitles,
network streaming, seeking and files beyond these limits are unsupported or
**NOT RUN**. High-profile test input is explicitly rejected. Physical audio,
GPU acceleration and physical-machine playback are **NOT RUN**.

## SDK and sources

`pollikos/movie.h` exposes open/close, dimensions, audio rate, video frames with
timestamps, borrowed RGB/YUV buffers, streamed PCM and decoder error status.
`/usr/lib/libpollikvideo.a` is the optional static userspace library. Link it
alongside the existing C runtime. It is not included in kernel link objects.

- [h264bsd](https://github.com/oneam/h264bsd), pinned
  `42bcb5d753ad86d84903354bf3c68423c28adb7b`: Apache-2.0 original/AOSP decoder,
  MIT wrapper additions; full notices retained.
- [FAAD2](https://github.com/FreewareAdvancedAudio/faad2), pinned
  `864ccb5d79186d427ac175c6f5a1733ea67b30c5`: GPL-2.0-or-later notices retained;
  this build enables AAC-LC only. The Nero attribution is printed by the player.

Licences are shipped under `/usr/share/licenses`. The demo is an original
generated test pattern/tone with fixed packaged bytes, not a kernel resource.

## Verification and interpretation

Host tests compare all 24 baseline frames and five cropped silent frames
byte-for-byte to FFmpeg YUV. AAC sample counts match; the explicit tolerance for
the two independent floating-point decoders is RMS ≤1 PCM16 LSB and peak ≤8 LSB.
The measured baseline clip gives RMS 0.274851 and peak 5 LSB.

Guest tests decode the same file inside PollikOS and compare every YUV frame
hash/timestamp. Two actual framebuffer images must equal the independently
converted/scaled FFmpeg-YUV reference regions. The cursor is moved outside the
region; no image assertion is masked. The first less-specific motion check was
strengthened rather than accepted as proof.

The audio-start/stop global PMM log can differ during playback because the
userspace H.264 decoder grows its reference-picture heap after audio begins.
That is not a driver-owned DMA allocation. Tests therefore also compare the
global PMM counter before launching and after fully reaping the entire player:
both headless and GUI playback must return exactly to baseline. Raw logs retain
the intermediate numbers rather than claiming they match.

Raw commands, counts, sizes, failures and the final table are in
`MP4_EVIDENCE.md`. No existing assertion was removed or weakened.
