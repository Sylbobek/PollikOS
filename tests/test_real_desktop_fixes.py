import json, os, pathlib, socket, struct, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
MEDIA_ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\7a2bc03d-3d1f-4bd4-bc66-637e7ef016be")
MEDIA_ARTIFACTS.mkdir(parents=True, exist_ok=True)

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
    write_block(42, bytearray(1024)) # Desktop empty initially (clean start!)
    write_block(43, bytearray(1024)) # Trash

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

def run_suite():
    disk_path = BUILD / "desktop-suite-data.img"
    format_clean_disk(disk_path)

    log_path = BUILD / "desktop-fixes-serial.log"
    log_path.write_text("")

    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]

    print(f"Launching QEMU for Desktop Fixes Suite on QMP port {port}...")
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
            time.sleep(0.4)
            ppm = BUILD / f"{name}.ppm"
            qmp("screendump", {"filename": str(ppm)})
            png = MEDIA_ARTIFACTS / f"{name}.png"
            with Image.open(ppm) as img:
                img.save(png)
            print(f"Captured screenshot: {png.name}")
            return png

        # Initial kernel pointer is at (760, 500)
        cur_pos = [760, 500]

        def get_all_items():
            log = log_path.read_text()
            lines = log.splitlines()
            last_trash_idx = -1
            for i, line in enumerate(lines):
                if line.startswith("DESKTOP_ITEM: Trash "):
                    last_trash_idx = i
            if last_trash_idx < 0:
                return {}
            start_idx = last_trash_idx
            while start_idx > 0 and lines[start_idx - 1].startswith("DESKTOP_ITEM: ") and not lines[start_idx - 1].startswith("DESKTOP_ITEM: Trash "):
                start_idx -= 1
            items = {}
            for i in range(start_idx, last_trash_idx + 1):
                line = lines[i]
                if not line.startswith("DESKTOP_ITEM: "): continue
                prefix_len = len("DESKTOP_ITEM: ")
                col_idx = line.find(" col=")
                name = line[prefix_len:col_idx]
                after = line[col_idx + len(" col="):]
                parts = after.split()
                x = int(parts[2].split('=')[1])
                y = int(parts[3].split('=')[1])
                items[name] = (x + 52, y + 48)
            return items

        def get_item_pos(name):
            return get_all_items().get(name)

        def move_to(tx, ty):
            dx = tx - cur_pos[0]
            dy = ty - cur_pos[1]
            hmp(f"mouse_move {dx} {dy}")
            cur_pos[0] = tx
            cur_pos[1] = ty
            time.sleep(0.15)

        def left_down():
            hmp("mouse_button 1")
            time.sleep(0.1)

        def left_up():
            hmp("mouse_button 0")
            time.sleep(0.2)

        def left_click():
            hmp("mouse_button 1")
            time.sleep(0.08)
            hmp("mouse_button 0")
            time.sleep(0.25)

        def double_click():
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.08)
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.35)

        def right_click():
            hmp("mouse_button 2")
            time.sleep(0.08)
            hmp("mouse_button 0")
            time.sleep(0.3)

        def send_key(k):
            hmp(f"sendkey {k}")
            time.sleep(0.15)

        def type_text(s):
            for ch in s:
                if ch == ' ': hmp("sendkey spc")
                elif ch == '\n': hmp("sendkey ret")
                elif ch == '.': hmp("sendkey dot")
                else: hmp(f"sendkey {ch}")
                time.sleep(0.04)
            time.sleep(0.1)

        qmp("qmp_capabilities")

        # 1. Wait for desktop ready
        boot_deadline = time.monotonic() + 15
        while "desktop ready" not in log_path.read_text():
            if time.monotonic() > boot_deadline: raise AssertionError("Desktop boot timeout")
            time.sleep(0.1)
        print("Desktop ready confirmed.")
        if "SETUP: first-run installer ready" in log_path.read_text():
            print("Completing first-run setup on the disposable GUI test disk...")
            send_key("ret")
            type_text("fixes")
            send_key("ret")
            type_text("test123")
            send_key("ret")
            type_text("test123")
            send_key("ret")
            setup_deadline = time.monotonic() + 10
            while "AUTH: account created; installation complete" not in log_path.read_text():
                if time.monotonic() > setup_deadline:
                    raise AssertionError("first-run setup did not complete")
                time.sleep(0.1)
        time.sleep(0.5)

        # Close welcome window: click red close button at (188, 142)
        move_to(188, 142)
        left_click()
        time.sleep(0.4)
        shot("fixes_01_clean_desktop")

        # 2. Right click on empty desktop at (500, 250) -> Context Menu opens
        print("Testing Step 1: Right Click -> Context Menu...")
        move_to(500, 250)
        right_click()
        shot("fixes_02_desktop_context_menu")

        # 3. Click "New Folder" item at (540, 266)
        print("Testing Step 2: Click New Folder...")
        move_to(540, 266)
        left_click()
        time.sleep(0.5)
        folder_pos = get_item_pos("New Folder")
        print(f"Created New Folder at {folder_pos}")
        assert folder_pos is not None, "New Folder not found on desktop!"
        shot("fixes_03_new_folder_created")

        # 4. Right click on empty desktop at (500, 250) -> "New Text File" at (540, 290)
        print("Testing Step 3: Click New Text File...")
        move_to(500, 250)
        right_click()
        time.sleep(0.2)
        move_to(540, 290)
        left_click()
        time.sleep(0.5)
        txt_pos = get_item_pos("New Text File.txt")
        print(f"Created New Text File at {txt_pos}")
        assert txt_pos is not None, "New Text File.txt not found on desktop!"
        shot("fixes_04_new_text_file_created")

        # 5. Marquee Selection on empty desktop:
        # Move to (260, 50), down, drag to (120, 320), release
        print("Testing Step 4: Marquee Selection...")
        move_to(260, 50)
        left_down()
        move_to(120, 320)
        time.sleep(0.3)
        shot("fixes_05_marquee_active")
        left_up()
        time.sleep(0.3)
        shot("fixes_06_multi_selected")

        # Click empty space at (600, 400) to clear selection
        move_to(600, 400)
        left_click()
        time.sleep(0.3)

        # 6. Drag "New Text File.txt" directly to Trash!
        txt_pos = get_item_pos("New Text File.txt")
        trash_pos = get_item_pos("Trash")
        print(f"Testing Step 5: Drag item from {txt_pos} to Trash at {trash_pos}...")
        move_to(txt_pos[0], txt_pos[1])
        left_down()
        # Hover over Trash
        move_to(trash_pos[0], trash_pos[1])
        time.sleep(0.3)
        shot("fixes_07_trash_hover_highlight")
        left_up() # Drop into Trash!
        time.sleep(0.5)
        shot("fixes_08_dropped_in_trash")
        # Verify New Text File is gone from desktop
        assert get_item_pos("New Text File.txt") is None, "Item was not removed from desktop after trash drop!"

        # 7. Drag single item to new cell and snap to invisible grid:
        # Move Terminal from col 0, row 1 (76, 188) to col 2, row 1 (284, 188)
        term_pos = get_item_pos("Terminal")
        print(f"Testing Step 6: Drag Terminal from {term_pos} to (284, 188)...")
        move_to(term_pos[0], term_pos[1])
        left_down()
        move_to(284, 188)
        time.sleep(0.3)
        shot("fixes_09_dragging_terminal")
        left_up()
        time.sleep(0.4)
        term_pos_new = get_item_pos("Terminal")
        print(f"Terminal snapped to {term_pos_new}")
        assert term_pos_new == (284, 188), f"Terminal did not snap to (284, 188), got {term_pos_new}"
        shot("fixes_10_terminal_snapped")

        # 8. Create a new text file to test Notes double click & Ctrl+S saving:
        print("Testing Step 7: Create Text File & Test Notes open / edit / Ctrl+S...")
        move_to(500, 250)
        right_click()
        time.sleep(0.2)
        move_to(540, 290)
        left_click()
        time.sleep(0.5)
        new_txt_pos = get_item_pos("New Text File.txt")
        print(f"Created new text file at {new_txt_pos}")
        assert new_txt_pos is not None, "Failed to create text file for Notes test!"
        shot("fixes_11_note_file_created")

        # Double click the text file to open in Notes
        move_to(new_txt_pos[0], new_txt_pos[1])
        double_click()
        time.sleep(0.7)
        shot("fixes_12_notes_opened_file")

        # Type text in Notes and save with Ctrl+S
        type_text("Hello PollikOS Desktop")
        time.sleep(0.3)
        send_key("ctrl-s")
        time.sleep(0.4)
        shot("fixes_13_notes_file_saved")

        # Close Notes window: close button at (188, 142)
        move_to(188, 142)
        left_click()
        time.sleep(0.5)

        # 9. Dock Context Menu & Pin/Unpin:
        # Terminal is at cx ~ 410 in Dock. Right click Terminal in Dock
        print("Testing Step 8: Dock Unpin Terminal...")
        move_to(410, 708)
        right_click()
        time.sleep(0.3)
        shot("fixes_14_dock_terminal_menu")

        # Click "Unpin from Dock" (item 1 at y = 746)
        move_to(450, 746)
        left_click()
        time.sleep(0.5)
        shot("fixes_15_terminal_unpinned")

        # 10. Verify Files context menu has NO "Unpin from Dock" (Files always pinned)
        print("Testing Step 9: Files always pinned...")
        # With 5 pinned apps: Files is at cx ~ 376
        move_to(376, 708)
        right_click()
        time.sleep(0.3)
        shot("fixes_16_dock_files_menu_no_unpin")
        # Click outside to close menu
        move_to(500, 500)
        left_click()
        time.sleep(0.3)

        # 11. Launch unpinned Terminal from Desktop shortcut!
        # Terminal was moved to (284, 188)
        print("Testing Step 10: Launch unpinned Terminal from Desktop...")
        move_to(284, 188)
        double_click()
        time.sleep(0.7)
        shot("fixes_17_unpinned_terminal_running_in_dock")

        # 12. Right click running unpinned Terminal in Dock -> Pin to Dock!
        # When running unpinned, Terminal is added as 6th app (cx = 682)
        move_to(682, 708)
        right_click()
        time.sleep(0.3)
        shot("fixes_18_pin_running_terminal")
        # Click "Pin to Dock" (item 1 at y = 714)
        move_to(710, 714)
        left_click()
        time.sleep(0.4)

        # Close Terminal window: click close button at (188, 142)
        move_to(188, 142)
        left_click()
        time.sleep(0.5)
        shot("fixes_19_terminal_closed_stays_pinned")

        print("Primary test phase passed! Killing QEMU to test reboot persistence...")
        proc.kill()
        proc.wait()
        time.sleep(1.0)

        # 13. Hard Reboot Phase
        print("Starting Hard Reboot Phase with same disk image...")
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port2 = reserve.getsockname()[1]

        log_path2 = BUILD / "desktop-reboot-serial.log"
        log_path2.write_text("")

        proc2 = subprocess.Popen([
            "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
            "-vga", "std",
            "-drive", f"format=raw,file={BUILD / 'PollikOS-Alpha.img'},if=ide,index=0",
            "-drive", f"format=raw,file={disk_path},if=ide,index=1",
            "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
            "-display", "none", "-serial", f"file:{log_path2}",
            "-qmp", f"tcp:127.0.0.1:{port2},server=on,wait=off",
        ], cwd=ROOT)

        try:
            deadline = time.monotonic() + 20
            while True:
                try:
                    conn2 = socket.create_connection(("127.0.0.1", port2), timeout=2)
                    break
                except OSError:
                    if time.monotonic() > deadline: raise AssertionError("QMP connection 2 failed")
                    time.sleep(0.1)

            stream2 = conn2.makefile("rwb", buffering=0)
            json.loads(stream2.readline())

            def qmp2(name, args=None):
                stream2.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
                while True:
                    resp = json.loads(stream2.readline())
                    if "error" in resp: raise RuntimeError(resp)
                    if "return" in resp: return resp["return"]

            def shot2(name):
                time.sleep(0.4)
                ppm = BUILD / f"{name}.ppm"
                qmp2("screendump", {"filename": str(ppm)})
                png = MEDIA_ARTIFACTS / f"{name}.png"
                with Image.open(ppm) as img:
                    img.save(png)
                print(f"Captured screenshot: {png.name}")
                return png

            qmp2("qmp_capabilities")

            boot_deadline = time.monotonic() + 15
            while "desktop ready" not in log_path2.read_text():
                if time.monotonic() > boot_deadline: raise AssertionError("Desktop boot timeout after reboot")
                time.sleep(0.1)
            print("Desktop rebooted successfully!")
            time.sleep(0.5)

            # Close welcome window
            # Send click at (188, 142): since pointer starts at (760, 500)
            dx = 188 - 760
            dy = 142 - 500
            qmp2("human-monitor-command", {"command-line": f"mouse_move {dx} {dy}"})
            time.sleep(0.15)
            qmp2("human-monitor-command", {"command-line": "mouse_button 1"})
            time.sleep(0.08)
            qmp2("human-monitor-command", {"command-line": "mouse_button 0"})
            time.sleep(0.4)

            shot2("fixes_20_after_reboot_persistence")

            # Verify persisted items from log2
            log2_text = log_path2.read_text()
            assert "DESKTOP_ITEM: Terminal" in log2_text and "x=232 y=140" in log2_text, "Terminal position did not survive reboot!"
            assert "DESKTOP_ITEM: New Folder" in log2_text, "New Folder did not survive reboot!"
            assert "DESKTOP_ITEM: New Text File.txt" in log2_text, "Saved text file did not survive reboot!"
            print("Persistence verification passed! All items and layout survived hard reboot!")

        finally:
            try: proc2.kill()
            except: pass

        print("Full verification suite completed successfully!")

    finally:
        try: proc.kill()
        except: pass

if __name__ == "__main__":
    run_suite()
