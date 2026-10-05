"""Boot the real disk in QEMU and exercise PS/2 input through QMP."""
import json
import struct
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
parser.add_argument('--no-wallpapers', action='store_true', help='Exercise the no-wallpaper fallback path')
parser.add_argument('--corrupt-wallpaper', action='store_true', help='Exercise corrupt PNG fallback')
parser.add_argument('--no-data-disk', action='store_true', help='Boot without a PollikFS data disk')
options = parser.parse_args()
if options.no_wallpapers and options.corrupt_wallpaper:
    parser.error('--no-wallpapers and --corrupt-wallpaper are mutually exclusive')

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
sys.path.insert(0, str(ROOT / "tools"))
from sync_system_files import sync as install_wallpapers
if not options.no_wallpapers:
    install_wallpapers(data_disk)
if options.corrupt_wallpaper:
    from sync_system_files import StagedImage
    fixture_fs = StagedImage.load(data_disk)
    fixture_fs.remove_system_file('/usr/share/wallpapers/light.png')
    fixture_fs.install_file('/usr/share/wallpapers/light.png', b'not a valid PNG')
    fixture_fs.save(data_disk)
qemu_args = [
    "qemu-system-x86_64", "-machine", "pc", "-m", "64M", "-vga", "std",
    "-drive", f"format=raw,file={BUILD / os.environ.get('POLLIK_TEST_IMAGE', 'PollikOS-Alpha.img')},if=ide,index=0,snapshot=on",
    "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
    "-display", "none", "-serial", f"file:{log}",
    "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
]
if not options.no_data_disk:
    qemu_args[9:9] = ["-drive", f"format=raw,file={data_disk},if=ide,index=1"]
process = subprocess.Popen(qemu_args, cwd=ROOT,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
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

    def move_mouse_precise(dx, dy):
        x, y = pointer_position()
        move_mouse_to(max(0,min(1023,x+dx)),max(0,min(767,y+dy)))

    pointer_symbols = {}
    for line in subprocess.check_output(['llvm-nm','-S',str(BUILD/'kernel.elf')],text=True).splitlines():
        fields=line.split()
        if len(fields)==4 and fields[3] in ('mx','my','g_windows'):
            assert fields[3] not in pointer_symbols,'ambiguous pointer symbol'
            pointer_symbols[fields[3]]=int(fields[0],16)
    assert len(pointer_symbols)==3,'missing pointer/window symbols'

    def notes_window_state():
        path=BUILD/'smoke-window-state.bin'
        qmp('pmemsave',{'val':pointer_symbols['g_windows']+3*84+6*4,'size':4,'filename':str(path)})
        return int.from_bytes(path.read_bytes(),'little')

    def wait_notes_state(expected):
        deadline=time.monotonic()+15
        while notes_window_state()!=expected:
            assert time.monotonic()<deadline,('Notes F11 state',expected,notes_window_state())
            time.sleep(.01)

    def notes_f11(expected):
        # Queue the break before waiting for a potentially slow frame. Holding
        # F11 while polling can trigger typematic and toggle a second time.
        hmp('sendkey f11 100')
        wait_notes_state(expected)
        time.sleep(.12)
        assert notes_window_state()==expected, 'F11 repeated after the requested state'
        path=BUILD/'smoke-notes-window.bin'
        qmp('pmemsave',{'val':pointer_symbols['g_windows']+3*84,'size':84,'filename':str(path)})
        print('RAW Notes F11 window',struct.unpack('<21I',path.read_bytes())[2:7],flush=True)

    def pointer_position():
        result=[]
        for name in ('mx','my'):
            path=BUILD/'smoke-pointer.bin'
            qmp('pmemsave',{'val':pointer_symbols[name],'size':4,'filename':str(path)})
            result.append(int.from_bytes(path.read_bytes(),'little'))
        return tuple(result)

    def move_mouse_to(x,y):
        deadline=time.monotonic()+20
        while pointer_position()!=(x,y):
            assert time.monotonic()<deadline,('pointer target',x,y,pointer_position())
            mx,my=pointer_position()
            sx,sy=max(-5,min(5,x-mx)),max(-5,min(5,y-my))
            hmp(f'mouse_move {sx} {sy}')
            # A host sleep alone did not guarantee guest consumption: reports
            # could merge or overflow while a slow TCG frame was in progress.
            while pointer_position()!=(mx+sx,my+sy):
                assert time.monotonic()<deadline,'PS/2 packet was not consumed exactly'
                time.sleep(.005)

    def changed_patch(a, b, x, y, width, height):
        a, b = a[-1024*768*3:], b[-1024*768*3:]
        return sum(a[(py*1024+px)*3:(py*1024+px+1)*3] !=
                   b[(py*1024+px)*3:(py*1024+px+1)*3]
                   for py in range(y, y+height) for px in range(x, x+width))

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
    if options.no_data_disk:
        fallback = "GFX wallpaper unavailable; procedural background active\n"
        assert "SETUP: PollikFS unavailable; explicit format required" in log.read_text(), \
            "Missing data disk did not leave the system in its recoverable setup screen"
        assert log.read_text().count(fallback) == 1, "Missing data disk did not log exactly one fallback"
        qmp("quit")
        process.wait(timeout=5)
        print('PASS: missing data disk reaches setup and uses one procedural fallback log')
        raise SystemExit(0)
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
    # Account creation writes its serial marker just before the auth surface is
    # replaced by the desktop. Wait for two identical frames so the cursor-only
    # comparison cannot accidentally include that full-screen transition.
    previous = desktop
    for attempt in range(12):
        stable = shot(f"welcome-stable-{attempt}")
        if stable == previous:
            desktop = stable
            break
        previous = stable
    else:
        raise AssertionError("Desktop did not settle before cursor-only check")
    assert b"1024 768" in desktop[:40], "Wrong display dimensions"
    assert "INPUT PS/2 wheel ready" in log.read_text()
    # Moving only the pointer must leave every other desktop pixel untouched.
    hmp("mouse_move -100 -100")
    moved = shot("cursor-moved")
    a,b=desktop[-1024*768*3:],moved[-1024*768*3:]
    changed=sum(a[i:i+3]!=b[i:i+3] for i in range(0,len(a),3))
    assert 0<changed<2304, f"Cursor invalidated unrelated pixels: {changed}"
    # The hotspot remains visible at every edge/corner; the arrow mirrors toward
    # the display when its normal top-left orientation would be clipped away.
    corner_targets = ((-2048,-2048,0,0,0,0), (2048,0,992,0,1020,0),
                      (0,2048,992,736,1020,764), (-2048,0,0,736,0,764))
    previous = desktop
    for index,(dx,dy,px,py,hx,hy) in enumerate(corner_targets):
        move_mouse(dx,dy)
        current = shot(f"cursor-corner-{index}")
        pixels = changed_patch(previous,current,px,py,32,32)
        assert 0<pixels<=32*32, f"Cursor missing or unbounded at corner {index}: {pixels} pixels"
        tip_w = min(4, 1024-hx); tip_h = min(4, 768-hy)
        assert changed_patch(previous,current,hx,hy,tip_w,tip_h)>0, \
            f"Cursor artwork missing near the hotspot at corner {index}"
        previous = current
    move_mouse_precise(512,-55)
    dock_frame = shot("cursor-over-dock")
    dock_pixels = changed_patch(previous,dock_frame,496,696,32,32)
    assert 0<dock_pixels<=32*32, f"Cursor missing or unbounded over the dock: {dock_pixels} pixels"
    # Open Notes while the pointer is stationary over its future client area.
    # The window scene changes beneath the cursor; the saved background must be
    # refreshed before any later pointer movement.
    move_mouse_precise(-12,-412)
    stationary_before = shot("cursor-stationary-before-window")
    key("f4")
    stationary_after = shot("cursor-stationary-after-window")
    assert changed_patch(stationary_before,stationary_after,484,284,32,32)>0, \
        "Opening a window beneath a stationary cursor did not repaint the scene"
    # Maximize and restore Notes from the keyboard; the window changes position
    # and size while the PS/2 pointer stays fixed over its client area.
    stationary_before = shot("cursor-stationary-before-window-move")
    assert notes_window_state()==0,'Notes must start in its normal viewport'
    notes_f11(2)
    stationary_after = shot("cursor-stationary-after-window-move")
    assert changed_patch(stationary_before,stationary_after,484,284,32,32)>0, \
        "Moving a window beneath a stationary cursor did not repaint the scene"
    notes_f11(0)
    shot("cursor-stationary-after-window-restore")
    print('Notes F11 state: normal=0 maximized=2 restored=0',flush=True)
    print("PASS: cursor visible at four corners; stationary-cursor scene repaint")
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
    restored = shot("notes-restored")
    # The RTC date/time may tick between captures. Mask only its rendered
    # text; compare every other framebuffer pixel exactly.
    before_body = bytearray(before.split(b"255\n", 1)[1])
    restored_body = bytearray(restored.split(b"255\n", 1)[1])
    for y in range(7, 19):
        start = (y * 1024 + 886) * 3
        end = (y * 1024 + 989) * 3
        before_body[start:end] = b"\0" * (end - start)
        restored_body[start:end] = b"\0" * (end - start)
    assert restored_body == before_body, "Note backspace did not restore document"
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
        key("f5")
        if options.no_wallpapers:
            shot("settings-no-wallpapers")
            fallback = "GFX wallpaper unavailable; procedural background active\n"
            assert log.read_text().count(fallback) == 1, "Missing wallpapers did not log exactly one fallback"
            qmp("quit")
            process.wait(timeout=5)
            print('PASS: missing wallpaper files use one procedural fallback log')
            raise SystemExit(0)
        wallpaper_before = shot("settings-wallpaper-before")
        cache_count = log.read_text().count("[WALLPAPER] cache=ready")
        path=BUILD/'smoke-settings-window.bin'
        qmp('pmemsave',{'val':pointer_symbols['g_windows']+4*84,'size':84,'filename':str(path)})
        settings_window=struct.unpack('<21I',path.read_bytes())
        sx,sy=settings_window[2:4]
        # New contract: wallpaper is selected by the theme; no independent rows.
        move_mouse_to(sx+188+65, sy+142)  # Light theme tile
        hmp("mouse_button 1")
        time.sleep(.1)
        hmp("mouse_button 0")
        deadline = time.monotonic() + 45
        before_pixels = wallpaper_before.split(b"255\n", 1)[1]
        while True:
            wallpaper_after = shot("settings-wallpaper-selected")
            after_pixels = wallpaper_after.split(b"255\n", 1)[1]
            changed = sum(before_pixels[(y*1024+x)*3:(y*1024+x+1)*3] !=
                          after_pixels[(y*1024+x)*3:(y*1024+x+1)*3]
                          for y in range(380, 650) for x in range(900, 1024))
            if changed > 1000: break
            assert time.monotonic()<deadline, f'Wallpaper cache ready but LFB not repainted ({changed} pixels)'
        print('RAW wallpaper LFB changed pixels',changed,flush=True)
        assert changed > 1000, f"Switching theme did not repaint the desktop ({changed} pixels)"
        assert log.read_text().count("[WALLPAPER] cache=ready")==cache_count, "Theme switch redecoded a PNG"
        move_mouse_to(sx+30,sy+130)  # Desktop & Dock tab
        hmp("mouse_button 1")
        time.sleep(.1)
        hmp("mouse_button 0")
        move_mouse_to(sx+188+50,sy+406)  # Pointer Acceleration toggle
        hmp("mouse_button 1")
        time.sleep(.1)
        hmp("mouse_button 0")
        fallback = "GFX wallpaper unavailable; procedural background active\n"
        if options.corrupt_wallpaper:
            assert log.read_text().count(fallback) == 1, "Corrupt PNG did not log exactly one fallback"
        else:
            assert fallback not in log.read_text(), "Valid wallpaper unexpectedly fell back"
        qmp("quit")
        process.wait(timeout=5)
        from pollikfs_install import PollikFsImage
        settings_fs = PollikFsImage.load(data_disk)
        appearance = settings_fs.read_file("/home/.config/appearance.conf").decode("ascii")
        assert "theme=1\n" in appearance, "Light theme was not persisted"
        assert "wallpaper=" not in appearance, "Independent wallpaper override persisted"
        assert "pointer_accel=0\n" in appearance, "Pointer acceleration off state was not persisted"
        print('PASS: focused GUI cursor, Terminal, Notes editing and wheel round trip')
        print('PASS: theme selects matching PollikFS wallpaper, no repeated PNG decode, theme persists without override')
        print('PASS: pointer acceleration can be disabled and persists')
        raise SystemExit(0)
    key("f5")
    settings = shot("settings")
    # Pointer is at (620,420). Move to the second accent swatch at (427,386)
    # with separated sub-threshold PS/2 reports so acceleration stays neutral.
    move_mouse_to(427,386)
    time.sleep(.2)
    hmp("mouse_button 1")
    time.sleep(.15)
    hmp("mouse_button 0")
    move_mouse_to(900,400)  # move away before checking the swatch pixels
    accent = shot("settings-accent")
    assert accent != settings, "PS/2 click did not change the selected accent"
    assert changed_patch(settings,accent,414,373,26,26)>0, \
        "Accent click did not update the selected swatch"
    key("esc")
    move_mouse_to(456,710)  # Terminal dock icon center
    print(f'Dock pointer actual={pointer_position()} expected=(456, 710)',flush=True)
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
        return int(re.search(rf"{pid}   WORKER     \w+ +\d+% +(\d+) / \d+", table)[1])
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
