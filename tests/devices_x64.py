"""Native Ring 3 device controls, real QEMU Ethernet/DMA, snapshot-only disks."""
import argparse,http.server,json,shutil,socket,struct,subprocess,sys,tempfile,threading,time,wave
from pathlib import Path
from PIL import Image
from x86_64_console import Console,ROOT
sys.path.insert(0,str(ROOT/'tools'))
from sync_system_files import sync
B=ROOT/'build/x86_64/system'
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'device-proof';self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*args):pass
def run(ram,audio):
    label=f'devices-{ram}-{"ac97" if audio else "speaker"}'
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    with tempfile.TemporaryDirectory(prefix='pollik-devices-') as tmp:
        data=Path(tmp)/'data.img';shutil.copyfile(B/'PollikData-system.img',data)
        sync(data,{'/bin/device_test.pol':B/'userspace/devices_c.elf'})
        sp,qp=port(),port();wav=B/(label+'.wav')
        cmd=['qemu-system-x86_64','-S','-accel','tcg','-cpu','qemu64','-m',str(ram),'-vga','std','-display','none','-no-reboot',
            '-drive',f'file={B/"PollikOS-x86_64.img"},format=raw,if=ide,index=0,snapshot=on',
            '-drive',f'file={data},format=raw,if=ide,index=1,snapshot=on',
            '-netdev','user,id=n','-device','rtl8139,netdev=n',
            '-serial',f'tcp:127.0.0.1:{sp},server=on,wait=off','-qmp',f'tcp:127.0.0.1:{qp},server=on,wait=off']
        if audio:cmd+=['-audiodev',f'wav,id=a,path={wav}','-device','AC97,audiodev=a']
        print('COMMAND '+subprocess.list2cmdline(cmd),flush=True)
        proc=subprocess.Popen(cmd,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));stream=q=c=None
        try:
            end=time.monotonic()+30
            while stream is None:
                try:stream=socket.create_connection(('127.0.0.1',sp),timeout=1)
                except OSError:assert time.monotonic()<end;time.sleep(.05)
            q=socket.create_connection(('127.0.0.1',qp),timeout=5);f=q.makefile('rwb',buffering=0);f.readline()
            def call(command,args=None):
                f.write((json.dumps({'execute':command,'arguments':args or {}})+'\n').encode())
                while True:
                    response=json.loads(f.readline());assert 'error' not in response,response
                    if 'return' in response:return response['return']
            call('qmp_capabilities');c=Console(stream);call('cont')
            c.wait_for('[AUTH64] First run: create a local account.',120)
            for text,prompt in [('devuser','Create password (6-63 characters):'),('test123','Confirm password:'),('test123','Account created.')]:c.send(text);c.wait_for(prompt,120)
            c.wait_for('[terminal] output active',120);c.wait_for_prompt(120)
            start=len(c.transcript);c.run(f'/bin/device_test.pol http://10.0.2.2:{server.server_port}/proof',expected='PASS native device ABI',timeout=120)
            evidence=c.text()[start:];assert 'FAIL' not in evidence,evidence
            assert f'output_mask={3 if audio else 1}' in evidence,evidence
            print(evidence.strip(),flush=True)
            c.run('echo $?',expected='\n0\n')
            # UART is the independent text console. Close the graphical
            # terminal through its actual PS/2 shortcut before clicking Desktop.
            call('human-monitor-command',{'command-line':'sendkey ctrl-shift-q 1'})
            c.wait_for('[terminal] closing',30)
            time.sleep(.4)
            # The current native desktop falls back to a 720x500 client.
            shot=B/(label+'.ppm');call('screendump',{'filename':str(shot)});im=Image.open(shot)
            sw,sh=im.size;cx=(sw-724)//2+2;cy=(sh-534)//2+32
            def wait_pixel(x,y,color):
                end=time.monotonic()+30;attempts=0
                while True:
                    call('screendump',{'filename':str(shot)});img=Image.open(shot);attempts+=1
                    if img.getpixel((x,y))==color:return img
                    assert time.monotonic()<end,('framebuffer completion',x,y,color,img.getpixel((x,y)),attempts)
                    time.sleep(.12)
            wait_pixel(cx+8,cy+15,(22,28,42))
            xy=[sw//2,sh//2]
            def move(x,y):
                while xy!=[x,y]:
                    dx=max(-90,min(90,x-xy[0]));dy=max(-90,min(90,y-xy[1]))
                    call('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':dx}},{'type':'rel','data':{'axis':'y','value':dy}}]})
                    time.sleep(.06);xy[0]+=dx;xy[1]+=dy
            def click(x,y):
                move(x,y)
                for down in (True,False):call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]});time.sleep(.12)
            click(cx+360,cy+15);time.sleep(.5)
            px,py=cx+170,cy+40
            im=wait_pixel(px+8,py+200,(37,42,59))
            assert im.getpixel((px+8,py+200))==(37,42,59),('native panel',im.getpixel((px+8,py+200)))
            mark=len(c.transcript);click(px+40,py+135)
            c.wait_for('NetworkManager: all traffic administratively blocked',15,mark)
            blocked=wait_pixel(px+20,py+132,(104,84,191))
            assert blocked.getpixel((px+20,py+132))==(104,84,191),blocked.getpixel((px+20,py+132))
            print(f'PIXEL airplane enabled ({px+20},{py+132})={blocked.getpixel((px+20,py+132))}',flush=True)
            mark=len(c.transcript);click(px+40,py+135);c.wait_for('NetworkManager: networking enabled',15,mark)
            click(px+165,py+62);time.sleep(.2);call('screendump',{'filename':str(shot)})
            radio=Image.open(shot);assert radio.crop((px+20,py+165,px+340,py+205)).tobytes()!=im.crop((px+20,py+165,px+340,py+205)).tobytes()
            radio.save(shot.with_name(shot.stem+'-wifi.png'))
            # Real held pointer events, including clamping beyond the track.
            move(px+24,py+264);call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':True}}]});time.sleep(.15)
            move(px+324,py+264);time.sleep(.25)
            call('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':False}}]});time.sleep(.2)
            dragged=wait_pixel(px+300,py+265,(167,151,240))
            assert dragged.getpixel((px+300,py+265))==(167,151,240),dragged.getpixel((px+300,py+265))
            print(f'PIXEL volume dragged to 100 ({px+300},{py+265})={dragged.getpixel((px+300,py+265))}',flush=True)
            click(px+340,py+235);time.sleep(.3);call('screendump',{'filename':str(shot)});im=Image.open(shot)
            assert im.getpixel((px+20,py+300)) in ((104,84,191),(52,57,77)),im.getpixel((px+20,py+300))
            im.save(shot.with_suffix('.png'))
            print(f'PIXEL native panel ({px+8},{py+200})={im.getpixel((px+8,py+200))}; output row ({px+20},{py+300})={im.getpixel((px+20,py+300))}; screenshot={shot.with_suffix(".png")}',flush=True)
            call('human-monitor-command',{'command-line':'sendkey esc 1'});time.sleep(.4)
            call('screendump',{'filename':str(shot)})
            closed=Image.open(shot);assert closed.getpixel((px+8,py+200))!=(37,42,59)
            assert 'PANIC' not in c.text() and '[X64] FAIL' not in c.text()
        finally:
            if c:(B/(label+'.log')).write_text(c.text(),encoding='utf-8')
            if c and proc.poll() is None:
                # QMP quit flushes/finalises the WAV header. TerminateProcess
                # on Windows bypasses QEMU's audio close routine.
                try:call('quit')
                except (OSError,ValueError):pass
                # QMP can close its socket before process teardown finishes.
                try:proc.wait(timeout=5)
                except subprocess.TimeoutExpired:pass
            if stream:stream.close()
            if q:q.close()
            if proc.poll() is None:proc.terminate()
            proc.wait(timeout=5);server.shutdown();server.server_close()
            print(f'QEMU exit={proc.returncode}',flush=True)
        if audio:
            raw=wav.read_bytes()
            assert raw[:4]==b'RIFF' and raw[8:16]==b'WAVEfmt ' and raw[36:40]==b'data',raw[:44]
            fmt,channels,rate,byte_rate,align,bits=struct.unpack_from('<HHIIHH',raw,20)
            assert fmt==1 and channels==2 and bits==16 and align==4 and byte_rate==rate*align
            pcm=raw[44:];assert len(pcm)%align==0
            frames=len(pcm)//align
            riff_len=struct.unpack_from('<I',raw,4)[0];data_len=struct.unpack_from('<I',raw,40)[0]
            # This Windows QEMU build leaves its initial zero size fields even
            # after QMP quit/exit 0. Preserve that raw artifact and report it.
            assert (riff_len,data_len) in ((0,0),(len(raw)-8,len(pcm))), (riff_len,data_len,len(raw))
            print(f'CAPTURE original RIFF/data sizes={riff_len}/{data_len}; PCM payload_bytes={len(pcm)}',flush=True)
            assert any(pcm), 'AC97 DMA produced silent WAV'
            samples=struct.unpack('<'+'h'*(len(pcm)//2),pcm)
            assert min(samples)<-100 and max(samples)>100,(min(samples),max(samples))
            # A separately named playback copy; the original capture is intact.
            with wave.open(str(wav.with_name(wav.stem+'-playback.wav')),'wb') as w:
                w.setnchannels(channels);w.setsampwidth(bits//8);w.setframerate(rate);w.writeframes(pcm)
            print(f'WAV {wav.name}: channels={channels} frames={frames} nonzero_bytes={sum(b!=0 for b in pcm)}',flush=True)
        print(f'PASS devices guest RAM={ram} MiB audio={audio}: real HTTP, airplane packet balance, native panel pixels',flush=True)
def main():
    p=argparse.ArgumentParser();p.add_argument('--ram',type=int,nargs='+',default=[64,5120]);p.add_argument('--no-audio',action='store_true');a=p.parse_args()
    for ram in a.ram:run(ram,not a.no_audio)
if __name__=='__main__':main()
