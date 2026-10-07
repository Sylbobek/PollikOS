# Browser stability and application lifecycle

2026-10-07. This checkpoint repairs the reproduced GC corruption and close/reopen lifetime. Full HTML/CSS/JavaScript compatibility is still incomplete.

## Implemented

Elk's moving collector previously changed an object's offset while C retained the old value. DOM wrapper construction then wrote through that stale value. Explicit caller-owned roots now retain and relocate C handles; native argument intervals are rooted too. Argument OOM restores the stack interval exactly. Existing engine, syscall and on-disk structs were not resized.

DOM wrappers sequence recursive construction before reading the rooted setter target. Query lists and event objects are rooted. Native cached object/style handles are rooted and released on subtree replacement/destruction.

Closing the legacy Browser aborts its owned synchronous TCP request, stops further subresources, releases DOM/timers after the loader unwinds, and resets to about:home. A reopen during unwind starts a fresh session. Geometry is preserved. Alt-F4 is accepted as a host window operation during loading; client editing stays suppressed.

Legacy Calculator now clears its runtime model on close. It remains a kernel client, not a separate userspace process.

The native stock applications already exit on close. No kernel fix was invented for that existing behavior. A new actual-guest test verifies five distinct PIDs each for Calculator, Browser, Files, Notes and Terminal, wait/reap, fresh Calculator pixels and exact PMM return. Terminal also terminates its shell group.

## Reproduced failures and focused tests

```text
COMMAND python tests/elk_roots_native.py (before)
HANDLE local=4144 live=40
FAIL C object handle updated after compaction
FAIL native argument rooted and relocated during nested GC
COMMAND python tests/elk_roots_native.py (after)
HANDLE local=40 live=40
PASS Elk roots: reentrant native wrapper, relocated argument, exact root release

COMMAND python tests/browser_js_bindings_native.py --before-release
FAIL destroyed DOM retained C roots
COMMAND python tests/browser_js_bindings_native.py
PASS DOM bindings: forced GC, live event target, subtree invalidation, exact root release

COMMAND python tests/browser_cooperative.py (before)
FAIL line 164: !g_browser.open && !g_browser.document && !g_browser.is_loading
COMMAND python tests/browser_cooperative.py (after)
PASS: script types: two classic scripts, JSON/data untouched, module reported unsupported
PASS: 1336 cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input

COMMAND python tests/browser_url.py
PASS: HTTP cancellation aborts exactly once and releases response buffers
PASS: shared URL resolver (absolute/relative/root/dot/query/fragment/protocol-relative/port)

COMMAND python tests/calculator_close_gui.py (before)
RAW close cycle=0 expression=b'2+3' result=b'5'
AssertionError: Calculator retained runtime state after close
COMMAND python tests/calculator_close_gui.py (after)
RAW close cycle=0 expression=b'' result=b'0'
RAW close cycle=1 expression=b'' result=b'0'
RAW close cycle=2 expression=b'' result=b'0'
RAW close cycle=3 expression=b'' result=b'0'
RAW close cycle=4 expression=b'' result=b'0'
PASS legacy Calculator: close clears runtime; five fresh reopen states

COMMAND python tests/gui_registry.py
PASS: gui registry (19393 checks)

COMMAND python tests/browser_close_gui.py
RAW closed browser: document=0 loading=0 TCP before=0 after=0
PASS browser close: active HTTP aborted, DOM released, fresh home, no background requests
```

Logs: `build/elk-roots-before.log`, `build/elk-roots-final.log`, `build/js-bindings-release-before.log`, `build/js-bindings-final.log`, `build/browser-close-before.log`, `build/browser-close-after.log`, `build/http-cancel-test.log`, `build/calculator-close-before.log`, `build/calculator-close-final.log`, `build/calculator-registry-after.log`, `build/browser-close-guest-final.log`.

The before-release reproducer reconstructs the previous hunk in a copy under build; it never replaces working source. The 64-bit host before-sequencing reconstruction also passes: the ordering change is portable C sequencing, while the i386 guest comparison proves the original crash repair.

## Native close/reopen evidence

```text
COMMAND python tests/window_lifecycle_x64.py
SHELL_FIRST_COMMAND PMM before=2094270 after=2094251
PASS calculator: closed and reaped 5 PIDs=['6', '7', '8', '9', '10']; PMM before=2094251 after=2094251
PASS browser: closed and reaped 5 PIDs=['12', '13', '14', '15', '16']; PMM before=2094251 after=2094251
PASS files: closed and reaped 5 PIDs=['18', '19', '20', '21', '22']; PMM before=2094251 after=2094251
PASS notes: closed and reaped 5 PIDs=['24', '25', '26', '27', '28']; PMM before=2094251 after=2094251
PASS terminal: closed and reaped 5 PIDs=['30', '32', '34', '36', '38']; PMM before=2094251 after=2094251
PASS 25 window lifecycles: fresh processes; Calculator display resets; Terminal shell group exits; exact PMM baseline
```

Log: `build/window-lifecycle-final.log`; complete guest transcript: `build/x86_64/system/window-lifecycle.log`.

The initial comparison mistakenly included first-command initialization of the persistent shell. A separate pwd command measures that 19-page allocation before child lifetime measurement. The code path is pollish history_append -> fopen -> malloc; the allocator extends brk in 64-KiB chunks. No compensating offset is applied: exact equality is required after every application.

A later run failed the PID count because printf emits several writes and another process split a record:

```text
[LIFE] start cycle=3[terminal] shell connected through PollikOS pipes
 pid=36
[LIFE] reaped cycle=3 pid=36 exit=0
```

The test-only coordinator now uses snprintf plus one write for each short record. No PID assertion was relaxed. Failure evidence remains in `build/window-lifecycle-interleaved-failure.log` and `build/window-lifecycle-interleaved-serial.log`.

## Browser and broad regressions

```text
COMMAND python tests/browser_sites.py --accel whpx --cpu qemu64,+rdrand https://www.youtube.com
RESULT url=https://www.youtube.com loaded=True statuses=['200', '200', '200', '200', '200'] JS_errors=10 panic=False heap_errors=0
LIVE_SITE_RESULTS loaded_200=1/1 (not JS/CSS conformance)

COMMAND python tests/browser_images_x64.py
PIXELS PNG=3072 RGB=(18,171,52); JPEG=3072 expected=(21, 64, 218) maxerr=1; JS-styled=5163 RGB=(193,123,208)
PASS native browser: real HTTP HTML/CSS/JS, document-relative PNG/JPEG after nested script URL, framebuffer colors, clean close

COMMAND python tests/browser_js.py
PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event

COMMAND python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 5039 / [PROC] [TEST] PMM free pages after:  5039
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 54143 / [PROC] [TEST] PMM free pages after:  54143

COMMAND python tests/smoke.py --notes-only
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override
PASS: pointer acceleration can be disabled and persists

COMMAND python tests/x86_64_boot.py
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
PASS: missing/corrupt/unsupported/truncated storage refused without image changes
EXIT_CODE 0
```

Full logs have the prefix `build/js-lifecycle-`, including `youtube-final.log`, `native-browser-final.log`, `browser-js-final.log`, `process-final.log`, `smoke-final.log`, `x64-boot.log`, and `x64-storage.log`.

The final live-close harness waits for the actual load_active boundary, not merely is_loading (which can clear early for home pages), and checks each typed URL prefix. Tests wait for isolated boot stress before authentication: login can release the framebuffer cache and must not change the PMM count inside that isolated interval. Earlier setup failures are retained in tool output; no production retry or larger HTTP timeout was added.

## Guest marker counts and free-frame evidence

Counts are obtained from the saved logs with Python str.count("PASS:") and str.count("[X64] SELFTEST PASS"). Each successful selftest log contains the literal `[X64] SELFTEST PASS` once.

| Saved selftest label | PASS: count | SELFTEST PASS count |
|---|---:|---:|
| 16 | 154 | 1 |
| 64 | 154 | 1 |
| 256 | 154 | 1 |
| 5120 | 155 | 1 |
| 32768 | 155 | 1 |
| 64-reboot1 | 154 | 1 |
| 64-reboot2 | 155 | 1 |
| 64-full | 141 | 1 |

```text
build/x86_64/selftest-qemu64-64.log:
[MM64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[USER64] balance before=0x0000000000003d7b after=0x0000000000003d7b
[ELF64] balance before=0x0000000000003d6a after=0x0000000000003d6a
[SCHED64] balance before=0x0000000000003d6e after=0x0000000000003d6e
[X64] SELFTEST PASS
build/x86_64/selftest-qemu64-32768.log:
[MM64] balance before=0x00000000007ff635 after=0x00000000007ff635
[USER64] balance before=0x00000000007ff635 after=0x00000000007ff635
[ELF64] balance before=0x00000000007ff624 after=0x00000000007ff624
[SCHED64] balance before=0x00000000007ff628 after=0x00000000007ff628
[X64] SELFTEST PASS
```

## Changed existing assertions

These are requested lifecycle contract changes, not relaxed bug checks:

| Test | Previous expectation | Current requirement | Reason |
|---|---|---|---|
| browser_cooperative.c, close case | check_loaded required a retained document after close | NULL document, loading/pending zero, zero live allocations, one cancellation; then fresh home | User requested ending the session instead of hiding it. |
| gui_registry.c, Calculator metadata | no CLOSE callback | CLOSE callback required | Calculator now resets on close. |
| gui_registry.c, Calculator lifecycle | CLOSE was a no-op | exactly one CLOSE dispatch | Stronger routing check for the new callback. |

Other existing assertions remain. URL and framebuffer expectations were not weakened; new ownership assertions were added. Core before/after roots, repeated input, rendering and unsafe-reentry checks remain enforced.

## Builds, size and parallel work

```text
COMMAND .\build.ps1 -NoSync
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (743836 kernel bytes, 1453 sectors loaded at 1 MiB)
COMMAND .\build-x86_64.ps1 -Production
Built build/x86_64/system/PollikOS-x86_64.img (432275 kernel bytes)
COMMAND .\build-x86_64.ps1
Built build/x86_64/kernel/PollikOS-x86_64.img (448659 kernel bytes)
COMMAND .\build-x86_64.ps1 -SelfTest
Built build/x86_64/selftest/PollikOS-x86_64.img (538883 kernel bytes)
COMMAND Get-Item build/kernel.bin | Select Length
744684
```

The actual i386 binary was 736616 B at the start, and the latest observed binary is 744684 B. Concurrent auth.c/compositor.c/desktop.c edits and builds are included; this growth is not attributed exclusively to these fixes. A concurrent splash edit temporarily left compositor_splash_poll/finish undefined. No stub or revert was added; subsequent definitions and build succeeded.

The tested i386 image stayed identical through final process/smoke/browser regressions:

```text
COMMAND Get-FileHash -Algorithm SHA256 build/PollikOS-Alpha.img
BEFORE A01F4D6791EC969CD91C646B4AC25CD686C0E2326AC30DFF7452BDF99AB0A9F4
AFTER  A01F4D6791EC969CD91C646B4AC25CD686C0E2326AC30DFF7452BDF99AB0A9F4
```

The concurrent full-screen auth backdrop formula at 1024x768 allocates 1024*768*4/4096 = 768 frames, matching the difference from earlier idle process baselines. Current before/after counts are exact within each isolated run.

All fixture disks are disposable or snapshots. No user disk was formatted, and these guests never attach build/PollikData.img or backup images. No syscall number or PollikFS format changed.

## Remaining limitations and checkpoint table

Modern JS/DOM/CSS coverage, modules/frameworks, Web Platform Tests/Test262 compliance, YouTube video/MSE/DRM, arbitrary-site guarantees, IPv6, physical hardware, full smoke, browser_responsive at both resolutions and installer end-to-end: **NOT RUN / incomplete**.

Native close was verified for responsive stock apps; a program ignoring close or never polling input is not proven to terminate. Legacy clients remain in the kernel; Terminal/Notes retain their existing persistent state. Full client migration to processes is not done here. During legacy DNS/connect before socket ownership, close can still wait for the existing bounded timeout; a remote peer can keep its own connection until timeout after guest abort.

| Item | Status | Evidence | NOT RUN / remaining |
|---|---|---|---|
| Moving-GC lifetime | repaired | elk_roots_native.py and bindings test before failures / final output above | Full engine fuzzing |
| YouTube kernel crash | no panic in final live run | browser_sites.py: loaded_200=1/1, panic=False, heap_errors=0 | Video and full JS |
| Legacy Browser close | repaired | browser_close_gui.py: DOM/TCP baseline zero, fresh home | DNS/connect cancellation latency |
| Legacy Calculator reset | repaired | five raw empty expressions / result 0 | Separate i386 process migration |
| Native process close/reopen | existing behavior verified | 25 fresh/reaped PIDs; PMM 2094251 -> 2094251 | Unresponsive/close-ignoring app |
| Regressions | executed subset passes | boot/storage, process stress, notes smoke, browser JS/framebuffer | Suites listed above NOT RUN |
| i386 size | measured | 736616 -> 744684 B, includes parallel UI work | Exclusive size attribution not performed |
