"""Compare a converted S2 act's foreground, as the game holds it at runtime,
against Sonic 2's own data — pixel by pixel in palette-index space.

  python tools/trilogy/s2_fgcheck.py ZONE ACT RAMDUMP VRAMDUMP [--out diff.png]

The region is the camera view in the RAM dump (Camera_X/Y_pos_copy). The game
side is rendered from its RAM (layout $8000, chunks $0000, blocks $9000) and
the VRAM dump; the S2 side from the cartridge (level art + zone PLCs + APM),
with each line's colour indices taken through the converter's line tables.
Output: game | S2 | mismatches (red: pixel differs, magenta: one side empty).
"""
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_bank as bb  # noqa: E402
import s2_level  # noqa: E402
from mdcodecs import kosinski_decode, nemesis_decode  # noqa: E402

S2ROM = 'C:/Users/hyper/roms/Sonic The Hedgehog 2 (World).md'


def be16(b, a): return (b[a] << 8) | b[a + 1]


def tile_px(vram, tw, sx, sy):
    t = tw & 0x7FF
    if tw & 0x800: sx = 7 - sx
    if tw & 0x1000: sy = 7 - sy
    v = vram[(t * 32 + sy * 4 + sx // 2) & 0xFFFF]
    return (v >> 4) if sx % 2 == 0 else v & 15


def s2_pixel(ctx, wx, wy):
    rom, lay, chunks, blocks, vram, l0 = ctx
    r, c = (wy >> 7) & 15, (wx >> 7) & 127
    if r >= len(lay) or c >= len(lay[r]):
        return 0
    cw = be16(chunks, lay[r][c] * 128 + (((wy & 0x7F) >> 4) * 8 + ((wx & 0x7F) >> 4)) * 2)
    bx, by = (wx >> 3) & 1, (wy >> 3) & 1
    if cw & 0x400: bx ^= 1
    if cw & 0x800: by ^= 1
    tw = be16(blocks, (cw & 0x3FF) * 8 + (by * 2 + bx) * 2)
    tw ^= (cw & 0xC00) << 1
    v = tile_px(vram, tw, wx & 7, wy & 7)
    if not v:
        return 0
    line = (tw >> 13) & 3
    tab = {0: l0, 1: s2_level.S2_L1_PERM}.get(line, list(range(16)))
    return line * 16 + tab[v]


def game_pixel(ram, vram, wx, wy):
    hdr = 0x8000
    w = be16(ram, hdr)
    h = be16(ram, hdr + 4)
    cy = (wy >> 7)
    cx = wx >> 7
    if w <= 0 or cx >= w or cy >= h:
        return 0
    row = be16(ram, hdr + 8 + cy * 4)
    ch = ram[(row + cx) & 0xFFFF]
    cw = be16(ram, ch * 128 + (((wy & 0x7F) >> 4) * 8 + ((wx & 0x7F) >> 4)) * 2)
    bx, by = (wx >> 3) & 1, (wy >> 3) & 1
    if cw & 0x400: bx ^= 1
    if cw & 0x800: by ^= 1
    tw = be16(ram, 0x9000 + (cw & 0x3FF) * 8 + (by * 2 + bx) * 2)
    tw ^= (cw & 0xC00) << 1
    v = tile_px(vram, tw, wx & 7, wy & 7)
    return ((tw >> 13) & 3) * 16 + v if v else 0


def tile_pixels(vram, tw, tab, cache):
    key = (tw, id(vram))
    if key not in cache:
        line = (tw >> 13) & 3
        cache[key] = tuple(line * 16 + tab[line][v] if v else 0
                           for v in (tile_px(vram, tw, x, y) for y in range(8) for x in range(8)))
    return cache[key]


def full_check(name, act, ram, gvram, ctx, anim_tiles):
    """Every tile of the whole FG and BG layouts: game memory vs S2."""
    rom, fg, chunks, blocks, vram, l0 = ctx
    _, bg = ctx_bg
    tab = {0: l0, 1: s2_level.S2_L1_PERM, 2: list(range(16)), 3: list(range(16))}
    ident = {k: list(range(16)) for k in range(4)}
    cache = {}
    hdr = 0x8000
    report = []
    for plane, lay, off in (('FG', fg, 0), ('BG', bg, 2)):
        gw, gh = be16(ram, hdr + off), be16(ram, hdr + 4 + off)
        bad = {}
        for cy in range(min(gh, len(lay))):
            grow = be16(ram, hdr + 8 + cy * 4 + off)
            for cx in range(min(gw, len(lay[cy]))):
                gch, sch = ram[(grow + cx) & 0xFFFF], lay[cy][cx]
                for k in range(64):
                    gcw = be16(ram, gch * 128 + k * 2)
                    scw = be16(chunks, sch * 128 + k * 2)
                    for q in range(4):
                        bx, by = q & 1, q >> 1
                        gbx, gby = bx ^ ((gcw >> 10) & 1), by ^ ((gcw >> 11) & 1)
                        sbx, sby = bx ^ ((scw >> 10) & 1), by ^ ((scw >> 11) & 1)
                        gtw = be16(ram, 0x9000 + (gcw & 0x3FF) * 8 + (gby * 2 + gbx) * 2) ^ ((gcw & 0xC00) << 1)
                        stw = be16(blocks, (scw & 0x3FF) * 8 + (sby * 2 + sbx) * 2) ^ ((scw & 0xC00) << 1)
                        if (stw & 0x7FF) in anim_tiles:
                            continue
                        if tile_pixels(gvram, gtw, ident, cache) != tile_pixels(vram, stw, tab, cache):
                            wx = cx * 128 + (k & 7) * 16 + bx * 8
                            wy = cy * 128 + (k >> 3) * 16 + by * 8
                            bad.setdefault((stw & 0x7FF, (stw >> 13) & 3), []).append((wx, wy))
        n = sum(len(v) for v in bad.values())
        report.append(f"{plane}: {n} differing tiles")
        for (t, line), where in sorted(bad.items(), key=lambda kv: -len(kv[1]))[:12]:
            report.append(f"   S2 tile ${t:03X} line {line}: {len(where)}x, e.g. {where[0][0]:X},{where[0][1]:X}")
    return report


ctx_bg = None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    out = 'scratch/s2fgcheck.png'
    if '--out' in sys.argv:
        out = sys.argv[sys.argv.index('--out') + 1]
        args = [a for a in args if a != out]
    name, act, ramf, vramf = args[0], int(args[1]), args[2], args[3]
    ram, gvram = Path(ramf).read_bytes(), Path(vramf).read_bytes()
    x0, y0 = be16(ram, 0xEE80), be16(ram, 0xEE84)
    rom = Path(S2ROM).read_bytes()

    class BB:
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
    fg, bgl = s2.layout(z, s2act)
    global ctx_bg
    ctx_bg = (fg, bgl)
    l0 = [0] + [bb.nearest(s2_level.S2_SONIC[i], bb.S3K_SONIC, bb.SUPER_CYCLED) for i in range(1, 16)]
    ctx = (rom, fg, s2.kos(hdr['blk128']), blocks, vram, l0)
    if '--full' in sys.argv:
        anim = set()
        for _, _, tile, nt, frames in s2.anim_scripts(z):
            anim.update(range(tile, tile + nt))
        if name == 'CNZ':
            anim.update(range(0x550, 0x580))
        for line in full_check(name, act, ram, gvram, ctx, anim):
            print(line)
        return
    W, H = 320, 224
    im = Image.new('RGB', (W * 3, H))
    px = im.load()
    pal = [((i * 53) % 256, (i * 97) % 256, (i * 151) % 256) for i in range(64)]
    bad = 0
    animated = set()
    for y in range(H):
        for x in range(W):
            g = game_pixel(ram, gvram, x0 + x, y0 + y)
            s = s2_pixel(ctx, x0 + x, y0 + y)
            px[x, y] = pal[g] if g else (0, 0, 0)
            px[W + x, y] = pal[s] if s else (0, 0, 0)
            if g != s:
                bad += 1
                px[2 * W + x, y] = (255, 0, 255) if (g == 0) != (s == 0) else (255, 0, 0)
            else:
                px[2 * W + x, y] = (40, 40, 40) if g else (0, 0, 0)
    im.save(out)
    print(f"{out}: camera {x0:X},{y0:X}  {bad} mismatching pixels")


if __name__ == '__main__':
    main()
