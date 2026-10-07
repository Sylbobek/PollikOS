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

## Live sites: final run

```text
COMMAND python tests/browser_sites.py --accel whpx --cpu qemu64,+rdrand
RESULT url=https://www.wikipedia.org loaded=True statuses=['200', '200', '200', '200'] JS_errors=4 panic=False heap_errors=0
RESULT url=https://en.m.wikipedia.org loaded=False statuses=[] JS_errors=0 panic=False heap_errors=0
RESULT url=http://neverssl.com loaded=False statuses=[] JS_errors=0 panic=False heap_errors=0
RESULT url=https://www.google.com loaded=True statuses=['200', '200', '200'] JS_errors=11 panic=False heap_errors=0
RESULT url=https://www.youtube.com loaded=False statuses=['200', '200', '200', '200', '200'] JS_errors=10 panic=True heap_errors=0
RESULT url=http://frogfind.com loaded=False statuses=[] JS_errors=0 panic=False heap_errors=0
LIVE_SITE_RESULTS loaded_200=2/6 (not JS/CSS conformance)
EXIT_CODE 1
```

Raw log: `build/browser-sites-final.log`; per-guest serial logs and PNG screendumps: `build/browser-site-0` through `build/browser-site-5`. Initial and type-only logs are preserved separately.

Wikipedia and Google load content with HTTP 200 and no heap-error marker in the final run, but still report unsupported JavaScript. The initial/type-only runs reported heap errors; the serializer repair has a deterministic canary reproducer, but attributing every live-site heap error solely to it is not proven.

YouTube still panics the i386 kernel. The read-only QMP stack capture pinpoints `memmove` called from `js_gc`; `js_stmt` is the next caller. The invalid large copy size comes from a damaged GC entity chain. The original cause of that entity-chain damage is **NOT FOUND**. No speculative GC patch, skipped test, disabled JavaScript, fabricated success or retry was introduced.

```text
COMMAND python (nearest symbols from llvm-nm -n build/kernel.elf for captured return addresses)
0x15772f js_gc 0x37b
0x158253 js_stmt 0x1c
reason=USER WRITE NOT_PRESENT
reason=USER WRITE PROTECTION_VIOLATION
reason=USER WRITE NOT_PRESENT [STACK_OVERFLOW_GUARD_PAGE]
reason=SUPERVISOR WRITE NOT_PRESENT [STACK_OVERFLOW_GUARD_PAGE]
EIP: 0x00116058  CS: 0x00000008  EFLAGS: 0x00010286
ESP: 0x0009b11c  EBP: 0x0009b15c  SS: 0x00000010
EAX: 0x0028b994  EBX: 0x1d915900  ECX: 0x1dba12f8  EDX: 0xe26edd0c
```

Stack: `build/browser-site-4-stack.txt`. This live failure remains an open bug, not a passed regression.

Wikipedia mobile follows two redirects but does not complete the final response within the harness deadline. The root cause remains unverified. A host curl request succeeds, which is not a guest result:

```text
COMMAND curl.exe -sS -L --max-time 25 -o build/browser-host-mobile.html -w 'HOST_MOBILE status=%{http_code} size=%{size_download}\n' https://en.m.wikipedia.org
HOST_MOBILE status=200 size=258926
```

NeverSSL failed to connect in the final IPv4 guest run after an earlier guest HTTP 200 success. Host default-family curl succeeds, while the explicit IPv4 curl times out. This does not prove a particular upstream outage, but the failure also occurs outside PollikOS on the IPv4 path. FrogFind HTTP times out both in the guest and on this host.

```text
COMMAND curl.exe -sS --max-time 12 -o NUL -w 'HOST_NEVERSSL status=%{http_code}\n' http://neverssl.com
HOST_NEVERSSL status=200
COMMAND curl.exe -4 -sS --max-time 12 -o NUL -w 'HOST_IPV4_NEVERSSL status=%{http_code} ip=%{remote_ip}\n' http://neverssl.com
HOST_IPV4_NEVERSSL status=000 ip=
curl: (28) Connection timed out after 12010 milliseconds
COMMAND curl.exe -sS --max-time 12 -o NUL -w 'HOST_FROGFIND status=%{http_code}\n' http://frogfind.com
curl: (28) Connection timed out after 12011 milliseconds
HOST_FROGFIND status=000
```

## Checkpoint table

| Item | Status | Command / evidence | NOT RUN or remaining |
|---|---|---|---|
| qemu64 HTTPS entropy | repaired | `browser_e2e.py`, qemu64 fails; qemu64,+rdrand WHPX passes HTTPS/TLS/HTTP 200 | Native HTTPS E2E; hosts without RDRAND |
| script type dispatch | repaired | `browser_cooperative.py`: two classic scripts, JSON/data untouched, module reported; `browser_images_x64.py`: data-not-executed assertion and original pixels | Modules remain unsupported |
| innerHTML bounds | repaired | `browser_html.py`: 24 failed canary assertions before; all pass after | Full parser fuzzing |
| local HTML/CSS/JS/media behavior | verified subset | `browser_js.py`, `browser_images_x64.py`: existing assertions pass | Full standards compliance |
| live sites | partial, failed matrix | final `browser_sites.py`: 2/6; HTTP content on Wikipedia/Google, JS errors remain | Mobile response; YouTube GC panic; IPv4 connectivity to NeverSSL/FrogFind |
| i386 process / notes smoke | verified at earlier checkpoint | exact 5807/54911 process baselines; notes smoke PASS lines above | Not repeated after final serializer fix |
| builds / size | built | i386 736616 B; native production 432275 B | Full suites listed above NOT RUN |

No full compatibility claim is made. Further browser work must address the GC corruption and modern JavaScript/DOM support before claiming modern sites work. The x86-64 userspace browser provides process isolation; the existing i386 browser still runs in the kernel.
