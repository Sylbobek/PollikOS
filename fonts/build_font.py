"""Pollik Sans: original geometric stroke outlines, no external font input.

Coordinates use a 1000-unit em, cap line 120, baseline 800. Quadratic/cubic
curves are rasterized at 8x, then filtered to native screen sizes. The kernel
uses 4-bit coverage, never enlarges a low-resolution bitmap.
Run: python fonts/build_font.py (Pillow needed only to regenerate assets).
"""
from pathlib import Path
import math
import re
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
GLYPHS = {}


def glyph(c, width, *paths):
    GLYPHS[c] = (width, paths)


glyph(' ', 300)
glyph('A', 640, 'M 70 800 L 320 120 L 570 800', 'M 160 565 L 480 565')
glyph('B', 610, 'M 100 800 L 100 120 L 320 120 C 580 120 580 440 320 440 L 100 440', 'M 320 440 C 620 440 620 800 320 800 L 100 800')
glyph('C', 650, 'M 565 225 C 475 70 90 55 90 460 C 90 865 475 850 565 695')
glyph('D', 650, 'M 100 800 L 100 120 L 280 120 C 655 120 655 800 280 800 L 100 800')
glyph('E', 550, 'M 470 120 L 100 120 L 100 800 L 470 800', 'M 100 450 L 425 450')
glyph('F', 540, 'M 470 120 L 100 120 L 100 800', 'M 100 450 L 425 450')
glyph('G', 690, 'M 590 230 C 490 60 90 55 90 460 C 90 865 580 875 580 655 L 580 485 L 365 485')
glyph('H', 660, 'M 100 120 L 100 800', 'M 560 120 L 560 800', 'M 100 455 L 560 455')
glyph('I', 260, 'M 130 120 L 130 800')
glyph('J', 480, 'M 380 120 L 380 610 C 380 870 65 850 65 650')
glyph('K', 600, 'M 100 120 L 100 800', 'M 520 120 L 100 545', 'M 260 385 L 550 800')
glyph('L', 530, 'M 100 120 L 100 800 L 455 800')
glyph('M', 800, 'M 100 800 L 100 120 L 400 610 L 700 120 L 700 800')
glyph('N', 680, 'M 100 800 L 100 120 L 580 800 L 580 120')
glyph('O', 690, 'M 345 110 C 5 110 5 810 345 810 C 685 810 685 110 345 110')
glyph('P', 590, 'M 100 800 L 100 120 L 305 120 C 605 120 605 475 305 475 L 100 475')
glyph('Q', 690, GLYPHS['O'][1][0], 'M 395 635 L 625 865')
glyph('R', 610, GLYPHS['P'][1][0], 'M 310 475 L 555 800')
glyph('S', 600, 'M 505 215 C 390 30 75 95 95 290 C 110 455 490 410 505 625 C 520 850 180 880 80 710')
glyph('T', 600, 'M 55 120 L 545 120', 'M 300 120 L 300 800')
glyph('U', 660, 'M 100 120 L 100 555 C 100 885 560 885 560 555 L 560 120')
glyph('V', 630, 'M 70 120 L 315 800 L 560 120')
glyph('W', 900, 'M 65 120 L 245 800 L 450 275 L 655 800 L 835 120')
glyph('X', 610, 'M 80 120 L 530 800', 'M 530 120 L 80 800')
glyph('Y', 610, 'M 65 120 L 305 465 L 545 120', 'M 305 465 L 305 800')
glyph('Z', 580, 'M 85 120 L 505 120 L 75 800 L 505 800')
oval = 'M 285 320 C 5 320 5 805 285 805 C 565 805 565 320 285 320'
glyph('a', 590, oval, 'M 495 335 L 495 800')
glyph('b', 590, oval, 'M 75 120 L 75 800')
glyph('c', 540, 'M 465 400 C 350 235 75 320 75 565 C 75 810 350 895 465 730')
glyph('d', 590, oval, 'M 495 120 L 495 800')
glyph('e', 565, 'M 80 560 L 480 560 C 500 250 75 230 75 565 C 75 840 350 855 480 725')
glyph('f', 380, 'M 315 135 C 205 85 135 145 135 270 L 135 800', 'M 50 335 L 325 335')
glyph('g', 590, oval, 'M 495 335 L 495 815 C 495 1020 230 1040 105 925')
glyph('h', 585, 'M 95 120 L 95 800', 'M 95 500 C 100 260 485 260 485 515 L 485 800')
glyph('i', 245, 'M 122 335 L 122 800', 'M 122 150 L 122 160')
glyph('j', 280, 'M 170 335 L 170 850 C 170 960 110 995 30 960', 'M 170 150 L 170 160')
glyph('k', 535, 'M 95 120 L 95 800', 'M 455 335 L 95 660', 'M 260 510 L 480 800')
glyph('l', 265, 'M 120 120 L 120 725 Q 120 800 195 800')
glyph('m', 885, 'M 95 335 L 95 800', 'M 95 490 C 95 260 440 260 440 500 L 440 800', 'M 440 500 C 440 260 785 260 785 500 L 785 800')
glyph('n', 585, GLYPHS['h'][1][1], 'M 95 335 L 95 800')
glyph('o', 570, oval)
glyph('p', 590, oval, 'M 75 335 L 75 980')
glyph('q', 590, oval, 'M 495 335 L 495 980')
glyph('r', 400, 'M 95 335 L 95 800', 'M 95 500 Q 95 300 335 335')
glyph('s', 500, 'M 420 410 C 300 245 80 315 85 435 C 90 565 415 520 420 675 C 425 825 185 860 75 740')
glyph('t', 365, 'M 135 180 L 135 665 Q 135 835 305 780', 'M 45 335 L 305 335')
glyph('u', 585, 'M 95 335 L 95 630 C 95 875 485 875 485 610 L 485 335', 'M 485 610 L 485 800')
glyph('v', 530, 'M 60 335 L 265 800 L 470 335')
glyph('w', 790, 'M 65 335 L 215 800 L 395 390 L 575 800 L 725 335')
glyph('x', 510, 'M 65 335 L 445 800', 'M 445 335 L 65 800')
glyph('y', 540, 'M 65 335 L 290 800', 'M 485 335 L 230 940 Q 205 995 125 980')
glyph('z', 495, 'M 75 335 L 425 335 L 65 800 L 435 800')
glyph('0', 610, 'M 305 115 C -5 115 -5 805 305 805 C 615 805 615 115 305 115')
glyph('1', 400, 'M 80 270 L 225 120 L 225 800', 'M 80 800 L 360 800')
glyph('2', 590, 'M 85 265 C 85 45 510 55 500 280 C 490 440 155 560 85 800 L 515 800')
glyph('3', 570, 'M 80 200 C 250 25 555 130 450 330 Q 395 425 255 430', 'M 255 430 C 610 375 600 925 80 745')
glyph('4', 610, 'M 430 800 L 430 120 L 65 580 L 550 580')
glyph('5', 575, 'M 485 120 L 125 120 L 100 440 C 525 240 670 885 80 760')
glyph('6', 590, 'M 470 140 C 170 20 30 450 100 660 C 210 990 650 680 435 450 C 320 330 155 405 90 540')
glyph('7', 550, 'M 60 120 L 485 120 L 180 800')
glyph('8', 600, 'M 300 115 C 10 115 40 445 300 445 C 560 445 590 115 300 115', 'M 300 445 C -10 445 -10 805 300 805 C 610 805 610 445 300 445')
glyph('9', 590, 'M 120 780 C 420 900 560 470 490 260 C 380 -70 -60 240 155 470 C 270 590 435 515 500 380')
glyph('.', 240, 'M 120 775 L 120 785')
glyph(',', 240, 'M 135 765 L 75 900')
glyph(':', 240, 'M 120 380 L 120 390', 'M 120 765 L 120 775')
glyph(';', 240, 'M 120 380 L 120 390', 'M 135 765 L 75 900')
glyph('!', 260, 'M 130 120 L 130 585', 'M 130 775 L 130 785')
glyph('?', 540, 'M 75 250 C 75 55 455 45 455 255 C 455 405 265 385 265 575', 'M 265 775 L 265 785')
glyph('-', 460, 'M 85 520 L 375 520')
glyph('_', 560, 'M 40 900 L 520 900')
glyph('+', 600, 'M 90 470 L 510 470', 'M 300 255 L 300 685')
glyph('=', 600, 'M 90 375 L 510 375', 'M 90 595 L 510 595')
glyph('/', 430, 'M 65 890 L 365 65')
glyph('\\', 430, 'M 65 65 L 365 890')
glyph('|', 245, 'M 122 65 L 122 935')
glyph('(', 350, 'M 275 65 C 35 255 35 745 275 935')
glyph(')', 350, 'M 75 65 C 315 255 315 745 75 935')
glyph('[', 350, 'M 275 65 L 100 65 L 100 935 L 275 935')
glyph(']', 350, 'M 75 65 L 250 65 L 250 935 L 75 935')
glyph('{', 380, 'M 310 65 Q 145 65 145 265 L 145 350 Q 145 480 65 500 Q 145 520 145 650 L 145 735 Q 145 935 310 935')
glyph('}', 380, 'M 70 65 Q 235 65 235 265 L 235 350 Q 235 480 315 500 Q 235 520 235 650 L 235 735 Q 235 935 70 935')
glyph('<', 560, 'M 460 265 L 90 500 L 460 735')
glyph('>', 560, 'M 100 265 L 470 500 L 100 735')
glyph('"', 380, 'M 115 120 L 115 300', 'M 265 120 L 265 300')
glyph("'", 220, 'M 110 120 L 110 300')
glyph('`', 280, 'M 80 100 L 200 230')
glyph('~', 610, 'M 70 550 C 205 325 405 675 540 450')
glyph('^', 580, 'M 80 370 L 290 120 L 500 370')
glyph('*', 520, 'M 260 150 L 260 580', 'M 65 260 L 455 480', 'M 455 260 L 65 480')
glyph('#', 660, 'M 260 130 L 150 800', 'M 510 130 L 400 800', 'M 100 360 L 585 360', 'M 65 600 L 550 600')
glyph('$', 600, GLYPHS['S'][1][0], 'M 300 40 L 300 900')
glyph('%', 810, 'M 125 800 L 685 120', 'M 195 120 C 20 120 20 415 195 415 C 370 415 370 120 195 120', 'M 615 505 C 440 505 440 800 615 800 C 790 800 790 505 615 505')
glyph('&', 710, 'M 610 800 L 220 345 C 10 75 440 0 420 235 C 410 365 80 425 85 635 C 90 955 565 825 605 480')
glyph('@', 880, 'M 590 410 C 520 255 300 350 300 565 C 300 750 575 715 585 540 L 610 365', 'M 585 540 C 470 825 870 745 785 375 C 685 -40 60 55 70 530 C 75 870 465 995 710 805')


# Extend original outlines; ASCII indices stay stable for the i386 UI.
POLISH = 'ĄąĆćĘęŁłŃńÓóŚśŹźŻż'
for char, base in zip(POLISH, 'AaCcEeLlNnOoSsZzZz'):
    advance, paths = GLYPHS[base]
    if char in 'ĄąĘę':
        accent = 'M 455 780 Q 335 960 475 970'
    elif char in 'Łł':
        accent = 'M 60 570 L 405 360'
    elif char in 'Żż':
        accent = 'M 290 40 L 290 50' if char.isupper() else 'M 260 205 L 260 215'
    else:
        accent = 'M 245 70 L 365 0' if char.isupper() else 'M 205 245 L 325 155'
    glyph(char, advance, *paths, accent)


def points(path):
    tokens = re.findall(r'[MLQC]|-?\d+(?:\.\d+)?', path)
    i, current, result = 0, (0, 0), []
    while i < len(tokens):
        cmd = tokens[i]
        i += 1
        n = {'M': 2, 'L': 2, 'Q': 4, 'C': 6}[cmd]
        v = list(map(float, tokens[i:i+n]))
        i += n
        end = (v[-2], v[-1])
        if cmd in ('M', 'L'):
            result.append(end)
        else:
            for step in range(1, 41):
                t = step / 40
                u = 1-t
                if cmd == 'Q':
                    p = tuple(u*u*current[k] + 2*u*t*v[k] + t*t*end[k] for k in range(2))
                else:
                    p = tuple(u**3*current[k] + 3*u*u*t*v[k] + 3*u*t*t*v[k+2] + t**3*end[k] for k in range(2))
                result.append(p)
        current = end
    return result


def raster(char, size):
    advance, paths = GLYPHS[char]
    w, h = math.ceil(advance * size / 1000) + 1, size + 2
    scale = size * 8 / 1000
    im = Image.new('L', (w * 8, h * 8))
    draw = ImageDraw.Draw(im)
    weight = max(1, round((65 if size < 22 else 57) * scale))
    for path in paths:
        pts = [(x*scale, y*scale) for x,y in points(path)]
        draw.line(pts, fill=255, width=weight, joint='curve')
        r = weight/2
        for x,y in [pts[0], pts[-1]]:
            draw.ellipse((x-r, y-r, x+r, y+r), fill=255)
    return im.resize((w, h), Image.Resampling.LANCZOS), max(3, round(advance*size/1000))


def build():
    assert all(chr(c) in GLYPHS for c in range(32, 127))
    characters = [chr(c) for c in range(32, 127)] + list(POLISH)
    data, metadata = [], []
    for size in (14, 18, 26, 40, 52):
        row = []
        for char in characters:
            im, advance = raster(char, size)
            values = [round(p * 15 / 255) for p in im.tobytes()]
            if len(values) % 2:
                values.append(0)
            row.append((len(data), im.width, im.height, advance))
            data.extend((values[i] << 4) | values[i+1] for i in range(0, len(values), 2))
        metadata.append(row)
    ascii_data, ascii_metadata = [], []
    for row in metadata:
        ascii_row = []
        for offset, width, height, advance in row[:95]:
            ascii_row.append((len(ascii_data),width,height,advance))
            ascii_data.extend(data[offset:offset+(width*height+1)//2])
        ascii_metadata.append(ascii_row)
    notice = '/* Generated by fonts/build_font.py: original Pollik Sans, 4-bit coverage. */'
    descriptor = 'typedef struct { u32 offset; u8 width, height, advance; } FontGlyph;'
    header = [notice, '#ifndef POLLIK_FONT_DATA_H', '#define POLLIK_FONT_DATA_H', descriptor,
              'typedef enum { FONT_FORMAT_1BPP=1, FONT_FORMAT_4BPP=4 } FontPixelFormat;',
              'typedef struct { const char *name; int first_char, char_count, cell_width, cell_height; FontPixelFormat format; const FontGlyph *glyphs; const u8 *coverage; } BitmapFont;',
              '#if defined(__i386__)', '#define FONT_GLYPH_COUNT 95',
              'extern const u8 font_coverage[%d];' % len(ascii_data),
              '#else', '#define FONT_GLYPH_COUNT %d' % len(characters),
              'extern const u8 font_coverage[%d];' % len(data), '#endif',
              'extern const u32 font_codepoints[FONT_GLYPH_COUNT];',
              'extern const FontGlyph font_glyphs[5][FONT_GLYPH_COUNT];',
              '#endif', '']
    def atlas(chars, rows, coverage):
        result = ['const u32 font_codepoints[] = {' + ','.join(str(ord(c)) for c in chars) + '};',
                  'const FontGlyph font_glyphs[5][%d] = {' % len(chars)]
        result += ['{' + ','.join('{%d,%d,%d,%d}' % entry for entry in row) + '},' for row in rows]
        result += ['};', 'const u8 font_coverage[] = {']
        result += [','.join(str(v) for v in coverage[i:i+32]) + ',' for i in range(0,len(coverage),32)]
        return result + ['};']
    out = [notice, '#include <stdint.h>', 'typedef uint32_t u32; typedef uint8_t u8;', descriptor,
           '#if defined(__i386__) /* Preserve the legacy installer memory budget. */']
    out += atlas(characters[:95],ascii_metadata,ascii_data) + ['#else'] + atlas(characters,metadata,data) + ['#endif','']
    (ROOT / 'kernel/font_data.h').write_text('\n'.join(header), encoding='ascii')
    (ROOT / 'sdk/lib/font_data.c').write_text('\n'.join(out), encoding='ascii')
    specimen = Image.new('RGB', (1100, 420), '#f8f8fc')
    for label, y, size in [('Pollik Sans', 28, 52), ('Made for PollikOS. Designed from scratch.', 112, 26),
                           ('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 177, 26),
                           ('abcdefghijklmnopqrstuvwxyz', 231, 26),
                           ('0123456789  @ & ! ? ( ) + =', 285, 26),
                           ('A softer desktop. Clear type. No shadows.', 354, 18)]:
        x = 40
        for c in label:
            im, advance = raster(c, size)
            specimen.paste('#272731', (x, y), im)
            x += advance
    specimen.save(ROOT / 'fonts/PollikSans.png')
    print(f'Pollik Sans: {len(characters)} glyphs (ASCII + Polish), 5 native sizes, {len(data)} bytes')


if __name__ == '__main__':
    build()
