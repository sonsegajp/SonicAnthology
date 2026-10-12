"""Render a Sonic 1 level area straight from the ROM with S1's own palette.

Oracle for comparing the converted levels: S1 layout -> 256px chunks -> 16px
blocks -> tiles from the level PLCs' VRAM image, S1 palettes (Pal_Sonic +
zone palette, or the underwater palette with --water). Objects are not drawn.

  python tools/trilogy/s1_oracle.py ZONE ACT X Y [W H] [--water] [--out file.png]
  ZONE = GHZ LZ MZ SLZ SYZ SBZ; X/Y/W/H in hex pixels.
"""
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_bank as bb                           # noqa: E402
from mdcodecs import kosinski_decode, enigma_decode  # noqa: E402

S1ROM = 'C:/Users/hyper/roms/Sonic_The_Hedgehog_W_REV01_h3C.bin'


def col(w):
    return (((w >> 1) & 7) * 36, ((w >> 5) & 7) * 36, ((w >> 9) & 7) * 36)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    water = '--water' in sys.argv
    out = 'scratch/oracle.png'
    if '--out' in sys.argv:
        out = sys.argv[sys.argv.index('--out') + 1]
        args = [a for a in args if a != out]
    zone, act = bb.S1_ZONES.index(args[0]), int(args[1])
    x0, y0 = int(args[2], 16), int(args[3], 16)
    w = int(args[4], 16) if len(args) > 4 else 0x140
    h = int(args[5], 16) if len(args) > 5 else 0xE0
    rom = Path(S1ROM).read_bytes()
    s1 = bb.Sonic1(rom)
    hdr = s1.header(zone)
    vram = bytearray(0x10000)
    for pid in (hdr['plc1'], hdr['plc2']):
        for src, dst in s1.plc(pid):
            art, _ = bb.nemesis_decode(rom, src)
            vram[dst:dst + len(art)] = art[:0x10000 - dst]
    blocks = bytes(enigma_decode(rom, hdr['blk16']))
    chunks, _ = kosinski_decode(rom, hdr['blk256'])
    lay = s1.layout(zone, act, 1 if '--bg' in sys.argv else 0)
    be16 = lambda a: (rom[a] << 8) | rom[a + 1]
    pal = [col(be16(0x2380 + i * 2)) for i in range(16)]            # Pal_Sonic
    if water:
        src = 0x27A0 if act == 3 else 0x2460                         # Pal_SBZ3Water / Pal_LZWater
        pal = [col(be16(src + i * 2)) for i in range(64)]
    else:
        palid = 0x0C if (args[0] == 'LZ' and act == 3) else 0x0E if (args[0] == 'SBZ' and act in (1, 2)) else hdr['pal']
        pe = bb.S1['Pal_Index'] + palid * 8
        ps = bb.be32(rom, pe)
        pal += [col(be16(ps + i * 2)) for i in range(48)]
    im = Image.new('RGB', (w, h), pal[0x20])
    px = im.load()
    for ty in range(y0 & ~7, y0 + h, 8):
        for tx in range(x0 & ~7, x0 + w, 8):
            r, c = ty >> 8, tx >> 8
            if r >= len(lay) or c >= len(lay[r]):
                continue
            ch = lay[r][c] & 0x7F
            if not ch:
                continue
            o = (ch - 1) * 512 + (((ty & 0xFF) >> 4) * 16 + ((tx & 0xFF) >> 4)) * 2
            cw = (chunks[o] << 8) | chunks[o + 1]
            b = cw & 0x3FF
            bx, by = (tx >> 3) & 1, (ty >> 3) & 1
            if cw & 0x800: bx ^= 1
            if cw & 0x1000: by ^= 1
            tw = (blocks[b * 8 + (by * 2 + bx) * 2] << 8) | blocks[b * 8 + (by * 2 + bx) * 2 + 1]
            t, line = tw & 0x7FF, (tw >> 13) & 3
            hf = bool(tw & 0x800) ^ bool(cw & 0x800)
            vf = bool(tw & 0x1000) ^ bool(cw & 0x1000)
            for yy in range(8):
                for xx in range(8):
                    sx, sy = (7 - xx if hf else xx), (7 - yy if vf else yy)
                    v = vram[t * 32 + sy * 4 + sx // 2]
                    v = (v >> 4) if sx % 2 == 0 else v & 15
                    X, Y = tx + xx - x0, ty + yy - y0
                    if v and 0 <= X < w and 0 <= Y < h:
                        px[X, Y] = pal[line * 16 + v]
    im = im.resize((w * 2, h * 2), Image.NEAREST)
    im.save(out)
    print(out)


if __name__ == '__main__':
    main()
