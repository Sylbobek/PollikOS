"""Native HTML/CSS/JS and PNG/JPEG HTTP pipeline, actual guest framebuffer."""
import http.server,io,json,pathlib,shutil,socket,subprocess,tempfile,threading,time,os,collections,argparse
from PIL import Image
from x86_64_console import Console,ROOT
B=ROOT/'build/x86_64'/os.environ.get('POLLIK_X64_VARIANT','system')
class BrowserConsole(Console):
 def pump(self,seconds=.1):
  changed=super().pump(seconds)
  if changed:
   (B/'browser-images-native.log').write_text(self.text(),encoding='utf-8')
   fault=self.text().find('[USER64] fault')
   assert (fault<0 or '\n' not in self.text()[fault:]) and '[X64] FAIL' not in self.text(),self.text()[-1500:]
   assert 'pollikc: abort' not in self.text(),self.text()[-1500:]
  return changed
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
png=io.BytesIO();Image.new('RGB',(64,48),(18,171,52)).save(png,format='PNG')
jpeg=io.BytesIO();Image.new('RGB',(64,48),(22,64,218)).save(jpeg,format='JPEG',quality=100)
expected_jpeg=Image.open(io.BytesIO(jpeg.getvalue())).getpixel((0,0))
requests=[]
class Fixture(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  requests.append(self.path)
  redirects={'/entry':'/index.html','/api/redirect':'/api/data'}
  if self.path in redirects:
   self.send_response(302);self.send_header('Location',redirects[self.path]);self.send_header('Content-Length','0');self.end_headers();return
  files={
   '/index.html':b'<html><head><link rel="stylesheet" href="/styles/site.css"><script src="/scripts/app.js"></script><script>console.log("INLINE_ONCE");</script><script type="application/ld+json">{"name":"data only"}</script><script type="text/plain">console.log("DATA_MUST_NOT_RUN");</script></head><body><div id="status">waiting</div><img id="p" src="pictures/p.png"><img id="j" src="pictures/p.jpg"></body></html>',
   '/styles/site.css':b'body{margin:0;padding:0;} #status{width:180px;height:30px;background-color:#eeeeee;} img{display:block;width:64px;height:48px;}',
   '/scripts/app.js':b'''document.getElementById("status").style.backgroundColor="#c17bd0";document.getElementById("status").textContent="script works";
function check(value,message){if(!value)throw new Error(message);}
let ready=false,tick=false,done=false;
function finish(){if(ready&&tick&&done)console.log("WEB_API_PASS");}
document.addEventListener("DOMContentLoaded",()=>{ready=true;finish();});
const cancelled=setTimeout(()=>console.log("CANCELLED_TIMER_RAN"),400);clearTimeout(cancelled);
let count=0;const interval=setInterval(()=>{check(++count===1,"interval cancellation");clearInterval(interval);tick=true;finish();},50);
(async()=>{
 check(location.href.indexOf("/index.html")>=0,"final redirect URL");
 const response=await fetch("api/redirect");check(response.status===200&&response.ok,"HTTP metadata");
 check(response.url.indexOf("/api/data")>=0,"fetch redirect URL");check(!response.bodyUsed,"initial bodyUsed");
 const data=await response.json();check(data.answer===42,"JSON response");check(response.bodyUsed,"consumed bodyUsed");
 let rejected=false;try{await response.text();}catch(e){rejected=true;}check(rejected,"body read twice");
 const missing=await fetch("/missing");check(missing.status===404&&!missing.ok,"404 response must resolve");check(await missing.text()==="not found","text response");
 const binary=await fetch("/api/bytes");const bytes=new Uint8Array(await binary.arrayBuffer());check(bytes.length===3&&bytes[0]===0&&bytes[2]===255,"binary response");
 done=true;finish();
})().catch(e=>console.log("WEB_API_FAIL",String(e)));''',
   '/api/data':b'{"answer":42}','/api/bytes':bytes([0,127,255]),
   '/pictures/p.png':png.getvalue(),'/pictures/p.jpg':jpeg.getvalue()}
  body=files.get(self.path,b'not found');self.send_response(200 if self.path in files else 404)
  self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 def log_message(self,*args):pass
def main(https_url=None):
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=server.serve_forever,daemon=True).start()
 with tempfile.TemporaryDirectory(prefix='pollik-web-images-') as tmp:
  data=pathlib.Path(tmp)/'data.img';shutil.copyfile(B/('PollikData-test.img' if B.name=='kernel' else 'PollikData-system.img'),data);sp,qp=port(),port()
  cmd=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64,+rdrand','-rtc','base=utc','-m','512','-vga','std','-display','none','-no-reboot',
   '-drive',f'file={B/"PollikOS-x86_64.img"},format=raw,if=ide,index=0,snapshot=on',
   '-drive',f'file={data},format=raw,if=ide,index=1,snapshot=on','-netdev','user,id=n','-device','rtl8139,netdev=n',
   '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off']
  print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);proc=subprocess.Popen(cmd,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));q=stream=c=None
  try:
   end=time.monotonic()+30
   while stream is None:
    try:stream=socket.create_connection(('127.0.0.1',sp),timeout=1)
    except OSError:assert time.monotonic()<end;time.sleep(.05)
   q=socket.create_connection(('127.0.0.1',qp),timeout=5);f=q.makefile('rwb',buffering=0);f.readline()
   def call(op,args=None):
    f.write((json.dumps({'execute':op,'arguments':args or {}})+'\n').encode())
    while True:
     raw=f.readline()
     if not raw:return {}
     r=json.loads(raw);assert 'error' not in r,r
     if 'return' in r:return r['return']
   call('qmp_capabilities');c=BrowserConsole(stream);call('cont')
   c.wait_for('[AUTH64] First run: create a local account.',120)
   for value,prompt in [('webuser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:c.send(value);c.wait_for(prompt,120)
   c.wait_for('[terminal] output active',120);c.wait_for_prompt(120)
   mark=len(c.transcript);c.send(f'/bin/browser.pol http://10.0.2.2:{server.server_port}/entry')
   c.wait_for('[browser] HTTP document loaded',120,mark)
   c.wait_for('WEB_API_PASS',120,mark)
   assert 'WEB_API_FAIL' not in c.text()[mark:] and 'CANCELLED_TIMER_RAN' not in c.text()[mark:],c.text()[mark:]
   assert c.text()[mark:].count('[browser] image decoded')==2,c.text()[mark:]
   assert c.text()[mark:].count('INLINE_ONCE')==1,('inline script must execute once',c.text()[mark:])
   assert 'DATA_MUST_NOT_RUN' not in c.text()[mark:],('data block evaluated as JavaScript',c.text()[mark:])
   expected=['/entry','/index.html','/styles/site.css','/scripts/app.js','/pictures/p.png','/pictures/p.jpg','/api/redirect','/api/data','/missing','/api/bytes'];assert collections.Counter(requests)==collections.Counter(expected),requests
   shot=B/'browser-images-native.ppm';deadline=time.monotonic()+30
   while True:
    call('screendump',{'filename':str(shot)});im=Image.open(shot).convert('RGB');pixels=list(im.getdata())
    green=sum(v==(18,171,52) for v in pixels)
    blue=sum(max(abs(v[i]-expected_jpeg[i]) for i in range(3))<=1 for v in pixels)
    styled=sum(v==(193,123,208) for v in pixels)
    if green>=2000 and blue>=2000 and styled>=1000:break
    assert time.monotonic()<deadline,('pixels',green,blue,styled);time.sleep(.15)
   im.save(shot.with_suffix('.png'))
   print('HTTP_REQUESTS',requests,flush=True)
   print(f'PIXELS PNG={green} RGB=(18,171,52); JPEG={blue} expected={expected_jpeg} maxerr=1; JS-styled={styled} RGB=(193,123,208)',flush=True)
   def key(value):
    call('human-monitor-command',{'command-line':'sendkey '+value});time.sleep(.15);c.pump(.01)
   def go(url):
    key('ctrl-l')
    names={'/':'slash',':':'shift-semicolon','.':'dot','-':'minus','?':'shift-slash','=':'equal','_':'shift-minus'}
    for char in url:key(names.get(char,char))
    key('ret')
   saved_mark=len(c.transcript);key('ctrl-d');c.wait_for('[browser] bookmarks saved',30,saved_mark)
   key('ctrl-s');c.wait_for('[browser] saved page /home/Downloads/',30,saved_mark)
   go('about:home');time.sleep(.3);home=B/'browser-home-native.ppm';call('screendump',{'filename':str(home)});Image.open(home).save(home.with_suffix('.png'))
   if https_url:
    https_mark=len(c.transcript);go(https_url);c.wait_for('[browser] HTTP document loaded',120,https_mark)
    assert '[browser] page status=200 url=https://' in c.text()[https_mark:],c.text()[https_mark:]
    time.sleep(.3);public=B/'browser-https-native.ppm';call('screendump',{'filename':str(public)});Image.open(public).save(public.with_suffix('.png'))
    print('PASS public HTTPS through guest DNS/TCP/TLS: '+https_url,flush=True)
   # Escape only leaves address editing in this browser; close its native WM
   # titlebar through real pointer input rather than changing that contract.
   mx,my=im.width//2,im.height//2
   tx=(im.width-924)//2+910;ty=(im.height-674)//2+15
   while (mx,my)!=(tx,ty):
    dx=max(-90,min(90,tx-mx));dy=max(-90,min(90,ty-my))
    call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},{'type':'rel','data':{'axis':'y','value':dy}}]});time.sleep(.08);mx+=dx;my+=dy
   for down in (True,False):call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]});time.sleep(.12)
   c.wait_for_prompt(30,mark)
   assert 'PANIC' not in c.text() and '[X64] FAIL' not in c.text()
   print('PASS native browser: HTTP redirects, HTML/CSS/JS, PNG/JPEG pixels, DOMContentLoaded, timers, async fetch JSON/text/binary/404/bodyUsed, bookmark/save, homepage and clean close',flush=True)
  finally:
   if c:(B/'browser-images-native.log').write_text(c.text(),encoding='utf-8')
   if q:q.close()
   if stream:stream.close()
   if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
   server.shutdown();server.server_close()
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--https',help='Also test a public HTTPS page returning HTTP 200');args=parser.parse_args();main(args.https)
