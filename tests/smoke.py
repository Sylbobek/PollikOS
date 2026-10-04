"""Boot the real disk in QEMU and exercise PS/2 input through QMP."""
import json
import pathlib
import os
import socket
import subprocess
import time
import argparse
import sys
from format_pollikfs2 import format_disk

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--notes-only', action='store_true', help='Run cursor, Terminal and Notes checks only')
options = parser.parse_args()

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]
log = BUILD / "smoke-serial.log"
log.write_text("")
data_disk = BUILD / "smoke-data.img"
format_disk(data_disk, total_size_mb=40)
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
from pollikfs_install import PollikFsImage
fixture_fs = PollikFsImage.load(data_disk)
fixture_fs.install_file("/home/test.pol", (BUILD / "hello.elf").read_bytes())
fixture_fs.save(data_disk)
process = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "pc", "-m", "64M", "-vga", "std",
    "-drive", f"format=raw,file={BUILD / os.environ.get('POLLIK_TEST_IMAGE', 'PollikOS-Alpha.img')},if=ide,index=0,snapshot=on",
    "-drive", f"format=raw,file={data_disk},if=ide,index=1",
    "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
    "-display", "none", "-serial", f"file:{log}",
    "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
], cwd=ROOT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
try:
    deadline = time.monotonic() + 15
    while True:
        try:
            connection = socket.create_connection(("127.0.0.1", port), timeout=2)
            break
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(.1)
    stream = connection.makefile("rwb", buffering=0)
    json.loads(stream.readline())

    def qmp(name, args=None):
        stream.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
        while True:
            response = json.loads(stream.readline())
            if "error" in response:
                raise RuntimeError(response)
            if "return" in response:
                return response["return"]

    def hmp(command):
        return qmp("human-monitor-command", {"command-line": command})

    def move_mouse(dx, dy):
        while dx or dy:
            sx = max(-80, min(80, dx))
            sy = max(-80, min(80, dy))
            hmp(f"mouse_move {sx} {sy}")
            dx -= sx
            dy -= sy
            time.sleep(.005)

    def key(name):
        hmp("sendkey " + name)
        time.sleep(.15)

    def shot(name):
        time.sleep(.25)
        path = BUILD / (name + ".ppm")
        qmp("screendump", {"filename": str(path)})
        return path.read_bytes()

    qmp("qmp_capabilities")
    while "desktop ready" not in log.read_text():
        if time.monotonic() > deadline:
            raise AssertionError("Kernel did not reach the desktop")
        time.sleep(.1)
    # The disposable disk starts with a valid empty filesystem, so first boot
    # must finish account setup before desktop shortcuts are usable.
    if "SETUP: first-run installer ready" not in log.read_text():
        raise AssertionError("Fresh smoke filesystem did not enter first-run setup")
    key("ret")
    for char in "smoke":
        key(char)
    key("ret")
    for _ in range(2):
        for char in "smokepass":
            key(char)
        key("ret")
    deadline = time.monotonic() + 15
    while "AUTH: account created; installation complete" not in log.read_text():
        if time.monotonic() > deadline:
            raise AssertionError("First-run account setup did not complete")
        time.sleep(.1)
    desktop = shot("welcome")
    assert b"1024 768" in desktop[:40], "Wrong display dimensions"
    assert "INPUT PS/2 wheel ready" in log.read_text()
    # Moving only the pointer must leave every other desktop pixel untouched.
    hmp("mouse_move -100 -100")
    moved = shot("cursor-moved")
    a,b=desktop[-1024*768*3:],moved[-1024*768*3:]
    changed=sum(a[i:i+3]!=b[i:i+3] for i in range(0,len(a),3))
    assert 0<changed<2304, f"Cursor invalidated unrelated pixels: {changed}"
    hmp("mouse_move 100 100")
    key("f3")
    terminal = shot("terminal")
    assert terminal != desktop, "F3 did not open terminal"
    for char in "about":
        key(char)
    key("ret")
    assert shot("terminal-about") != terminal, "Shell did not update"
    key("f4")
    key("ctrl-s")
    before = shot("notes-before")
    key("x")
    assert shot("notes-typed") != before, "Note did not accept text"
    key("backspace")
    key("ctrl-s")
    assert shot("notes-restored") == before, "Note backspace did not restore document"
    # The welcome document fits the responsive viewport; it cannot scroll.
    # Measure the active Notes body from the actual framebuffer (full window
    # includes 34px chrome), then use notes.c's 150px reserve / 20px row pitch.
    pixels = before[-1024*768*3:]
    # See GUI_SMOKE_FIXTURE.md: current dark compositor uses 121520; the
    # historical faf9fc body remains valid for light-mode Notes.
    body_colors = (b"\x12\x15\x20", b"\xfa\xf9\xfc")
    body_rows = [y for y in range(768) if any(
        pixels[i:i+3] in body_colors
        for i in range(y*1024*3, (y+1)*1024*3, 3))]
    assert body_rows, "Active Notes body not found"
    # Matching pixels also occur in dock icons: use the longest contiguous run,
    # not the span from the first matching pixel to the last one on the screen.
    body_height = run = 0
    previous = -2
    for y in body_rows:
        run = run + 1 if y == previous + 1 else 1
        body_height = max(body_height, run)
        previous = y
    note_height = body_height + 34
    visible_rows = max(1, (note_height - 150) // 20)
    # Distinct short lines ensure a three-row wheel step changes text pixels.
    for row in range(visible_rows + 3):
        key("ret")
        for digit in str(row):
            key(digit)
    key("ctrl-s")
    # Typing follows the end; start the wheel round trip at the document top.
    for _ in range((visible_rows + 12) // 10):
        key("pgup")
    before = shot("notes-scroll-top")
    print(f"Notes wheel fixture: {note_height}px window, {visible_rows} visible rows, "
          f"{visible_rows + 3} added lines", flush=True)
    qmp("input-send-event", {"events":[{"type":"btn","data":{"down":True,"button":"wheel-down"}}]})
    qmp("input-send-event", {"events":[{"type":"btn","data":{"down":False,"button":"wheel-down"}}]})
    assert shot("notes-wheel") != before, "Mouse wheel did not scroll notes"
    qmp("input-send-event", {"events":[{"type":"btn","data":{"down":True,"button":"wheel-up"}}]})
    qmp("input-send-event", {"events":[{"type":"btn","data":{"down":False,"button":"wheel-up"}}]})
    assert shot("notes-wheel-restored") == before, "Reverse scroll did not restore notes"
    if options.notes_only:
        print('PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip')
        qmp('quit')
        raise SystemExit(0)
    key("f5")
    settings = shot("settings")
    # Settings opens at (170,125); accent swatch 1 is centered at local
    # (257,261), so move there from the current pointer at (760,500).
    hmp("mouse_move -333 -114")
    time.sleep(.2)
    hmp("mouse_button 1")
    time.sleep(.15)
    hmp("mouse_button 0")
    accent = shot("settings-accent")
    assert accent != settings, "PS/2 click did not change the selected accent"
    key("esc")
    # Move from the accent swatch to the Terminal dock icon at (443,710).
    hmp("mouse_move 16 324")
    time.sleep(.2)
    hover_image=shot("dock-hover")
    assert hover_image != accent, "Dock hover did not redraw"
    hmp("mouse_button 1")
    time.sleep(.15)
    hmp("mouse_button 0")
    shot("dock-terminal")
    assert "APP opened" in log.read_text(), "Dock did not respond to mouse"
    key("f1")
    shot("final-desktop")

    def shell(command):
        key("f3")
        start = len(log.read_text())
        for c in command:
            key("spc" if c == " " else "dot" if c == "." else c)
        key("ret")
        deadline = time.monotonic() + 5
        while True:
            result = log.read_text()[start:]
            if "SHELL END" in result or (command == "reboot" and "own kernel entered" in result):
                return result
            if time.monotonic() > deadline:
                raise AssertionError(f"Command did not complete: {command}: {result}")
            time.sleep(.05)

    # Explorer launches a real Ring 3 .pol executable.
    key("f2")
    time.sleep(.1)
    launch_start = len(log.read_text())
    key("down")
    key("down")
    key("down")
    key("ret")
    deadline = time.monotonic() + 5
    while "[TEST] EVENT_QUEUE PASS" not in log.read_text()[launch_start:]:
        if time.monotonic() > deadline:
            raise AssertionError("Explorer did not launch the selected .pol Ring 3 program")
        time.sleep(.05)
    installed = shell("ls /Applications")
    assert ".app" not in installed, "Applications folder still contains placeholder .app packages"
    move_mouse(-2048, -2048)
    assert "FS commit OK" in log.read_text()
    assert "NET RTL8139 ready" in log.read_text()
    assert "RUNNING" in shell("ps")
    assert "partial updates" in shell("gfx")
    import re
    first = shell("ps")
    time.sleep(.3)
    second = shell("ps")
    def work(table, pid):
        return int(re.search(rf"{pid}   WORKER     \w+ +(\d+)", table)[1])
    assert work(second, 1) > work(first, 1), "Worker 1 is not executing"
    assert work(second, 2) > work(first, 2), "Worker 2 is not executing"
    paused = shell("pause 1")
    later = shell("ps")
    assert work(paused, 1) == work(later, 1), "Paused process still running"
    assert work(later, 2) > work(paused, 2), "Other process stopped with paused worker"
    shell("resume 1")
    shell("faulttest")
    time.sleep(.3)
    assert "PROCESS isolated fault vector=13" in log.read_text(), "CPL3 privileged operation was not isolated"
    assert "STOPPED" in shell("ps"), "Faulting process not stopped"
    assert "RUNNING" in shell("spawn 1"), "Process restart failed"
    shell("ping")
    time.sleep(.5)
    assert "NET PING reply OK" in log.read_text(), log.read_text()
    shot("network-terminal")
    assert "PING OK" in shell("net")
    shot("network-status")
    shell("new test.txt")
    key("x")
    key("y")
    key("z")
    key("ctrl-s")
    assert "test.txt" in shell("ls")
    # Reset through the guest's keyboard controller, then re-open the file.
    boot_count = log.read_text().count("desktop ready")
    shell("reboot")
    deadline = time.monotonic() + 15
    while log.read_text().count("desktop ready") == boot_count:
        if time.monotonic() > deadline:
            raise AssertionError("Reboot failed")
        time.sleep(.1)
    assert "FS mounted persistent snapshot" in log.read_text()
    deadline = time.monotonic() + 15
    while "AUTH: login required" not in log.read_text():
        if time.monotonic() > deadline:
            raise AssertionError("Reboot did not show the account login screen")
        time.sleep(.1)
    for char in "smokepass":
        key(char)
    key("ret")
    deadline = time.monotonic() + 15
    while "AUTH: login accepted" not in log.read_text():
        if time.monotonic() > deadline:
            raise AssertionError("Test account login failed after reboot")
        time.sleep(.1)
    shell("open test.txt")
    assert "xyz" in shell("cat test.txt"), "Saved file did not survive reboot"
    shell("open welcome.txt")
    assert "File removed" in shell("rm test.txt")
    assert "test.txt" not in shell("ls"), "Removed file still listed"
    key("f1")
    shot("final-desktop")
    print("PASS: boot, rounded UI, input, shell, note editing, PS/2 mouse, dock")
    print("PASS: durable files + reboot + deletion, ring3 scheduling/pause/fault/restart, ARP + ICMP ping")
    qmp("quit")
finally:
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
