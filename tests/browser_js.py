"""Boot the real disk in QEMU and exercise PS/2 input through QMP."""
import json
import pathlib
import os
import http.server
import threading
import base64
PAGE = b'<html><head><title>Before</title><link rel="stylesheet" href="/site.css"></head><body><h1 id="hello">Before</h1><img src="/pixel.png" style="width:24px;height:24px"><p>This document came over real HTTP.</p><script src="/app.js"></script></body></html>'
CSS = b'body {padding:20px;} #hello {color: blue;} h1 {font-size:26px;}'
JS = b'let total=0; for(let i=0;i<4;i++){total+=i;}; if(total===6){document.title="JS PASS"; document.querySelector("#hello").textContent="JavaScript executed"; document.querySelector("#hello").style.color="red";}; document.querySelector("#hello").addEventListener("click",function(){document.title="CLICK PASS";});'
PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR42mP4z8AAAAMBAQD3A0FDAAAAAElFTkSuQmCC")
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/pixel.png":
            self.send_response(200); self.send_header("Content-Type", "image/png"); self.send_header("Content-Length", str(len(PNG))); self.end_headers(); self.wfile.write(PNG); return
        if self.path == "/site.css":
            self.send_response(200); self.send_header("Content-Type", "text/css"); self.send_header("Content-Length", str(len(CSS))); self.end_headers(); self.wfile.write(CSS); return
        if self.path == "/app.js":
            self.send_response(200); self.send_header("Content-Type", "application/javascript"); self.send_header("Content-Length", str(len(JS))); self.end_headers(); self.wfile.write(JS); return
        time.sleep(1)
        self.send_response(200)
        self.send_header('Content-Length',str(len(PAGE)))
        self.send_header('Content-Security-Policy', 'default-src https:; ' + ' ' * 600)
        self.end_headers();self.wfile.write(PAGE)
    def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',18080),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()

import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]
log = BUILD / "browser-js.log"
log.write_text("")
data_disk = BUILD / "browser-js-data.img"
data_disk.write_bytes(bytes(4 * 1024 * 1024))
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
    key("f6")
    key("ctrl-l")
    for c in "http://10.0.2.2:18080/": key({".":"dot",":":"shift-semicolon","/":"slash"}.get(c,c))
    key("ret")
    loading = shot("browser-loading")
    deadline=time.monotonic()+35
    while "BROWSER: Page loaded successfully" not in log.read_text():
        assert time.monotonic()<deadline,log.read_text()
        time.sleep(.2)
    rendered = shot("browser-js")
    assert rendered != loading, 'Page did not repaint after the request without user input'
    assert "BROWSER title: JS PASS" in log.read_text(),log.read_text()
    assert "IMAGE: PNG/JPEG decoded" in log.read_text(),log.read_text()
    # HMP sends relative PS/2 packets.  Keep each delta in the signed-byte
    # range; larger deltas set the overflow bit and are intentionally ignored.
    # The system pointer starts at (760,500), heading at about (240,255).
    hmp("mouse_move -250 -245")
    time.sleep(.12)
    hmp("mouse_move -250 0")
    time.sleep(.12)
    hmp("mouse_move -20 0")
    time.sleep(.12)
    hmp("mouse_button 1"); time.sleep(.15); hmp("mouse_button 0")
    deadline=time.monotonic()+3
    while "BROWSER title: CLICK PASS" not in log.read_text():
        assert time.monotonic()<deadline,log.read_text()
        time.sleep(.1)
    assert "Response status: 200" in log.read_text(),log.read_text()
    print("PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event")
finally:
    process.terminate()
    process.wait(timeout=5)
