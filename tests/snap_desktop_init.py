import json, os, pathlib, socket, subprocess, time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ARTIFACTS = pathlib.Path(r"C:\Users\syltu\.gemini\antigravity-ide\brain\7a2bc03d-3d1f-4bd4-bc66-637e7ef016be")
ARTIFACTS.mkdir(parents=True, exist_ok=True)

with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]

log = BUILD / "desktop-init-serial.log"
log.write_text("")

proc = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-m", "2G",
    "-vga", "std",
    "-drive", f"format=raw,file={BUILD / 'PollikOS-Alpha.img'},if=ide,index=0",
    "-drive", f"format=raw,file={BUILD / 'test-data.img'},if=ide,index=1",
    "-display", "none", "-serial", f"file:{log}",
    "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
], cwd=ROOT)

try:
    deadline = time.monotonic() + 15
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

    # Wait for desktop ready
    boot_deadline = time.monotonic() + 15
    while "desktop ready" not in log.read_text():
        if time.monotonic() > boot_deadline: raise AssertionError("Boot timeout")
        time.sleep(0.1)
    
    time.sleep(1.0)
    ppm_path = BUILD / "desktop_first_view.ppm"
    qmp("screendump", {"filename": str(ppm_path)})
    png_path = ARTIFACTS / "desktop_first_view.png"
    with Image.open(ppm_path) as img:
        img.save(png_path)
    print(f"Saved initial desktop view to {png_path}")
finally:
    proc.terminate()
    proc.communicate()
