import os, pathlib, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
log = BUILD / "quick-boot.log"
log.write_text("")

print("Booting QEMU to test PollikFS v2 mount & desktop init...")
proc = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
    "-vga", "std",
    "-drive", f"format=raw,file={BUILD / 'PollikOS-Alpha.img'},if=ide,index=0",
    "-drive", f"format=raw,file={BUILD / 'test-data.img'},if=ide,index=1",
    "-display", "none", "-serial", f"file:{log}",
], cwd=ROOT)

try:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        text = log.read_text(errors="replace")
        if "desktop ready" in text:
            print("Desktop ready reached!")
            break
        time.sleep(0.2)
    time.sleep(1)
    text = log.read_text(errors="replace")
    print("=== LOG OUTPUT ===")
    for line in text.splitlines():
        if any(w in line for w in ["PollikFS", "VFS", "DESKTOP", "desktop", "TRASH", "Trash", "PANIC", "ERROR"]):
            print(line)
finally:
    proc.terminate()
    proc.communicate()
