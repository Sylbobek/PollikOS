"""Boot diagnostics on disposable disks; never opens PollikData.img.

Usage: python tests/boot_memory.py [--ram 64 256] [--expect-oom]
Images are selected with POLLIK_TEST_IMAGE (default: PollikOS-Surface.img).
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def boot(ram, expect_oom):
    log = BUILD / f"boot-memory-{ram}.log"
    log.write_text("")
    with tempfile.TemporaryDirectory(prefix="pollikos-boot-") as directory:
        disk = Path(directory) / "data.img"
        with disk.open("wb") as stream:
            stream.truncate(40 * 1024 * 1024)
        args = ["qemu-system-x86_64", "-machine", "pc", "-m", f"{ram}M",
                "-vga", "std", "-nic", "none", "-display", "none",
                "-serial", f"file:{log}", "-monitor", "none", "-no-reboot",
                "-drive", f"format=raw,file={BUILD / os.environ.get('POLLIK_TEST_IMAGE', 'PollikOS-Surface.img')},if=ide,index=0,snapshot=on",
                "-drive", f"format=raw,file={disk},if=ide,index=1"]
        process = subprocess.Popen(args, stderr=subprocess.PIPE,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if "desktop ready" in text or "FATAL: cannot allocate" in text:
                    break
                if process.poll() is not None:
                    raise AssertionError(process.stderr.read().decode(errors="replace"))
                time.sleep(.1)
            text = log.read_text(errors="replace")
            if expect_oom:
                assert "FATAL: cannot allocate window surface" in text, text
                assert "desktop ready" not in text, text
            else:
                assert "desktop ready" in text, text
                assert "FATAL" not in text and "PANIC" not in text, text
            print(f"PASS: {ram} MiB {'expected surface allocation failure' if expect_oom else 'desktop boot'}")
            print("\n".join(line for line in text.splitlines() if "[GUI]" in line or "[WM]" in line))
        finally:
            process.terminate()
            process.communicate(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ram", nargs="+", type=int, default=[64, 256])
    parser.add_argument("--expect-oom", action="store_true")
    options = parser.parse_args()
    for ram in options.ram:
        boot(ram, options.expect_oom)
