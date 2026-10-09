"""Real guest TCP transport and Ring 3 HTTP parser against a local fixture."""
import http.server,threading,tempfile,shutil,subprocess,socket,time,os
from pathlib import Path
from x86_64_console import Console,newest_image,ROOT
from x86_64_security_session import port,connect
B=ROOT/'build/x86_64'/os.environ.get('POLLIK_X64_VARIANT','kernel')
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  self.send_response(200)
  if self.path=='/chunked':
   self.send_header('Transfer-Encoding','chunked');self.end_headers();self.wfile.write(b'2\r\nbe\r\n2\r\nta\r\n0\r\n\r\n')
  else:
   self.send_header('Content-Length','5');self.end_headers();self.wfile.write(b'alpha')
 def log_message(self,*args):pass
def run():
 subprocess.run(['powershell','-NoProfile','-File','sdk/tools/pollikcc.ps1','sdk/tests/network_clients.c','-o','build/network_clients.pol'],cwd=ROOT,check=True)
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
 with tempfile.TemporaryDirectory(prefix='pollik-network-') as temporary:
  disk=Path(temporary)/'data.img';shutil.copyfile(B/'PollikData-test.img',disk)
  subprocess.run(['python','sdk/tools/pollikinstall.py',str(disk),'/bin/network_clients.pol','build/network_clients.pol'],cwd=ROOT,check=True)
  serial=port();process=subprocess.Popen(['qemu-system-x86_64','-accel','tcg','-cpu','qemu64','-m','128','-vga','std','-display','none','-monitor','none',
   '-serial',f'tcp:127.0.0.1:{serial},server=on,wait=on','-netdev','user,id=n','-device','rtl8139,netdev=n','-no-reboot',
   '-drive',f'file={newest_image(B)},format=raw,if=ide,index=0,snapshot=on','-drive',f'file={disk},format=raw,if=ide,index=1'],
   stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
  stream=None;c=None
  try:
   stream=connect(serial);c=Console(stream);c.wait_for('Create account name:',180);c.send('networkuser')
   c.wait_for('Create password (6-63 characters):',180);c.send('test123');c.wait_for('Confirm password:',180);c.send('test123')
   c.wait_for('Account created.',180);c.wait_for_prompt(300)
   c.run(f'/bin/network_clients.pol {server.server_port}',expected='NETWORK_CLIENTS_PASS',timeout=120)
   print('PASS native simultaneous HTTP, chunk decoding, foreign-owner denial and closed-handle rejection',flush=True)
  finally:
   if c:(ROOT/'build/stage610-network.log').write_text(c.text(),encoding='utf-8')
   if stream:stream.close()
   process.terminate();process.communicate(timeout=10);server.shutdown();server.server_close()
if __name__=='__main__':run()
