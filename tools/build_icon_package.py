from pathlib import Path
import struct,sys
from sync_system_files import FILES
target=Path(sys.argv[1]);files=[(p,f.read_bytes()) for p,f in sorted(FILES.items()) if p.startswith('/usr/share/icons/')]
blob=bytearray(b'ICPK'+struct.pack('<I',len(files)))
for path,data in files:
    name=path.encode('ascii');assert len(name)<64
    blob+=name.ljust(64,b'\0')+struct.pack('<I',len(data))+data
target.write_bytes(blob);print(f'Packed installer icons: files={len(files)} bytes={len(blob)}')
