# Performance experiment log

## Preflight: requested SDL guest benchmark display (2026-10-04)

Scope: PollikMark full-run baseline at 1024x768, CPU qemu64, first TCG then WHPX, `-display sdl`. No graphics-kernel changes were made.

Attempt 1:

```text
$ POLLIK_GUI_DISPLAY=sdl POLLIK_GUI_CPU=qemu64 POLLIK_GUI_ACCEL=tcg python tests/pollikmark.py --full-run --resolution 1024x768
TCG 1024x768 exit=1
ConnectionResetError: [WinError 10054] An existing connection was forcibly closed by the remote host
```

Attempt 2, isolated exact SDL guest boot with disposable PollikFS disk, TCG/qemu64, QMP, VGA:

```text
RAW exact SDL guest probe QEMU exit before QMP= None
RAW exact SDL guest probe greeting={"QMP":{"version":{"qemu":{"micro":0,"minor":0,"major":11},"package":"v11.0.0-12122-ga4bb4b10c9"},"capabilities":["oob"]}}
RAW exact SDL QEMU exit=3221225477 stderr=
```

Attempt 3:

```text
$ POLLIK_GUI_DISPLAY=sdl POLLIK_GUI_CPU=qemu64 POLLIK_GUI_ACCEL=whpx python tests/pollikmark.py --full-run --resolution 1024x768
WHPX 1024x768 exit=1
ConnectionResetError: [WinError 10054] An existing connection was forcibly closed by the remote host
```

Decision after three attempts: SDL-backed guest measurement is blocked by QEMU process termination during startup on this host. The temporary display/timeout changes to `tests/gui_metrics.py` were reverted exactly to `HEAD`; `git status --short` was empty after the revert. Continue independent native graphics work. PollikMark values collected with `-display none` must be labeled separately and cannot be compared with historical SDL results. SDL baseline and after numbers are NOT RUN.

Raw logs: `build/perf-base-pollikmark-tcg-1024x768.log`, `build/perf-base-pollikmark-whpx-1024x768.log`.

## PollikMark fallback TCG baseline timeout (2026-10-04)

After SDL guest startup failed, one separate TCG/qemu64 run used the harness's
default `-display none` as a diagnostic only. It is not comparable with the
historical SDL result.

```text
$ POLLIK_GUI_CPU=qemu64 POLLIK_GUI_ACCEL=tcg python tests/pollikmark.py --full-run --resolution 1024x768
GUI fixture: appearance installed before QEMU launch
PASS startup PMM check completed before full PollikMark
PollikMark timer: source=1 resolution_us=1 tsc_khz=4626310
AssertionError: all eight PollikMark workloads complete
```

The run reached the harness's 300-second full-suite deadline before producing
the eight workload results. TCG full-run numbers and all eight baseline scores
are NOT RUN. QEMU was no longer running when checked afterward. This is one
diagnostic attempt; the SDL failure decision above remains unchanged.
