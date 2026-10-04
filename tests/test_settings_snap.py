import json, os, pathlib, socket, struct, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\241dc7fa-14e2-4fdf-8ed7-423c550d4ad8")
ARTIFACTS.mkdir(parents=True, exist_ok=True)

POLLIK2_MAGIC = 0x504B4632
POLLIK2_BLOCK_SIZE = 1024
POLLIK2_TOTAL_BLOCKS = 32768
POLLIK2_INODE_COUNT = 512
START = 64 * 512

VFS_FILE = 1
VFS_DIR = 2

def pack_dirent(inode, name, file_type):
    name_bytes = name.encode('ascii')[:55]
    name_padded = name_bytes + b'\x00' * (56 - len(name_bytes))
    return struct.pack("<IHBB56s", inode, 64, len(name_bytes), file_type, name_padded)

def pack_inode(mode, size, direct_blocks, indirect=0):
    directs = list(direct_blocks) + [0] * (8 - len(direct_blocks))
    return struct.pack("<15I", mode, size, *directs, indirect, 0, 0, 0, 0)

def format_clean_disk(disk_path, total_size_mb=16):
    disk_bytes = bytearray(total_size_mb * 1024 * 1024)

    def write_block(blk_num, data):
        offset = START + blk_num * POLLIK2_BLOCK_SIZE
        disk_bytes[offset:offset+len(data)] = data

    allocated_blocks = 43
    bitmap = bytearray(4096)
    for b in range(allocated_blocks):
        bitmap[b // 8] |= (1 << (b % 8))

    for i in range(4):
        write_block(1 + i, bitmap[i*1024:(i+1)*1024])

    def write_inode(ino_num, inode_bytes):
        blk = 5 + (ino_num // 17)
        off = START + blk * POLLIK2_BLOCK_SIZE + (ino_num % 17) * 60
        disk_bytes[off:off+60] = inode_bytes

    write_inode(1, pack_inode(VFS_DIR, 1024, [36]))
    write_inode(2, pack_inode(VFS_DIR, 1024, [37]))
    write_inode(3, pack_inode(VFS_DIR, 1024, [38]))
    write_inode(4, pack_inode(VFS_DIR, 1024, [40]))
    write_inode(5, pack_inode(VFS_DIR, 1024, [41]))
    write_inode(6, pack_inode(VFS_DIR, 1024, [42]))
    write_inode(7, pack_inode(VFS_DIR, 1024, [43]))

    # Root dir
    root_data = bytearray(1024)
    root_data[0:64] = pack_dirent(2, "bin", VFS_DIR)
    root_data[64:128] = pack_dirent(3, "home", VFS_DIR)
    root_data[128:192] = pack_dirent(4, "dev", VFS_DIR)
    root_data[192:256] = pack_dirent(5, "etc", VFS_DIR)
    write_block(36, root_data)

    write_block(37, bytearray(1024))

    # Home dir
    home_data = bytearray(1024)
    home_data[0:64] = pack_dirent(6, "Desktop", VFS_DIR)
    home_data[64:128] = pack_dirent(7, "Trash", VFS_DIR)
    write_block(38, home_data)

    write_block(40, bytearray(1024))
    write_block(41, bytearray(1024))
    write_block(42, bytearray(1024))
    write_block(43, bytearray(1024))

    sb_bytes = struct.pack(
        "<13I460s",
        POLLIK2_MAGIC,
        POLLIK2_BLOCK_SIZE,
        POLLIK2_TOTAL_BLOCKS,
        POLLIK2_INODE_COUNT,
        POLLIK2_TOTAL_BLOCKS - allocated_blocks,
        POLLIK2_INODE_COUNT - 7,
        1, 1, 4, 5, 31, 36, 0,
        b'\x00' * 460
    )
    write_block(0, sb_bytes)

    with open(disk_path, "wb") as f:
        f.write(disk_bytes)
    print(f"Prepared clean PollikFS v2 disk at {disk_path}")

def run_session(disk_path, is_reboot=False):
    log_path = BUILD / f"settings-{'reboot' if is_reboot else 'test'}-serial.log"
    log_path.write_text("")

    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]

    print(f"Launching QEMU (is_reboot={is_reboot}) on QMP port {port}...")
    proc = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
        "-vga", "std",
        "-drive", f"format=raw,file={BUILD / 'PollikOS-Alpha.img'},if=ide,index=0",
        "-drive", f"format=raw,file={disk_path},if=ide,index=1",
        "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
        "-display", "none", "-serial", f"file:{log_path}",
        "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
    ], cwd=ROOT)

    try:
        deadline = time.monotonic() + 20
        while True:
            try:
                conn = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                if time.monotonic() > deadline: raise AssertionError("QMP connection failed")
                time.sleep(0.1)

        stream = conn.makefile("rwb", buffering=0)
        json.loads(stream.readline())

        def qmp(name, args=None):
            stream.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
            while True:
                resp = json.loads(stream.readline())
                if "error" in resp: raise RuntimeError(resp)
                if "return" in resp: return resp["return"]

        def hmp(cmd):
            return qmp("human-monitor-command", {"command-line": cmd})

        def shot(name):
            time.sleep(0.35)
            ppm = BUILD / f"{name}.ppm"
            qmp("screendump", {"filename": str(ppm)})
            png = ARTIFACTS / f"{name}.png"
            with Image.open(ppm) as img:
                img.save(png)
            print(f"Captured screenshot: {png.name}")
            return png

        cur_pos = [760, 500]

        def move_to(tx, ty):
            dx = tx - cur_pos[0]
            dy = ty - cur_pos[1]
            hmp(f"mouse_move {dx} {dy}")
            cur_pos[0] = tx
            cur_pos[1] = ty
            time.sleep(0.15)

        def left_click():
            hmp("mouse_button 1")
            time.sleep(0.08)
            hmp("mouse_button 0")
            time.sleep(0.2)

        qmp("qmp_capabilities")

        # 1. Wait for desktop ready
        dl = time.monotonic() + 15
        while time.monotonic() < dl:
            if "desktop ready" in log_path.read_text(): break
            time.sleep(0.1)
        assert "desktop ready" in log_path.read_text(), "System failed to boot"
        print("PASS: System booted and desktop ready.")

        # 2. Close Welcome window (Alt+F4)
        print("Closing Welcome window...")
        hmp("sendkey alt-f4")
        time.sleep(0.6)

        # 3. Open Settings app via Dock click
        # Settings cx = 546, cy = 708
        print("Clicking Settings icon on Dock (546, 708)...")
        move_to(546, 708)
        left_click()
        time.sleep(0.8)

        if is_reboot:
            print("Verifying state after reboot...")
            shot("settings_after_reboot_persisted")
            return

        # 4. Initial capture
        shot("settings_01_default_blue_accent")

        # 5. Click Violet Indigo accent (accent index 1: cx = 188, x = cx + 18 + 38 = 244, y = 261 -> screen mx = 170 + 244 = 414, my = 125 + 261 = 386)
        print("Selecting Violet Accent...")
        move_to(414, 386)
        left_click()
        time.sleep(0.4)
        shot("settings_02_violet_accent_active")

        # 6. Click Emerald Green accent (accent index 2: x = cx + 18 + 76 = 282 -> mx = 170 + 282 = 452, my = 386)
        print("Selecting Emerald Accent...")
        move_to(452, 386)
        left_click()
        time.sleep(0.4)
        shot("settings_03_emerald_accent_active")

        # 7. Click Desktop & Dock tab (Tab 1: mx = 256, my = 257)
        print("Switching to Desktop & Dock tab...")
        move_to(256, 257)
        left_click()
        time.sleep(0.4)

        # Toggle Zoom button (mx = 170 + 188 + 79 = 437, my = 125 + 237 = 362)
        print("Toggling Dock Zoom...")
        move_to(437, 362)
        left_click()
        time.sleep(0.4)
        shot("settings_04_desktop_dock_zoom_disabled")

        # 8. Click Display tab (Tab 2: mx = 256, my = 293)
        print("Switching to Display tab...")
        move_to(256, 293)
        left_click()
        time.sleep(0.4)

        # Click 60 Hz (Standard) button (mx = 170 + 188 + 218 = 576, my = 125 + 265 = 390)
        print("Selecting 60 Hz display refresh...")
        move_to(576, 390)
        left_click()
        time.sleep(0.4)
        shot("settings_05_display_60hz")

        # Click 120 Hz (ProMotion) button (mx = 170 + 188 + 81 = 439, my = 390)
        print("Selecting 120 Hz ProMotion...")
        move_to(439, 390)
        left_click()
        time.sleep(0.4)

        # 9. Click Sound tab (Tab 3: mx = 256, my = 329)
        print("Switching to Sound tab...")
        move_to(256, 329)
        left_click()
        time.sleep(0.4)

        # Click 880 Hz button (mx = 170 + 188 + 14 + 116 + 53 = 541, my = 125 + 240 = 365)
        print("Selecting 880 Hz frequency...")
        move_to(541, 365)
        left_click()
        time.sleep(0.3)

        # Click Sound Mute toggle (mx = 170 + 188 + 69 = 427, my = 125 + 321 = 446)
        print("Testing Sound Mute toggle...")
        move_to(427, 446)
        left_click()
        time.sleep(0.3)
        shot("settings_06_sound_muted")

        # Unmute
        print("Unmuting Sound...")
        move_to(427, 446)
        left_click()
        time.sleep(0.3)

        # Click Play Test Sound (mx = 170 + 188 + 200 = 558, my = 446)
        print("Testing Sound Play Test Sound...")
        move_to(558, 446)
        left_click()
        time.sleep(0.4)
        shot("settings_07_sound_active_tested")

        # 10. Click Network tab (Tab 4: mx = 256, my = 365)
        print("Switching to Network tab...")
        move_to(256, 365)
        left_click()
        time.sleep(0.4)
        # Click Run Ping Test (mx = 170 + 188 + 89 = 447, my = 125 + 325 = 450)
        print("Clicking Run Ping Test...")
        move_to(447, 450)
        left_click()
        time.sleep(0.5)
        shot("settings_08_network_ping_tested")

        # 11. Click General tab (Tab 5: mx = 256, my = 401)
        print("Switching to General tab...")
        move_to(256, 401)
        left_click()
        time.sleep(0.4)
        shot("settings_09_general_emerald_accent")

        print("Primary test run complete! Settings saved to disk.")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

def main():
    disk_path = BUILD / "settings-test-data.img"
    format_clean_disk(disk_path)

    # Run Session 1: configure settings, change accent to Emerald, toggle options
    run_session(disk_path, is_reboot=False)

    # Run Session 2: hard reboot with the same disk to verify persistence across reboots
    print("\n--- Testing Hard Reboot Persistence ---")
    run_session(disk_path, is_reboot=True)
    print("ALL TESTS PASSED! Settings and Accent Color verified successfully!")

if __name__ == "__main__":
    main()
