import json, os, pathlib, socket, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
MEDIA_ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\7a2bc03d-3d1f-4bd4-bc66-637e7ef016be")
MEDIA_ARTIFACTS.mkdir(parents=True, exist_ok=True)

def test_buttons():
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]

    proc = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
        "-vga", "std",
        "-drive", f"format=raw,file={BUILD / 'PollikOS-Alpha.img'},if=ide,index=0",
        "-drive", f"format=raw,file={BUILD / 'PollikData.img'},if=ide,index=1",
        "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
        "-display", "none",
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
            print(f"Captured: {png.name}")
            return png

        qmp("qmp_capabilities")
        time.sleep(3.0)

        cur_pos = [760, 500]

        def move_to(tx, ty):
            while cur_pos[0] != tx or cur_pos[1] != ty:
                dx = tx - cur_pos[0]
                dy = ty - cur_pos[1]
                step_x = max(-80, min(80, dx))
                step_y = max(-80, min(80, dy))
                hmp(f"mouse_move {step_x} {step_y}")
                cur_pos[0] += step_x
                cur_pos[1] += step_y
                time.sleep(0.04)
            time.sleep(0.2)

        # 1. Hover on RED close button at (190, 142)
        print("Hovering on RED close button at (190, 142)...")
        move_to(190, 142)
        shot("cursor_01_red_hover")

        # 2. Hover on YELLOW minimize button at (208, 142) (18px to right)
        print("Hovering on YELLOW minimize button at (208, 142)...")
        move_to(208, 142)
        shot("cursor_02_yellow_hover")

        # 3. Hover on GREEN maximize button at (226, 142) (18px to right)
        print("Hovering on GREEN maximize button at (226, 142)...")
        move_to(226, 142)
        shot("cursor_03_green_hover")

        print("Finished cursor button tests!")

    finally:
        try: proc.kill()
        except: pass

if __name__ == "__main__":
    test_buttons()
