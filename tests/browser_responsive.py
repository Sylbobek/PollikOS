"""Delayed LOCAL HTTP regression; build Surface image + matching kernel.elf first.

No builds are performed here. Running this file DOES boot QEMU. Two server gates
hold the document body and then an external stylesheet (partially built DOM).
Each gate is released only after PS/2 cursor presentation and WM drag progress.
This tests cooperative waits, not maximum latency of parsing/JS/decoding/render.
Requires qemu-system-x86_64, llvm-nm and Pillow; no public Internet endpoint.
"""
import argparse
import http.server
import json
import pathlib
import socket
import struct
import subprocess
import threading
import time

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
PAGE = (b'<html><head><title>Responsive PASS</title>'
        b'<link rel="stylesheet" href="/site.css"></head><body>'
        b'<h1>Local delayed response</h1><p>Window actions remained live.</p>'
        b'</body></html>')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolution", choices=("1024x768", "1920x1080"), default="1024x768")
    args = parser.parse_args()
    requested_size = tuple(map(int, args.resolution.split("x")))
    prefix = f"browser-responsive-{args.resolution}"
    image, elf = BUILD / "PollikOS-Surface.img", BUILD / "kernel.elf"
    if not image.is_file() or not elf.is_file():
        raise RuntimeError("Build PollikOS-Surface.img and matching kernel.elf first")
    from surface_support import checked_surface_symbols
    symbols, app_count = checked_surface_symbols()
    gates = {path: (threading.Event(), threading.Event()) for path in ("/slow", "/site.css")}
    timed_out = threading.Event()
    requested = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path not in gates:
                self.send_error(404)
                return
            requested.append(self.path)
            body = PAGE if self.path == "/slow" else b"h1 { color: blue; }"
            self.send_response(200)
            self.send_header("Content-Type", "text/html" if self.path == "/slow" else "text/css")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.flush()
            entered, release = gates[self.path]
            entered.set()
            # Shorter than the guest HTTP inactivity timeout. Never auto-pass.
            if not release.wait(12):
                timed_out.set()
                return
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                timed_out.set()

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    log = BUILD / f"{prefix}.log"
    data = BUILD / f"{prefix}-data.img"
    log.write_text("")
    with data.open("wb") as disk:
        disk.truncate(64 * 1024 * 1024)
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    process = connection = stream = None
    try:
        process = subprocess.Popen([
            "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
            "-device", "VGA,vgamem_mb=32", "-display", "none", "-no-reboot",
            "-fw_cfg", f"name=opt/pollikos/display,string={args.resolution}",
            "-drive", f"format=raw,file={image},if=ide,index=0,snapshot=on",
            "-drive", f"format=raw,file={data},if=ide,index=1",
            "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
            "-serial", f"file:{log}", "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
        ], cwd=ROOT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        deadline = time.monotonic() + 40
        while connection is None:
            try:
                connection = socket.create_connection(("127.0.0.1", port), timeout=3)
            except OSError:
                assert process.poll() is None and time.monotonic() < deadline, "QMP unavailable"
                time.sleep(.1)
        stream = connection.makefile("rwb", buffering=0)
        json.loads(stream.readline())

        def qmp(command, arguments=None):
            stream.write((json.dumps({"execute": command, "arguments": arguments or {}}) + "\n").encode())
            while True:
                response = json.loads(stream.readline())
                assert "error" not in response, response
                if "return" in response:
                    return response["return"]

        def hmp(command):
            return qmp("human-monitor-command", {"command-line": command})

        def wait(predicate, description, seconds=6):
            end = time.monotonic() + seconds
            while not predicate():
                assert process.poll() is None and time.monotonic() < end, description
                assert not timed_out.is_set(), "Local server gate expired"
                time.sleep(.04)

        def memory(address, size):
            dump = BUILD / f"{prefix}-memory.bin"
            qmp("pmemsave", {"val": address, "size": size, "filename": str(dump)})
            return dump.read_bytes()

        def words(name, index=0):
            address, size = symbols[name]
            if name in ("g_windows", "g_surfaces"):
                size //= app_count
            raw = memory(address + index * size, size)
            return struct.unpack("<" + "I" * (size // 4), raw)

        def pointer():
            return words("mx")[0], words("my")[0]

        def move(x, y):
            while pointer() != (x, y):
                mx, my = pointer()
                dx, dy = max(-80, min(80, x - mx)), max(-80, min(80, y - my))
                hmp(f"mouse_move {dx} {dy}")
                wait(lambda: pointer() == (mx + dx, my + dy), "Guest did not consume PS/2 motion")

        def key(name):
            hmp("sendkey " + name)
            time.sleep(.15)

        def shot(name):
            path = BUILD / f"{prefix}-{name}.ppm"
            qmp("screendump", {"filename": str(path)})
            with Image.open(path) as frame:
                assert frame.size == requested_size, ("Unexpected actual GUI size", frame.size, requested_size)
                return frame.convert("RGB")

        qmp("qmp_capabilities")
        wait(lambda: "desktop ready" in log.read_text(), "Desktop did not boot", 40)
        wait(lambda: "DHCP: Bound successfully" in log.read_text(), "DHCP did not bind", 20)
        assert f"GFX resolution: {args.resolution}" in log.read_text()
        screen_width, screen_height = shot("desktop").size
        print(f"GUI validated: {screen_width}x{screen_height} (serial + QMP screenshot)", flush=True)
        key("f6")
        wait(lambda: words("g_windows", 5)[7] == 1, "Browser did not open")
        # Let home/open animation settle before isolating the delayed request.
        wait(lambda: not any(words("g_animations")[i] for i in range(0, app_count * 17, 17)), "Open animation stuck")
        time.sleep(.4)
        w = words("g_windows", 5)
        assert w[6] == 0 and 0 < w[4] < screen_width and 0 < w[5] < screen_height, "Expected movable normal Browser"
        assert w[2] + w[4] <= screen_width and w[3] + w[5] <= screen_height, ("Browser outside GUI", w)
        move(w[2] + 200, w[3] + 17)  # Blank titlebar, not a window control.
        key("ctrl-l")
        url = f"http://10.0.2.2:{server.server_port}/slow"
        for char in url:
            key({".": "dot", ":": "shift-semicolon", "/": "slash"}.get(char, char))
        log_start = len(log.read_text())
        key("ret")

        def check_gate(path, stage):
            entered, release = gates[path]
            wait(entered.is_set, f"Guest did not request {path}")
            assert not release.is_set() and not timed_out.is_set()
            w = words("g_windows", 5)
            mx, my = pointer()
            before = shot(stage + "-before")
            move(mx + 18, my)
            # Observe actual presented pixels, not just input counters.
            region = (mx - 2, my - 2, mx + 52, my + 38)
            baseline = before.crop(region).tobytes()
            wait(lambda: shot(stage + "-cursor").crop(region).tobytes() != baseline,
                 "Pointer moved in memory but cursor was not presented")
            frame_before = words("g_perf_stats")[0]
            dx = 30 if w[2] + w[4] + 30 < screen_width else -30
            dy = 24 if w[3] + w[5] + 24 < screen_height else -24
            assert 0 <= w[2] + dx <= screen_width - w[4]
            assert 0 <= w[3] + dy <= screen_height - w[5]
            hmp("mouse_button 1")
            time.sleep(.12)
            move(mx + 18 + dx, my + dy)
            wait(lambda: words("g_windows", 5)[2:4] == (w[2] + dx, w[3] + dy),
                 "WM drag stalled while response body was withheld")
            hmp("mouse_button 0")
            wait(lambda: words("g_dragged_window")[0] == 0xffffffff, "Mouse release was not consumed")
            wait(lambda: words("g_perf_stats")[0] > frame_before, "WM frame was not completed")
            # Verify completed surface, scene and actual LFB at the NEW position.
            # framebuffer_init switches VBE 24-bpp to DISPI 32-bpp without
            # updating the BIOS block at 0x7000. Use framebuffer.c runtime state,
            # not its stale VBE pitch (3072 rather than 4096 at 1024x768).
            lfb, pitch, pixel_bytes = (words(name)[0] for name in ("address", "stride", "bytes"))
            assert pixel_bytes in (3, 4) and pitch >= before.width * pixel_bytes
            observed = dict(resolution=args.resolution, cursor_from=(mx, my),
                            cursor_to=(mx + 18, my), cursor_pixels_changed=True,
                            window_from=w[2:4], drag_delta=(dx, dy),
                            completed_frames=words("g_perf_stats")[0] - frame_before)
            def title_presented():
                now, surface = words("g_windows", 5), words("g_surfaces", 5)
                # Blank titlebar: below text/controls, away from the pointer.
                x, y = now[4] - 20, 30
                sx, sy = now[2] + x, now[3] + y
                a = memory(surface[0] + (y * surface[3] + x) * 4, 4)
                b = memory(lfb + sy * pitch + sx * pixel_bytes, pixel_bytes)
                scene = memory(words("pixels")[0] + (sy * before.width + sx) * 4, 4)
                observed.update(window_xy=now[2:4], screen_probe=(sx, sy),
                                surface_pixel=a.hex(), scene_pixel=scene.hex(),
                                lfb_pixel=b.hex(), pitch=pitch, bpp=pixel_bytes * 8)
                expected = struct.pack("<I", 0xf2eff6)
                return a == scene == expected and b == expected[:pixel_bytes]
            try:
                wait(title_presented, "Moved window did not reach framebuffer")
                presented = shot(stage + "-dragged")
                assert presented.getpixel(observed["screen_probe"]) == (242, 239, 246), observed
            except AssertionError:
                shot(stage + "-failed")
                print(stage, "FAIL", json.dumps(observed), "process_exit=", process.poll())
                raise
            # load_active permits service, not unconditional repainting. After
            # release damage drains, an idle withheld response must stay clean
            # while guest timer ticks continue (not a frozen guest false pass).
            wait(lambda: words("shell")[2:4] == (0, 0) and words("dirty_client")[5] == 0,
                 "Loading chrome never became clean")
            idle_frame, idle_tick = words("g_perf_stats")[0], words("ticks")[0]
            wait(lambda: (words("ticks")[0] - idle_tick) & 0xffffffff >= 20,
                 "Guest timer stalled during idle gate")
            idle_frames = words("g_perf_stats")[0] - idle_frame
            assert idle_frames == 0, f"Idle loading repainted {idle_frames} frames"
            observed["idle_frames"] = idle_frames
            observed["idle_ticks"] = (words("ticks")[0] - idle_tick) & 0xffffffff
            print(stage, json.dumps(observed))
            assert not timed_out.is_set() and not release.is_set()
            assert "BROWSER: Page loaded successfully" not in log.read_text()[log_start:]
            release.set()  # Only real pointer + WM + framebuffer progress opens the gate.

        check_gate("/slow", "document")
        check_gate("/site.css", "stylesheet")
        wait(lambda: "BROWSER title: Responsive PASS" in log.read_text()[log_start:], "Load did not complete", 20)
        assert requested == ["/slow", "/site.css"], requested
        assert not timed_out.is_set()
        shot("complete")
        print("PASS: cursor pixels + WM drag + framebuffer progressed during held document/CSS bodies")
    finally:
        for _entered, release in gates.values():
            release.set()
        if stream is not None:
            stream.close()
        if connection is not None:
            connection.close()
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
