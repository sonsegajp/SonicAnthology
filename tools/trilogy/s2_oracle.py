"""Render a Sonic 2 level area straight from the cartridge, in S2's own palette.

Oracle for the converted levels: Kosinski level art + the zone PLCs -> VRAM,
16px blocks, 128px chunks, layout, S2 palette lines (Sonic + zone). Objects
are not drawn.

  python tools/trilogy/s2_oracle.py ZONE ACT X Y [W H] [--bg] [--out file.png]
  ZONE = EHZ CPZ ...; ACT 0-based; X/Y/W/H in hex pixels.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import s2_level  # noqa: E402
from mdcodecs import kosinski_decode, nemesis_decode  # noqa: E402

S2ROM = 'C:/Users/hyper/roms/Sonic The Hedgehog 2 (World).md'


def be16(b, a): return (b[a] << 8) | b[a + 1]


def col(c):
    return (((c >> 1) & 7) * 36, ((c >> 5) & 7) * 36, ((c >> 9) & 7) * 36)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    out = 'scratch/s2oracle.png'
    if '--out' in sys.argv:
        out = sys.argv[sys.argv.index('--out') + 1]
        args = [a for a in args if a != out]
    name, act = args[0], int(args[1])
    x0, y0 = int(args[2], 16), int(args[3], 16)
    w = int(args[4], 16) if len(args) > 4 else 0x140
    h = int(args[5], 16) if len(args) > 5 else 0xE0
    rom = Path(S2ROM).read_bytes()

    class BB:   # the bits of build_bank Sonic2 needs for reading
        pass
    s2 = object.__new__(s2_level.Sonic2)
    s2.rom = rom
    s2.bb = BB()
    s2.bb.kosinski_decode = kosinski_decode
    z, s2act = s2_level.S2_ZONES[name][1][act]
    hdr = s2.art_header(z)
    vram = bytearray(0x10000)
    art = s2.level_art(z)
    vram[0:len(art)] = art
    for pid in (hdr['plc1'], hdr['plc2']):
        for src, dst in s2.plc(pid):
            pa = nemesis_decode(rom, src)[0]
            vram[dst:dst + len(pa)] = pa[:0x10000 - dst]
    blocks = s2.level_blocks(z)
    chunks = s2.kos(hdr['blk128'])
    fg, bg = s2.layout(z, s2act)
    lay = bg if '--bg' in sys.argv else fg
    pe = s2_level.S2['PalPointers'] + 3 * 8           # Sonic & Tails
    pal = [col(be16(rom, struct.unpack('>I', rom[pe:pe + 4])[0] + i * 2)) for i in range(16)]
    pe = s2_level.S2['PalPointers'] + hdr['pal'] * 8
    ps = struct.unpack('>I', rom[pe:pe + 4])[0]
    pal += [col(be16(rom, ps + i * 2)) for i in range(48)]
    im = Image.new('RGB', (w, h), pal[0x20])
    px = im.load()
    for ty in range(y0 & ~7, y0 + h, 8):
        for tx in range(x0 & ~7, x0 + w, 8):
            r, c = (ty >> 7) & 15, (tx >> 7) & 127
            ch = lay[r][c]
            cw = be16(chunks, ch * 128 + (((ty & 0x7F) >> 4) * 8 + ((tx & 0x7F) >> 4)) * 2)
            b = cw & 0x3FF
            bx, by = (tx >> 3) & 1, (ty >> 3) & 1
            if cw & 0x400: bx ^= 1
            if cw & 0x800: by ^= 1
            tw = be16(blocks, b * 8 + (by * 2 + bx) * 2)
            t, line = tw & 0x7FF, (tw >> 13) & 3
            hf = bool(tw & 0x800) ^ bool(cw & 0x400)
            vf = bool(tw & 0x1000) ^ bool(cw & 0x800)
            for yy in range(8):
                for xx in range(8):
                    sx, sy = (7 - xx if hf else xx), (7 - yy if vf else yy)
                    v = vram[t * 32 + sy * 4 + sx // 2]
                    v = (v >> 4) if sx % 2 == 0 else v & 15
                    X, Y = tx + xx - x0, ty + yy - y0
                    if v and 0 <= X < w and 0 <= Y < h:
                        px[X, Y] = pal[line * 16 + v]
    im.save(out)
    print(out)


if __name__ == '__main__':
    main()
