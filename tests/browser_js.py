"""Boot the real disk in QEMU and exercise PS/2 input through QMP."""
import json
import struct
import tempfile
import pathlib
import os
import http.server
import threading
import base64
import io
from PIL import Image
gif_stream = io.BytesIO()
Image.new("RGB", (12, 12), (255, 0, 255)).save(
    gif_stream, format="GIF", save_all=True,
    append_images=[Image.new("RGB", (12, 12), (0, 255, 255))],
    duration=[80, 80], loop=0, disposal=2)
ANIMATED_GIF = gif_stream.getvalue()
PAGE = b'<html><head><title>Before</title><link rel="stylesheet" href="/site.css"></head><body><img src="/animation.gif" style="width:32px;height:32px"><h1 id="hello">Before</h1><img src="/pixel.png" style="width:24px;height:24px"><p>This document came over real HTTP.</p><script src="/app.js"></script></body></html>'
CSS = b'body {padding:20px;} #hello {color: blue;} h1 {font-size:26px;}'
JS = b'let total=0; for(let i=0;i<4;i++){total+=i;}; if(total===6){document.title="JS PASS"; document.querySelector("#hello").textContent="JavaScript executed"; document.querySelector("#hello").style.color="red";}; document.querySelector("#hello").addEventListener("click",function(){document.title="CLICK PASS";});'
PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR42mP4z8AAAAMBAQD3A0FDAAAAAElFTkSuQmCC")
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/animation.gif":
            self.send_response(200); self.send_header("Content-Type", "image/gif"); self.send_header("Content-Length", str(len(ANIMATED_GIF))); self.end_headers(); self.wfile.write(ANIMATED_GIF); return
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
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
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
data_folder=tempfile.TemporaryDirectory(prefix='pollikos-browser-js-')
data_disk = pathlib.Path(data_folder.name) / "data.img"
from gui_fixture import create_gui_disk
create_gui_disk(data_disk)
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
    from browser_support import browser_offsets
    offsets = browser_offsets()
    symbols = {}
    for line in subprocess.check_output(['llvm-nm','-S',str(BUILD/'kernel.elf')],text=True).splitlines():
        fields=line.split()
        if len(fields)==4: symbols[fields[3]]=(int(fields[0],16),int(fields[1],16))
    def memory(address, size):
        path=BUILD/'browser-js-probe.bin'
        qmp('pmemsave', {'val':address,'size':size,'filename':str(path)})
        return path.read_bytes()
    def scalar(name):
        return int.from_bytes(memory(symbols[name][0],symbols[name][1]),'little')
    def browser_field(name):
        return int.from_bytes(memory(symbols['g_browser'][0]+offsets[name],4),'little')
    def browser_input():
        return memory(symbols['g_browser'][0]+offsets['input_url'],256).split(b'\0')[0].decode()
    def wait(predicate, message, seconds=30):
        end=time.monotonic()+seconds
        while not predicate():
            assert process.poll() is None and time.monotonic()<end,message
            time.sleep(.02)
    while "desktop ready" not in log.read_text():
        if time.monotonic() > deadline:
            raise AssertionError("Kernel did not reach the desktop")
        time.sleep(.1)
    if "SETUP: first-run installer ready" in log.read_text():
        hmp("sendkey ret")
        for character in "browserjs": key(character)
        key("ret")
        for character in "test123": key(character)
        key("ret")
        for character in "test123": key(character)
        key("ret")
        deadline = time.monotonic() + 10
        while "AUTH: account created; installation complete" not in log.read_text():
            if time.monotonic() > deadline:
                raise AssertionError("First-run setup did not complete")
            time.sleep(.1)
    else:
        assert "AUTH: login required" in log.read_text(), "fresh browser test disk was not detected"
    deadline = time.monotonic()+15
    while "DHCP: Bound successfully" not in log.read_text():
        assert time.monotonic()<deadline,log.read_text()
        time.sleep(.1)
    key("f6")
    wait(lambda: scalar('g_focused_window')==5 and not scalar('load_active') and
         not browser_field('has_pending_navigation') and not browser_field('is_loading') and
         browser_field('document'), 'Browser home did not finish before Ctrl+L')
    # Focus the visible address bar with the mouse so the smoke test exercises
    # the same path as a desktop user, independent of host Ctrl-key synthesis.
    def move(x,y):
        while (scalar('mx'),scalar('my')) != (x,y):
            mx,my=scalar('mx'),scalar('my')
            dx,dy=max(-5,min(5,x-mx)),max(-5,min(5,y-my))
            hmp(f'mouse_move {dx} {dy}')
            wait(lambda: (scalar('mx'),scalar('my'))==(mx+dx,my+dy),'PS/2 delta consumption')
    window=struct.unpack('<21I',memory(symbols['g_windows'][0]+5*84,84))
    move(window[2]+200,window[3]+50)
    hmp("mouse_button 1"); time.sleep(.1); hmp("mouse_button 0")
    key("ctrl-l")
    wait(lambda: browser_field('is_typing_url') and browser_input()=='','Ctrl+L did not focus/clear address')
    url=f"http://10.0.2.2:{server.server_port}/"
    for index,c in enumerate(url):
        key({".":"dot",":":"shift-semicolon","/":"slash"}.get(c,c))
        wait(lambda: browser_input()==url[:index+1],'address input diverged: '+browser_input())
    print('RAW verified address',browser_input(),flush=True)
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
    assert "IMAGE: animated GIF decoded" in log.read_text(),log.read_text()
    frame_a = shot("browser-gif-frame-a")
    image_a=Image.open(io.BytesIO(frame_a)).convert('RGB')
    gif_points=[(x,y) for y in range(image_a.height) for x in range(image_a.width)
                if image_a.getpixel((x,y)) in ((255,0,255),(0,255,255))]
    assert len(gif_points)==32*32, 'GIF fixture did not occupy its 32x32 viewport'
    gif_box=(min(x for x,y in gif_points),min(y for x,y in gif_points),
             max(x for x,y in gif_points)+1,max(y for x,y in gif_points)+1)
    first_gif=image_a.crop(gif_box).tobytes()
    deadline=time.monotonic()+3
    while True:
        frame_b = shot("browser-gif-frame-b")
        second_gif=Image.open(io.BytesIO(frame_b)).convert('RGB').crop(gif_box).tobytes()
        if second_gif != first_gif: break
        assert time.monotonic()<deadline, 'GIF viewport did not advance within 3 seconds'
    assert frame_a != frame_b, "Animated GIF did not repaint between frames"
    # HMP sends relative PS/2 packets.  Keep each delta in the signed-byte
    # range; larger deltas set the overflow bit and are intentionally ignored.
    # The system pointer starts at (760,500), heading at about (240,255).
    # The animated image now occupies one line before the heading, so aim at
    # the heading's updated vertical position.
    move(window[2]+70,window[3]+150)
    hmp("mouse_button 1"); time.sleep(.15); hmp("mouse_button 0")
    deadline=time.monotonic()+3
    while "BROWSER title: CLICK PASS" not in log.read_text():
        assert time.monotonic()<deadline,log.read_text()
        time.sleep(.1)
    assert "Response status: 200" in log.read_text(),log.read_text()
    print("PASS: HTTP document, PNG, Elk arithmetic/loop/conditional, DOM mutation and click event")
except BaseException:
    symbols = {}
    for line in subprocess.check_output(['llvm-nm','-S',str(BUILD/'kernel.elf')],text=True).splitlines():
        fields=line.split()
        if len(fields)==4: symbols[fields[3]]=(int(fields[0],16),int(fields[1],16))
    qmp('stop')
    def probe(name, size=None):
        address, length = symbols[name]
        path=BUILD/'browser-js-diagnostic.bin'
        qmp('pmemsave', {'val':address,'size':size or length,'filename':str(path)})
        return path.read_bytes()
    for name in ('mx','my','g_focused_window','g_windows','load_active'):
        raw=probe(name)
        print('RAW failure',name,struct.unpack('<'+'I'*(len(raw)//4),raw) if len(raw)%4==0 else tuple(raw),flush=True)
    raw=probe('g_browser')
    off=offsets['input_url']
    print('RAW failure input_url',raw[off:off+256].split(b'\0')[0],
          'typing',browser_field('is_typing_url'),'pending',browser_field('has_pending_navigation'),
          'loading',browser_field('is_loading'),flush=True)
    qmp('screendump',{'filename':str(BUILD/'browser-js-failure.ppm')})
    raise
finally:
    process.terminate()
    process.wait(timeout=5)
    server.shutdown()
    server.server_close()
    data_folder.cleanup()
