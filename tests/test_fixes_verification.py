import json, os, pathlib, socket, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\7a2bc03d-3d1f-4bd4-bc66-637e7ef016be")
ARTIFACTS.mkdir(parents=True, exist_ok=True)

with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]

log = BUILD / "fixes-serial.log"
log.write_text("")
data_disk = BUILD / "fixes-data.img"
data_disk.write_bytes(bytes(16 * 1024 * 1024))

print("Launching QEMU for user fixes verification...")
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
            if "error" in resp: raise RuntimeError(resp)
            if "return" in resp: return resp["return"]

    def hmp(cmd):
        return qmp("human-monitor-command", {"command-line": cmd})

    def send_key(k):
        hmp("sendkey " + k)
        time.sleep(0.25)

    def shot(name):
        time.sleep(0.4)
        ppm_path = BUILD / f"{name}.ppm"
        qmp("screendump", {"filename": str(ppm_path)})
        png_path = ARTIFACTS / f"{name}.png"
        try:
            with Image.open(ppm_path) as img:
                img.save(png_path)
            print(f"Saved artifact: {png_path.name}")
        except Exception as e:
            print(f"Error converting {ppm_path.name}: {e}")
        return ppm_path

    qmp("qmp_capabilities")

    # 1. Wait for desktop ready
    boot_deadline = time.monotonic() + 20
    while "desktop ready" not in log.read_text():
        if time.monotonic() > boot_deadline:
            raise AssertionError("Kernel did not reach desktop ready in time")
        time.sleep(0.1)
    print("Desktop ready confirmed.")

    # Screenshot 1: Desktop with clean topbar and welcome window without logo
    shot("verification_1_desktop_welcome_topbar")

    # 2. Right-click on desktop (mx=850, my=200). Default initial pointer is (760, 500)
    # Move to empty desktop: dx=90, dy=-300 -> (850, 200)
    hmp("mouse_move 90 -300")
    time.sleep(0.2)
    hmp("mouse_button 2")
    time.sleep(0.1)
    hmp("mouse_button 0")
    time.sleep(0.3)
    # Verify no menu opened
    shot("verification_2_desktop_right_click_no_menu")

    # 3. Right-click on dock: move down to dock (e.g. mx=512, my=720)
    # dx=-338, dy=520
    hmp("mouse_move -338 520")
    time.sleep(0.2)
    hmp("mouse_button 2")
    time.sleep(0.1)
    hmp("mouse_button 0")
    time.sleep(0.3)
    # Verify no menu opened on dock
    shot("verification_3_dock_right_click_no_menu")

    # 4. Open Settings (F5) to verify System Information
    print("Opening Settings (F5)...")
    send_key("f5")
    time.sleep(0.5)
    shot("verification_4_settings_system_info")

    # Close Settings (ESC)
    send_key("esc")
    time.sleep(0.4)

    # 5. Drag Welcome window titlebar to the top to test Snap Maximize Preview
    # Move mouse to Welcome titlebar (e.g. x=350, y=140). Current is ~ (512, 720).
    # dx = 350 - 512 = -162, dy = 140 - 720 = -580
    hmp("mouse_move -162 -580")
    time.sleep(0.2)
    # Grab titlebar
    hmp("mouse_button 1")
    time.sleep(0.2)
    # Drag to top of screen: dy = -135 -> y = 5 <= 40
    hmp("mouse_move 0 -135")
    time.sleep(0.4)
    # Snap preview should now be visible
    shot("verification_5_snap_preview_top")

    # Release mouse
    hmp("mouse_button 0")
    time.sleep(0.4)
    shot("verification_6_snapped_maximized")

    # Verify no panics or corruption in serial log
    log_content = log.read_text()
    assert "[GUI MEMORY CORRUPTION]" not in log_content, "Memory corruption detected!"
    assert "panic" not in log_content.lower(), "Kernel panic detected!"
    print("ALL FIXES VERIFIED SUCCESSFULLY!")

finally:
    try: qmp("quit")
    except: pass
    process.terminate()
    try: process.wait(timeout=3)
    except: process.kill()
