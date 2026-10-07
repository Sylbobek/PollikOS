from pathlib import Path
import subprocess,struct,math
R=Path(__file__).resolve().parents[1];B=R/'build/mp4-native';B.mkdir(exist_ok=True)
cmd=['python','tools/build_mp4_codecs.py','--output',str(B/'codecs')];print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);subprocess.run(cmd,cwd=R,check=True)
cmd=['clang','-D_CRT_SECURE_NO_WARNINGS','-O2','-Wall','-Wextra','-Werror','-idirafter','sdk/include','-Ithird_party/h264bsd/src','-Ithird_party/faad2/include','tests/movie_native.c','sdk/media/movie.c',str(B/'codecs/libpollikvideo.a'),'-o',str(B/'movie.exe')]
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);subprocess.run(cmd,cwd=R,check=True)
path=B/'clip.mp4'
cmd=['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x96:rate=12:duration=2','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=2','-c:v','libx264','-profile:v','baseline','-pix_fmt','yuv420p','-bf','0','-x264-params','ref=1:scenecut=0','-c:a','aac','-ac','2','-shortest','-movflags','+faststart',str(path)]
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);subprocess.run(cmd,check=True)
ours_yuv=B/'ours.yuv';ours_pcm=B/'ours.pcm'
subprocess.run([str(B/'movie.exe'),str(path),str(ours_yuv),str(ours_pcm)],check=True)
ref_yuv=B/'reference.yuv';ref_pcm=B/'reference.pcm'
subprocess.run(['ffmpeg','-v','error','-y','-i',str(path),'-an','-pix_fmt','yuv420p','-f','rawvideo',str(ref_yuv)],check=True)
subprocess.run(['ffmpeg','-v','error','-y','-i',str(path),'-vn','-ar','48000','-ac','2','-f','s16le',str(ref_pcm)],check=True)
a,b=ours_yuv.read_bytes(),ref_yuv.read_bytes();assert a==b,('YUV mismatch',len(a),len(b),sum(x!=y for x,y in zip(a,b)))
print(f'GOLDEN YUV bytes={len(a)} frames={len(a)//(160*96*3//2)} pixel-identical to FFmpeg',flush=True)
a=ours_pcm.read_bytes();b=ref_pcm.read_bytes();print('AUDIO_BYTES ours',len(a),'reference',len(b),flush=True)
assert len(a)==len(b),('AAC frame count',len(a),len(b))
aa=struct.unpack('<'+'h'*(len(a)//2),a);bb=struct.unpack('<'+'h'*(len(b)//2),b)
errors=[x-y for x,y in zip(aa,bb)];rms=math.sqrt(sum(x*x for x in errors)/len(errors));maximum=max(map(abs,errors))
assert rms<=1 and maximum<=8,(rms,maximum)
print(f'GOLDEN AAC frames={len(aa)//2} max_error={maximum} rms_lsb={rms:.6f}',flush=True)
# Cropped (non-macroblock-aligned) output and silent tracks.
cropped=B/'crop.mp4'
subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=10:duration=0.5','-c:v','libx264','-profile:v','baseline','-pix_fmt','yuv420p','-bf','0',str(cropped)],check=True)
subprocess.run([str(B/'movie.exe'),str(cropped),str(B/'crop.yuv'),str(B/'crop.pcm')],check=True)
subprocess.run(['ffmpeg','-v','error','-y','-i',str(cropped),'-pix_fmt','yuv420p','-f','rawvideo',str(B/'crop-reference.yuv')],check=True)
assert (B/'crop.yuv').read_bytes()==(B/'crop-reference.yuv').read_bytes()
assert not (B/'crop.pcm').read_bytes()
print('GOLDEN cropped 160x90: 5 frames exact; silent video supported',flush=True)
# A larger profile is rejected, not labelled supported because of .mp4.
high=B/'high.mp4'
subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x96:rate=10:duration=0.2','-c:v','libx264','-profile:v','high','-pix_fmt','yuv420p',str(high)],check=True)
r=subprocess.run([str(B/'movie.exe'),str(high),str(ours_yuv),str(ours_pcm)],capture_output=True,text=True);assert r.returncode==3,r.stdout
print('REJECT H264 High profile (explicit unsupported boundary)',flush=True)
for name,data in [('truncated',path.read_bytes()[:-17]),('bad-size',b'\xff'*16),('overflow',b'\0\0\0\1ftyp'+b'\xff'*8)]:
 p=B/(name+'.mp4');p.write_bytes(data);r=subprocess.run([str(B/'movie.exe'),str(p),str(ours_yuv),str(ours_pcm)],capture_output=True,text=True)
 assert r.returncode==3,(name,r.stdout,r.returncode)
print('PASS MP4 host: native C demux/H264 Baseline/AAC LC, 24 golden frames, malformed atom bounds',flush=True)
