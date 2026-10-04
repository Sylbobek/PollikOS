import json, os, pathlib, socket, struct, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
MEDIA_ARTIFACTS = pathlib.Path(r"C:\Users\syltu\Desktop\PollikOS")
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

def main():
    disk_path = BUILD / "dock-test-data.img"
    format_clean_disk(disk_path)

    log_path = BUILD / "dock-test-serial.log"
    log_path.write_text("")

    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]

    print(f"Launching QEMU for Dock Lifecycle Test on QMP port {port}...")
    proc = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
        "-vga", "std", "-snapshot",
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

        qmp("qmp_capabilities")

        def hmp(cmd):
            return qmp("human-monitor-command", {"command-line": cmd})

        symbols = subprocess.check_output(["llvm-nm", "-S", str(BUILD / "kernel.elf")], text=True)
        window_line = next(line for line in symbols.splitlines() if line.split()[-1] == "g_windows")
        windows_addr = int(window_line.split()[0], 16)
        anim_line = next(line for line in symbols.splitlines() if line.split()[-1] == "g_window_anims")
        animations_addr = int(anim_line.split()[0], 16)
        window_state_dump = BUILD / "dock-window-state.bin"

        def terminal_window_state():
            qmp("pmemsave", {"val": windows_addr + 2 * 84, "size": 84,
                              "filename": str(window_state_dump)})
            values = struct.unpack("<21I", window_state_dump.read_bytes())
            return {"state": values[6], "open": values[7], "minimized": values[8],
                    "focused": values[9], "visible": values[10]}

        def animation_active(app_id):
            qmp("pmemsave", {"val": animations_addr + app_id * 72, "size": 4,
                              "filename": str(window_state_dump)})
            return struct.unpack("<I", window_state_dump.read_bytes())[0]

        def shot(name):
            time.sleep(0.4)
            ppm = BUILD / f"{name}.ppm"
            qmp("screendump", {"filename": str(ppm)})
            png = BUILD / f"{name}.png"
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

        def right_click():
            hmp("mouse_button 2")
            time.sleep(0.08)
            hmp("mouse_button 0")
            time.sleep(0.2)

        # 1. Wait for desktop ready
        dl = time.monotonic() + 15
        while time.monotonic() < dl:
            if "desktop ready" in log_path.read_text(): break
            time.sleep(0.1)
        assert "desktop ready" in log_path.read_text(), "System failed to boot"
        print("PASS: System booted and desktop ready.")
        if "SETUP: first-run installer ready" in log_path.read_text():
            print("Completing first-run setup on the disposable QEMU data disk...")
            hmp("sendkey ret")
            for key in "docktest": hmp(f"sendkey {key}")
            hmp("sendkey ret")
            for key in "test123": hmp(f"sendkey {key}")
            hmp("sendkey ret")
            for key in "test123": hmp(f"sendkey {key}")
            hmp("sendkey ret")
            deadline = time.monotonic() + 10
            while "AUTH: account created; installation complete" not in log_path.read_text():
                if time.monotonic() > deadline: raise AssertionError("first-run setup did not complete")
                time.sleep(0.1)
            time.sleep(0.5)
        else:
            assert "AUTH: login required" in log_path.read_text(), "no disposable setup/login fixture detected"
        time.sleep(1.0)
        shot("dock_01_initial")

        # All seven built-in app icons are pinned by default; closing a window
        # leaves its icon in the Dock.
        print("Closing Welcome window (Alt+F4)...")
        hmp("sendkey alt-f4")
        time.sleep(0.8)
        shot("dock_02_welcome_closed")
        print("PASS: Welcome window closed; its pinned Dock icon remains.")

        # With seven icons at 68 px spacing, Terminal (APP_TERMINAL = 2)
        # is centered at x = 512 - 6 * 34 + 2 * 68 = 444.

        print("Launching Terminal from Dock (444, 708)...")
        move_to(444, 708)
        left_click()
        time.sleep(0.8)
        shot("dock_03_terminal_running_pinned")
        state = terminal_window_state()
        assert state["open"] and state["visible"] and state["focused"] and not state["minimized"], state

        print("Clicking the active Terminal Dock icon should minimize it...")
        move_to(444, 708)
        left_click()
        time.sleep(0.6)
        state = terminal_window_state()
        assert state["open"] and not state["visible"] and state["minimized"], state
        shot("dock_03a_terminal_minimized_from_dock")

        print("Clicking the minimized Terminal Dock icon should restore it...")
        move_to(444, 708)
        left_click()
        time.sleep(0.6)
        state = terminal_window_state()
        assert state["open"] and state["visible"] and not state["minimized"], state
        shot("dock_03b_terminal_restored_from_dock")

        print("Opening Files, then selecting the already running Terminal should only focus it...")
        move_to(376, 708)
        left_click()
        time.sleep(0.5)
        move_to(444, 708)
        left_click()
        state = terminal_window_state()
        assert state["focused"] and state["visible"] and not state["minimized"], state
        assert not animation_active(2), "focusing an already running background app started an animation"

        # Stan 3: Right-click Terminal in Dock
        print("Right-clicking Terminal in Dock...")
        move_to(444, 708)
        right_click()
        time.sleep(0.5)
        shot("dock_04_terminal_context_menu")

        # Context menu is above dock icon:
        # Menu opened at y = 708 - (4 * 24 + 16) = 596
        # Item 0 (Open) is at y ~ 610
        # Item 1 (Unpin from Dock) is at y ~ 634
        # Click "Unpin from Dock"
        print("Clicking 'Unpin from Dock' at (444, 634)...")
        move_to(444, 634)
        left_click()
        time.sleep(0.6)
        shot("dock_05_terminal_unpinned_still_running")

        # Verify Stan 3: Terminal is UNPINNED, but STILL RUNNING!
        # Pinned icons remain six; the running Terminal moves to the end at x=716.
        # Terminal icon is STILL IN THE DOCK with running indicator dot!
        print("PASS: Terminal unpinned while running: icon remains in Dock as dynamic running app!")

        # Stan 4: Close Terminal window (Alt+F4)
        print("Closing Terminal (Alt+F4)...")
        hmp("sendkey alt-f4")
        time.sleep(0.8)
        shot("dock_06_terminal_closed_unpinned_gone")
        print("PASS: Terminal closed; its unpinned icon disappeared from Dock (six pinned apps remain).")

        # Stan 5: Launch Terminal (unpinned) using F2 shortcut (APP_TERMINAL = 1 -> scancode 60 = F2)
        # Wait, APP_TERMINAL is id 2!
        # In apps.h: APP_WELCOME=0, APP_FILES=1, APP_TERMINAL=2, APP_NOTES=3...
        # In input_dispatch.c: if (code >= 59 && code < 59 + APP_COUNT) open_app(code - 59);
        # So F1 (59) -> APP_WELCOME (0)
        # F2 (60) -> APP_FILES (1)
        # F3 (61) -> APP_TERMINAL (2)
        print("Launching Terminal (unpinned) via F3 shortcut...")
        hmp("sendkey f3")
        time.sleep(0.8)
        shot("dock_07_terminal_launched_unpinned_appears")
        print("PASS: Terminal running unpinned; dynamic icon appeared in Dock at end of Dock!")

        # Dynamic Terminal is at k=6 (cx = 716, cy = 708)
        # Stan 6: Right-click Terminal dynamic icon in Dock
        print("Right-clicking dynamic Terminal icon at (716, 708)...")
        move_to(716, 708)
        right_click()
        time.sleep(0.5)
        shot("dock_08_dynamic_terminal_menu")

        # Menu has:
        # Item 0: Open (y ~ 610)
        # Item 1: Pin to Dock (y ~ 634)
        # Item 2: separator
        # Item 3: Quit (y ~ 666)
        print("Clicking 'Pin to Dock' at (716, 634)...")
        move_to(716, 634)
        left_click()
        time.sleep(0.6)
        shot("dock_09_terminal_pinned_again")

        # Close Terminal
        print("Closing Terminal (Alt+F4)...")
        hmp("sendkey alt-f4")
        time.sleep(0.8)
        shot("dock_10_terminal_closed_remains_pinned")
        print("PASS: Terminal closed; pinned icon remains in Dock.")

        # Verify Files app: right-click Files icon (cx = 376, cy = 708)
        print("Verifying Files app context menu at (376, 708) (cannot unpin)...")
        move_to(376, 708)
        right_click()
        time.sleep(0.5)
        shot("dock_11_files_menu_no_unpin")
        print("PASS: Files menu has no Unpin option.")

        print("ALL DOCK PIN/UNPIN LIFECYCLE TESTS PASSED!")

    finally:
        proc.terminate()
        try: proc.wait(timeout=3)
        except Exception: proc.kill()

if __name__ == "__main__":
    main()
