# Browser connection and script-type checkpoint

2026-10-07. This is a partial repair, not full web standards support.

## Implemented

- Both launchers expose RDRAND with the qemu64 model. The WHPX auto probe checks that same model. TLS certificate validation and secure entropy requirements remain enabled. The native launcher uses the RTC in UTC; the legacy launcher already did.
- Both browser frontends distinguish classic JavaScript, unsupported module scripts, and data blocks. JSON, JSON-LD and text/plain blocks stay in the DOM and are not evaluated. Modules remain explicitly unsupported.
- innerHTML serialization now checks capacity before writing the attribute equals sign and opening quote. The host regression exercises every capacity from 1 to 128 with byte canaries.

## Actual fixes versus limits

The real i386 error was reproduced with qemu64: secure entropy or valid UTC RTC unavailable. With qemu64,+rdrand under WHPX, the same HTTPS end-to-end test completed. No insecure RNG, certificate bypass or HTTP downgrade was added.

Elk still supports only a subset of JavaScript; the local binding layer, HTML parser and CSS engine are also incomplete. Successful DNS/TLS/HTTP 200 does not establish full page behavior, correct layout, video playback or script compatibility. Modern modules, arbitrary JS frameworks, YouTube playback, HTTP/2/3, universal TLS compatibility and a Web Platform Tests/Test262 conformance run are NOT RUN / not implemented.

Primary references: [Elk documentation](https://github.com/cesanta/elk), [HTML script type processing](https://html.spec.whatwg.org/multipage/scripting.html#the-script-element).

## Reproducers and executed results

### CPU before

```text
COMMAND $env:POLLIK_TEST_CPU='qemu64'; python tests/browser_e2e.py
COMMAND qemu-system-x86_64 -machine pc -accel tcg -cpu qemu64 -rtc base=utc -m 2G -vga std -drive format=raw,file=C:\Users\syltu\Desktop\PollikOS\build\PollikOS-Alpha.img,if=ide,index=0,snapshot=on -drive format=raw,file=C:\Users\syltu\Desktop\PollikOS\build\browser-e2e-data.img,if=ide,index=1 -netdev user,id=net0 -device rtl8139,netdev=net0 -display none -serial file:C:\Users\syltu\Desktop\PollikOS\build\browser-e2e.log -qmp tcp:127.0.0.1:56436,server=on,wait=off
TLS: secure entropy or valid UTC RTC unavailable
HTTP: TLS handshake failed
```

Raw log: `build/browser-compat-qemu64-before.log`.

### CPU after

```text
COMMAND $env:POLLIK_TEST_CPU='qemu64,+rdrand'; $env:POLLIK_TEST_ACCEL='whpx'; python tests/browser_e2e.py
COMMAND qemu-system-x86_64 -machine pc -accel whpx -cpu qemu64,+rdrand -rtc base=utc -m 2G -vga std -drive format=raw,file=C:\Users\syltu\Desktop\PollikOS\build\PollikOS-Alpha.img,if=ide,index=0,snapshot=on -drive format=raw,file=C:\Users\syltu\Desktop\PollikOS\build\browser-e2e-data.img,if=ide,index=1 -netdev user,id=net0 -device rtl8139,netdev=net0 -display none -serial file:C:\Users\syltu\Desktop\PollikOS\build\browser-e2e.log -qmp tcp:127.0.0.1:52415,server=on,wait=off
PASS: DHCP/DNS/TCP/TLS/HTTP/HTML/CSS/layout/render example.com
```

Raw log: `build/browser-compat-rdrand-whpx.log`.

### Data types before

```text
COMMAND python tests/browser_cooperative.py
FAIL line 168: script_calls==2
```

Raw log: `build/browser-script-types-before.log`.

### Data types after

```text
COMMAND python tests/browser_cooperative.py
PASS: script types: two classic scripts, JSON/data untouched, module reported unsupported
PASS: 1128 cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input
```

Raw log: `build/browser-cooperative-final.log`.

### Serialization before

```text
COMMAND python tests/browser_html.py
FAIL line 239: guarded[i]==0xa5
FAIL line 239: guarded[i]==0xa5
FAILED: 24 HTML/CSS assertions
```

Raw log: `build/browser-serialize-before.log`.

### Serialization after

```text
COMMAND python tests/browser_html.py
PASS: HTML tokenizer/DOM (entities, malformed, attrs, raw text) and CSS cascade/selectors/units
```

Raw log: `build/browser-serialize-after.log`.

### Legacy guest JS

```text
COMMAND python tests/browser_js.py
RAW verified address http://10.0.2.2:64166/
PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event
```

Raw log: `build/browser-js-final.log`.

### Native browser framebuffer

```text
COMMAND python tests/browser_images_x64.py
HTTP_REQUESTS ['/index.html', '/styles/site.css', '/scripts/app.js', '/pictures/p.png', '/pictures/p.jpg']
PIXELS PNG=3072 RGB=(18,171,52); JPEG=3072 expected=(21, 64, 218) maxerr=1; JS-styled=5163 RGB=(193,123,208)
PASS native browser: real HTTP HTML/CSS/JS, document-relative PNG/JPEG after nested script URL, framebuffer colors, clean close
```

Raw log: `build/browser-native-final.log`.

### i386 build

```text
COMMAND .\build.ps1 -NoSync
System sync: skipped (-NoSync); data image unchanged.
PollikOS built: build/PollikOS-Alpha.img (736616 kernel bytes, 1439 sectors loaded at 1 MiB)
```

Raw log: `build/browser-compat-i386-final-build.log`.

### native build

```text
COMMAND .\build-x86_64.ps1 -Production
Built build/x86_64/system/PollikOS-x86_64.img (432275 kernel bytes)
```

Raw log: `build/browser-compat-x64-final-build.log`.

### i386 process stress

```text
COMMAND python tests/process_stress.py --ram 64 256
PASS: 64 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 5807 / [PROC] [TEST] PMM free pages after:  5807
PASS: 256 MiB process stress; 100 Ring 3 runs, 100 exits; [PROC] [TEST] PMM free pages before: 54911 / [PROC] [TEST] PMM free pages after:  54911
```

Raw log: `build/browser-compat-process.log`.

### GUI notes smoke

```text
COMMAND python tests/smoke.py --notes-only
PASS: cursor visible at four corners; stationary-cursor scene repaint
PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip
PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override
PASS: pointer acceleration can be disabled and persists
```

Raw log: `build/browser-compat-smoke.log`.

## Assertions, size and safety

No existing assertion was removed or weakened. browser_e2e.py now accepts explicit CPU/accel through environment variables, still defaulting to TCG and retaining its existing TLS and HTTP 200 assertions. New assertions were added for data-block execution and bounded serialization. The native browser fixture adds typed data blocks and an inline-once marker; its original request-order and framebuffer color/size checks remain unchanged. The inline-once check also passed before the repair: no duplicate-execution fix was invented.

i386 build/kernel.bin: 736032 -> 736616 bytes (+584). Native production kernel: 432275 -> 432275 bytes. The i386 binary is below the actual current 4 MiB loader cap, but is already above the historical 524288-byte cap; this checkpoint does not claim otherwise.

All browser guests use newly created disposable data disks and boot snapshots. No guest in these tests attaches build/PollikData.img. Builds use -NoSync. Native system packaging updates generated stock data images only. No user disk formatting, on-disk format change or syscall-number change was made.

The process-stress and notes smoke runs preceded the final two-line serializer bounds fix; focused HTML, cooperative, JS and native framebuffer tests cover the final parser. Unrelated expensive regressions were not repeated after that change.

Full x64 boot/storage/selftest suite, full smoke, browser_responsive, physical hardware, native HTTPS end-to-end and universal browser conformance in this checkpoint: NOT RUN.

## Live sites

The live-site matrix and final status table are appended after the final run. Initial and type-only investigation logs are preserved in build/browser-compat-sites-before and build/browser-compat-types-only. The original matrix counted HTTP 200 loads separately from JS conformance. The strengthened final harness also reports panic and heap errors and requires neither for a successful result.
