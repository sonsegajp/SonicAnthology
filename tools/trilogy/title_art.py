#!/usr/bin/env python3
"""Sonic Anthology's own title art, in the language of the Japanese Mega Drive
box fronts: paper-white ground, flat primary wedges and discs, Memphis
confetti with off-register colour doubles, saw-tooth bands.

    title_art.py [--fonts DIR] [--logo MASTER.png] [--out game/trilogy/assets]

Writes uncompressed 32-bit TGA (what trilogy_title.c reads):
  title_bg.tga    640x224 ground, the 4:3 picture centred (x 160..479); the
                  extra width carries on for widescreen. The band's saw-tooth
                  top is drawn at run time (it scrolls right)
  title_confetti.tga  the ground's confetti, its own layer: 640 wide, the
                  rows from CONF_Y0 to the band, wrapping top to bottom (the
                  game scrolls it upwards)
  title_confetti_open.tga  the same with no clearing for the logo, 256 rows:
                  a taller screen's sky
  title_side.tga  the left margin's black saw-tooth band with a vertical
                  "SONIC" (Sonic 2's Japanese box), drawn at 16:9 and wider
  title_logo.tga  the Blender render (logo.blend) reduced to its 1x size,
                  with a hard offset shadow
  menu_bg.tga     the options page's ground (Sonic 3's Japanese box: the
                  yellow / red / green disc trio)
  ds_tile.tga     the Data Select's pattern tile (player sprites are added
                  at run time, from the cartridge)
  ds_band.tga     the Data Select's title band
  title_text.tga  the band's lettering (PRESS START, the menu), white and
                  gold rows

Fonts are referenced, never copied (their licences forbid redistribution);
only the rendered pictures are kept.
"""
import argparse
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]

# Mega Drive colour steps (the BRIEF's snapped values)
PAPER = (255, 255, 255)
RED = (255, 49, 49)
GREEN = (87, 174, 87)
YELLOW = (255, 255, 87)
GOLD = (255, 206, 49)
BLUE = (49, 87, 174)
SKY = (119, 146, 206)
BLACK = (0, 0, 0)
NAVY = (0, 0, 49)
PRIMARIES = [RED, GREEN, YELLOW, BLUE, BLACK]

W, H = 640, 224
CX = W // 2                       # the 4:3 picture is x CX-160 .. CX+159
LOGO_BOX = (CX - 142, 14, CX + 142, 156)
BAND_TOP = 184                    # the blue band (PRESS START / the menu)
CONF_Y0 = 15                      # the confetti layer: rows CONF_Y0 .. BAND_TOP (under the header rule)


def tga(img, path):
    img = img.convert('RGBA')
    w, h = img.size
    hdr = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 32, 0x28])
    px = img.tobytes('raw', 'BGRA')
    Path(path).write_bytes(hdr + px)


def crisp_text(text, font_path, height, colour, scale=4, threshold=110, angle=0):
    """text rendered large (and turned), reduced and thresholded: hard
    pixels, no blur"""
    f = ImageFont.truetype(str(font_path), height * scale)
    l, t, r, b = f.getbbox(text)
    big = Image.new('L', (r - l + 4 * scale, b - t + 4 * scale), 0)
    ImageDraw.Draw(big).text((2 * scale - l, 2 * scale - t), text, font=f, fill=255)
    if angle:
        big = big.rotate(angle, expand=True, resample=Image.BICUBIC)
    small = big.resize((big.width // scale, big.height // scale), Image.LANCZOS)
    mask = small.point(lambda v: 255 if v >= threshold else 0)
    out = Image.new('RGBA', small.size, colour + (0,))
    out.putalpha(mask)
    return out.crop(out.getbbox())


def outlined(img, colour, r=1):
    """img (RGBA, hard alpha) with an r-pixel outline in `colour`"""
    w, h = img.size
    out = Image.new('RGBA', (w + 2 * r, h + 2 * r), (0, 0, 0, 0))
    a = img.getchannel('A')
    sil = Image.new('RGBA', img.size, colour + (255,))
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx or dy:
                out.paste(sil, (r + dx, r + dy), a)
    out.alpha_composite(img, (r, r))
    return out


def sawtooth(d, x0, x1, y, tooth=10, depth=5, fill=BLUE, up=True):
    """a row of teeth along y, pointing up (or down) from the band"""
    for x in range(x0 - tooth, x1 + tooth, tooth):
        tip = y - depth if up else y + depth
        d.polygon([(x, y), (x + tooth // 2, tip), (x + tooth, y)], fill=fill)


# ---- confetti -----------------------------------------------------------------
def rot(pts, ang, ox, oy):
    c, s = math.cos(ang), math.sin(ang)
    return [(round(ox + x * c - y * s), round(oy + x * s + y * c)) for x, y in pts]


def shape(rng, kind, x, y, colour, d):
    ang = rng.choice([-0.6, -0.3, 0, 0.3, 0.6, 1.2, -1.2])
    if kind == 'zigzag':
        n, step, amp = rng.randint(4, 7), rng.choice([3, 4]), rng.choice([3, 4, 5])
        pts = [(i * step - n * step / 2, (-amp if i & 1 else amp) / 2) for i in range(n + 1)]
        d.line(rot(pts, ang, x, y), fill=colour, width=2, joint=None)
    elif kind == 'squiggle':
        n, amp = rng.randint(14, 24), rng.choice([2.5, 3.5])
        pts = [(i - n / 2, amp * math.sin(i * 0.55)) for i in range(n + 1)]
        d.line(rot(pts, ang, x, y), fill=colour, width=2)
    elif kind == 'circle':
        r = rng.randint(3, 5)
        d.ellipse([x - r, y - r, x + r, y + r], outline=colour, width=2 if r > 3 else 1)
    elif kind == 'triangle':
        r = rng.randint(4, 6)
        pts = rot([(0, -r), (r * 0.87, r / 2), (-r * 0.87, r / 2)], ang, x, y)
        d.polygon(pts, outline=colour, width=1)
        d.line(pts + [pts[0]], fill=colour, width=2)
    elif kind == 'dots':
        pts = rot([(i * 4 - 8, 0) for i in range(5)], ang, x, y)
        for px, py in pts:
            d.rectangle([px, py, px + 1, py + 1], fill=colour)
    elif kind == 'square':
        d.rectangle([x, y, x + 3, y + 3], fill=colour)
    elif kind == 'coil':
        for i in range(5):
            d.arc([x + i * 3 - 8, y - 3, x + i * 3 - 2, y + 3], 180, 360, fill=colour, width=1)
            d.arc([x + i * 3 - 8, y - 3, x + i * 3 - 2, y + 3], 0, 180, fill=colour, width=1)


def confetti(img, rng, regions, count, exclude=(), sparse=0.15, wrap=0):
    """Memphis confetti in the given boxes: each piece in one primary, over a
    1 px off-register copy in another (the box fronts' misprint look); wrap:
    the picture's height, pieces carried round top to bottom (it scrolls)"""
    d = ImageDraw.Draw(img)
    kinds = ['zigzag'] * 4 + ['squiggle'] * 3 + ['circle'] * 3 + ['triangle'] * 2 + ['dots'] * 2 + ['square'] * 2 + ['coil']
    placed = []
    tries = 0
    while len(placed) < count and tries < count * 60:
        tries += 1
        x0, y0, x1, y1 = rng.choice(regions)
        x, y = rng.randint(x0, x1), rng.randint(y0, y1)
        if any(ex[0] <= x <= ex[2] and ex[1] <= y <= ex[3] for ex in exclude) and rng.random() > sparse:
            continue
        if any((x - px) ** 2 + (y + k - py) ** 2 < 17 ** 2 for px, py in placed
               for k in ((0, -wrap, wrap) if wrap else (0,))):
            continue
        placed.append((x, y))
        kind = rng.choice(kinds)
        c1 = rng.choice(PRIMARIES)
        c2 = rng.choice([c for c in PRIMARIES if c != c1])
        state = rng.getstate()
        for k in ((0, -wrap, wrap) if wrap else (0,)):
            rng.setstate(state)
            shape(rng, kind, x + 1, y + k + 1, c2, d)         # the off-register double
            rng.setstate(state)
            shape(rng, kind, x, y + k, c1, d)


# ---- the title ----------------------------------------------------------------
def title_bg(fonts):
    img = Image.new('RGBA', (W, H), PAPER + (255,))
    d = ImageDraw.Draw(img)
    rng = random.Random(1991)
    # Sonic 1's colour wedges: red sweeping in from the upper right, green
    # rising from the lower left (both carry on past 16:9)
    d.polygon([(CX + 186, 0), (W + 40, 0), (W + 40, BAND_TOP), (CX + 76, BAND_TOP)], fill=RED)
    d.polygon([(-40, 104), (CX - 96, BAND_TOP), (-40, BAND_TOP)], fill=GREEN)
    # the yellow disc, up and to the left of the logo's centre
    r = 76
    dx, dy = CX - 34, 82
    d.ellipse([dx - r, dy - r, dx + r, dy + r], fill=YELLOW)
    # a hollow blue ring echoing it on the right (Sonic 1's back cover)
    d.ellipse([CX + 132, 120, CX + 172, 160], outline=BLUE, width=3)
    d.ellipse([CX + 133, 121, CX + 173, 161], outline=BLACK, width=1)
    # a row of black squares and a dash bar (S1 JP's band furniture)
    for i in range(7):
        d.rectangle([CX - 150 + i * 7, 170, CX - 146 + i * 7, 174], fill=BLACK)
    d.rectangle([CX + 108, 174, CX + 150, 176], fill=BLACK)
    # (the confetti is its own layer: title_confetti)
    # the header strip's rule, as under the boxes' "Mega Drive Cartridge"
    # line (its text is the game's own font, drawn by trilogy_title.c)
    d.rectangle([CX - 154, 13, CX + 153, 13], fill=BLUE)
    # the blue band: PRESS START and the menu sit here (its saw-tooth top is
    # drawn by the game, scrolling)
    d.rectangle([0, BAND_TOP, W, H], fill=BLUE)
    # white accents on the band (S1 JP): two small blocks at each side
    for sx in (CX - 152, CX + 140):
        d.rectangle([sx, H - 10, sx + 4, H - 7], fill=PAPER)
        d.rectangle([sx + 7, H - 10, sx + 11, H - 7], fill=PAPER)
    return img


def title_confetti(fonts):
    """the title's confetti: dense in the margins, sparse in the logo's
    columns; it wraps top to bottom, the game scrolling it up"""
    h = BAND_TOP - CONF_Y0
    img = Image.new('RGBA', (W, h), (0, 0, 0, 0))
    rng = random.Random(1991)
    confetti(img, rng, [(4, 0, W - 4, h - 1)], 128, exclude=[(LOGO_BOX[0], -h, LOGO_BOX[2], 2 * h)],
             sparse=0.05, wrap=h)
    return img


def title_confetti_open(fonts):
    """the confetti with no clearing for the logo, at the margins' density:
    the sky a taller screen adds, above and below the title's own (the game's
    title and the Android setup screen); 256 rows, wrapping top to bottom"""
    h = 256
    img = Image.new('RGBA', (W, h), (0, 0, 0, 0))
    confetti(img, random.Random(1994), [(4, 0, W - 4, h - 1)], 350, wrap=h)
    return img


def title_side(fonts):
    """Sonic 2's left band: black, a saw-tooth inner edge, vertical SONIC"""
    w = 40
    img = Image.new('RGBA', (w, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 7, H], fill=BLACK)
    for y in range(-4, H, 10):
        d.polygon([(w - 7, y), (w - 1, y + 5), (w - 7, y + 10)], fill=BLACK)
    word = crisp_text('SONIC', fonts / 'NiseSegaSonic/NiseSegaSonic.TTF', 24, PAPER, threshold=128)
    word = word.rotate(90, expand=True)
    img.alpha_composite(word, ((w - 7 - word.width) // 2, (BAND_TOP - word.height) // 2 + 4))
    # a yellow squiggle down the band, under the word
    pts = [(14 + 4 * math.sin(i * 0.5), i) for i in range(BAND_TOP - 30, H)]
    d.line(pts, fill=YELLOW, width=2)
    return img


def title_logo(master, width=264):
    src = Image.open(master).convert('RGBA')
    h = round(src.height * width / src.width)
    # premultiplied reduction so the dark outline doesn't halo
    small = src.resize((width, h), Image.LANCZOS, reducing_gap=3.0)
    a = small.getchannel('A').point(lambda v: 0 if v < 24 else v)
    small.putalpha(a)
    out = Image.new('RGBA', (width + 3, h + 3), (0, 0, 0, 0))
    hard = a.point(lambda v: 255 if v >= 96 else 0)
    shadow = Image.new('RGBA', (width, h), NAVY + (255,))
    out.paste(shadow, (3, 3), hard)                        # the hard offset shadow
    out.alpha_composite(small, (0, 0))
    return out


def menu_bg(fonts):
    """Sonic 3's Japanese box: the disc trio, tech doodles, the S&K band"""
    img = Image.new('RGBA', (W, H), PAPER + (255,))
    d = ImageDraw.Draw(img)
    rng = random.Random(1994)
    d.ellipse([CX - 236, 30, CX - 96, 170], fill=GOLD)
    d.pieslice([CX + 70, -90, CX + 250, 90], 0, 180, fill=RED)
    d.ellipse([CX + 150, 96, CX + 250, 196], fill=(49, 146, 87))
    # tech doodles: thick lines ending in rings, stair zig-zags
    for i in range(14):
        x, y = rng.randint(8, W - 8), rng.randint(20, H - 40)
        c = rng.choice(PRIMARIES)
        if i & 1:
            x2 = x + rng.choice([-1, 1]) * rng.randint(14, 30)
            d.line([(x, y), (x2, y)], fill=c, width=3)
            d.ellipse([x2 - 3, y - 3, x2 + 3, y + 3], outline=c, width=2)
        else:
            pts, px, py = [], x, y
            for k in range(5):
                pts += [(px, py), (px + 5, py)]
                px, py = px + 5, py + (5 if k & 1 else -5)
            d.line(pts, fill=c, width=2)
    confetti(img, rng, [(4, 4, W - 4, H - 34)], 50, exclude=[(CX - 160, 8, CX + 160, 214)], sparse=0.0)
    d.rectangle([0, H - 22, W, H], fill=(0, 87, 174))
    sawtooth(d, 0, W, H - 22, fill=(0, 87, 174))
    return img


# The Data Select's pattern: a diamond lattice on paper, cells DS_PX x DS_PY,
# odd rows shifted half a cell. Shape cells hold one of the boxes' shapes
# (drawn here); the others are where trilogy_title.c stamps a greyscale
# player sprite from the cartridge at run time (DS_SPRITES, centres).
DS_TW, DS_TH, DS_PX, DS_PY = 512, 104, 64, 52


def ds_cells():
    for r in range(2):
        for c in range(DS_TW // DS_PX):
            x, y = (c * DS_PX + DS_PX // 2 + r * DS_PX // 2) % DS_TW, r * DS_PY + DS_PY // 2
            yield (c + r) & 1, x, y


def ds_tile(fonts):
    """the Data Select's seamless pattern tile: paper and the boxes' shapes,
    each over a 1 px off-register double"""
    big = Image.new('RGBA', (DS_TW * 3, DS_TH * 3), PAPER + (255,))
    d = ImageDraw.Draw(big)
    kinds = ['ring', 'triangle', 'zigzag', 'squiggle']
    colours = [(RED, BLUE), (BLUE, YELLOW), (GREEN, RED), (YELLOW, BLACK)]
    k = 0
    for shape_cell, x, y in ds_cells():
        if not shape_cell:
            continue
        kind, (c1, c2) = kinds[k % 4], colours[k % 4]
        k += 1
        for ox in (0, DS_TW, 2 * DS_TW):               # (drawn into a 3x3 so edges wrap)
            for oy in (0, DS_TH, 2 * DS_TH):
                for (dx, c) in ((1, c2), (0, c1)):
                    X, Y = x + ox + dx, y + oy + dx
                    if kind == 'ring':
                        d.ellipse([X - 7, Y - 7, X + 7, Y + 7], outline=c, width=2)
                    elif kind == 'triangle':
                        d.line([(X, Y - 8), (X + 8, Y + 6), (X - 8, Y + 6), (X, Y - 8)], fill=c, width=2)
                    elif kind == 'zigzag':
                        d.line([(X - 12 + i * 4, Y + (-4 if i & 1 else 4)) for i in range(7)], fill=c, width=2)
                    else:
                        d.line([(X - 12 + i, Y + 4 * math.sin(i * 0.5)) for i in range(25)], fill=c, width=2)
        # a little dotted trail beside each shape
        for ox in (0, DS_TW, 2 * DS_TW):
            for oy in (0, DS_TH, 2 * DS_TH):
                for i in range(3):
                    X, Y = x + ox + 16 + i * 4, y + oy + 12
                    d.rectangle([X, Y, X + 1, Y + 1], fill=BLACK)
    return big.crop((DS_TW, DS_TH, 2 * DS_TW, 2 * DS_TH))


def ds_band(fonts):
    """the black saw-tooth band at the Data Select's foot and its title (the
    cartridge's own title sprite is retired while this ground shows)"""
    img = Image.new('RGBA', (W, 30), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 6, W, 30], fill=BLACK)
    sawtooth(d, 0, W, 6, fill=BLACK)
    face = fonts / 'NiseSegaSonic/NiseSegaSonic.TTF'
    title = crisp_text('DATA SELECT', face, 12, GOLD, threshold=128)
    kana = crisp_text('データセレクト', fonts / 'ofl_substitutes/DelaGothicOne-Regular.ttf', 11, PAPER)
    x = CX - (title.width + 10 + kana.width) // 2
    img.alpha_composite(title, (x, 12))
    img.alpha_composite(kana, (x + title.width + 10, 12 + (title.height - kana.height) // 2))
    return img


TEXT_STRIPS = ['PRESS START BUTTON', 'START GAME', 'COMPETITION', 'OPTIONS']
STRIP_H = 18


def title_text(fonts):
    """The band's lettering in the boxes' heavy geometric face: white with a
    black keyline and a hard shadow; each word twice, white then gold (the
    choice). Rows of STRIP_H in TEXT_STRIPS order, left aligned; the game
    measures each row's width from its alpha."""
    face = fonts / 'NiseSegaSonic/NiseSegaSonic.TTF'
    rows = []
    for text in TEXT_STRIPS:
        for colour in (PAPER, GOLD):
            g = outlined(crisp_text(text, face, 11, colour, threshold=120), BLACK, 1)
            sh = Image.new('RGBA', (g.width + 1, g.height + 1), (0, 0, 0, 0))
            sil = Image.new('RGBA', g.size, NAVY + (255,))
            sh.paste(sil, (1, 1), g.getchannel('A'))           # the hard shadow
            sh.alpha_composite(g, (0, 0))
            rows.append(sh)
    img = Image.new('RGBA', (max(r.width for r in rows), STRIP_H * len(rows)), (0, 0, 0, 0))
    for i, r in enumerate(rows):
        img.alpha_composite(r, (0, i * STRIP_H + (STRIP_H - r.height) // 2))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fonts', default=r'C:\Users\hyper\scd_ref\design\fonts')
    ap.add_argument('--logo', default=r'C:\Users\hyper\scd_ref\design\logo_master.png')
    ap.add_argument('--out', default=str(ROOT / 'game' / 'trilogy' / 'assets'))
    ap.add_argument('--preview', help='also write PNG previews here')
    a = ap.parse_args()
    fonts, out = Path(a.fonts), Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    pics = {
        'title_bg': title_bg(fonts),
        'title_confetti': title_confetti(fonts),
        'title_confetti_open': title_confetti_open(fonts),
        'title_side': title_side(fonts),
        'title_logo': title_logo(a.logo),
        'menu_bg': menu_bg(fonts),
        'ds_tile': ds_tile(fonts),
        'ds_band': ds_band(fonts),
        'title_text': title_text(fonts),
    }
    for name, img in pics.items():
        tga(img, out / (name + '.tga'))
        if a.preview:
            Path(a.preview).mkdir(parents=True, exist_ok=True)
            img.save(Path(a.preview) / (name + '.png'))
    print('wrote', ', '.join(pics), 'to', out)


if __name__ == '__main__':
    main()
