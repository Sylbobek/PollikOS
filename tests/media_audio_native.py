"""Real PCM/float WAVE and MP3 decoder tests; FFmpeg is a host oracle only."""
import math,pathlib,struct,subprocess,wave
R=pathlib.Path(__file__).resolve().parents[1];B=R/'build/media-native';B.mkdir(exist_ok=True)
cmd=['clang','-D_CRT_SECURE_NO_WARNINGS','-O2','-fno-vectorize','-Wall','-Wextra','-Werror','-idirafter','sdk/include','-idirafter','build/x86_64/system/sdk/include','tests/media_audio_native.c','sdk/lib/media_audio.c','-o',str(B/'decode.exe')]
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);subprocess.run(cmd,cwd=R,check=True)
def decode(path,ok=True):
 out=path.with_suffix('.raw');p=subprocess.run([str(B/'decode.exe'),str(path),str(out)],text=True,capture_output=True)
 print(p.stdout.strip(),flush=True);assert (p.returncode==0)==ok,(path,p.returncode,p.stderr)
 return out.read_bytes() if ok else b''
def wav_file(path,fmt,bits,channels,data,rate=48000):
 align=bits//8*channels;header=struct.pack('<HHIIHH',fmt,channels,rate,rate*align,align,bits)
 body=b'WAVEfmt '+struct.pack('<I',len(header))+header+b'data'+struct.pack('<I',len(data))+data
 if len(data)%2:body+=b'\0'
 path.write_bytes(b'RIFF'+struct.pack('<I',len(body))+body)
samples=[-32768,-16384,-1,0,1,16384,32767]*33
checks=0
for bits in (8,16,24,32):
 for channels in (1,2):
  data=bytearray();expected=[]
  for n,v in enumerate(samples):
   pair=[v,-v if v!=-32768 else 32767]
   for c in range(channels):
    x=pair[c]
    if bits==8:data.append((x>>8)+128)
    elif bits==16:data+=struct.pack('<h',x)
    elif bits==24:data+=(x<<8&0xffffff).to_bytes(3,'little')
    else:data+=struct.pack('<i',x<<16)
   a=(v>>8)*256 if bits==8 else v;b=a if channels==1 else ((pair[1]>>8)*256 if bits==8 else pair[1]);expected.extend((a,b))
  path=B/f'pcm-{bits}-{channels}.wav';wav_file(path,1,bits,channels,data)
  assert decode(path)==struct.pack('<'+'h'*len(expected),*expected),path;checks+=1
path=B/'float.wav';floats=(-1.,-.5,0.,.5,1.,float('nan'),float('inf'))
wav_file(path,3,32,1,b''.join(struct.pack('<f',x) for x in floats));expected=(-32768,-32768,-16384,-16384,0,0,16384,16384,32767,32767,0,0,0,0)
assert decode(path)==struct.pack('<14h',*expected);checks+=1
valid=(B/'pcm-16-2.wav').read_bytes()
for bits in (24,32):
 ext=B/f'extensible-{bits}.wav'
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(B/'pcm-16-2.wav'),'-c:a',f'pcm_s{bits}le',str(ext)],check=True)
 assert decode(ext)==decode(B/'pcm-16-2.wav');checks+=1
for name,data in [('truncated',valid[:-1]),('rate-zero',valid[:24]+bytes(4)+valid[28:]),('invalid-align',valid[:32]+bytes(2)+valid[34:]),('mp4',b'\0\0\0\x18ftypisom'+bytes(64)),('random',b'no audio'*20)]:
 p=B/(name+'.bin');p.write_bytes(data);decode(p,False);checks+=1
# Different source rates and channel formats, using a real independent encoder.
for rate in (22050,44100,48000):
 wav=B/f'sine-{rate}.wav';mp3=B/f'sine-{rate}.mp3'
 subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'sine=frequency=440:sample_rate={rate}:duration=0.6','-ac','2','-c:a','pcm_s16le',str(wav)],check=True)
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(wav),'-c:a','libmp3lame','-b:a','96k',str(mp3)],check=True)
 data=decode(mp3);s=struct.unpack('<'+'h'*(len(data)//2),data);left=s[::2]
 assert len(left)>=28800 and max(left)>1000 and min(left)<-1000
 crossings=[i for i in range(1,len(left)) if left[i-1]<=0<left[i] and left[i]>0]
 # Discard encoder padding and the first/last crossings around it.
 periods=[b-a for a,b in zip(crossings[2:-2],crossings[3:-1]) if 90<b-a<130]
 measured=48000/(sum(periods)/len(periods));assert abs(measured-440)<2,(rate,measured)
 print(f'ORACLE MP3 source_rate={rate} frames={len(left)} frequency_hz={measured:.4f}',flush=True);checks+=1
print(f'PASS media decoder: {checks} cases, PCM golden samples exact, float/clamps, malformed rejection, MP3 3 rates and independent tone-frequency oracle',flush=True)
