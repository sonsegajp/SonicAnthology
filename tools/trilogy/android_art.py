"""android_art.py: the Android app's setup screen art, in the title screen's
look (title_art.py's layers, its lettering, its colours).

  android_art.py [--fonts DIR]

Writes android/app/src/main/assets/ui/ (the words, the title layers as PNG:
Android has no TGA decoder; the ground's flat shapes TitleView draws itself,
at any height) and the launcher icon (res/mipmap-*). The words
are drawn in the boxes' typeface like title_text.tga; as there, the font is
only referenced (its licence forbids redistribution), never copied.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import title_art as ta                                     # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'game' / 'trilogy' / 'assets'
OUT = ROOT / 'android' / 'app' / 'src' / 'main' / 'assets' / 'ui'
RES = ROOT / 'android' / 'app' / 'src' / 'main' / 'res'

# the menu along the band (white, gold: the choice), as the title's
MENU = {'choose': 'CHOOSE DUMPS', 'cd': 'SONIC CD', 'build': 'BUILD', 'play': 'PLAY'}
# the panel's rows and its status line (smaller)
ROWS = {'s1': 'SONIC THE HEDGEHOG', 's2': 'SONIC THE HEDGEHOG 2', 's3k': 'SONIC 3 & KNUCKLES',
        'scd': 'SONIC CD', 'optional': 'OPTIONAL'}
STATUS = {'need': 'CHOOSE YOUR DUMPS', 'ready': 'READY TO BUILD', 'building': 'BUILDING',
          'done': 'ALL SET', 'failed': 'BUILD FAILED'}
HEADER = {'h_name': 'SONIC ANTHOLOGY', 'h_years': '1991 TO 1994'}


def word(text, face, height, colour, shadow=True):
    """title_text.tga's lettering: a black keyline and a hard navy shadow"""
    g = ta.outlined(ta.crisp_text(text, face, height, colour, threshold=120), ta.BLACK, 1)
    if not shadow:
        return g
    sh = Image.new('RGBA', (g.width + 1, g.height + 1), (0, 0, 0, 0))
    sil = Image.new('RGBA', g.size, ta.NAVY + (255,))
    sh.paste(sil, (1, 1), g.getchannel('A'))
    sh.alpha_composite(g, (0, 0))
    return sh


def tga(name):
    return Image.open(ASSETS / (name + '.tga')).convert('RGBA')


def icon():
    """the launcher icon: the title's ground and sun behind the logo"""
    bg, conf, logo = tga('title_bg'), tga('title_confetti'), tga('title_logo')
    side = 160
    x0 = (bg.width - side) // 2
    sq = bg.crop((x0, 30, x0 + side, 30 + side))
    sq.alpha_composite(conf.crop((x0, 15, x0 + side, 15 + side)))
    big = sq.resize((side * 4, side * 4), Image.NEAREST)
    lw = int(side * 4 * 0.92)
    lg = logo.resize((lw, lw * logo.height // logo.width), Image.NEAREST)
    big.alpha_composite(lg, ((big.width - lg.width) // 2, (big.height - lg.height) // 2))
    for d, px in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
        p = RES / ('mipmap-' + d) / 'ic_launcher.png'
        p.parent.mkdir(parents=True, exist_ok=True)
        big.resize((px, px), Image.LANCZOS).save(p, optimize=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fonts', default=r'C:\Users\hyper\scd_ref\design\fonts')
    a = ap.parse_args()
    face = Path(a.fonts) / 'NiseSegaSonic' / 'NiseSegaSonic.TTF'
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ('title_confetti', 'title_confetti_open', 'title_side', 'title_logo'):   # (TitleView draws the ground)
        tga(name).save(OUT / (name + '.png'), optimize=True)
    for key, text in MENU.items():
        word(text, face, 11, ta.PAPER).save(OUT / ('m_%s_w.png' % key), optimize=True)
        word(text, face, 11, ta.GOLD).save(OUT / ('m_%s_g.png' % key), optimize=True)
    for key, text in ROWS.items():
        word(text, face, 8, ta.PAPER).save(OUT / ('r_%s.png' % key), optimize=True)
    for key, text in STATUS.items():
        word(text, face, 9, ta.GOLD).save(OUT / ('s_%s.png' % key), optimize=True)
    for key, text in HEADER.items():
        ta.crisp_text(text, face, 8, ta.NAVY, threshold=120).save(OUT / (key + '.png'), optimize=True)
    icon()
    print('android art -> %s, %s' % (OUT.relative_to(ROOT), (RES / 'mipmap-*').relative_to(ROOT)))


if __name__ == '__main__':
    main()
