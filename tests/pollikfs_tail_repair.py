"""Safe tail repair preserves files and refuses missing data/corruption."""
import sys
from pathlib import Path
import struct
import tempfile
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from repair_pollikfs_tail import repair
from sync_system_files import validate, manifest, SPAN
from pollikfs_install import PollikFsImage, PollikFsError, START, BLOCK_SIZE
from format_pollikfs2 import format_disk

with tempfile.TemporaryDirectory(prefix='pollikos-tail-test-') as folder:
    folder = Path(folder)
    source = folder / 'source.img'
    format_disk(source, total_size_mb=40)
    fs = PollikFsImage.load(source)
    fs.install_file('/home/keep.txt', b'user contents must survive' * 150)
    fs.save(source)
    full = source.read_bytes()[:SPAN]
    before = manifest(validate(full))
    truncated = bytearray(full[:SPAN // 2])
    struct.pack_into('<2I', truncated, START + 16, 32700, 498)
    source.write_bytes(truncated)
    backup = folder / 'original.img'
    repair(source, backup)
    result = source.read_bytes()
    assert backup.read_bytes() == truncated
    assert result[:START + 16] == truncated[:START + 16]
    assert result[START + 24:len(truncated)] == truncated[START + 24:]
    assert result[len(truncated):] == bytes(SPAN - len(truncated))
    assert manifest(validate(result)) == before
    print('PASS tail repair: original backup exact; all file hashes and live metadata preserved; zero tail; exact counters')

    def refuse(label, data):
        target = folder / (label + '.img')
        target.write_bytes(data)
        try: repair(target, folder / (label + '.original'))
        except PollikFsError as error: print(f'REFUSED {label}: {error}')
        else: raise AssertionError('unsafe repair accepted: ' + label)
        assert target.read_bytes() == data
        assert not (folder / (label + '.original')).exists()
        print(f'PASS {label}: original byte-identical')

    # Root block is allocated: truncating through it must never invent data.
    refuse('missing-allocated', full[:START + 36 * BLOCK_SIZE])
    broken = bytearray(truncated)
    # Mark an unowned, present block allocated: counters alone cannot fix it.
    block = 200
    broken[START + BLOCK_SIZE + block // 8] |= 1 << (block % 8)
    refuse('orphan-bitmap', broken)
    broken = bytearray(truncated)
    struct.pack_into('<I', broken, START + 40, 30)
    refuse('wrong-geometry', broken)
    source.write_bytes(truncated)
    try: repair(source, backup)
    except FileExistsError: pass
    else: raise AssertionError('backup overwritten')
    assert source.read_bytes() == truncated and backup.read_bytes() == truncated
    print('PASS existing backup: not overwritten; source unchanged')
