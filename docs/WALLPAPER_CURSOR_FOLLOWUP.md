# Wallpaper / cursor follow-up (2026-10-04)

## Persistent data disk: blocked safely

The existing `build/PollikData.img` has a current PollikFS v2 header but is only
16,777,216 bytes long. Its header declares 32,768 blocks of 1,024 bytes starting
at byte 32,768: a minimum image length of 33,587,200 bytes. The kernel accepts the
header and mounts the present root directory; this does not validate the absent
tail. `/usr/share` does not exist. The earlier build installed wallpapers only
when creating or explicitly formatting a data disk. Preserving this older disk
therefore preserved the missing system files too.

The real disk was booted with QEMU `snapshot=on`, and its SHA-256 remained
`a622bcaf5c26730511735eda6e43456e582d4e36920b603dd303dae46ad3a289`.
No tail repair, growth, format or migration was performed. The new sync refuses
it. A separate, explicitly authorized repair of the truncated image is needed
before stock wallpapers can be installed on this particular disk.

```
python tools/sync_system_files.py build/PollikData.img
System sync REFUSED: damaged/truncated tail: image=16777216 bytes, required=33587200; image unchanged
```

## Safe system-file sync

`build.ps1` calls `tools/sync_system_files.py`; `-NoSync` skips that call. It uses
one exclusive Windows handle (share mode zero) from validation through commit.
It accepts only existing v2 geometry [31,36], validates block ownership,
references, bitmaps, free counts and directory entries, and never migrates or
formats an existing image. Missing parent directories and the two stock PNGs
are staged in RAM. Unchanged stock files are detected by exact contents; reports
use SHA-256. Replaced stock files alone release their old inode/block allocation.
All staging errors, including ENOSPC, occur before writes. The commit patches
only changed blocks and leaves the rest of the image, including its tail, alone.
Recoverable write errors roll back modified blocks. Host power loss during this
commit is **not** guaranteed atomic; this tool is not a filesystem journal.

Every unrelated regular file's size and SHA-256, every pre-existing live inode
and populated directory entry, and the image tail were compared on a copy of a
64 MiB fixture last written on 2026-09-24 (before the wallpaper milestone).
Missing directory entries necessarily change free slots in their parents;
existing populated entries remain byte-for-byte identical. A separate full
parent-directory case checks growth and preserves timestamps/reserved fields.
Full before/after lists and raw results are in `build/system-sync-evidence/`.
`tests/system_file_sync.py --boot-check` also boots the ENOSPC image after refusal.
The saved old-layout fixture is a disposable evidence copy, not the user disk.

## Deterministic GUI fixtures

`tests/gui_fixture.py` creates valid 40 MiB (or explicitly larger) disposable
images, installs both stock PNGs, selects the light theme and 100% cursor, and
finishes real account setup through PS/2 keys. It is used by gui_metrics,
surface_bounds, corners_guards, browser_js, browser_e2e, browser_responsive and
display. Smoke retains its dark theme fixture and installs both PNGs with the
same sync tool; its existing missing/corrupt/no-disk variants remain explicit.
No existing expected framebuffer colour was changed in this follow-up.

Wallpaper proof samples the independent PNG crop/nearest-neighbour calculation,
the guest cache, scene pixels and QEMU LFB screenshot. Diagnostic lines now report
the path, mount state, guest `/usr/share` entries, read/decode failures and cache
completion. Loading/scaling remains a cache-fill operation, not per-frame work.

## Cursor artwork and scaling

The Python generator authors polygons, circles and stroked lines. Artwork is
original. Rasterisation uses 4x4 supersampling at 100/125/150/200%, with ARGB
alpha and no shadow. Default is always 100%, independent of resolution. Its
arrow has a 12x19 visible bounding box on a 32x32 canvas, a white fill, a one-pixel
black outline, and hotspot (0,0). The hand hotspot is on its extended fingertip;
I-beam and resize/move/busy/not-allowed hotspots are centered. The generator's
resize arrowheads were corrected to face outwards at both ends rather than
collapsing into bars. All four scales use a lossless alpha/gray run stream,
decoded once when kind or scale changes; actual runtime cache contains ARGB.

Settings > Desktop & Dock > Cursor size saves `cursor_size=100|125|150|200` in
`/home/.config/appearance.conf`. Changing it presents immediately; the renderer
retains the old saved rectangle so a larger sprite is completely erased on a
size decrease. The constrained installer itself stays at 100%; the installed
runtime supports all four sizes. Native framebuffer tests compare every pixel
with full repaint at all scales, corners, dock and moving-window cases. Guest
checks calculate independent vector/alpha-blend expectations against actual LFB
pixels, including the previous cursor rectangle, and check real Settings clicks
and persistence. Screenshots are in `build/cursor-shots/<resolution>/`.

## Previous smoke changes: exact audit

Comparison command (full unified diff retained in the evidence folder):

```
git diff 4af6180 -- tests/smoke.py
```

The previous edits to existing checks/coordinates were:

1. Relative PS/2 packet spacing: `time.sleep(.005)` -> `time.sleep(.02)` in
   `move_mouse_precise`. This separates sub-threshold packets; default pointer
   acceleration otherwise coalesces them. No assertion was removed.
2. Notes backspace equality excludes only the displayed RTC text rectangle:

   Before:
   ```python
   assert shot("notes-restored") == before, "Note backspace did not restore document"
   ```
   After:
   ```python
   restored = shot("notes-restored")
   before_body = bytearray(before.split(b"255\n", 1)[1])
   restored_body = bytearray(restored.split(b"255\n", 1)[1])
   for y in range(7, 19):
       start = (y * 1024 + 886) * 3
       end = (y * 1024 + 989) * 3
       before_body[start:end] = b"\0" * (end - start)
       restored_body[start:end] = b"\0" * (end - start)
   assert restored_body == before_body, "Note backspace did not restore document"
   ```
   `desktop_draw_bar()` draws RTC date/time at y=7, outside the Notes window at
   y=125. This deliberately stops checking those 1,236 clock-area pixels; it
   cannot hide a Notes client edit failure. It could hide a clock-rendering
   failure there, so it is not clock regression coverage. The reverse-scroll
   screenshot equality remains completely unmasked. A forced RTC rollover is
   NOT RUN in this follow-up; the captured difference counts are recorded below.
3. Accent click before: `hmp("mouse_move -333 -114")` (comment assumed pointer
   760,500). After: `move_mouse_precise(-193,-34)` from actual 620,420 to 427,386.
   Then `move_mouse_precise(473,14)` parks at 900,400 before inspection. Existing
   `assert accent != settings` stays; new assertion checks the swatch itself:
   ```python
   assert changed_patch(settings,accent,414,373,26,26)>0, \
       "Accent click did not update the selected swatch"
   ```
4. Dock move before: `hmp("mouse_move 16 324")`, expecting icon 443,710. After:
   `move_mouse_precise(-444,310)` from 900,400 to current icon center 456,710.
   Existing `assert hover_image != accent` stays unchanged.
5. Process-table work parser before:
   ```python
   return int(re.search(rf"{pid}   WORKER     \w+ +(\d+)", table)[1])
   ```
   After (the displayed work column now follows a CPU percentage):
   ```python
   return int(re.search(rf"{pid}   WORKER     \w+ +\d+% +(\d+) / \d+", table)[1])
   ```
   Worker-progress/pause/resume assertions were not changed.
6. Stationary-cursor F11 maximize/restore, fallback, Settings wallpaper and
   acceleration-persistence checks were added. They replaced no old assertion.
   All added checks appear verbatim in the retained unified diff.

This follow-up additionally waits for `[WALLPAPER] cache=ready` before the
wallpaper-change screenshot instead of sleeping 3 seconds. The earlier run
captured the scene before synchronous scaling/presentation finished (decode had
completed). The `changed > 1000` pixel requirement is unchanged and subsequently
passed. The on-screen rendering/clock coordinates were not changed this turn.

## Known broader-suite failures

Existing assertions are preserved: gui_registry expects Settings minimum height
410 while current code declares 520; held_drag expects old inactive traffic-light
colours and rounded maximized corners; corners_guards expects old maximize work
area (width-16,height-142), while the guest returns (width,height-128);
surface_bounds expects unaccelerated PS/2 deltas while the default pointer
acceleration is enabled. These suites are not reported as passing. Their exact
failure output is retained in the follow-up logs. No unrelated GUI repair was
included to conceal these failures.

## NOT RUN / limits

- Repair or extension of the real truncated PollikData.img; unauthorized here.
- Sync and wallpaper display on that real image; safely refused. Valid old-layout
  copies and fresh fixtures have separate full sync and guest pixel coverage.
- Physical ATA/AHCI/USB hardware installation (QEMU coverage only).
- Optional cursor drop-shadow toggle (no shadow is generated or enabled).
- Forced clock/day rollover in the smoke clock-mask audit.

### Additional raw coordinate evidence and correction in this follow-up

The failing full-smoke screenshots were retained as `old-driver-*.ppm` in the
evidence directory. Searching each framebuffer for the generator's complete
opaque cursor-pixel pattern produced:

```
settings-accent.ppm arrow opaque-pattern anchors [(427, 386)]
dock-hover.ppm arrow opaque-pattern anchors [(442, 400)]
```

These are not the intended parked pointer (900,400) or Dock target (456,710).
The earlier host-only `.02` packet sleep did not ensure guest consumption while
TCG was rendering. The old test failure was exposed, not hidden by changing a
pixel expectation. This follow-up reads `mx/my` through the matching ELF and QMP,
sends one sub-threshold packet at a time and requires its exact consumption.
The three target coordinates are now absolute (427,386; 900,400; 456,710).
All accent, Dock and process assertions remain unchanged. This also makes the
existing relative helper wait for real guest consumption. New cursor guest
checks use bounded packets and explicitly wait for Dock animation completion;
an initial capture midway through composition was rejected by the exact LFB
comparison. The final capture pass retains the same pixel assertions.

Clock-mask audit of the captured Notes round trip:

```
SMOKE_CLOCK_AUDIT differences_total=0 in_clock_mask=0 outside_clock_mask=0
```

This run needed no mask to obtain equality; it does not exercise a clock rollover.

## Final verification snapshot

- Alpha and Surface: `build/kernel.bin` = 817,588 bytes; installer kernel =
  4,075,340 bytes. `-NoSync` builds completed. Default build reached the actual
  sync call and refused the existing truncated user disk, with no writes.
- Full smoke and notes-only smoke passed after consumed-packet coordinates and
  explicit F11 press/observed-state/release barriers. Maximized is state **2**
  (`WINDOW_STATE_MINIMIZED=1` in `wm.h`); a newly added guard initially used 1,
  was wrong, and was corrected to 2. No pre-existing assertion was relaxed.
- Corrupt PNG, absent wallpapers and absent data-disk smoke variants passed.
- Both resolutions have six full-resolution cursor PNGs; both runs include
  exact LFB checks and on-disk checks for 150/125/200/100 settings individually.
- ATA and AHCI install tests were repeated using the final installer artifact;
  both installed exact PNG bytes and booted the resulting target.
- Process stress: 64 MiB 7,344 -> 7,344 pages; 256 MiB 56,448 -> 56,448 pages.
- `resize_layout.py --resolution 1024x768 --skip-native` failed `top-left
  placement`; no assertion was changed. `browser_js.py` passed DHCP setup but
  timed out waiting for `BROWSER: Page loaded successfully`; its later page,
  PNG/JPEG, JS and GIF assertions were NOT RUN. This failure was not diagnosed
  as a wallpaper or cursor defect.
- `browser_e2e.py`, `browser_responsive.py`, `display.py` and x86_64 suites are
  NOT RUN in this follow-up. Their fixture changes are implemented, not claimed
  as a passing end-to-end run.

Commands and unedited output are collected in
`build/wallpaper-cursor-followup-evidence.txt`; separate full logs remain in
`build/`. Native cursor stdout was observed directly in the task tool output.
The screenshot index is `docs/WALLPAPER_CURSOR_SCREENSHOTS.md`.

## Integrity qualification: later user-disk change during normal use

The snapshot probe itself read the identical `a622bcaf...` hash before and after.
A later whole-session check read `c37e34202a379756cb3cee76fd690b68ff13d5c4b4519a2b3f1432488838db45`.
Therefore **the user disk cannot be claimed byte-identical across the entire
session**. Its last-write time is 20:37:44; the default-build refusal was logged
at 20:39:45. Its current appearance store contains `theme=0`, whereas the earlier
snapshot selected the light wallpaper. The writer and all changed bytes are not
established. A one-byte RAM-only theme hypothesis did not reproduce the old hash.
No diagnostic candidate was written to the source. A read-only evidence copy was
saved as `build/system-sync-evidence/current-user-disk-observed.img`.

Padding the missing tail **only in host RAM** also causes the strict validator to
reject `bitmap/references/free-block counter mismatch`. This was not checked on
the earlier version, so it is not labelled a newly introduced corruption. No
actual tail repair, image extension, format, migration or sync was performed on
this disk. All successful preservation tests refer to explicitly disposable
copies/fixtures. Separate investigation/authorization is needed for the real
image; the user was asked whether a normal QEMU session was active at that time.

Raw evidence:
```
wallpaper-persistent-proof.log LastWriteTime=20:28:26
PollikData.img LastWriteTime=20:37:44 Length=16777216
followup-default-sync-build.log LastWriteTime=20:39:45
USER_DATA bytes=16777216 SHA256=c37e34202a379756cb3cee76fd690b68ff13d5c4b4519a2b3f1432488838db45
pollikfs_install.PollikFsError: bitmap/references/free-block counter mismatch
```

User clarification: "Yes, I was using it" in response to the normal QEMU/Settings question for 20:37:44. The whole-session hash change coincides with confirmed normal use; individual changed bytes were not fully attributed. Snapshot-probe preservation and refused sync are separate observations.
