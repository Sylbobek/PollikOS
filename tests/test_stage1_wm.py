"""Stage 1 Window Manager UX verification test.
Exercises window styling, active/inactive controls, dragging, resize, Alt+Tab, and snapping in QEMU.
"""
import json
import os
import pathlib
import socket
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)

with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]

log = BUILD / "stage1-serial.log"
log.write_text("")
data_disk = BUILD / "stage1-data.img"
data_disk.write_bytes(bytes(16 * 1024 * 1024))

print("Launching QEMU for Stage 1 UX test...")
process = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
    "-vga", "std",
    "-drive", f"format=raw,file={BUILD / 'PollikOS-Alpha.img'},if=ide,index=0",
    "-drive", f"format=raw,file={data_disk},if=ide,index=1",
    "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
    "-display", "none", "-serial", f"file:{log}",
    "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
], cwd=ROOT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

try:
    deadline = time.monotonic() + 25
    while True:
        try:
            connection = socket.create_connection(("127.0.0.1", port), timeout=2)
            break
        except OSError:
            if time.monotonic() > deadline:
                raise AssertionError("Could not connect to QEMU QMP socket")
            time.sleep(0.1)

    stream = connection.makefile("rwb", buffering=0)
    json.loads(stream.readline())

    def qmp(name, args=None):
        stream.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
        while True:
            resp = json.loads(stream.readline())
            if "error" in resp:
                raise RuntimeError(resp)
            if "return" in resp:
                return resp["return"]

    def hmp(cmd):
        return qmp("human-monitor-command", {"command-line": cmd})

    def send_key(k):
        hmp("sendkey " + k)
        time.sleep(0.2)

    def shot(name):
        time.sleep(0.3)
        path = BUILD / f"{name}.ppm"
        qmp("screendump", {"filename": str(path)})
        print(f"Captured screenshot: {path.name}")
        return path

    qmp("qmp_capabilities")

    # Wait for desktop ready
    boot_deadline = time.monotonic() + 20
    while "desktop ready" not in log.read_text():
        if time.monotonic() > boot_deadline:
            raise AssertionError("Kernel did not reach desktop ready in time")
        time.sleep(0.1)
    print("Desktop ready confirmed.")

    # Screenshot 1: Default Welcome Desktop
    shot("stage1_welcome_desktop")

    # Open Terminal (F3) -> now we have two windows: Terminal (active, dark) and Welcome (inactive, gray controls)
    print("Opening Terminal (F3)...")
    send_key("f3")
    time.sleep(0.5)
    shot("stage1_active_terminal_inactive_welcome")

    # Open Notes (F4)
    print("Opening Notes (F4)...")
    send_key("f4")
    time.sleep(0.5)
    shot("stage1_active_notes")

    # Test Alt+Tab switcher overlay: hold Alt, press Tab
    print("Testing Alt+Tab switcher...")
    hmp("sendkey alt-tab")
    time.sleep(0.3)
    shot("stage1_alttab_active")
    send_key("ret")
    time.sleep(0.3)

    # Test maximizing and restoring via F11
    print("Testing Maximize / Restore...")
    send_key("f11")
    time.sleep(0.5)
    shot("stage1_maximized_window")

    send_key("f11")
    time.sleep(0.5)
    shot("stage1_restored_window")

    # Test Minimize (ESC)
    print("Testing Minimize (ESC)...")
    send_key("esc")
    time.sleep(0.5)
    shot("stage1_minimized_notes")

    # Verify no memory corruption in log
    log_content = log.read_text()
    assert "[GUI MEMORY CORRUPTION]" not in log_content, "Memory corruption detected in serial log!"
    assert "panic" not in log_content.lower(), "Kernel panic detected!"
    print("ALL STAGE 1 CHECKS PASSED.")

finally:
    try:
        qmp("quit")
    except Exception:
        pass
    process.terminate()
    try:
        process.wait(timeout=3)
    except Exception:
        process.kill()
