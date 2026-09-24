#!/usr/bin/env python3
"""Export Supaplex (Amiga) graphics and levels from the disk image.

The port reads everything directly from the ADF; this tool is only for looking
at the data. Needs Pillow (pip install pillow).

usage: export_assets.py <image.adf> <out-dir>

Writes:
  tiles_1/2.png        tile sheets (PHIL_02, 4 bitplanes, game palette)
  intro_1..3.png       intro pictures (PHIL_00)
  levels.txt           all 111 levels as text (one character per cell)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from adflib import Adf  # noqa: E402

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None

PHIL02_BASE = 0x1EE00


def palette(data, off, n=16):
    cols = []
    for i in range(n):
        v = struct.unpack('>H', data[off + 2 * i:off + 2 * i + 2])[0]
        cols.append((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17))
    return cols


def planar_to_image(data, off, width, height, planes, pal, interleaved, plane_stride=0):
    bpl = width // 8
    img = Image.new('RGB', (width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            c = 0
            for p in range(planes):
                if interleaved:
                    a = off + (y * planes + p) * bpl + x // 8
                else:
                    a = off + p * plane_stride + y * bpl + x // 8
                if data[a] & (0x80 >> (x & 7)):
                    c |= 1 << p
            px[x, y] = pal[c]
    return img


TILE_CHARS = {0: ' ', 1: 'O', 2: '.', 3: 'M', 4: 'i', 5: 'c', 6: '#', 7: 'E', 8: 'o', 9: '>', 10: 'v', 11: '<',
              12: '^', 13: '>', 14: 'v', 15: '<', 16: '^', 17: 'S', 18: 'y', 19: 'T', 20: 'r', 21: '|', 22: '-',
              23: '+', 24: 'e', 25: 'b', 26: 'c', 27: 'c', 38: 'c', 39: 'c',
              **{t: '=' for t in range(28, 38)}}


def main():
    adf = Adf(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    p00, p01, p02 = adf.read('PHIL_00'), adf.read('PHIL_01'), adf.read('PHIL_02')

    # resource pointers ($1B5A0 in PHIL_01, offsets accumulate)
    base = 0x7E00
    ptr = []
    a = 0x1B5A0 - base
    ptr.append(struct.unpack('>I', p01[a:a + 4])[0])
    while True:
        a += 4
        v = struct.unpack('>I', p01[a:a + 4])[0]
        if v == 0xFFFFFFFF:
            break
        ptr.append(ptr[-1] + v)
    tiles = ptr[1] - PHIL02_BASE
    game_pal = palette(p02, ptr[2] - PHIL02_BASE)

    if Image:
        # two sheets of 320x244, stored plane after plane ($2620 bytes per plane);
        # the game converts them to interleaved planes when a level starts ($8B02)
        for k in range(2):
            sheet = planar_to_image(p02, tiles + k * 0x9880, 320, 244, 4, game_pal, False, 0x2620)
            sheet.resize((640, 488), Image.NEAREST).save(os.path.join(out, 'tiles_%d.png' % (k + 1)))
        pics = [(0x548A6 - 0x54000, 256, 0x2800), (0x5E8C6 - 0x54000, 200, 0x1F40), (0x665E6 - 0x54000, 200, 0x1F40)]
        pals = [0x5E8A6 - 0x54000, 0x665C6 - 0x54000, 0x6E2E6 - 0x54000]
        for i, ((off, h, stride), po) in enumerate(zip(pics, pals)):
            img = planar_to_image(p00, off, 320, h, 4, palette(p00, po), False, stride)
            img.save(os.path.join(out, 'intro_%d.png' % (i + 1)))
    else:
        print('Pillow not installed: skipping PNG export')

    with open(os.path.join(out, 'levels.txt'), 'w') as f:
        f.write('legend: ' + ' '.join('%s=%02X' % (c, t) for t, c in TILE_CHARS.items() if c != ' ') + '\n\n')
        for n in range(1, 112):
            lv = adf.read('PHIL_%02X' % (n + 0x0F))
            title = lv[1446:1469].decode('latin1')
            f.write('level %d: %s  gravity=%d freeze_zonks=%d infotrons_needed=%d special_ports=%d\n' % (
                n, title.strip(' -'), lv[1444], lv[1469], lv[1470], lv[1471]))
            for y in range(24):
                f.write(''.join(TILE_CHARS.get(b, '?') for b in lv[y * 60:(y + 1) * 60]) + '\n')
            f.write('\n')
    print('written to', out)


if __name__ == '__main__':
    main()
