"""Exercise shared security/account/VFS code on a copy of a disposable disk."""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import tempfile
from format_pollikfs2 import format_disk
ROOT=Path(__file__).resolve().parents[1]
salt=bytes(range(16)); password=b'legacy123'
digest=hashlib.sha256(salt+password).digest()
for _ in range(8191): digest=hashlib.sha256(digest+salt+password).digest()
(ROOT/'build/security-legacy.bin').write_bytes(struct.pack('<III32s16s32s36s',0x31524341,1,8192,b'legacy',salt,digest,b''))
sources=['kernel/fs_journal.c','tests/security_account_native.c','third_party/monocypher/monocypher.c','third_party/bearssl/src/hash/sha2small.c',
         'third_party/bearssl/src/codec/dec32be.c','third_party/bearssl/src/codec/enc32be.c']
command=['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-fuse-ld=lld','-Ithird_party/bearssl/inc','-Ithird_party/bearssl/src',*sources,'-o','build/security_account_native.exe']
subprocess.run(command,cwd=ROOT,check=True)
with tempfile.TemporaryDirectory(prefix='pollikos-security-') as temporary:
    original=Path(temporary)/'original.img';copy=Path(temporary)/'copy.img'
    format_disk(original,total_size_mb=40);before=hashlib.sha256(original.read_bytes()).digest()
    shutil.copyfile(original,copy)
    subprocess.run([str(ROOT/'build/security_account_native.exe'),str(copy)],cwd=ROOT,check=True)
    assert hashlib.sha256(original.read_bytes()).digest()==before
