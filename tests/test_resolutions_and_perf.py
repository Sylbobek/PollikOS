import json, os, pathlib, socket, struct, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\7a2bc03d-3d1f-4bd4-bc66-637e7ef016be")
ARTIFACTS.mkdir(parents=True, exist_ok=True)

from test_real_desktop import format_clean_disk

def get_symbol_address(elf_path, name):
    output = subprocess.check_output(["llvm-nm", "-S", str(elf_path)], text=True)
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[3] == name:
            return int(parts[0], 16), int(parts[1], 16)
        elif len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16), 4
    raise RuntimeError(f"Symbol {name} not found")

def test_resolution(res):
    print(f"\n================ Testing Resolution {res} ================")
    w_str, h_str = res.split("x")
    expected_w, expected_h = int(w_str), int(h_str)

    disk_path = BUILD / f"res-{res}-data.img"
    format_clean_disk(disk_path)

    log_path = BUILD / f"res-{res}-serial.log"
    log_path.write_text("")

    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]

    proc = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
        "-vga", "std",
        "-fw_cfg", f"name=opt/pollikos/display,string={res}",
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
                if time.monotonic() > deadline: raise AssertionError(f"QMP connection failed for {res}")
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

        qmp("qmp_capabilities")

        # Wait for desktop ready
        boot_deadline = time.monotonic() + 25
        while "desktop ready" not in log_path.read_text():
            if time.monotonic() > boot_deadline: raise AssertionError(f"Boot timeout for {res}")
            time.sleep(0.2)

        # Check resolution reported in serial
        serial_content = log_path.read_text()
        assert f"GFX resolution: {res}" in serial_content, f"Expected 'GFX resolution: {res}' in serial log:\n{serial_content}"
        print(f"Verified GFX resolution in serial: {res}")

        time.sleep(1.0) # Settle initial animations

        # Dismiss welcome window to reveal full clean desktop
        hmp("sendkey f1")
        time.sleep(0.5)

        # Capture screenshot
        ppm_path = BUILD / f"res_{res}.ppm"
        hmp(f"screendump {ppm_path}")
        time.sleep(0.4)

        img = Image.open(ppm_path)
        print(f"Captured screenshot dimensions: {img.size[0]}x{img.size[1]}")
        assert img.size == (expected_w, expected_h), f"Screenshot size {img.size} != expected ({expected_w}, {expected_h})"

        png_path = ARTIFACTS / f"desktop_res_{res}.png"
        img.save(png_path)
        print(f"Saved artifact screenshot to {png_path}")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

def test_perf_and_memory():
    print("\n================ Testing Performance & Memory Stability (1024x768) ================")
    elf_path = BUILD / "kernel.elf"
    pmm_addr, _ = get_symbol_address(elf_path, "pmm_free_page_count")
    perf_addr, perf_size = get_symbol_address(elf_path, "g_perf_stats")
    print(f"Symbols: pmm_free_page_count @ 0x{pmm_addr:08x}, g_perf_stats @ 0x{perf_addr:08x} (size {perf_size})")

    disk_path = BUILD / "perf-data.img"
    format_clean_disk(disk_path)

    log_path = BUILD / "perf-serial.log"
    log_path.write_text("")

    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]

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

        qmp("qmp_capabilities")

        # Wait for desktop ready
        boot_deadline = time.monotonic() + 25
        while "desktop ready" not in log_path.read_text():
            if time.monotonic() > boot_deadline: raise AssertionError("Boot timeout")
            time.sleep(0.2)

        # Dismiss welcome window
        hmp("sendkey f1")
        time.sleep(1.0)

        mem_dump_file = BUILD / "mem_probe.bin"

        def read_u32(phys_addr):
            qmp("pmemsave", {"val": phys_addr, "size": 4, "filename": str(mem_dump_file)})
            data = mem_dump_file.read_bytes()
            return struct.unpack("<I", data)[0]

        def read_perf():
            qmp("pmemsave", {"val": perf_addr, "size": perf_size, "filename": str(mem_dump_file)})
            data = mem_dump_file.read_bytes()
            # Struct GuiPerfStats layout:
            # frame_count, fps, avg_frame_us, p95_frame_us, p99_frame_us, worst_frame_us,
            # input_events_sec, coalesced_mouse_sec, client_paints_sec, compositor_frames_sec,
            # presents_sec, pixels_composed_sec, pixels_presented_sec, full_redraw_count,
            # damage_rects_count, layout_us, paint_us, present_us, input_us, compose_us, window_count
            ints = struct.unpack("<21I", data[:84])
            return {
                "frame_count": ints[0],
                "fps": ints[1],
                "avg_frame_us": ints[2],
                "p95_frame_us": ints[3],
                "p99_frame_us": ints[4],
                "worst_frame_us": ints[5],
                "full_redraw_count": ints[13],
                "damage_rects_count": ints[14],
                "layout_us": ints[15],
                "paint_us": ints[16],
                "present_us": ints[17],
                "compose_us": ints[19],
            }

        initial_free_pages = read_u32(pmm_addr)
        print(f"\nInitial PMM Free Pages: {initial_free_pages} ({initial_free_pages * 4 / 1024:.2f} MiB)")

        # Run 5 cycles of Launch Terminal -> Minimize -> Restore -> Close
        print("\n--- Running 5 Window Lifecycle Cycles (Terminal) ---")
        for cycle in range(1, 6):
            # Double click Terminal (Grid pos 0,1: x=24+104/2=76, y=40+96+96/2=184)
            hmp("mouse_move 76 184")
            time.sleep(0.05)
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.05)
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.4) # Window open animation

            # Minimize: click minimize button at Terminal wx=210, wy=145 -> px=246, py=161
            hmp("mouse_move 246 161")
            time.sleep(0.05)
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.4) # Window minimize animation to Dock

            # Restore: click Terminal in Dock (slot 2: center x = 512-25+2*50 = 587, y = 724)
            hmp("mouse_move 587 724")
            time.sleep(0.05)
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.4) # Window restore animation

            # Close: click close button at Terminal wx=210, wy=145 -> px=228, py=161
            hmp("mouse_move 228 161")
            time.sleep(0.05)
            hmp("mouse_button 1")
            time.sleep(0.05)
            hmp("mouse_button 0")
            time.sleep(0.4) # Window close animation

            free_pages = read_u32(pmm_addr)
            print(f"Cycle {cycle} complete: PMM Free Pages = {free_pages} (diff: {free_pages - initial_free_pages})")

        post_window_free_pages = read_u32(pmm_addr)
        print(f"\nPost-window cycles Free Pages: {post_window_free_pages}")

        # Move mouse around to generate presentation frames and read performance stats
        for i in range(20):
            hmp(f"mouse_move {300 + i * 15} {300 + (i % 3) * 20}")
            time.sleep(0.03)

        perf = read_perf()
        print("\n================ Real Performance Telemetry Snapshot ================")
        print(f"Total Frame Count:      {perf['frame_count']}")
        print(f"Compositor FPS:         {perf['fps']}")
        print(f"Avg Frame Time:         {perf['avg_frame_us']} us ({perf['avg_frame_us']/1000.0:.3f} ms)")
        print(f"P95 Frame Time:         {perf['p95_frame_us']} us ({perf['p95_frame_us']/1000.0:.3f} ms)")
        print(f"P99 Frame Time:         {perf['p99_frame_us']} us ({perf['p99_frame_us']/1000.0:.3f} ms)")
        print(f"Worst Frame Time:       {perf['worst_frame_us']} us ({perf['worst_frame_us']/1000.0:.3f} ms)")
        print(f"Last Compose Time:      {perf['compose_us']} us ({perf['compose_us']/1000.0:.3f} ms)")
        print(f"Last Present Time:      {perf['present_us']} us ({perf['present_us']/1000.0:.3f} ms)")
        print(f"Last Paint Time:        {perf['paint_us']} us ({perf['paint_us']/1000.0:.3f} ms)")
        print(f"Damage Rects Drawn:     {perf['damage_rects_count']}")
        print(f"Full Redraws:           {perf['full_redraw_count']}")
        print("======================================================================\n")

        # PMM free pages check: no unbounded leak
        page_diff = initial_free_pages - post_window_free_pages
        print(f"Net Page Difference after 5 full open/minimize/restore/close cycles: {page_diff} pages ({page_diff * 4} KiB)")
        assert page_diff <= 16, f"Memory leak detected: {page_diff} pages lost ({page_diff * 4} KiB)"
        print("PASS: Memory leak test passed with zero significant memory loss!")

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

if __name__ == "__main__":
    for res in ["1024x768", "1280x720", "1920x1080"]:
        test_resolution(res)
    test_perf_and_memory()
    print("\nALL RESOLUTION & PERFORMANCE TESTS COMPLETED SUCCESSFULLY!")
