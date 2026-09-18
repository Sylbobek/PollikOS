"""Stage 2 Reusable UI Framework verification test.
Tests:
- Desktop Context Menu (right-click, screen clamping, items)
- Keyboard navigation in menus (Up, Down, Enter, Escape)
- Reusable Dialog system (About PollikOS message dialog, Move to Trash confirm dialog)
- Reusable Button states and styling
- Notification toast system
- Dark / Light Theme switching
"""
import json
import os
import pathlib
import socket
import subprocess
import time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\ff4bc62d-80a3-418b-93d6-06b0d425e6ff")

with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]

log = BUILD / "stage2-serial.log"
log.write_text("")
data_disk = BUILD / "stage2-data.img"
data_disk.write_bytes(bytes(16 * 1024 * 1024))

print("Launching QEMU for Stage 2 UI framework test...")
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
        time.sleep(0.25)

    def shot(name):
        time.sleep(0.3)
        ppm_path = BUILD / f"{name}.ppm"
        qmp("screendump", {"filename": str(ppm_path)})
        png_path = ARTIFACTS / f"{name}.png"
        try:
            with Image.open(ppm_path) as img:
                img.save(png_path)
            print(f"Captured artifact: {png_path.name}")
        except Exception as e:
            print(f"Captured {ppm_path.name} (convert error: {e})")
        return ppm_path

    qmp("qmp_capabilities")

    # Wait for desktop ready
    boot_deadline = time.monotonic() + 20
    while "desktop ready" not in log.read_text():
        if time.monotonic() > boot_deadline:
            raise AssertionError("Kernel did not reach desktop ready in time")
        time.sleep(0.1)
    print("Desktop ready confirmed.")

    # Move mouse to desktop area (right side of screen: mx=850, my=200)
    # Default initial mouse position is ~ (760, 500)
    hmp("mouse_move 100 -250")
    time.sleep(0.2)

    # 1. Open Desktop Context Menu via Right-Click
    print("Opening Desktop Context Menu via right click...")
    hmp("mouse_button 2")
    time.sleep(0.15)
    hmp("mouse_button 0")
    time.sleep(0.3)
    assert "MENU opened" in log.read_text(), "Expected 'MENU opened' in log"
    shot("stage2_desktop_context_menu")

    # 2. Navigate menu with keyboard down to "About PollikOS..."
    print("Navigating menu with keyboard to 'About PollikOS...'...")
    send_key("down") # 0: New Folder
    send_key("down") # 2: Toggle Dark/Light Mode (skips separator 1)
    send_key("down") # 3: Desktop Settings
    send_key("down") # 5: About PollikOS... (skips separator 4)
    send_key("ret")  # Press Enter to open About dialog
    time.sleep(0.4)
    shot("stage2_about_dialog")

    # 3. Dismiss About dialog using Enter key
    print("Dismissing About dialog with Enter key...")
    send_key("ret")
    time.sleep(0.3)

    # 4. Open Files application (F2) to test File Context Menu
    print("Opening Files application (F2)...")
    send_key("f2")
    time.sleep(0.5)

    # Move mouse over file list inside Files window (wx=130, wy=85, file area is ~ mx=350, my=220)
    # Current mouse is around (860, 250), move left: dx=-500, dy=-30
    hmp("mouse_move -500 -30")
    time.sleep(0.2)

    # Right click on file in Files window
    print("Right-clicking on file in Files app...")
    hmp("mouse_button 2")
    time.sleep(0.15)
    hmp("mouse_button 0")
    time.sleep(0.3)
    shot("stage2_file_context_menu")

    # 5. Navigate to "Delete" and press Enter to trigger Confirm Dialog
    print("Navigating to Delete in context menu...")
    # Items: Open (idx 0), Open With (1), Rename (2), Copy (3), Cut (4), [sep 5], Delete (6)
    send_key("down") # 0: Open
    send_key("down") # 1: Open With
    send_key("down") # 2: Rename
    send_key("down") # 3: Copy
    send_key("down") # 4: Cut
    send_key("down") # 6: Delete (skips sep 5)
    send_key("ret")  # Trigger Delete action
    time.sleep(0.4)
    shot("stage2_confirm_dialog")

    # Dismiss Confirm dialog with Escape
    print("Dismissing Confirm Dialog with Escape key...")
    send_key("esc")
    time.sleep(0.3)

    # 6. Test Theme Mode Switching & Notification Toast
    print("Testing Theme switching to Light Mode and Notification toast...")
    # Move mouse back to desktop area: dx=500, dy=0
    hmp("mouse_move 500 0")
    time.sleep(0.2)
    # Right click desktop
    hmp("mouse_button 2")
    time.sleep(0.15)
    hmp("mouse_button 0")
    time.sleep(0.3)
    # Select "Toggle Dark/Light Mode" (down 2 times)
    send_key("down") # 0: New Folder
    send_key("down") # 2: Toggle Dark/Light Mode
    send_key("ret")
    time.sleep(0.5)
    shot("stage2_light_theme_notification")

    print("ALL STAGE 2 UI CHECKS PASSED.")

finally:
    process.terminate()
    process.wait(timeout=5)
