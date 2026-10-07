"""Native HTML/CSS/JS and PNG/JPEG HTTP pipeline, actual guest framebuffer."""
import http.server,io,json,pathlib,shutil,socket,subprocess,tempfile,threading,time
from PIL import Image
from x86_64_console import Console,ROOT
B=ROOT/'build/x86_64/system'
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
png=io.BytesIO();Image.new('RGB',(64,48),(18,171,52)).save(png,format='PNG')
jpeg=io.BytesIO();Image.new('RGB',(64,48),(22,64,218)).save(jpeg,format='JPEG',quality=100)
expected_jpeg=Image.open(io.BytesIO(jpeg.getvalue())).getpixel((0,0))
requests=[]
class Fixture(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  requests.append(self.path)
  files={
   '/index.html':b'<html><head><link rel="stylesheet" href="/styles/site.css"><script src="/scripts/app.js"></script><script>console.log("INLINE_ONCE");</script><script type="application/ld+json">{"name":"data only"}</script><script type="text/plain">console.log("DATA_MUST_NOT_RUN");</script></head><body><div id="status">waiting</div><img id="p" src="pictures/p.png"><img id="j" src="pictures/p.jpg"></body></html>',
   '/styles/site.css':b'body{margin:0;padding:0;} #status{width:180px;height:30px;background-color:#eeeeee;} img{display:block;width:64px;height:48px;}',
   '/scripts/app.js':b'document.getElementById("status").style.backgroundColor="#c17bd0";document.getElementById("status").textContent="script works";',
   '/pictures/p.png':png.getvalue(),'/pictures/p.jpg':jpeg.getvalue()}
  body=files.get(self.path,b'not found');self.send_response(200 if self.path in files else 404)
  self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 def log_message(self,*args):pass
def main():
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture);threading.Thread(target=server.serve_forever,daemon=True).start()
 with tempfile.TemporaryDirectory(prefix='pollik-web-images-') as tmp:
  data=pathlib.Path(tmp)/'data.img';shutil.copyfile(B/'PollikData-system.img',data);sp,qp=port(),port()
  cmd=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64','-m','8192','-vga','std','-display','none','-no-reboot',
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
   call('qmp_capabilities');c=Console(stream);call('cont')
   c.wait_for('[AUTH64] First run: create a local account.',120)
   for value,prompt in [('webuser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:c.send(value);c.wait_for(prompt,120)
   c.wait_for('[terminal] output active',120);c.wait_for_prompt(120)
   mark=len(c.transcript);c.send(f'/bin/browser.pol http://10.0.2.2:{server.server_port}/index.html')
   c.wait_for('[browser] HTTP document loaded',120,mark)
   assert c.text()[mark:].count('[browser] image decoded')==2,c.text()[mark:]
   assert c.text()[mark:].count('INLINE_ONCE')==1,('inline script must execute once',c.text()[mark:])
   assert 'DATA_MUST_NOT_RUN' not in c.text()[mark:],('data block evaluated as JavaScript',c.text()[mark:])
   expected=['/index.html','/styles/site.css','/scripts/app.js','/pictures/p.png','/pictures/p.jpg'];assert requests==expected,requests
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
   print('PASS native browser: real HTTP HTML/CSS/JS, document-relative PNG/JPEG after nested script URL, framebuffer colors, clean close',flush=True)
  finally:
   if c:(B/'browser-images-native.log').write_text(c.text(),encoding='utf-8')
   if q:q.close()
   if stream:stream.close()
   if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
   server.shutdown();server.server_close()
if __name__=='__main__':main()
