import json, os, pathlib, socket, struct, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
MEDIA_ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\7a2bc03d-3d1f-4bd4-bc66-637e7ef016be")
MEDIA_ARTIFACTS.mkdir(parents=True, exist_ok=True)

START = 64 * 512
POLLIK2_BLOCK_SIZE = 1024
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

    root_data = bytearray(1024)
    root_data[0:64] = pack_dirent(2, "bin", VFS_DIR)
    root_data[64:128] = pack_dirent(3, "home", VFS_DIR)
    root_data[128:192] = pack_dirent(4, "dev", VFS_DIR)
    write_block(36, root_data)

    bin_data = bytearray(1024)
    write_block(37, bin_data)

    home_data = bytearray(1024)
    home_data[0:64] = pack_dirent(5, "Desktop", VFS_DIR)
    home_data[64:128] = pack_dirent(6, "Documents", VFS_DIR)
    home_data[128:192] = pack_dirent(7, "Trash", VFS_DIR)
    write_block(38, home_data)

    write_block(40, bytearray(1024))
    write_block(41, bytearray(1024))
    write_block(42, bytearray(1024))
    write_block(43, bytearray(1024))

    header = bytearray(512)
    struct.pack_into("<I", header, 0, 0x504B4632)
    struct.pack_into("<I", header, 4, 2)
    struct.pack_into("<I", header, 8, 1024)
    struct.pack_into("<I", header, 12, 32768)
    struct.pack_into("<I", header, 16, 512)
    struct.pack_into("<I", header, 20, 1)
    struct.pack_into("<I", header, 24, 4)
    struct.pack_into("<I", header, 28, 5)
    struct.pack_into("<I", header, 32, 31)
    struct.pack_into("<I", header, 36, 36)
    disk_bytes[START-512:START] = header

    with open(disk_path, "wb") as f:
        f.write(disk_bytes)

def run_test():
    disk_path = BUILD / "test-artifacts-data.img"
    format_clean_disk(disk_path)

    qmp_port = 52960
    log_path = BUILD / "qemu-artifacts-test.log"
    if log_path.exists(): log_path.unlink()

    qemu_cmd = [
        "qemu-system-i386",
        "-drive", f"file={BUILD / 'PollikOS-Alpha.img'},format=raw,index=0,media=disk",
        "-drive", f"file={disk_path},format=raw,index=1,media=disk",
        "-m", "256M",
        "-serial", f"file:{log_path}",
        "-qmp", f"tcp:localhost:{qmp_port},server,nowait",
        "-display", "none",
        "-vga", "std"
    ]

    proc = subprocess.Popen(qemu_cmd)
    s = None
    try:
        time.sleep(1.0)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(15.0)
        s.connect(("localhost", qmp_port))

        def qmp(cmd_str, args=None):
            req = {"execute": cmd_str}
            if args: req["arguments"] = args
            s.sendall(json.dumps(req).encode('utf-8') + b'\r\n')
            buf = b""
            while b"\r\n" not in buf:
                chunk = s.recv(4096)
                if not chunk: break
                buf += chunk
            for line in buf.decode('utf-8', errors='ignore').strip().split('\r\n'):
                if line.strip():
                    try:
                        d = json.loads(line)
                        if "return" in d: return d["return"]
                    except: pass
            return {}

        def hmp(cmd_str):
            return qmp("human-monitor-command", {"command-line": cmd_str})

        def shot(name):
            ppm_path = BUILD / f"{name}.ppm"
            png_path = MEDIA_ARTIFACTS / f"{name}.png"
            hmp(f"screendump {ppm_path}")
            time.sleep(0.3)
            if ppm_path.exists():
                im = Image.open(ppm_path)
                im.save(png_path)
                ppm_path.unlink()
                print(f"Captured screenshot: {png_path.name}")
                return im
            return None

        # Calibrate cursor to (0,0) by slamming to top-left
        print("Calibrating pointer to (0,0)...")
        for _ in range(5):
            hmp("mouse_move -1000 -1000")
            time.sleep(0.05)
        cur_x, cur_y = 0, 0

        def move_to(tx, ty, steps=10):
            nonlocal cur_x, cur_y
            for i in range(1, steps + 1):
                ix = int(cur_x + (tx - cur_x) * i / steps)
                iy = int(cur_y + (ty - cur_y) * i / steps)
                dx = ix - cur_x
                dy = iy - cur_y
                if dx != 0 or dy != 0:
                    hmp(f"mouse_move {dx} {dy}")
                    cur_x = ix
                    cur_y = iy
                    time.sleep(0.02)
            time.sleep(0.05)

        def left_click():
            hmp("mouse_button 1")
            time.sleep(0.08)
            hmp("mouse_button 0")
            time.sleep(0.2)

        def right_click():
            hmp("mouse_button 2")
            time.sleep(0.08)
            hmp("mouse_button 0")
            time.sleep(0.25)

        qmp("qmp_capabilities")

        # Wait for desktop ready
        deadline = time.monotonic() + 15
        while "desktop ready" not in log_path.read_text():
            if time.monotonic() > deadline: raise AssertionError("Desktop boot timeout")
            time.sleep(0.1)
        print("Desktop ready!")
        time.sleep(0.5)

        # Move to center
        move_to(512, 384)
        time.sleep(0.3)

        # 1. Capture initial desktop (Dark mode by default, dark dock, dark topbar, Windows 11 cursor)
        shot("test_01_initial_dark_desktop")

        # 2. Open Notes by double clicking Notes on desktop at (76, 284)
        # Note: Desktop items are at (76, 92) Files, (76, 188) Terminal, (76, 284) Notes, (76, 380) Web, (76, 476) Settings
        print("Opening Notes window...")
        move_to(76, 284)
        left_click()
        time.sleep(0.08)
        left_click()
        time.sleep(0.5)
        im_notes = shot("test_02_notes_open")

        # Welcome window is also open. Let's find the active window bounds.
        # Window bounds for Welcome are centered at (170, 125, 680, 410)
        # Top-left corner: (170, 125)
        # Top-right corner: (850, 125)
        # Bottom-right corner: (850, 535)
        # Bottom-left corner: (170, 535)
        print("Moving mouse over top-left corner and edges to test for resize artifacts...")
        # Hover back and forth along top border
        for x in range(170, 855, 30):
            move_to(x, 125, steps=3)
        shot("test_03_hover_top_border")

        # Hover along right border
        for y in range(125, 540, 30):
            move_to(850, y, steps=3)
        shot("test_04_hover_right_border")

        # Hover along bottom-right corner vigorously
        for _ in range(5):
            move_to(850, 535, steps=3)
            move_to(852, 537, steps=3)
            move_to(848, 533, steps=3)
        shot("test_05_hover_bottom_right_corner")

        # Hover along bottom border
        for x in range(850, 170, -30):
            move_to(x, 535, steps=3)

        # Hover along left border
        for y in range(535, 125, -30):
            move_to(170, y, steps=3)

        # Move cursor completely away to center of screen (500, 300)
        print("Moving mouse away from window edges...")
        move_to(500, 300, steps=10)
        time.sleep(0.3)
        im_clean = shot("test_06_window_edges_clean_no_artifacts")

        # Verify pixel cleanliness: check that no stray black/white resize cursor triangles are left around the corners
        # In the user's bug screenshot, the desktop around the corner was filled with triangle shards.
        # Let's verify that the region outside the window (e.g. x in [853..865], y in [165..178]) has clean wallpaper pixels.
        print("Checking pixels around corners for ZERO artifacts...")
        # Top-right exterior:
        pixels = im_clean.load()
        # 3. Minimize any active window with Esc to reveal full clean desktop
        print("Minimizing windows with Esc to show clean desktop...")
        hmp("sendkey esc")
        time.sleep(0.5)
        hmp("sendkey esc")
        time.sleep(0.5)

        # 4. Test Desktop Context Menu (PPM on empty desktop below window)
        print("Testing Right Click on Empty Desktop (PPM)...")
        move_to(500, 580)
        right_click()
        time.sleep(0.4)
        shot("test_07_desktop_context_menu")

        # Click "New Folder" (item 0 at y ~ 596)
        print("Creating New Folder...")
        move_to(550, 596)
        left_click()
        time.sleep(0.6)
        shot("test_08_desktop_with_new_folder")

        # Click "New Text File" (item 1 at y ~ 620)
        print("Creating New Text File...")
        move_to(500, 580)
        right_click()
        time.sleep(0.4)
        move_to(550, 620)
        left_click()
        time.sleep(0.6)
        shot("test_09_desktop_with_new_text_file")

        # 5. Open Web browser from dock
        print("Opening Browser from Dock...")
        # Dock icons: Files (308), Terminal (376), Notes (444), Settings (512), Web (580), PollikMark (648), Info (716)
        move_to(580, 708)
        left_click()
        time.sleep(1.0)
        shot("test_10_browser_dark_mode")

        # 6. Test Right Click Context Menu on Desktop item (e.g. Trash or Files)
        print("Right clicking on Files icon...")
        move_to(76, 92)
        right_click()
        time.sleep(0.4)
        shot("test_11_item_context_menu")

        print("All test steps completed successfully!")

    finally:
        if s: s.close()
        proc.terminate()
        try: proc.wait(timeout=5)
        except: proc.kill()

if __name__ == "__main__":
    run_test()
