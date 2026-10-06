"""Test-only Surface image identity and explicitly validated 32-bit GUI ABI."""
import hashlib
from pathlib import Path
import subprocess
import tempfile

BUILD = Path(__file__).resolve().parents[1] / 'build'


def checked_surface_symbols():
    image, elf = BUILD / 'PollikOS-Surface.img', BUILD / 'kernel.elf'
    with tempfile.TemporaryDirectory(prefix='pollikos-identity-') as directory:
        binary = Path(directory) / 'kernel.bin'
        subprocess.run(['llvm-objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
        blob = binary.read_bytes()
    with image.open('rb') as disk:
        disk.seek(4608)
        assert disk.read(len(blob)) == blob, 'Surface image does not match kernel.elf'
    symbols, duplicates = {}, set()
    for line in subprocess.check_output(['llvm-nm', '-S', str(elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 4:
            name = fields[3]
            if name in symbols:
                duplicates.add(name)
            symbols[name] = (int(fields[0], 16), int(fields[1], 16))
    required = ('g_windows', 'g_surfaces', 'g_animations', 'address', 'stride', 'bytes',
                'pixels', 'sizes', 'g_surface_phys', 'g_surface_capacity', 'g_surface_pages')
    assert not duplicates.intersection(required), ('ambiguous symbols', duplicates.intersection(required))
    apps, remainder = divmod(symbols['g_windows'][1], 84)
    assert not remainder and apps == 8, 'Expected eight 84-byte Windows (Calculator appended)'
    assert symbols['g_surfaces'][1] == apps * 36, 'WindowSurface ABI36 changed'
    assert symbols['g_animations'][1] == apps * 17 * 4, 'Animation ABI17 words changed'
    for name in ('g_surface_phys', 'g_surface_capacity'):
        assert symbols[name][1] == apps * 4, name
    if 'g_surface_pages' in symbols:
        assert symbols['g_surface_pages'][1] == apps * 4
    assert symbols['sizes'][1] == apps * 8, 'GuiAppSize ABI changed'
    print(f'IDENTITY: kernel_bytes={len(blob)} sha256={hashlib.sha256(blob).hexdigest()} '
          f'ELF matches Surface offset4608; apps={apps}; ABI Window84 Surface36 Animation68', flush=True)
    return symbols, apps


def check_guards(read_words, symbols, apps, width, height):
    """read_words(address, byte_count); drawable corners are NOT canaries."""
    for app in range(apps):
        base = read_words(symbols['g_surface_phys'][0] + app * 4, 4)[0]
        capacity = read_words(symbols['g_surface_capacity'][0] + app * 4, 4)[0]
        s = read_words(symbols['g_surfaces'][0] + app * 36, 36)
        assert base and base % 4096 == 0, (app, base)
        assert capacity == width * height, (app, capacity)
        if 'g_surface_pages' in symbols:
            pages = read_words(symbols['g_surface_pages'][0] + app * 4, 4)[0]
            assert (capacity + 2) * 4 <= pages * 4096
        assert s[0] == base + 4 and s[1] * s[2] <= capacity, (app, s)
        assert read_words(base, 4) == (0xDEADBEEF,), ('prefix guard', app)
        assert read_words(base + (capacity + 1) * 4, 4) == (0xDEADBEEF,), ('suffix guard', app)
