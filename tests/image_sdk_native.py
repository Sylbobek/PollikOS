import pathlib,subprocess
from PIL import Image
R=pathlib.Path(__file__).resolve().parents[1];B=R/'build/image-sdk';B.mkdir(exist_ok=True)
cmd=['clang','-D_CRT_SECURE_NO_WARNINGS','-O2','-Wall','-Wextra','-Werror','-idirafter','sdk/include','-idirafter','build/x86_64/system/sdk/include','tests/image_sdk_native.c','sdk/lib/image.c','-o',str(B/'decode.exe')]
print('COMMAND '+subprocess.list2cmdline(cmd),flush=True);subprocess.run(cmd,cwd=R,check=True)
for ext in ('png','jpg','bmp','gif'):
 path=B/('picture.'+ext);im=Image.new('RGB',(64,48),(18,171,52))
 im.save(path,quality=100);raw=path.with_suffix('.raw')
 p=subprocess.run([str(B/'decode.exe'),str(path),str(raw)],capture_output=True,text=True,check=True);print(p.stdout.strip(),flush=True)
 expected=Image.open(path).convert('RGBA').tobytes();got=raw.read_bytes()
 assert len(got)==len(expected)
 error=max(abs(a-b) for a,b in zip(got,expected));assert error<=(1 if ext=='jpg' else 0),(ext,error)
 print(f'PIXELS {ext} count={64*48} max_channel_error={error}',flush=True)
for name,payload in [('bad',b'not an image'),('truncated',(B/'picture.png').read_bytes()[:24])]:
 path=B/(name+'.png');path.write_bytes(payload)
 p=subprocess.run([str(B/'decode.exe'),str(path),str(path.with_suffix('.raw'))],capture_output=True,text=True);assert p.returncode==5,p
print('PASS SDK images: PNG/BMP/GIF exact RGBA, JPEG max error 1, malformed rejection',flush=True)
