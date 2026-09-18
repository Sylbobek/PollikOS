"""Boot the real disk in QEMU and exercise PS/2 input through QMP."""
import json
import pathlib
import os
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]
log = BUILD / "smoke-serial.log"
log.write_text("")
data_disk = BUILD / "smoke-data.img"
data_disk.write_bytes(bytes(4 * 1024 * 1024))
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
    body_rows = [y for y in range(768) if any(
        pixels[i:i+3] == b"\xfa\xf9\xfc"
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
    key("f5")
    settings = shot("settings")
    # Initial pointer is (760,500); ocean palette is at (514..794,249..323).
    hmp("mouse_move -110 -220")
    time.sleep(.2)
    hmp("mouse_button 1")
    time.sleep(.15)
    hmp("mouse_button 0")
    ocean = shot("settings-ocean")
    assert ocean[-1024*3:] != settings[-1024*3:], "PS/2 click did not change wallpaper"
    key("esc")
    # Move to terminal dock icon (508,710) and open it with the mouse.
    hmp("mouse_move -142 430")
    time.sleep(.2)
    hover_image=shot("dock-hover")
    assert hover_image != ocean, "Dock hover did not redraw"
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
    shell("open test.txt")
    assert "xyz" in shell("cat"), "Saved file did not survive reboot"
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
