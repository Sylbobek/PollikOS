"""Boot on a disposable data disk and wait for the kernel's real process tests.

The kernel runs, after the desktop is up: the three Ring 3 fault-isolation
ELFs, then 100 real spawn/run/exit/reap cycles of the embedded hello ELF and
compares the PMM free-page count before and after. This script asserts the
address-space isolation self-test, the fault tests and the stress balance all
passed, and that no kernel panic occurred.

Usage: python tests/process_stress.py [--ram 256] [--timeout 90]
Images are selected with POLLIK_TEST_IMAGE (default: PollikOS-Alpha.img).
Never opens PollikData.img.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def run(ram, timeout):
    log = BUILD / f"process-stress-{ram}.log"
    log.write_text("")
    image = BUILD / os.environ.get("POLLIK_TEST_IMAGE", "PollikOS-Alpha.img")
    with tempfile.TemporaryDirectory(prefix="pollikos-stress-") as directory:
        disk = Path(directory) / "data.img"
        with disk.open("wb") as stream:
            stream.truncate(40 * 1024 * 1024)
        args = ["qemu-system-x86_64", "-machine", "pc", "-m", f"{ram}M",
                "-vga", "std", "-nic", "none", "-display", "none",
                "-serial", f"file:{log}", "-monitor", "none", "-no-reboot",
                "-drive", f"format=raw,file={image},if=ide,index=0,snapshot=on",
                "-drive", f"format=raw,file={disk},if=ide,index=1"]
        process = subprocess.Popen(args, stderr=subprocess.PIPE,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            deadline = time.monotonic() + timeout
            text = ""
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if "[TEST] PHASE 2 PASS" in text or "[TEST] PHASE 2 FAIL" in text or "PANIC" in text:
                    break
                if process.poll() is not None:
                    raise AssertionError(process.stderr.read().decode(errors="replace"))
                time.sleep(.2)
            text = log.read_text(errors="replace")
        finally:
            process.terminate()
            process.communicate(timeout=5)

    def expect(marker):
        assert marker in text, f"missing {marker!r} in {log}"

    assert "PANIC" not in text, f"kernel panic in {log}"
    expect("Isolation self-test PASSED")
    expect("desktop ready")
    expect("addr=0x00000000 USER WRITE NOT_PRESENT")
    expect("addr=0x00100000 USER WRITE PROTECTION_VIOLATION")
    expect("[STACK_OVERFLOW_GUARD_PAGE]")
    expect("[TEST] spawn cycles completed: 100")
    expect("[TEST] spawn failures: 0")
    expect("[TEST] PMM no leak")
    expect("[TEST] PHASE 2 PASS")
    assert "Isolation test FAILED" not in text, log
    assert "PHASE 2 FAIL" not in text, log
    hello_runs = text.count("Hello from Ring 3 ELF!")
    assert hello_runs >= 100, f"only {hello_runs} hello runs reached Ring 3 ({log})"
    exits = text.count("exited code=42\n")
    assert exits >= 100, f"only {exits} clean exits ({log})"
    before = [line for line in text.splitlines() if "PMM free pages before" in line]
    after = [line for line in text.splitlines() if "PMM free pages after" in line]
    print(f"PASS: {ram} MiB process stress; {hello_runs} Ring 3 runs, {exits} exits; {before[-1].strip()} / {after[-1].strip()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ram", nargs="+", type=int, default=[256])
    parser.add_argument("--timeout", type=int, default=90)
    options = parser.parse_args()
    for ram in options.ram:
        run(ram, options.timeout)
