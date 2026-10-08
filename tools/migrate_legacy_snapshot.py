"""Stage an explicit, lossless relocation of legacy desktop snapshots.

Only recognized snapshot records are moved out of PollikFS's journal prefix.
The existing filesystem blocks are byte-identical; never formats or commits
the original image. The caller can review/test the output before replacing it.
"""
import argparse
import hashlib
import os
import struct
from pathlib import Path
from sync_system_files import exclusive_image, validate, manifest, SPAN
from pollikfs_install import START, PollikFsError


def checksum(data):
    result = 2166136261
    for byte in data:
        result = ((result ^ byte) * 16777619) & 0xffffffff
    return result


def stage(source, output):
    with exclusive_image(source) as disk:
        original = disk.read()
        if len(original) < SPAN:
            raise PollikFsError('incomplete filesystem; repair its tail first')
        prefix = original[:START]
        if any(prefix[36 * 512:]):
            raise PollikFsError('unknown data beyond the two legacy snapshots')
        valid = 0
        for slot in range(2):
            at = slot * 18 * 512
            header = prefix[at:at + 512]
            if not any(header):
                if any(prefix[at + 512:at + 18 * 512]):
                    raise PollikFsError('unknown payload without a snapshot header')
                continue
            magic, generation, length, payload_sum, header_sum = struct.unpack_from('<5I', header)
            if magic != 0x32534650 or length != 8416:
                raise PollikFsError('unknown prefix header; relocation refused')
            payload = prefix[at + 512:at + 18 * 512]
            if checksum(header[:16]) != header_sum or checksum(payload) != payload_sum:
                raise PollikFsError('damaged legacy snapshot; relocation refused')
            for index in range(8):
                name, size, contents = struct.unpack_from('<24sI1024s', payload, index * 1052)
                if name[-1] or size > 1023 or contents[size] or any(
                        c not in b'abcdefghijklmnopqrstuvwxyz0123456789.-_' for c in name.split(b'\0')[0]):
                    raise PollikFsError('invalid legacy file record')
            valid += 1
        if not valid:
            raise PollikFsError('no valid legacy snapshot to relocate')
        if any(original[SPAN:SPAN + START]):
            raise PollikFsError('snapshot destination already contains data')
        relocated = bytearray(original)
        relocated[:START] = bytes(START)
        before = manifest(validate(relocated))
        if len(relocated) < SPAN + START:
            relocated.extend(bytes(SPAN + START - len(relocated)))
        relocated[SPAN:SPAN + START] = prefix
        after = manifest(validate(relocated))
        if before != after or relocated[START:SPAN] != original[START:SPAN]:
            raise PollikFsError('filesystem preservation check failed')
        with Path(output).open('xb') as staged:
            if staged.write(relocated) != len(relocated):
                raise OSError('short output write')
            staged.flush()
            os.fsync(staged.fileno())
        if Path(output).read_bytes() != relocated:
            raise OSError('staged image readback failed')
        print(f'STAGED {output} bytes={len(relocated)} legacy_snapshots={valid}')
        print(f'PRESERVED PollikFS_blocks=byte-identical filesystem_entries={len(before)} legacy_prefix=byte-identical_at_tail')
        print(f'ORIGINAL {source} bytes={len(original)} SHA256={hashlib.sha256(original).hexdigest()} unchanged')
        return before


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.image, args.output)
    except (PollikFsError, OSError, ValueError, struct.error) as error:
        raise SystemExit(f'Snapshot migration REFUSED: {error}')
