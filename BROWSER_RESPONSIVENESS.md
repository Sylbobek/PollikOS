# Browser loading responsiveness

## Implementation

Browser navigation is still a synchronous transaction on the kernel main thread,
not a background task or resumable HTTP/TLS state machine. The transaction now
opts into a narrow cooperative desktop service:

- `kernel/net/net_util.h/.c`: `net_set_wait_service()` installs/restores the
  main-thread callback; `net_service_wait()` throttles to at most one entry per
  tick and prevents recursive callback entry. No callback is installed normally.
- `kernel/net/tcp.c`, `dns.c`, `net_manager.c`, `tls.c`, `http.c`: service at
  connect/ACK/FIN, DNS/DHCP, TLS socket-read and HTTP-read wait boundaries,
  outside packet dispatch. Successful HTTP reads also reach a checkpoint.
- `kernel/gui/app_host.h`, `kernel/desktop.c`: `app_host_service_loading()`
  drains bounded input and invokes shared frame presentation, never app polling,
  commands, client input, menu/dialog actions or network operations.
- `kernel/input_dispatch.h/.c`: `input_dispatch_poll_window_only()` uses the
  existing PS/2 decoder and 256-byte drain limit. It permits pointer motion and
  window focus/drag/resize/close/minimize/maximize. Client clicks, typing, wheel,
  right-click menus and dock launches are discarded, not replayed. Modifier and
  mouse release states are maintained. Existing modal/menu mouse movement only
  updates hover/focus; modal/menu click callbacks are not run in this mode.
- `kernel/browser/browser.h`, `browser_app.c`: a separate `load_active` guard
  spans the whole transaction, including home/error DOM replacement after the
  UI `is_loading` flag changes. Recursive `browser_poll()` is rejected. A queued
  URL is copied before loading; any later navigation remains queued. Browser
  rendering during this scope draws loading chrome without traversing/layouting
  mutable DOM. DOM-dependent input and cursor handlers are guarded too.
- `kernel/browser/html_parser.c`, `css_engine.c`, `layout.c`, `images.c`,
  `browser_app.c`: load-time checkpoints at tree traversal/node boundaries and
  selected 256-character scan intervals. `browser_work_checkpoint()` is inert
  outside the load transaction, especially during normal layout/render/events.
  CSS collection also avoids appending a newline past its 16384-byte buffer.

The previous wait callback is restored on return. Terminal HTTP commands retain
their synchronous API and behavior and do not opt into this GUI service.
Closing the Browser hides the window and clears queued navigation; it does not
cancel or free the in-flight load, which completes on its existing stack.

## Safety boundaries and limitations

Do not replace the loading service with `desktop_poll()` or `gui_apps_poll()`:
that would run application actions on partially mutated browser/network state.
Do not yield from ordinary browser drawing: the compositor draw target and
frame are mutable and cannot be re-entered there.

This is not a hard latency guarantee. Parser checkpoints bound selected work
segments, not every operation. Allocation, copying, some scans, BearSSL crypto,
STB image decode and pure Elk interpreter execution remain uninterrupted.
Elk retains its existing instruction budget. Final normal layout/rasterization
also remains synchronous. Rendering a cooperative frame may itself be costly;
one-per-tick entry throttling is not a promise of a 10 ms response time.
Other applications are not polled and do not accept client input during loading.

`about:home` performs no network request. Opening-time stalls on that page need
separate layout/raster/compositor profiling. This change does not establish that
home opening is fast or resolve all CPU-bound GUI stalls.

The final Surface also includes compositor, graphics, WM, telemetry and
PollikMark changes described in [ARCHITECTURE.md](ARCHITECTURE.md); the older
statement that those implementations were unchanged does not describe this
integrated build. None of that turns navigation into asynchronous I/O or adds
browser process isolation. The cooperative service does not bypass TLS
verification or change the terminal's synchronous transport contract.

`run.ps1` still defaults to **`PollikOS-Alpha.img`**, and `Start-PollikOS.cmd`
does not override it. The verified image requires the explicit
`-ImageName PollikOS-Surface.img` selection. Launcher refresh-rate settings and
its “120 FPS” banner are not observed FPS or VSync guarantees.

## Verification — final Surface 522560 (2026-09-18)

This section reports **existing executions**, not new tests run during this
documentation-only update. No production files, test harnesses or launchers
were edited, and no image was rebuilt for this update.

| Identity | Recorded value |
| --- | --- |
| Image | `build/PollikOS-Surface.img` |
| Kernel bytes / limit / spare | **522560 / 524288 / 1728 B** |
| Kernel SHA-256 | `64756ee53d2455ceae00d3e37717d65710377a83a3a7f4560300fb125e7c574b` |
| ELF SHA-256 | `fd5fcba0ebf98accf8094b650ceace136197bb2a9e24eeb5441769bb1249ea96` |
| Image SHA-256 | `3b6fd8ed67e1c88b6615b9cf07b4b624bb343fc1d28529c2ab4cedf60cf45cba` |
| Image/ELF association | `final-ui-522560.json`: `elf_matches_image_offset4608 = true`; identity unchanged across runs |

Sources: `build/final-ui-522560.json` and `build/final-522560-tests.json`, with
archived outputs under `build/final-ui-522560/` and the report's output paths.
Older 501136/521336-byte results and file timestamps are not substituted for
this identity.

- **Native PASS:** `browser_cooperative` links real browser transaction, HTML
  parser, CSS, layout, browser-client adapter and wait-service code. HTTP,
  raster, JS and host services are mocked. The archived final output records
  **1009 cooperative services**, covering home/success/error/close and queued
  navigation, rejecting nested load/service and mutable DOM painting/layout/
  input side effects. It is not a real PS/2/WM/network integration or speed test.
- **QEMU responsive PASS at both 1024×768 and 1920×1080:** the final UI report
  records `tests/browser_responsive.py --resolution ...` for each. Both withheld
  document and stylesheet gates retain cursor presentation, WM drag, completed
  frame and exact surface/scene/runtime-LFB/screenshot checks, plus final serial
  validation. These are cooperative integration assertions, not a hard latency
  or 60 FPS result.
- **Runtime pitch correction:** the original `/slow` failure was a test-addressing
  bug: at 1024×768, BIOS VBE retained pitch 3072 / 24 bpp after initialization
  selected DISPI pitch 4096 / 32 bpp. Surface and scene already contained
  `0x00f2eff6`, also visible in the screenshot. The harness reads runtime
  `address`/`stride`/`bytes`, retaining the exact expected color and checking
  scene and screenshot pixels. This is a probe correction, not a production
  rendering fix. Final artifacts are resolution-qualified and bounds are
  GUI-derived; the old single-resolution timestamp/tick counts are not reused
  as final measurements.
- **Recorded JS and e2e PASS:** `browser_js` and `browser_e2e` in
  `final-522560-tests.json` use the same final kernel/ELF identity. They replace
  the obsolete “still need execution” status for these specific scenarios;
  they do not prove general Web compatibility or parser safety.

The responsive regression uses a loopback HTTP server, snapshot boot image and
disposable data disk, not a public Internet endpoint or the user's PollikData.
It does not build an image. ABI checks alone are not an identity guarantee for
arbitrary future runs; the recorded matching-image proof above applies to this
final set.

### Other recorded regressions and caveats

| Scope | Final recorded outcome |
| --- | --- |
| Six native harnesses: `gui_registry`, `held_drag`, `app_layout`, `browser_cooperative`, `soft3d_native`, `pollikmark_native` | **PASS**, final UI report; real sources with stubs, not image boots |
| Standard `surface_bounds`, `corners_guards`, `resize_layout`, PollikMark — both resolutions | **PASS**, final UI report; standard surface criteria rechecked unchanged, exit 0/0; PollikMark is a limited scenario, not Run all |
| GUI benchmark and perf — both resolutions | **PASS**, `final-522560-tests.json`; actual measurements in [PERFORMANCE.md](PERFORMANCE.md), workloads in [POLLIKMARK.md](POLLIKMARK.md) |
| Smoke — 1024×768 | **PASS**, final UI report; smoke 1920×1080 **NOT RUN** (no resolution option) |
| Boot — 64 and 256 MiB | **PASS**, `final-522560-tests.json`; not a full memory stress result |
| Optional `surface_bounds --wm` — both resolutions | **Initially FAIL; later PASS after assertion correction**, as disclosed below |
| `display.py` | **NOT RUN** for this final set; stale expectations, no PASS claim |

The earlier `final-522560-tests.json` says responsive 1920 was NOT RUN because
that invocation had no resolution argument. The later final UI report records
actual PASS runs at both resolutions after the harness gained that argument;
the earlier note is not the final status.

**WM caveat:** the UI report's overall status is
`PASS_REQUESTED_SUITE_WITH_WM_ASSERTION_CHANGE_CAVEAT`. The optional WM test
originally expected MAXIMIZED (`state=2`) after minimize; source and runtime
show MINIMIZED (`state=1`), with geometry and pre-minimize state retained. The
corrected assertion checks those saved values too. Later PASS is **not a pass
of the original criterion**. Initial failures remain in
`build/final-ui-522560/initial-attempt.json` and `initial-surface_bounds-wm-*`.
No kernel changes were made for that correction. The report also discloses
browser harness resolution support and smoke snapshot selection.

No unconditional “all tests PASS”, GPU acceleration, atomic framebuffer flip,
full PollikMark Run all, arbitrary-resolution coverage, long-run stability or
browser safety claim follows from these results.
