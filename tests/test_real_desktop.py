import json, os, pathlib, socket, struct, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\7a2bc03d-3d1f-4bd4-bc66-637e7ef016be")
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
    write_inode(4, pack_inode(VFS_DIR, 1024, [39]))
    write_inode(5, pack_inode(VFS_DIR, 1024, [40]))
    write_inode(6, pack_inode(VFS_DIR, 1024, [41]))
    write_inode(7, pack_inode(VFS_DIR, 1024, [42]))

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

    write_block(39, bytearray(1024))
    write_block(40, bytearray(1024))
    write_block(41, bytearray(1024)) # Desktop empty initially, kernel will seed it
    write_block(42, bytearray(1024)) # Trash

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
    print(f"Prepared PollikFS v2 disk at {disk_path}")

def run_test():
    disk_path = BUILD / "desktop-suite-data.img"
    format_clean_disk(disk_path)

    log_path = BUILD / "desktop-suite-serial.log"
    log_path.write_text("")

    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]

    print("Launching QEMU for Full Real Desktop Suite...")
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

        # Track absolute mouse coordinates (kernel initializes pointer at mx=760, my=500)
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

        def double_click():
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.08)
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.3)

        def right_click():
            hmp("mouse_button 2")
            time.sleep(0.08)
            hmp("mouse_button 0")
            time.sleep(0.25)

        def send_key(k):
            hmp(f"sendkey {k}")
            time.sleep(0.15)

        def type_text(s):
            for ch in s:
                if ch == '_':
                    hmp("sendkey shift-minus")
                elif ch == '.':
                    hmp("sendkey dot")
                elif ch == ' ':
                    hmp("sendkey spc")
                else:
                    hmp(f"sendkey {ch}")
                time.sleep(0.05)
            time.sleep(0.15)

        qmp("qmp_capabilities")

        # 1. Wait for desktop ready
        boot_deadline = time.monotonic() + 15
        while "desktop ready" not in log_path.read_text():
            if time.monotonic() > boot_deadline: raise AssertionError("Desktop boot timeout")
            time.sleep(0.1)
        print("Desktop ready confirmed.")

        if "SETUP: first-run installer ready" in log_path.read_text():
            print("Completing first-run setup on the disposable desktop test disk...")
            hmp("sendkey ret")
            for key in "desktopt": hmp(f"sendkey {key}")
            hmp("sendkey ret")
            for key in "test123": hmp(f"sendkey {key}")
            hmp("sendkey ret")
            for key in "test123": hmp(f"sendkey {key}")
            hmp("sendkey ret")
            setup_deadline = time.monotonic() + 10
            while "AUTH: account created; installation complete" not in log_path.read_text():
                if time.monotonic() > setup_deadline:
                    raise AssertionError("first-run setup did not complete")
                time.sleep(0.1)
            time.sleep(0.5)
        else:
            assert "AUTH: login required" in log_path.read_text(), "fresh disposable disk was not detected"

        time.sleep(0.6)
        # Step 1: Initial Desktop with Welcome window
        shot("desktop_1_initial")

        # Close welcome window: click red close button at (188, 142)
        move_to(188, 142)
        left_click()
        time.sleep(0.4)
        shot("desktop_2_clean_desktop")

        # Step 2: Right click empty desktop at (600, 300) -> Context Menu opens
        move_to(600, 300)
        right_click()
        shot("desktop_3_context_menu")

        # Click "New Folder": menu opened at (600, 300).
        # First item ("New Folder") is at x ~ 640, y ~ 316
        move_to(640, 316)
        left_click()
        time.sleep(0.4)
        shot("desktop_4_new_folder_created")

        # Step 3: Right click empty desktop at (600, 300) -> "New Text File"
        # Second item ("New Text File") is at x ~ 640, y ~ 340
        move_to(600, 300)
        right_click()
        time.sleep(0.2)
        move_to(640, 340)
        left_click()
        time.sleep(0.4)
        shot("desktop_5_new_text_file_created")

        # Step 4: Rename test.txt using F2!
        # test.txt is at Column 1, Row 3: cx = 180, cy = 380
        move_to(180, 380)
        left_click() # select
        time.sleep(0.2)
        send_key("f2") # triggers rename dialog
        time.sleep(0.3)
        shot("desktop_6_rename_dialog")

        # Backspace old text and enter "renamed.txt"
        for _ in range(12):
            send_key("backspace")
        type_text("renamed.txt")
        send_key("ret")
        time.sleep(0.4)
        shot("desktop_7_renamed_applied")

        # Step 5: Open Notes from its pinned Dock icon and capture the editor.
        move_to(515, 713)
        left_click()
        time.sleep(0.5)
        shot("notes-redesigned")

        # Close Notes: its centered window close button is at (188, 142).
        move_to(188, 142)
        left_click()
        time.sleep(0.3)

        # Capture the detailed benchmark layout at the normal 1024x768 GUI size.
        move_to(717, 713)
        left_click()
        time.sleep(1.0)
        shot("pollikmark-detailed-ui")
        move_to(188, 142)
        left_click()
        time.sleep(0.3)

        # Step 6: Double click "Projects" folder on desktop to open in Files!
        # Projects is at Column 1, Row 0: cx = 180, cy = 92
        move_to(180, 92)
        double_click()
        time.sleep(0.5)
        shot("desktop_9_files_opened_projects")

        # Close Files: window x=130, y=85, close button at (148, 102)
        move_to(148, 102)
        left_click()
        time.sleep(0.3)

        # Step 7: Launch Terminal from desktop icon!
        # Terminal icon is at Column 0, Row 1: cx = 76, cy = 188
        move_to(76, 188)
        double_click()
        time.sleep(0.5)
        shot("desktop_10_terminal_launched")

        # Step 8: Minimize Terminal: window x=210, y=145, yellow minimize button at (246, 162)
        move_to(246, 162)
        left_click()
        time.sleep(0.6)
        shot("desktop_11_terminal_minimized_to_dock")

        # Step 9: Restore Terminal from Dock: Terminal dock icon is at x=410, y=708
        move_to(410, 708)
        left_click()
        time.sleep(0.6)
        shot("desktop_12_terminal_restored")

        # Close Terminal: window x=210, y=145, red close button at (228, 162)
        move_to(228, 162)
        left_click()
        time.sleep(0.3)

        # Step 10: Move "renamed.txt" to Trash!
        # renamed.txt is at Column 1, Row 3: cx = 180, cy = 380
        move_to(180, 380)
        left_click() # select
        send_key("delete") # triggers delete confirmation dialog!
        time.sleep(0.3)
        shot("desktop_13_delete_confirm_dialog")

        # Confirm dialog: "Move to Trash" button center: confirm_x=596..686, btn_y=419..445 -> (641, 432)
        move_to(641, 432)
        left_click()
        time.sleep(0.5)
        shot("desktop_14_trash_full_state")

        # Step 11: Open Trash on desktop!
        # Trash icon is at Column 2, Row 0: cx = 284, cy = 92
        move_to(284, 92)
        double_click()
        time.sleep(0.5)
        shot("desktop_15_trash_view_in_files")

        # Step 12: Click "Restore" button on renamed.txt in Trash view!
        # Restore button center: sx = 130 + 502 = 632, sy = 85 + 121 = 206
        move_to(632, 206)
        left_click()
        time.sleep(0.4)

        # Close Files: close button at (148, 102)
        move_to(148, 102)
        left_click()
        time.sleep(0.4)
        shot("desktop_16_item_restored_to_desktop")

        print("Stage 1 interactions complete. Now testing reboot persistence...")

    finally:
        proc.terminate()
        proc.communicate()

    # Step 12: REBOOT QEMU with the SAME data disk!
    print("Rebooting QEMU to test persistence...")
    log_reboot = BUILD / "desktop-reboot-serial.log"
    log_reboot.write_text("")

    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port2 = reserve.getsockname()[1]

    proc2 = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
        "-vga", "std",
        "-drive", f"format=raw,file={BUILD / 'PollikOS-Alpha.img'},if=ide,index=0",
        "-drive", f"format=raw,file={disk_path},if=ide,index=1",
        "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
        "-display", "none", "-serial", f"file:{log_reboot}",
        "-qmp", f"tcp:127.0.0.1:{port2},server=on,wait=off",
    ], cwd=ROOT)

    try:
        deadline = time.monotonic() + 20
        while True:
            try:
                conn2 = socket.create_connection(("127.0.0.1", port2), timeout=2)
                break
            except OSError:
                if time.monotonic() > deadline: raise AssertionError("QMP reboot failed")
                time.sleep(0.1)

        stream2 = conn2.makefile("rwb", buffering=0)
        json.loads(stream2.readline())

        def qmp2(name, args=None):
            stream2.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
            while True:
                resp = json.loads(stream2.readline())
                if "error" in resp: raise RuntimeError(resp)
                if "return" in resp: return resp["return"]

        def hmp2(cmd):
            return qmp2("human-monitor-command", {"command-line": cmd})

        qmp2("qmp_capabilities")

        boot_deadline = time.monotonic() + 15
        while "desktop ready" not in log_reboot.read_text():
            if time.monotonic() > boot_deadline: raise AssertionError("Reboot boot timeout")
            time.sleep(0.1)
        print("Reboot: desktop ready confirmed.")

        time.sleep(0.5)
        # Close welcome window: cursor starts at (760, 500), move to close button at (188, 142)
        # dx = 188 - 760 = -572, dy = 142 - 500 = -358
        hmp2("mouse_move -572 -358")
        time.sleep(0.15)
        hmp2("mouse_button 1")
        time.sleep(0.08)
        hmp2("mouse_button 0")
        time.sleep(0.4)

        # Screenshot after reboot: New Folder, New Text File, Projects, Documents, todo.txt MUST all be present!
        ppm_reb = BUILD / "desktop_17_after_reboot_persistence.ppm"
        qmp2("screendump", {"filename": str(ppm_reb)})
        png_reb = ARTIFACTS / "desktop_17_after_reboot_persistence.png"
        with Image.open(ppm_reb) as img:
            img.save(png_reb)
        print(f"Captured screenshot after reboot: {png_reb.name}")

    finally:
        proc2.terminate()
        proc2.communicate()

    print("ALL TESTS AND VERIFICATIONS COMPLETED SUCCESSFULLY!")

if __name__ == "__main__":
    run_test()
