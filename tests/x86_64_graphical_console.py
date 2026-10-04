"""Boot the x86_64 graphical console, type through PS/2 and inspect its framebuffer."""
import http.server, json, os, pathlib, shutil, socket, subprocess, threading, time

ROOT=pathlib.Path(__file__).resolve().parents[1]
BUILD=ROOT/"build"/"x86_64"/"kernel"
disk=BUILD/"PollikData-graphical.img"
serial=BUILD/"graphical-console.log"
shot=BUILD/"graphical-console.ppm"
before=BUILD/"graphical-console-before-mouse.ppm"
window_shot=BUILD/"windowdemo.ppm"
window_moved=BUILD/"windowdemo-moved.ppm"
window_closed=BUILD/"windowdemo-closed.ppm"
desktop_shot=BUILD/"desktop.ppm"
browser_shot=BUILD/"browser-native.ppm"
notes_shot=BUILD/"notes-native.ppm"
terminal_initial=BUILD/"terminal-initial.ppm"
terminal_after=BUILD/"terminal-after.ppm"
for artifact in (terminal_initial,terminal_after): artifact.unlink(missing_ok=True)
shot.unlink(missing_ok=True)
before.unlink(missing_ok=True)
window_shot.unlink(missing_ok=True)
window_moved.unlink(missing_ok=True)
window_closed.unlink(missing_ok=True)
desktop_shot.unlink(missing_ok=True)
browser_shot.unlink(missing_ok=True)
notes_shot.unlink(missing_ok=True)
shutil.copyfile(BUILD/"PollikData-test.img",disk)
serial.write_text("")
class BrowserFixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.requests.append(self.path)
        if self.path=="/style.css":
            body=b"#remote{background-color:#117766;padding:8px;}"
            content_type="text/css"
        elif self.path=="/app.js":
            body=(b"document.getElementById('remote').style.color='#315a91';"
                  b"console.log('remote HTTP script ran');")
            content_type="application/javascript"
        else:
            body=("<!doctype html><html><head><link rel='stylesheet' href='/style.css'></head>"
                  "<body><h1 id='remote'>Remote HTML</h1><script src='/app.js'></script><script>"
                  "console.log('remote inline script ran');</script></body></html>").encode()
            content_type="text/html; charset=utf-8"
        self.send_response(200); self.send_header("Content-Type",content_type)
        self.send_header("Content-Length",str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self,*args): pass
http_fixture=http.server.ThreadingHTTPServer(("0.0.0.0",0),BrowserFixture)
http_fixture.requests=[]
threading.Thread(target=http_fixture.serve_forever,daemon=True).start()
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1",0)); port=reserve.getsockname()[1]
p=subprocess.Popen(["qemu-system-x86_64","-machine","pc","-cpu","max","-m","256M","-vga","std",
    "-drive",f"format=raw,file={BUILD/'PollikOS-x86_64.img'},if=ide,index=0,snapshot=on",
    "-drive",f"format=raw,file={disk},if=ide,index=1,snapshot=on",
    "-netdev","user,id=net0","-device","rtl8139,netdev=net0","-display","none",
    "-serial",f"file:{serial}","-qmp",f"tcp:127.0.0.1:{port},server=on,wait=off"],
    cwd=ROOT,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
try:
    end=time.monotonic()+30
    while True:
        try: q=socket.create_connection(("127.0.0.1",port),timeout=1); break
        except OSError:
            if time.monotonic()>end: raise
            time.sleep(.05)
    stream=q.makefile("rwb",buffering=0); stream.readline()
    def call(name,args=None):
        stream.write((json.dumps({"execute":name,"arguments":args or {}})+"\n").encode())
        while True:
            reply=json.loads(stream.readline())
            if "return" in reply or "error" in reply: return reply
    def send_key(key,delay=.12):
        key_name="spc" if key==" " else key
        call("human-monitor-command",{"command-line":"sendkey "+key_name})
        time.sleep(delay)
    def screendump(path,label):
        call("human-monitor-command",{"command-line":f"screendump {path}"})
        end=time.monotonic()+5
        while not path.exists():
            if time.monotonic()>end: raise AssertionError(label+" screenshot missing")
            time.sleep(.05)
    call("qmp_capabilities")
    end=time.monotonic()+120
    while "[AUTH64] First run: create a local account." not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("x86_64 first-run account prompt timeout")
        time.sleep(.05)
    for key in "x64graph": send_key(key)
    send_key("ret")
    end=time.monotonic()+10
    while "Create password (6-63 characters):" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("x86_64 account-name input did not advance")
        time.sleep(.05)
    for key in "test123": send_key(key)
    send_key("ret")
    end=time.monotonic()+10
    while "Confirm password:" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("x86_64 password input did not advance")
        time.sleep(.05)
    for key in "test123": send_key(key)
    send_key("ret")
    end=time.monotonic()+15
    while "Account created." not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("x86_64 account setup did not finish")
        time.sleep(.05)
    end=time.monotonic()+120
    while "PollikOS:/> " not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("x86_64 graphical shell prompt timeout")
        time.sleep(.05)
    for marker in ("[GUI] authenticated session started /bin/desktop.pol",
                   "[desktop] ready: Files is fixed left of the Dock separator",
                   "[terminal] shell connected through PollikOS pipes",
                   "[terminal] output active"):
        end=time.monotonic()+120
        while marker not in serial.read_text(errors="replace"):
            if time.monotonic()>end: raise AssertionError("post-login desktop startup missing: "+marker)
            time.sleep(.05)
    end=time.monotonic()+25
    while "DHCP: Bound successfully, IP=" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("x86_64 RTL8139 did not acquire a DHCP lease")
        time.sleep(.05)
    screendump(terminal_initial,"initial terminal")
    for key in "echo hi": send_key(key,.06)
    send_key("ret")
    time.sleep(.8)
    screendump(terminal_after,"terminal command")
    initial=terminal_initial.read_bytes(); initial_header=initial.find(b"\n255\n")+5
    after=terminal_after.read_bytes(); after_header=after.find(b"\n255\n")+5
    if initial_header<5 or after_header<5 or len(initial)-initial_header!=len(after)-after_header:
        raise AssertionError("terminal screenshot format changed")
    terminal_changed=sum(initial[initial_header+i]!=after[after_header+i]
                         for i in range(len(after)-after_header))
    if terminal_changed<100:
        raise AssertionError("GUI terminal command did not produce visible shell output")
    print("PASS: x86_64 login opens the native Desktop and .pol Terminal runs the real shell")
    call("human-monitor-command",{"command-line":f"screendump {before}"})
    end=time.monotonic()+5
    while not before.exists():
        if time.monotonic()>end: raise AssertionError("pre-mouse framebuffer screenshot missing")
        time.sleep(.05)
    call("input-send-event",{"events":[
        {"type":"rel","data":{"axis":"x","value":120}},
        {"type":"rel","data":{"axis":"y","value":-80}}]})
    time.sleep(.5)  # the polled PS/2 packet is drained by PIT
    call("human-monitor-command",{"command-line":f"screendump {shot}"})
    end=time.monotonic()+5
    while not shot.exists():
        if time.monotonic()>end: raise AssertionError("framebuffer screenshot missing")
        time.sleep(.05)
    raw=shot.read_bytes(); header=raw.find(b"\n255\n")+5
    if header<5 or len(set(raw[header:]))<3: raise AssertionError("graphical console framebuffer is blank")
    old=before.read_bytes(); old_header=old.find(b"\n255\n")+5
    if old_header<5 or len(old)-old_header!=len(raw)-header:
        raise AssertionError("framebuffer screenshot format changed")
    changed=sum(old[old_header+i]!=raw[header+i] for i in range(len(raw)-header))
    if os.environ.get("POLLIK_SKIP_MOUSE_FRAME_DIFF") != "1" and (changed<60 or changed>6000):
        raise AssertionError(f"PS/2 mouse cursor did not move cleanly (changed bytes={changed})")
    send_key("ctrl-shift-q")
    end=time.monotonic()+10
    while "[terminal] closing" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("Terminal close shortcut did not exit the app")
        time.sleep(.05)
    send_key("d")
    end=time.monotonic()+15
    while "[windowdemo] ready" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError(".pol window application did not start")
        time.sleep(.05)
    call("human-monitor-command",{"command-line":f"screendump {window_shot}"})
    end=time.monotonic()+5
    while not window_shot.exists():
        if time.monotonic()>end: raise AssertionError("window screenshot missing")
        time.sleep(.05)
    frame=window_shot.read_bytes(); frame_header=frame.find(b"\n255\n")+5
    width,height=map(int,frame[:frame.find(b"\n255\n")].splitlines()[1].split())
    pixel=((height-(220+34))//2+32)*width*3+((width-(360+4))//2+2)*3
    if frame[frame_header+pixel:frame_header+pixel+3]!=bytes((24,32,62)):
        raise AssertionError(".pol window surface was not composited at its expected pixel")
    call("human-monitor-command",{"command-line":"sendkey a"})
    call("human-monitor-command",{"command-line":"sendkey ctrl-a"})
    end=time.monotonic()+5
    while True:
        current=serial.read_text(errors="replace")
        if "[windowdemo] key=97 mods=0 down=1" in current and \
           "[windowdemo] key=97 mods=2 down=1" in current: break
        if time.monotonic()>end:
            raise AssertionError("PS/2 keyboard input/modifiers did not reach the active .pol window: "+current[-1000:])
        time.sleep(.05)
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"wheel-up","down":True}}]})
    time.sleep(.1)
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"wheel-up","down":False}}]})
    time.sleep(.2)
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":True}}]})
    time.sleep(.2)
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":False}}]})
    import re
    end=time.monotonic()+5
    while True:
        current=serial.read_text(errors="replace")
        events=[tuple(map(int,item)) for item in re.findall(
            r"event=(\d+) changed=(\d+) buttons=(\d+) wheel=(-?\d+)",current)]
        have_button=any(kind&2 and changed for kind,changed,buttons,wheel in events)
        have_wheel=any(kind&4 and wheel for kind,changed,buttons,wheel in events)
        if have_button and have_wheel: break
        if time.monotonic()>end: raise AssertionError("mouse button/wheel events did not reach the .pol app: "+current[-1400:])
        time.sleep(.05)
    button_events=re.findall(r"event=2 changed=1 buttons=\d+ wheel=-?\d+ at=(-?\d+),(-?\d+)",serial.read_text(errors="replace"))
    if not button_events: raise AssertionError("mouse event did not contain screen coordinates")
    mouse_pos=list(map(int,button_events[-1]))
    def move_pointer(target_x,target_y):
        while mouse_pos[0]!=target_x or mouse_pos[1]!=target_y:
            dx=max(-100,min(100,target_x-mouse_pos[0]))
            dy=max(-100,min(100,target_y-mouse_pos[1]))
            events=[]
            if dx: events.append({"type":"rel","data":{"axis":"x","value":dx}})
            if dy: events.append({"type":"rel","data":{"axis":"y","value":dy}})
            call("input-send-event",{"events":events}); time.sleep(.12)
            mouse_pos[0]+=dx; mouse_pos[1]+=dy
    move_pointer(400,270)  # drag from titlebar
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":True}}]})
    time.sleep(.15)
    move_pointer(440,300)
    time.sleep(.15)
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":False}}]})
    time.sleep(.2)
    call("human-monitor-command",{"command-line":f"screendump {window_moved}"})
    end=time.monotonic()+5
    while not window_moved.exists():
        if time.monotonic()>end: raise AssertionError("moved-window screenshot missing")
        time.sleep(.05)
    moved=window_moved.read_bytes(); moved_header=moved.find(b"\n255\n")+5
    moved_pixel=((height-(220+34))//2+32+30)*width*3+((width-(360+4))//2+2+40)*3
    if moved[moved_header+moved_pixel:moved_header+moved_pixel+3]!=bytes((24,32,62)):
        raise AssertionError("titlebar drag did not move the process window")
    target_x,target_y=720,300  # close control after the 40x30 titlebar drag
    while mouse_pos[0]!=target_x or mouse_pos[1]!=target_y:
        dx=max(-100,min(100,target_x-mouse_pos[0])); dy=max(-100,min(100,target_y-mouse_pos[1]))
        events=[]
        if dx: events.append({"type":"rel","data":{"axis":"x","value":dx}})
        if dy: events.append({"type":"rel","data":{"axis":"y","value":dy}})
        call("input-send-event",{"events":events}); time.sleep(.12)
        mouse_pos[0]+=dx; mouse_pos[1]+=dy
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":True}}]})
    time.sleep(.15)
    call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":False}}]})
    end=time.monotonic()+5
    while "[windowdemo] close event received" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("window titlebar close button did not close the .pol window")
        time.sleep(.05)
    time.sleep(.35)
    call("human-monitor-command",{"command-line":f"screendump {window_closed}"})
    end=time.monotonic()+5
    while not window_closed.exists():
        if time.monotonic()>end: raise AssertionError("closed-window screenshot missing")
        time.sleep(.05)
    closed=window_closed.read_bytes(); closed_header=closed.find(b"\n255\n")+5
    if closed[closed_header+moved_pixel:closed_header+moved_pixel+3]==bytes((24,32,62)):
        raise AssertionError("destroying the window did not restore its saved framebuffer")

    time.sleep(.4)
    screendump(desktop_shot,"desktop")

    def click_at(x,y):
        move_pointer(x,y)
        call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":True}}]})
        time.sleep(.15)
        call("input-send-event",{"events":[{"type":"btn","data":{"button":"left","down":False}}]})
        time.sleep(.2)
    def wait_count(marker,wanted,timeout=20):
        end=time.monotonic()+timeout
        while serial.read_text(errors="replace").count(marker)<wanted:
            if time.monotonic()>end: raise AssertionError(f"timed out waiting for {marker} count {wanted}")
            time.sleep(.05)

    # The Files Dock icon opens the real /bin/files.pol process. Its sorted
    # application list opens windowdemo.pol with Down, Down, Enter.
    desktop_w,desktop_h=(720,500) if width>=724 and height>=534 else (560,300)
    desktop_content_x=(width-(desktop_w+4))//2+2
    desktop_content_y=(height-(desktop_h+34))//2+32
    desktop_files_x=desktop_content_x+desktop_w//2-118+21
    desktop_demo_x=desktop_content_x+desktop_w//2-118+128+21
    desktop_browser_x=desktop_content_x+desktop_w//2-118+192+21
    desktop_dock_y=desktop_content_y+desktop_h-72+20
    click_at(desktop_files_x,desktop_dock_y)
    end=time.monotonic()+10
    while "[files] ready" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("Dock did not open Files")
        time.sleep(.05)
    ready_count=serial.read_text(errors="replace").count("[windowdemo] ready")
    # Select Window Demo from the expanded .pol list; the extra Arrow also
    # tolerates a dropped PS/2 key while remaining clamped to the last item.
    for _ in range(6): send_key("down")
    send_key("ret")
    wait_count("[windowdemo] ready",ready_count+1)
    escape_count=serial.read_text(errors="replace").count("[windowdemo] escape received")
    send_key("esc")
    wait_count("[windowdemo] escape received",escape_count+1)
    time.sleep(.35)  # allow exit/reap and transfer focus to Files
    files_w,files_h=(650,400) if width>=654 and height>=434 else (560,300)
    files_left=(width-(files_w+4))//2
    files_top=(height-(files_h+34))//2
    click_at(files_left+files_w+4-14,files_top+15)  # titlebar close control
    end=time.monotonic()+10
    while "[files] closing" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("Files did not close after titlebar close")
        time.sleep(.05)

    click_at(desktop_browser_x,desktop_dock_y)
    end=time.monotonic()+15
    while "[browser] ready: native HTML parser and CSS renderer" not in serial.read_text(errors="replace") or \
          "[browser] JavaScript executed" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("Dock did not launch the native Browser .pol app")
        time.sleep(.05)
    call("human-monitor-command",{"command-line":f"screendump {browser_shot}"})
    end=time.monotonic()+5
    while not browser_shot.exists():
        if time.monotonic()>end: raise AssertionError("native browser screenshot missing")
        time.sleep(.05)
    browser_frame=browser_shot.read_bytes();browser_header=browser_frame.find(b"\n255\n")+5
    title_color=bytes((0x17,0x24,0x3a))
    if browser_header<5 or browser_frame[browser_header:].count(title_color)<12:
        raise AssertionError("local HTML page CSS color was not rendered in the native browser")
    script_color=bytes((0x31,0x5a,0x91))
    if browser_frame[browser_header:].count(script_color)<12:
        raise AssertionError("local JavaScript did not mutate and restyle the HTML document")
    def color_bounds(frame,color):
        end_header=frame.find(b"\n255\n")+5
        screen_w,screen_h=map(int,frame[:frame.find(b"\n255\n")].splitlines()[1].split())
        xs=[];ys=[]
        for offset in range(end_header,len(frame),3):
            if frame[offset:offset+3]==color:
                pixel=(offset-end_header)//3
                xs.append(pixel%screen_w);ys.append(pixel//screen_w)
        if not xs:return None
        return min(xs),min(ys),max(xs),max(ys)
    button_bounds=color_bounds(browser_frame,bytes((0x0f,0x76,0x6e)))
    if not button_bounds or button_bounds[2]-button_bounds[0]<40:
        raise AssertionError("native page button was not rendered for JavaScript interaction")
    click_count=serial.read_text(errors="replace").count("[browser] click dispatched")
    click_at((button_bounds[0]+button_bounds[2])//2,(button_bounds[1]+button_bounds[3])//2)
    end=time.monotonic()+10
    while serial.read_text(errors="replace").count("[browser] click dispatched")<=click_count:
        if time.monotonic()>end: raise AssertionError("native page click did not reach the browser")
        time.sleep(.05)
    if "[browser] JS click listener matched" not in serial.read_text(errors="replace") or \
       "[browser:js] click listener body ran" not in serial.read_text(errors="replace") or \
       "[browser:js] The native browser ran this click handler." not in serial.read_text(errors="replace"):
        raise AssertionError("page addEventListener callback did not run")
    time.sleep(.2)
    browser_shot.unlink(missing_ok=True)
    call("human-monitor-command",{"command-line":f"screendump {browser_shot}"})
    end=time.monotonic()+5
    while not browser_shot.exists():
        if time.monotonic()>end: raise AssertionError("post-click browser screenshot missing")
        time.sleep(.05)
    browser_after=browser_shot.read_bytes();browser_after_header=browser_after.find(b"\n255\n")+5
    click_color=bytes((0xbe,0x12,0x3c))
    if browser_after_header<5 or browser_after[browser_after_header:].count(click_color)<12:
        raise AssertionError("JavaScript click listener did not update the DOM style")
    browser_left=(width-(920+4))//2
    browser_top=(height-(640+34))//2
    click_at(browser_left+20,browser_top+150)  # raise the wide Browser window above the centered Desktop
    send_key("f")
    remote_url=f"http://10.0.2.2:{http_fixture.server_address[1]}/"
    for character in remote_url:
        key={":":"shift-semicolon","/":"slash",".":"dot"}.get(character,character)
        send_key(key,.14)
    send_key("ret")
    call("human-monitor-command",{"command-line":f"screendump {BUILD/'browser-http-input.ppm'}"})
    end=time.monotonic()+20
    while "[browser] HTTP document loaded" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("browser did not finish the native HTTP response: "+serial.read_text(errors="replace")[-1800:])
        time.sleep(.05)
    output=serial.read_text(errors="replace")
    if http_fixture.requests!=["/","/style.css","/app.js"] or \
       "[browser:js] remote HTTP script ran" not in output or \
       "[browser:js] remote inline script ran" not in output:
        raise AssertionError("remote HTML/CSS/JavaScript assets did not run through the browser")
    time.sleep(.2)
    browser_shot.unlink(missing_ok=True)
    call("human-monitor-command",{"command-line":f"screendump {browser_shot}"})
    end=time.monotonic()+5
    while not browser_shot.exists():
        if time.monotonic()>end: raise AssertionError("remote browser screenshot missing")
        time.sleep(.05)
    remote_frame=browser_shot.read_bytes();remote_header=remote_frame.find(b"\n255\n")+5
    if remote_header<5 or remote_frame[remote_header:].count(bytes((0x31,0x5a,0x91)))<12 or \
       remote_frame[remote_header:].count(bytes((0x11,0x77,0x66)))<12:
        raise AssertionError("remote HTML inline CSS/JavaScript styling did not render")
    browser_left=(width-(920+4))//2
    browser_top=(height-(640+34))//2
    https_url=os.environ.get("POLLIK_X64_HTTPS_URL")
    if https_url:
        handshake_count=serial.read_text(errors="replace").count("TLS64: certificate-validated handshake complete")
        loaded_count=serial.read_text(errors="replace").count("[browser] HTTP document loaded")
        click_at(browser_left+20,browser_top+150)
        send_key("f")
        for character in https_url:
            key={":":"shift-semicolon","/":"slash",".":"dot","-":"minus"}.get(character,character)
            send_key(key,.14)
        send_key("ret")
        end=time.monotonic()+45
        while True:
            output=serial.read_text(errors="replace")
            if output.count("TLS64: certificate-validated handshake complete")>handshake_count and \
               output.count("[browser] HTTP document loaded")>loaded_count:
                break
            if time.monotonic()>end:
                raise AssertionError("x86_64 HTTPS page did not finish: "+output[-1800:])
            time.sleep(.1)
        if "HTTP64: request failed" in output:
            raise AssertionError("x86_64 HTTPS request failed after the handshake")
        print("PASS: x86_64 browser validates TLS certificates and loads an HTTPS page")
    browser_left=(width-(920+4))//2
    browser_top=(height-(640+34))//2
    click_at(browser_left+920+4-14,browser_top+15)
    end=time.monotonic()+10
    while "[browser] closing" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("native browser did not close cleanly")
        time.sleep(.05)

    desktop_notes_x=desktop_browser_x+64
    click_at(desktop_notes_x,desktop_dock_y)
    end=time.monotonic()+15
    while "[notes] ready: Ctrl+S saves /home/notes.txt" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("Dock did not launch Notes .pol")
        time.sleep(.05)
    screendump(notes_shot,"Notes")
    send_key("x")
    send_key("ctrl-s")
    end=time.monotonic()+10
    while "[notes] saved /home/notes.txt" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("Notes did not persist edits to PollikFS")
        time.sleep(.05)
    notes_left=(width-(760+4))//2
    notes_top=(height-(560+34))//2
    click_at(notes_left+760+4-14,notes_top+15)
    end=time.monotonic()+10
    while "[notes] closing" not in serial.read_text(errors="replace"):
        if time.monotonic()>end: raise AssertionError("Notes did not close cleanly")
        time.sleep(.05)
    loaded_count=serial.read_text(errors="replace").count("[notes] loaded /home/notes.txt")
    click_at(desktop_notes_x,desktop_dock_y)
    end=time.monotonic()+15
    while serial.read_text(errors="replace").count("[notes] loaded /home/notes.txt")<=loaded_count:
        if time.monotonic()>end: raise AssertionError("Notes did not reload its saved PollikFS file")
        time.sleep(.05)
    click_at(notes_left+760+4-14,notes_top+15)
    end=time.monotonic()+10
    while serial.read_text(errors="replace").count("[notes] closing")<2:
        if time.monotonic()>end: raise AssertionError("reopened Notes did not close cleanly")
        time.sleep(.05)
    print("PASS: Notes .pol edits, saves and reloads /home/notes.txt")

    # Open nine app windows at once. The former fixed eight-slot table failed
    # here; the manager now has one slot per possible process.
    ready_count=serial.read_text(errors="replace").count("[windowdemo] ready")
    click_at(desktop_demo_x,desktop_dock_y)  # Dock -> Window Demo
    wait_count("[windowdemo] ready",ready_count+1)
    for index in range(8):
        click_at(desktop_content_x+10,desktop_content_y-18)  # expose/activate Desktop's titlebar
        send_key("d")
        wait_count("[windowdemo] ready",ready_count+index+2)
    if serial.read_text(errors="replace").count("[windowdemo] ready")<ready_count+9:
        raise AssertionError("the desktop did not keep nine .pol windows alive at once")
    print("PASS: x86_64 Dock/Files .pol launch, keyboard/mouse windows and 9 concurrent app windows")
finally:
    p.terminate()
    try: p.wait(5)
    except subprocess.TimeoutExpired: p.kill()
    http_fixture.shutdown(); http_fixture.server_close()

