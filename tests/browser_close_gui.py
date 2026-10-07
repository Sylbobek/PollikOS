"""Close a real in-flight HTTP browser session and reopen a clean home page."""
import http.server,re,struct,subprocess,threading,time
from gui_metrics import Guest,ROOT
from browser_support import browser_offsets
from gui_fixture import finish_setup
started=threading.Event()
requests=[]
class SlowPage(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append(self.path)
        self.send_response(200);self.send_header('Content-Length','65536');self.end_headers()
        started.set()
        try:
            for _ in range(256):self.wfile.write(b' '*256);self.wfile.flush();time.sleep(.05)
        except (BrokenPipeError,ConnectionResetError,OSError):pass
    def log_message(self,*args):pass
def main():
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),SlowPage)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    # Offsets come from the actual private transport definition, not constants.
    source='#include "net/tcp.c"\nunsigned offsets[]={__builtin_offsetof(struct TcpSocket,active),sizeof(struct TcpSocket)};\n'
    ir=subprocess.check_output(['clang','--target=i386-none-elf','-ffreestanding','-Ikernel',
        '-x','c','-S','-emit-llvm','-o','-','-'],input=source,text=True,cwd=ROOT)
    row=next(line for line in ir.splitlines() if line.startswith('@offsets ='))
    active,size=map(int,re.findall(r'i32 (\d+)',row));offsets=browser_offsets()
    try:
        with Guest('1024x768','browser-close',headless=True,network=True,boot_only=True) as g:
            g.wait(lambda:'[TEST] PHASE 2 PASS' in g.log.read_text(),'boot stress baseline',120)
            finish_setup(g.qmp,g.log)
            base=g.symbol('g_browser')[0]
            def field(name):return struct.unpack('<I',g.memory(base+offsets[name],4))[0]
            def tcp_active():
                address,bytes=g.symbol('sockets');raw=g.memory(address,bytes)
                assert bytes%size==0
                return sum(struct.unpack_from('<I',raw,i+active)[0]!=0 for i in range(0,bytes,size))
            g.wait(lambda:'DHCP: Bound successfully' in g.log.read_text(),'network ready',30)
            baseline=tcp_active()
            g.key('f6',lambda:g.words('g_focused_window')[0]==5,'Browser open')
            g.wait(lambda:not g.words('g_window_anims',5)[0],'opening animation')
            g.wait(lambda:field('document') and not field('is_loading') and not g.words('load_active')[0],
                   'home transaction complete',20)
            g.hmp('sendkey ctrl-l 1')
            g.wait(lambda:field('is_typing_url'),'address focus')
            url=f'http://10.0.2.2:{server.server_port}/slow'
            for index,ch in enumerate(url):
                g.hmp('sendkey '+{':':'shift-semicolon','/':'slash','.':'dot'}.get(ch,ch)+' 1')
                g.wait(lambda:g.memory(base+offsets['input_url'],256).split(b'\0')[0]==url[:index+1].encode(),'typed URL prefix')
            print('RAW verified address '+url,flush=True)
            g.hmp('sendkey ret 1');assert started.wait(20),'guest never requested slow page'
            assert field('is_loading') and tcp_active()>baseline,'test did not enter active HTTP load'
            g.key('alt-f4',lambda:not g.window(5)[7],'Browser close while loading')
            g.wait(lambda:not field('document') and not field('is_loading'),'closed session reclaimed',15)
            assert tcp_active()==baseline,('owned TCP socket remains active',baseline,tcp_active())
            print(f'RAW closed browser: document={field("document")} loading={field("is_loading")} TCP before={baseline} after={tcp_active()}',flush=True)
            g.key('f6',lambda:g.words('g_focused_window')[0]==5 and g.window(5)[7],'Browser reopen')
            g.wait(lambda:field('document') and not field('is_loading') and not g.words('load_active')[0],
                   'fresh home transaction complete',20)
            address=g.memory(base+offsets['input_url'],256).split(b'\0')[0]
            assert address==b'about:home',address
            assert requests==['/slow'],requests
            assert 'PANIC' not in g.log.read_text() and 'MEM kfree corrupt' not in g.log.read_text()
            print('PASS browser close: active HTTP aborted, DOM released, fresh home, no background requests',flush=True)
    finally:server.shutdown();server.server_close()
if __name__=='__main__':main()
