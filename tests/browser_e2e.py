"""Boot the real disk in QEMU and exercise PS/2 input through QMP."""
import json
import pathlib
import os
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]
log = BUILD / "browser-e2e.log"
log.write_text("")
data_disk = BUILD / "browser-e2e-data.img"
from format_pollikfs2 import format_disk
format_disk(data_disk, total_size_mb=40)
process = subprocess.Popen([
    "qemu-system-x86_64", "-machine", "pc", "-cpu", "max", "-rtc", "base=utc", "-m", "2G", "-vga", "std",
    "-drive", f"format=raw,file={BUILD / os.environ.get('POLLIK_TEST_IMAGE', 'PollikOS-Alpha.img')},if=ide,index=0,snapshot=on",
    "-drive", f"format=raw,file={data_disk},if=ide,index=1",
    "-netdev", "user,id=net0", "-device", "rtl8139,netdev=net0",
    "-display", "none", "-serial", f"file:{log}",
    "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
], cwd=ROOT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
try:
    deadline = time.monotonic() + 15
    while True:
        try:
            connection = socket.create_connection(("127.0.0.1", port), timeout=2)
            break
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(.1)
    stream = connection.makefile("rwb", buffering=0)
    json.loads(stream.readline())

    def qmp(name, args=None):
        stream.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
        while True:
            response = json.loads(stream.readline())
            if "error" in response:
                raise RuntimeError(response)
            if "return" in response:
                return response["return"]

    def hmp(command):
        return qmp("human-monitor-command", {"command-line": command})

    def key(name):
        hmp("sendkey " + name)
        time.sleep(.15)

    def shot(name):
        time.sleep(.25)
        path = BUILD / (name + ".ppm")
        qmp("screendump", {"filename": str(path)})
        return path.read_bytes()

    qmp("qmp_capabilities")
    while "desktop ready" not in log.read_text():
        if time.monotonic() > deadline:
            raise AssertionError("Kernel did not reach the desktop")
        time.sleep(.1)
    deadline = time.monotonic()+15
    while "DHCP: Bound successfully" not in log.read_text():
        assert time.monotonic()<deadline,log.read_text()
        time.sleep(.1)
    if "SETUP: first-run installer ready" in log.read_text():
        key("ret")
        for c in "browsertest": key(c)
        key("ret")
        for c in "test123": key(c)
        key("ret")
        for c in "test123": key(c)
        key("ret")
        deadline = time.monotonic() + 12
        while "AUTH: account created; installation complete" not in log.read_text():
            assert time.monotonic() < deadline, log.read_text()
            time.sleep(.1)
    key("f6")
    # The first-run app was just opened; let its pending local home document
    # finish before sending address-bar keys.
    time.sleep(1)
    key("ctrl-l")
    for c in os.environ.get('POLLIK_TEST_URL', 'example.com'):
        key({'.':'dot', ':':'shift-semicolon', '/':'slash', '?':'shift-slash', '=':'equal', '-':'minus'}.get(c,c))
    key("ret")
    deadline=time.monotonic()+35
    while "BROWSER: Page loaded successfully" not in log.read_text():
        assert time.monotonic()<deadline,log.read_text()
        time.sleep(.2)
    shot("browser-example")
    assert "TLS: Handshake successful" in log.read_text(),log.read_text()
    assert "Response status: 200" in log.read_text(),log.read_text()
    print('PASS: DHCP/DNS/TCP/TLS/HTTP/HTML/CSS/layout/render ' + os.environ.get('POLLIK_TEST_URL', 'example.com'))
finally:
    process.terminate()
    process.wait(timeout=5)
