#!/usr/bin/env python3
"""Pack the player's own Sonic Origins sprites for Sonic Anthology's "player
sprites" option: origins.bin, beside the executable (never in the repo).

    origins_pack.py [--data C:\\Users\\...\\scd_ref\\origins] [--rom sonic3k.bin]
                    [--disasm game/skdisasm] [--out build/Release/origins.bin]
                    [--check DIR]

--data is rsdk.py's extraction of the Origins install (per game: <game>.json
and sheets/). Every S3K player mapping frame is matched to an Origins frame:
the frame's place in S3K's animation scripts (which animation, how far
along) picks the same animation and relative place in each Origins game.
Walk and run frames drawn on 45-degree angle sets map to the upright pose;
the renderer turns it. Frames only S3K's own gimmicks use stay native.

Each picture keeps its game's own colours. Every colour also names the
palette entry it plays the part of in S3K (the nearest of the player's line
0; for monitor icons also line 1's ring yellows) and that entry's colour in
S3K's own palette: the renderer draws the colour moved by however far the
entry has moved from it, so the cartridge's fades, water, Super palettes and
flashes still play over the Origins colours.

The animations that loop are the Origins game's own: walking, running and
rolling advance at the cartridge's speed-driven rate (one frame per
($800 / $400 - |ground speed|) / 256 + 1 frames), the rest at their Origins
speed; Origins' extra running animations (S3's jog and dash, CD's peel-out
run) take over at their speeds, and a charged drop dash shows each game's
own (S1 / S2 / CD hold the solid ball, S3 plays its Dropdash).

Monitors and star posts are matched too: each S3K monitor frame becomes
that style's box with its icon (Sonic CD, which lacks the elemental shields
and Eggman, borrows Sonic 2's icons for those on its own box); the icon that
rises from a broken monitor is packed alone; posts stand on S3K's foot.

origins.bin (little-endian):
  "TORG", u16 version (3), u16 styles (4: Sonic 1, Sonic 2, Sonic CD, S3K)
  styles x slots (7: Sonic, Tails, Knuckles, Super Sonic, Monitor, Monitor
  icon, Star post) x 256 frames: u16 image (0xFFFF: keep the cartridge's)
  Monitor frames are S3K's (Map_Monitor: 0 box, 1 static, 2 1-up, 3 Eggman,
  4 rings, 5 shoes, 6-8 fire / lightning / bubble, 9 invincibility, 10
  Super, 11 broken); the 1-up as Tails is frame $12, as Knuckles $13.
  styles x 4 players x 64 tracks (S3K animation 0-$2F, $30 jog, $31 dash,
  $32 drop dash, $33-$39 Sonic CD's 3D ramp pictures, $3A its spinning disc
  turns, in the style's colours, drawn upright): u16 first, u8 count, u8
  kind (0 none: the frame table;
  1 walk rate, 2 roll rate, 3 fixed), u8 ticks, u8 loop, u16 0
  u32 sequence length, u16 images (the tracks' frames)
  u32 images, then per image: i16 w, h, ox, oy (top-left from the object's
  position, unflipped), u16 colours, colours x (u8 r, g, b, palette entry
  0-63, u16 that entry's S3K colour), w*h bytes (0 clear, n: colour n-1)
"""
import argparse
import json
import re
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
STYLES = ['sonic1', 'sonic2', 'soniccd', 'sonic3k']
CHARS = ['Sonic', 'Tails', 'Knuckles', 'SuperSonic']
SLOTS = CHARS + ['Monitor', 'MonitorIcon', 'StarPost']

# S3K monitor frame -> each style's icon (Origins frame indices; the v4
# games' monitor script frames, S3K's ItemBox "Powerups"). None: borrow.
ICON_ROLES = {0: 'static2', 1: 'static', 2: 'sonic', 0x12: 'tails', 0x13: 'knux', 3: 'eggman', 4: 'rings',
              5: 'shoes', 6: 'fire', 7: 'lightning', 8: 'bubble', 9: 'invincible', 10: 'super'}
ICONS = {
    'sonic1': dict(box=0, static=18, static2=19, sonic=5, tails=6, knux=7, eggman=12, rings=1, shoes=4, fire=10,
                   lightning=11, bubble=9, invincible=3, super=8),
    'sonic2': dict(box=0, static=1, static2=2, sonic=7, tails=8, knux=9, eggman=14, rings=3, shoes=6, fire=12,
                   lightning=13, bubble=11, invincible=5, super=10),
    'soniccd': dict(box=0, static=11, static2=12, sonic=5, tails=7, knux=9, eggman=None, rings=1, shoes=4, fire=None,
                    lightning=None, bubble=None, invincible=3, super=8),     # (complete monitors)
    'sonic3k': dict(sonic=7, tails=8, knux=9, eggman=10, rings=0, shoes=6, fire=3, lightning=4, bubble=2,
                    invincible=5, super=14),
}
POSTS = {   # full post, pole, head unlit, head lit (Origins frame indices)
    'sonic1': (0, 1, 2, 3), 'sonic2': (2, 3, 0, 1), 'soniccd': (0, 1, 2, 4),
}
ANIM_FILES = {'Sonic': 'Sonic/Anim - Sonic.asm', 'Tails': 'Tails/Anim - Tails.asm',
              'Knuckles': 'Knuckles/Anim - Knuckles.asm', 'SuperSonic': 'Sonic/Anim - Super Sonic.asm'}
ANIM_LABEL = {'Sonic': 'AniSonic', 'Tails': 'AniTails', 'Knuckles': 'AniKnuckles', 'SuperSonic': 'AniSuperSonic'}
PALETTES = {'Sonic': 0x0A8A3C, 'Tails': 0x0A8A3C, 'Knuckles': 0x0A8AFC, 'SuperSonic': 0x0A8A3C}

# S3K animation (Sonic 2's numbering) -> Origins animation, per engine. A
# list splits the script's distinct frames: [(first, last, name), ...] by
# position. Missing / empty Origins animations fall back down the list.
# 'ROLL:<name>': the roll's solid-ball frame ($96, which S3K flashes between
# the curled ones) takes that animation's ball, the rest its curled frames.
V4 = {   # Sonic 1, Sonic 2, Sonic CD (Retro Engine v3/v4 names)
    0x00: ['Walking'], 0x01: ['Running'], 0x02: ['ROLL:Jumping'], 0x03: ['ROLL:Jumping'],
    0x04: ['Pushing'], 0x05: [(0, 0, 'Stopped'), (1, 4, 'Waiting'), (5, 99, 'Bored!', 'Waiting')],
    0x06: ['Flailing 1', 'Flailing Left'], 0x07: ['Looking Up'], 0x08: ['Looking Down'],
    0x09: ['Spin Dash'], 0x0B: ['Bouncing'], 0x0C: ['Flailing 2', 'Flailing Right'],
    0x0D: ['Skidding'], 0x0E: ['Fan Rotate'], 0x0F: ['Fan Rotate'], 0x10: ['Bouncing'],
    0x11: ['Clinging On'], 0x14: ['Hanging'], 0x15: ['Breathing'], 0x16: ['Dying'],
    0x17: ['Drowning'], 0x18: ['Dying'], 0x19: ['Hurt'], 0x1A: ['Hurt'], 0x1B: ['Hurt'],
    0x1F: ['Super Transform'], 0x22: ['Hanging'], 0x23: ['Hanging'],
}
# Tails' flight (Tails_Set_Flying_Animation): $20 / $21 falling / rising, $22 / $23 the
# same carrying Sonic, $24 tired; under water $25 / $26, $27 carrying, $28 tired
V4_TAILS = {0x20: ['Flying'], 0x21: ['Flying'], 0x22: [(0, 99, 'Fly Lift Down', 'Flying')],
            0x23: [(0, 99, 'Fly Lift Up', 'Flying')], 0x24: ['Flying Tired'], 0x25: ['Swimming'],
            0x26: ['Swimming'], 0x27: [(0, 99, 'Swim Lift', 'Swimming')], 0x28: ['Swimming Tired']}
V4_KNUX = {0x20: ['Gliding'], 0x21: ['Gliding Drop'], 0x22: ['Gliding Stop']}
V5 = {   # Sonic 3 & Knuckles (Retro Engine v5 names)
    0x00: ['Walk'], 0x01: ['Run'], 0x02: ['ROLL:Jump'], 0x03: ['ROLL:Jump'], 0x04: ['Push'],
    0x05: [(0, 0, 'Idle'), (1, 99, 'Bored 1', 'Idle')], 0x06: ['Balance 1'], 0x07: ['Look Up'],
    0x08: ['Crouch'], 0x09: ['Spindash'], 0x0B: ['Spring Diagonal'], 0x0C: ['Balance 2'],
    0x0D: ['Skid'], 0x0E: ['Fan'], 0x0F: ['Fan'], 0x10: ['Spring Diagonal'],
    0x14: ['HangGimmick'], 0x15: ['Breathe'], 0x16: ['Die'], 0x17: ['Drown'], 0x18: ['Die'],
    0x19: ['Hurt'], 0x1A: ['Hurt'], 0x1B: ['Hurt'], 0x1F: ['Transform'], 0x22: ['HangGimmick'],
    0x23: ['HangGimmick'],
}
V5_TAILS = {0x20: ['Fly'], 0x21: ['Fly'], 0x22: [(0, 99, 'Fly Lift Down', 'Fly')],
            0x23: [(0, 99, 'Fly Lift', 'Fly')], 0x24: ['Fly Tired'], 0x25: ['Swim'], 0x26: ['Swim'],
            0x27: [(0, 99, 'Swim Lift', 'Swim')], 0x28: ['Swim Tired']}
V5_KNUX = {0x20: ['Glide'], 0x21: ['Glide Drop'], 0x22: ['Glide Land']}


# Looping animations, run on the Origins game's own frames: S3K animation (or
# T_*) -> (Origins name(s), kind). 'BALL:<name>' is that animation's solid
# ball alone (the drop dash of S1 / S2 / CD).
WALK, ROLL, FIXED, IDLE = 1, 2, 3, 4      # (IDLE: the first name's pose held, then the second's loop)
T_JOG, T_DASH, T_DROPDASH, TRACKS = 0x30, 0x31, 0x32, 64
TRACKS_V4 = {
    0x00: ('Walking', WALK), 0x01: ('Running', WALK), 0x02: ('Jumping', ROLL), 0x03: ('Jumping', ROLL),
    0x04: ('Pushing', FIXED), 0x06: (['Flailing 1', 'Flailing Left'], FIXED), 0x09: ('Spin Dash', FIXED),
    0x07: ('Looking Up', FIXED), 0x08: ('Looking Down', FIXED),          # (into the pose, then held)
    0x05: (('Stopped', 'Waiting'), IDLE),
    0x0C: (['Flailing 2', 'Flailing Right'], FIXED), 0x0D: ('Skidding', FIXED),
    0x0E: ('Fan Rotate', FIXED), 0x0F: ('Fan Rotate', FIXED), T_DROPDASH: ('BALL:Jumping', FIXED),
    0x10: ('Bouncing', FIXED),                                          # (Sonic CD's spring spin)
}
HOLD_LAST = (0x07, 0x08)                  # (look up, crouch: into the pose, then held)
# S3K's tumble (Anim_Tumble, flip_type 0: corkscrews and twirling springs):
# frames $31-$3C, one per 30 degrees of flip_angle. Sonic 2's "Twirl H" is
# the same twelve turns; Sonic 1's and CD's "Corkscrew H" entries point at
# the wrong part of their sheets, so those styles borrow Sonic 2's,
# recoloured to their own (Sonic 1's paler blue, CD's). Sonic 3's style
# keeps the cartridge's own (they are Sonic 3's).
TUMBLE_FIRST, TUMBLE_COUNT = 0x31, 12
TRACKS_DASH = {T_DASH: ('Super Peel Out', WALK)}   # (S1 / CD's figure eight; S2's entry is stale)
T_LIFT_TIRED = 0x3B                       # (tired carrying Sonic: Mania's own pose; S3K shows $24)
TRACKS_V4_TAILS = {0x20: ('Flying', FIXED), 0x21: ('Flying', FIXED), 0x22: (['Fly Lift Down', 'Flying'], FIXED),
                   0x23: (['Fly Lift Up', 'Flying'], FIXED), 0x24: ('Flying Tired', FIXED),
                   0x25: ('Swimming', FIXED), 0x26: ('Swimming', FIXED), 0x27: (['Swim Lift', 'Swimming'], FIXED),
                   0x28: ('Swimming Tired', FIXED), T_LIFT_TIRED: (['Fly Lift Tired', 'Flying Tired'], FIXED)}
TRACKS_V5 = {
    0x00: ('Walk', WALK), T_JOG: ('Jog', WALK), 0x01: ('Run', WALK), T_DASH: ('Dash', WALK),
    0x02: ('Jump', ROLL), 0x03: ('Jump', ROLL), 0x04: ('Push', FIXED), 0x06: ('Balance 1', FIXED),
    0x07: ('Look Up', FIXED), 0x08: ('Crouch', FIXED), 0x05: (('Idle', 'Bored 1'), IDLE),
    0x09: ('Spindash', FIXED), 0x0C: ('Balance 2', FIXED), 0x0D: ('Skid', FIXED), 0x0E: ('Fan', FIXED),
    0x0F: ('Fan', FIXED), T_DROPDASH: ('Dropdash', FIXED),
}
TRACKS_V5_TAILS = {0x20: ('Fly', FIXED), 0x21: ('Fly', FIXED), 0x22: (['Fly Lift Down', 'Fly'], FIXED),
                   0x23: (['Fly Lift', 'Fly'], FIXED), 0x24: ('Fly Tired', FIXED), 0x25: ('Swim', FIXED),
                   0x26: ('Swim', FIXED), 0x27: (['Swim Lift', 'Swim'], FIXED), 0x28: ('Swim Tired', FIXED),
                   T_LIFT_TIRED: (['Fly Lift Tired', 'Fly Tired'], FIXED)}
# Sonic CD's own pictures for its gimmicks, in every style (in the style's
# colours): Palmtree Panic's 3D ramps ("3D Ramp 1".."7": six angles of
# running, the 3D roll) and spinning discs ("Spinning Top": picked by turn)
T_RAMP, T_SPINTOP = 0x33, 0x3A
CD_TRACKS = [(T_RAMP + k, '3D Ramp %d' % (k + 1), WALK) for k in range(6)] + [
    (T_RAMP + 6, '3D Ramp 7', ROLL), (T_SPINTOP, 'Spinning Top', FIXED)]
STYLE_POSES = ('Walking', 'Walk', 'Running', 'Run', 'Jumping', 'Jump', 'Stopped', 'Idle')


# the players' animation tables in the retail S&K cartridge (AniSonic, AniTails,
# AniKnuckles): the scripts without the disassembly (AnthologyBuilder)
ANIM_ROM = {'Sonic': 0x012AA6, 'Tails': 0x015AB0, 'Knuckles': 0x017EF4}


def anim_scripts_rom(rom, ch):
    """anim_scripts, read from the cartridge: word offsets to each script
    (duration, frames.., $FC-$FF)"""
    if ch not in ANIM_ROM:
        return {}                                 # (Super Sonic: Sonic's, as without its source)
    t = ANIM_ROM[ch]
    sw = lambda a: ((rom[a] << 8 | rom[a + 1]) ^ 0x8000) - 0x8000
    offs, first = [], 0x10000
    while len(offs) * 2 < first and len(offs) < 0x80:
        offs.append(sw(t + len(offs) * 2))
        if offs[-1] > 0:
            first = min(first, offs[-1])
    out = {}
    for i, o in enumerate(offs):
        if o <= 0:
            continue
        frames, a = [], t + o + 1
        while rom[a] < 0xFC:
            if rom[a] not in frames:
                frames.append(rom[a])
            a += 1
        out[i] = frames
    return out


def anim_scripts(disasm, ch, rom=None):
    """{anim index: [distinct frames in script order]} from S3K's source (else the cartridge)"""
    path = Path(disasm) / 'General' / 'Sprites' / ANIM_FILES[ch]
    if not path.exists():
        return anim_scripts_rom(rom, ch) if rom is not None and not Path(disasm).exists() else {}
    label = ANIM_LABEL[ch]
    text = path.read_text(encoding='utf-8', errors='replace')
    body = {}
    cur = None
    for line in text.splitlines():
        line = line.split(';')[0]
        m = re.match(r'^(%s[0-9A-F]{2})\s*:?\s*(.*)$' % label, line.strip())
        if m and 'dc.w' not in m[2]:
            cur = m[1]
            body[cur] = []
            line = m[2]
        if cur is None or 'dc.b' not in line:
            continue
        for tok in line.split('dc.b', 1)[1].split(','):
            tok = tok.strip()
            if tok:
                body[cur].append(int(tok[1:], 16) if tok.startswith('$') else int(tok))
    out = {}
    for name, seq in body.items():
        idx = int(name[len(label):], 16)
        frames = []
        for b in seq[1:]:                         # (byte 0: duration or $FF/$FE/$FD)
            if b >= 0xFC:
                break
            if b not in frames:
                frames.append(b)
        out[idx] = frames
    return out


def classify(scripts):
    """frame -> (anim, position, distinct count); the first script wins"""
    info = {}
    for anim in sorted(scripts):
        frames = scripts[anim]
        for i, f in enumerate(frames):
            if f and f not in info:
                info[f] = (anim, i, len(frames))
    return info


RING_YELLOWS = [(0x15, (252, 252, 0)), (0x16, (144, 144, 0)), (0x1F, (72, 72, 0))]   # (line 1)


def nearest(rgb, pal, extra=()):
    """index 1..15 of pal (or an extra (value, rgb)) nearest to rgb"""
    best, bi = None, 1
    for i, c in [(i, pal[i]) for i in range(1, 16)] + list(extra):
        r, g, b = c
        d = 3 * (rgb[0] - r) ** 2 + 4 * (rgb[1] - g) ** 2 + 2 * (rgb[2] - b) ** 2
        if best is None or d < best:
            best, bi = d, i
    return bi


class Game:
    def __init__(self, data, game):
        self.dir = Path(data) / game
        self.style = game
        self.j = json.loads((self.dir / (game + '.json')).read_text(encoding='utf-8'))
        self.sheets = {}

    def anims(self, ch):
        files = self.j['groups']['players'].get(ch)
        if not files:
            return {}
        af = self.j['animation_files'][files[0]]
        a = af['animations']
        return a if isinstance(a, dict) else {x.get('name', str(i)): x for i, x in enumerate(a)}

    def sheet(self, name):
        if name not in self.sheets:
            s = self.j['sheets'][name]
            im = Image.open(self.dir / s['indexed_png'])
            pal = [tuple(c[:3]) for c in s['palette']]
            self.sheets[name] = (im, pal, s.get('transparent_index', 0))
        return self.sheets[name]


def is_ball(g, fr, pal):
    """a solid ball: none of the player's red, skin or white"""
    im, spal, tr = g.sheet(fr['sheet'])
    crop = im.crop((fr['x'], fr['y'], fr['x'] + fr['w'], fr['y'] + fr['h']))
    px = [v for v in crop.get_flattened_data() if v != tr]
    other = sum(1 for v in px if nearest(spal[v], pal) in (1, 6, 7, 0xA, 0xB))
    return px and other < len(px) * 0.03


def frame_for(anims, spec, pos, count, g=None, pal=None, first=None):
    """the Origins frame for S3K position pos of count, per the spec list"""
    if spec and isinstance(spec[0], str) and spec[0].startswith('ROLL:'):
        a = anims.get(spec[0][5:])
        if not a or not a.get('frames'):
            return None
        n = a.get('frame_count_effective') or len(a['frames'])
        frames = a['frames'][:max(1, min(n, len(a['frames'])))]
        balls = [f for f in frames if is_ball(g, f, pal)]
        curls = [f for f in frames if f not in balls]
        if pos == 0 and first == 0x96 and balls:
            return balls[0]
        pick = curls or frames
        k = max(0, pos - 1) if first == 0x96 else pos
        return pick[min(len(pick) - 1, k * len(pick) // max(1, count - (1 if first == 0x96 else 0)))]
    for item in spec:
        if isinstance(item, str):
            names = [item]
        else:
            lo, hi, *names = item
            if not (lo <= pos <= hi):
                continue
            pos, count = pos - lo, min(hi, count - 1) - lo + 1
        for name in names:
            a = anims.get(name)
            if not a or not a.get('frames'):
                continue
            n = a.get('frame_count_effective') or len(a['frames'])
            n = max(1, min(n, len(a['frames'])))
            return a['frames'][min(n - 1, pos * n // max(1, count))]
        if not isinstance(item, str):
            return None
    return None


RING_WORDS = {0x15: 0x0EE, 0x16: 0x088, 0x1F: 0x044}


class Pack:
    """the pictures, deduplicated, each with its own colours"""
    def __init__(self):
        self.images, self.index = [], {}

    def add_rgba(self, key, img, ox, oy, pal, words, extra=()):
        """pal: S3K's line 0 as RGB, words: the same as palette words"""
        if key in self.index:
            return self.index[key]
        colours, slot, px = [], {}, bytearray()
        for r, g, b, a in img.get_flattened_data():
            if a < 128:
                px.append(0)
                continue
            if (r, g, b) not in slot:
                role = nearest((r, g, b), pal, extra)
                base = words[role] if role < 16 else RING_WORDS[role]
                if len(colours) >= 255:          # (more colours than a byte: share the nearest)
                    slot[(r, g, b)] = min(range(len(colours)), key=lambda i: sum((x - y) ** 2 for x, y in zip(colours[i][:3], (r, g, b)))) + 1
                else:
                    colours.append((r, g, b, role, base))
                    slot[(r, g, b)] = len(colours)
            px.append(slot[(r, g, b)])
        self.index[key] = len(self.images)
        self.images.append((img.width, img.height, ox, oy, colours, bytes(px)))
        return self.index[key]


def rgba(g, fr):
    s = g.j['sheets'][fr['sheet']]
    key = ('rgba', fr['sheet'])
    if key not in g.sheets:
        g.sheets[key] = Image.open(g.dir / s['png']).convert('RGBA')
    return g.sheets[key].crop((fr['x'], fr['y'], fr['x'] + fr['w'], fr['y'] + fr['h']))


def compose(parts):
    """[(image, left, top)] -> (image, left, top) covering them all"""
    l = min(x for _, x, _ in parts)
    t = min(y for _, _, y in parts)
    r = max(x + im.width for im, x, _ in parts)
    b = max(y + im.height for im, _, y in parts)
    out = Image.new('RGBA', (r - l, b - t), (0, 0, 0, 0))
    for im, x, y in parts:
        out.alpha_composite(im, (x - l, y - t))
    return out, l, t


def script_frames(g, name):
    so = (g.j.get('script_objects') or {}).get(name)
    return so['frames'] if so else []


def v5_anim(g, file, name):
    a = g.j['animation_files'][file]['animations'].get(name)
    return a['frames'] if a else []


def object_tables(pack, games, pal, words, rom):
    """the Monitor, Monitor icon and Star post slots, per style"""
    sb = lambda a: rom[a] - 256 if rom[a] & 0x80 else rom[a]
    w16 = lambda a: rom[a] << 8 | rom[a + 1]
    sw = lambda a: w16(a) - 0x10000 if w16(a) & 0x8000 else w16(a)

    def s3k_bounds(mp, f):           # (left, top, right, bottom) of an S3K mapping frame
        m = mp + sw(mp + f * 2)
        xs, ys = [], []
        for i in range(w16(m)):
            q = m + 2 + i * 6
            y, sz, x = sb(q), rom[q + 1], sw(q + 4)
            xs += [x, x + ((sz >> 2 & 3) + 1) * 8]
            ys += [y, y + ((sz & 3) + 1) * 8]
        return min(xs), min(ys), max(xs), max(ys)

    out = {}
    for style in STYLES:
        g = games[style]
        mon, icon, post = [0xFFFF] * 256, [0xFFFF] * 256, [0xFFFF] * 256
        v5 = style == 'sonic3k'
        roles = ICONS[style]
        if v5:
            box = v5_anim(g, '3K_Global/ItemBox.bin', 'Normal')[0]
            broken = v5_anim(g, '3K_Global/ItemBox.bin', 'Broken')[0]
            ups = v5_anim(g, '3K_Global/ItemBox.bin', 'Powerups')
            snow = v5_anim(g, '3K_Global/ItemBox.bin', 'Snow')[0]
            snow2 = v5_anim(g, '3K_Global/ItemBox.bin', 'Snow')[2]
            icon_of = lambda role: snow if role == 'static' else snow2 if role == 'static2' else ups[roles[role]]
        else:
            frames = script_frames(g, 'Monitor')
            box = frames[roles['box']]
            broken = script_frames(g, 'BrokenMonitor')[0]
            icon_of = lambda role: frames[roles[role]] if roles.get(role) is not None else None
        box_im = rgba(g, box)
        screen = None
        # icons centre on the style's own screen: Sonic 1 / 2 draw theirs at
        # their pivot over the box (centre (0, -2)), S3K's sits at (0, -5)
        centre = (0, -5) if v5 else (0, -2)
        if style == 'soniccd':       # (complete monitors: their icon fills this part of the screen)
            screen = (7, 7, 23, 21)
            centre = (box['pivot_x'] + (screen[0] + screen[2]) // 2, box['pivot_y'] + (screen[1] + screen[3]) // 2)
        for f, role in ICON_ROLES.items():
            fr = icon_of(role)
            src = g
            if fr is None:           # (borrowed from Sonic 2)
                src, fr = games['sonic2'], script_frames(games['sonic2'], 'Monitor')[ICONS['sonic2'][role]]
            im = rgba(src, fr)
            if screen and src is g:
                im = im.crop(screen)
            ix, iy = centre[0] - im.width // 2, centre[1] - im.height // 2
            icon[f] = pack.add_rgba((style, 'icon', f, src is g), im, ix, iy, pal, words, RING_YELLOWS)
            if screen and src is g:
                whole = rgba(g, fr)
                mon[f] = pack.add_rgba((style, 'mon', f), whole, box['pivot_x'], box['pivot_y'], pal, words, RING_YELLOWS)
            else:
                pic, l, t = compose([(box_im, box['pivot_x'], box['pivot_y']), (im, ix, iy)])
                mon[f] = pack.add_rgba((style, 'mon', f), pic, l, t, pal, words, RING_YELLOWS)
        b_im = rgba(g, broken)
        bl, bt, br, bb = s3k_bounds(0x1DBA2, 11)
        mon[11] = pack.add_rgba((style, 'broken'), b_im, -b_im.width // 2, bb - b_im.height, pal, words, RING_YELLOWS)
        # star posts: frames 0 / 1 stand on S3K's foot, heads centre on it
        if v5:
            full, pole = v5_anim(g, '3K_Global/StarPost.bin', 'Post')[1], v5_anim(g, '3K_Global/StarPost.bin', 'Post')[0]
            unlit, lit = v5_anim(g, '3K_Global/StarPost.bin', 'Bulb Unused')[0], v5_anim(g, '3K_Global/StarPost.bin', 'Bulb Used')[0]
        else:
            pf = script_frames(g, 'LampPost') or script_frames(g, 'StarPost')
            full, pole, unlit, lit = (pf[i] for i in POSTS[style])
        for f, fr in ((0, full), (1, pole)):
            im = rgba(g, fr)
            l, t, r, b = s3k_bounds(0x2D348, f)
            post[f] = pack.add_rgba((style, 'post', f), im, -im.width // 2, b - im.height, pal, words, RING_YELLOWS)
        for f, fr in ((2, unlit), (3, lit)):
            im = rgba(g, fr)
            l, t, r, b = s3k_bounds(0x2D348, f)
            post[f] = pack.add_rgba((style, 'post', f), im, (l + r) // 2 - im.width // 2, (t + b) // 2 - im.height // 2, pal, words, RING_YELLOWS)
        # frame 4, the post with its head lit (the activated post blinks 0 / 4):
        # the pole with the lit head where the full post's unlit one sits
        full_im, pole_im, lit_im = rgba(g, full), rgba(g, pole), rgba(g, lit)
        b = s3k_bounds(0x2D348, 0)[3]
        pic, l, t = compose([(pole_im, -pole_im.width // 2, s3k_bounds(0x2D348, 1)[3] - pole_im.height),
                             (lit_im, -lit_im.width // 2, b - full_im.height)])
        post[4] = pack.add_rgba((style, 'post', 4), pic, l, t, pal, words, RING_YELLOWS)
        out[style] = (mon, icon, post)
    return out


def ticks_of(a):
    """an Origins animation's frames-per-picture at its own speed"""
    if a.get('ticks_per_frame'):
        return max(1, round(a['ticks_per_frame']))
    sp = a.get('speed') or 0
    durs = [f.get('duration') for f in a['frames'] if f.get('duration')]
    if sp and durs:
        return max(1, round(sum(durs) / len(durs) / sp))
    return max(1, round(240 / sp)) if sp else 8


def build_tracks(pack, g, ch, style, pal, words, seq):
    """the 64 tracks of one player in one style"""
    v5 = style == 'sonic3k'
    spec = dict(TRACKS_V5 if v5 else TRACKS_V4)
    if style in ('sonic1', 'soniccd'):
        spec.update(TRACKS_DASH)
    if ch == 'Tails':
        spec.update(TRACKS_V5_TAILS if v5 else TRACKS_V4_TAILS)
    anims = g.anims(ch)
    tracks = [(0, 0, 0, 0, 0)] * TRACKS
    for t, (names, kind) in spec.items():
        if kind == IDLE:                                    # (the pose, then the waiting loop)
            stand, wait = anims.get(names[0]), anims.get(names[1])
            if not stand or not stand.get('frames') or not wait or not wait.get('frames'):
                continue
            n = wait.get('frame_count_effective') or len(wait['frames'])
            frames = [stand['frames'][0]] + [f for f in wait['frames'][:max(1, min(n, len(wait['frames'])))]
                                             if f.get('sheet') and f['w'] and f['h']]
            first = len(seq)
            for fr in frames:
                key = (style, fr['sheet'], fr['x'], fr['y'], fr['w'], fr['h'], fr['pivot_x'], fr['pivot_y'], ch)
                seq.append(pack.add_rgba(key, rgba(g, fr), fr['pivot_x'], fr['pivot_y'], pal, words))
            loop = 1 + (wait.get('loop_index') or 0)
            tracks[t] = (first, len(frames), kind, min(255, ticks_of(wait)), loop if loop < len(frames) else 1)
            continue
        a = None
        for name in ([names] if isinstance(names, str) else names):
            ball = name.startswith('BALL:')
            a = anims.get(name[5:] if ball else name)
            if a and a.get('frames'):
                break
            a = None
        if not a:
            continue
        n = a.get('frame_count_effective') or len(a['frames'])
        frames = [f for f in a['frames'][:max(1, min(n, len(a['frames'])))] if f.get('sheet') and f['w'] and f['h']]
        if ball:
            frames = [f for f in frames if is_ball(g, f, pal)][:1]
        if not frames:
            continue
        first = len(seq)
        for fr in frames:
            key = (style, fr['sheet'], fr['x'], fr['y'], fr['w'], fr['h'], fr['pivot_x'], fr['pivot_y'], ch)
            seq.append(pack.add_rgba(key, rgba(g, fr), fr['pivot_x'], fr['pivot_y'], pal, words))
        loop = a.get('loop_index') or 0
        if t in HOLD_LAST:
            loop = len(frames) - 1
        tracks[t] = (first, len(frames), kind, min(255, ticks_of(a)), loop if loop < len(frames) else 0)
    return tracks


def style_colours(g, ch, pal):
    """the colours a style draws a player in: palette entry -> [(rgb, pixels)]"""
    seen = {}
    anims = g.anims(ch)
    for name in STYLE_POSES:
        for fr in (anims.get(name) or {}).get('frames', []):
            if fr.get('sheet') and fr['w'] and fr['h']:
                for r, gr, b, al in rgba(g, fr).get_flattened_data():
                    if al >= 128:
                        seen[(r, gr, b)] = seen.get((r, gr, b), 0) + 1
    out = {}
    for rgb, n in seen.items():
        out.setdefault(nearest(rgb, pal), []).append((rgb, n))
    return out


def recolour(im, colours, pal):
    """a picture in another style's colours: each colour becomes the nearest of
    the colours that style gives the same palette entry"""
    near = lambda a, b: 3 * (a[0] - b[0]) ** 2 + 4 * (a[1] - b[1]) ** 2 + 2 * (a[2] - b[2]) ** 2
    swap, px = {}, []
    for r, g, b, a in im.get_flattened_data():
        if a < 128:
            px.append((0, 0, 0, 0))
            continue
        c = (r, g, b)
        if c not in swap:
            cands = colours.get(nearest(c, pal))
            swap[c] = min(cands, key=lambda t: near(t[0], c))[0] if cands else c
        px.append(swap[c] + (255,))
    out = Image.new('RGBA', im.size)
    out.putdata(px)
    return out


def cd_tracks(pack, games, ch, style, pal, words, seq, tracks):
    """the CD_TRACKS: Sonic CD's gimmick pictures (Super Sonic: Sonic's)"""
    cd = games['soniccd']
    anims = cd.anims('Sonic' if ch == 'SuperSonic' else ch)
    g = games[style]
    colours = None
    if style != 'soniccd':
        colours = style_colours(g, ch if g.anims(ch) else 'Sonic', pal)
    for t, name, kind in CD_TRACKS:
        a = anims.get(name)
        frames = [f for f in (a or {}).get('frames', []) if f.get('sheet') and f['w'] and f['h']]
        if not frames:
            continue
        first = len(seq)
        for fr in frames:
            im = rgba(cd, fr)
            if colours is not None:
                im = recolour(im, colours, pal)
            key = ('ramp', style, ch, fr['sheet'], fr['x'], fr['y'], fr['w'], fr['h'], fr['pivot_x'], fr['pivot_y'])
            seq.append(pack.add_rgba(key, im, fr['pivot_x'], fr['pivot_y'], pal, words))
        loop = a.get('loop_index') or 0
        tracks[t] = (first, len(frames), kind, min(255, ticks_of(a)), loop if loop < len(frames) else 0)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default=r'C:\Users\hyper\scd_ref\origins')
    ap.add_argument('--rom', default=str(ROOT / 'game' / 'sonic3k' / 'sonic3k.bin'))
    ap.add_argument('--disasm', default=str(ROOT / 'game' / 'skdisasm'))
    ap.add_argument('--out', default=str(ROOT / 'build' / 'Release' / 'origins.bin'))
    ap.add_argument('--check', help='write side-by-side check sheets here')
    a = ap.parse_args(argv)
    rom = Path(a.rom).read_bytes()
    md = lambda c: ((c >> 1 & 7) * 36, (c >> 5 & 7) * 36, (c >> 9 & 7) * 36)
    pals = {ch: [md(rom[p + i * 2] << 8 | rom[p + i * 2 + 1]) for i in range(16)] for ch, p in PALETTES.items()}
    words = {ch: [rom[p + i * 2] << 8 | rom[p + i * 2 + 1] for i in range(16)] for ch, p in PALETTES.items()}

    pack = Pack()
    images = pack.images
    tables = []
    report = []
    games = {style: Game(a.data, style) for style in STYLES}
    objects = object_tables(pack, games, pals['Sonic'], words['Sonic'], rom)
    tracks, seq = [], []
    for style in STYLES:
        v5 = style == 'sonic3k'
        for ch in CHARS:
            g = games[style]
            if ch == 'SuperSonic' and not g.anims(ch):
                g = games['sonic2']                   # (Sonic CD has no Super Sonic of its own: Sonic 2's)
            table = [0xFFFF] * 256
            scripts = anim_scripts(a.disasm, 'Sonic' if ch == 'SuperSonic' else ch, rom)
            if ch == 'SuperSonic':
                scripts = anim_scripts(a.disasm, ch, rom) or scripts
            spec = dict(V5 if v5 else V4)
            spec.update((V5_TAILS if v5 else V4_TAILS) if ch == 'Tails' else {})
            spec.update((V5_KNUX if v5 else V4_KNUX) if ch == 'Knuckles' else {})
            if ch == 'Tails':
                spec.pop(0x0C, None)                  # (Tails never balances with $0C: its $A4 is $24's, tired)
            info = classify({k: v for k, v in scripts.items() if k in spec})   # (only what is drawn owns frames)
            anims = g.anims(ch)
            mapped = 0
            for f, (anim, pos, count) in info.items():
                if anim not in spec or not anims:
                    continue
                fr = frame_for(anims, spec[anim], pos, count, g, pals[ch], scripts[anim][0])
                if not fr or not fr.get('sheet'):
                    continue
                key = (g.style, fr['sheet'], fr['x'], fr['y'], fr['w'], fr['h'], fr['pivot_x'], fr['pivot_y'], ch)
                table[f] = pack.add_rgba(key, rgba(g, fr), fr['pivot_x'], fr['pivot_y'], pals[ch], words[ch])
                mapped += 1
            if not v5:
                src = g
                tw = (g.anims(ch).get('Twirl H') or {}).get('frames') or []
                if len(tw) < TUMBLE_COUNT:
                    src = games['sonic2']
                    tw = (src.anims(ch).get('Twirl H') or {}).get('frames') or []
                # (borrowed: in this style's own colours, as its other pictures)
                colours = style_colours(g, ch, pals[ch]) if src is not g and g.anims(ch) else None
                for i, fr in enumerate(tw[:TUMBLE_COUNT]):
                    if not fr.get('sheet'):
                        continue
                    im = rgba(src, fr)
                    if colours:
                        im = recolour(im, colours, pals[ch])
                    key = (src.style, fr['sheet'], fr['x'], fr['y'], fr['w'], fr['h'], fr['pivot_x'], fr['pivot_y'], ch,
                           style if colours else None)
                    table[TUMBLE_FIRST + i] = pack.add_rgba(key, im, fr['pivot_x'], fr['pivot_y'], pals[ch], words[ch])
                    mapped += 1
            tables.append(table)
            tr = build_tracks(pack, g, ch, style, pals[ch], words[ch], seq)
            cd_tracks(pack, games, ch, style, pals[ch], words[ch], seq, tr)
            tracks.append(tr)
            report.append('%-8s %-10s %3d frames mapped, %2d tracks' % (style, ch, mapped, sum(1 for x in tr if x[2])))
        tables += list(objects[style])

    out = bytearray(b'TORG' + struct.pack('<HH', 3, len(STYLES)))
    for t in tables:
        out += struct.pack('<256H', *t)
    for tr in tracks:
        for first, count, kind, ticks, loop in tr:
            out += struct.pack('<HBBBBH', first, count, kind, ticks, loop, 0)
    out += struct.pack('<I', len(seq)) + struct.pack('<%dH' % len(seq), *seq)
    out += struct.pack('<I', len(images))
    for w, h, ox, oy, colours, px in images:
        out += struct.pack('<hhhhH', w, h, ox, oy, len(colours))
        for r, g, b, role, base in colours:
            out += struct.pack('<BBBBH', r, g, b, role, base)
        out += px
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_bytes(out)
    print('\n'.join(report))
    print('%d images, %d bytes -> %s' % (len(images), len(out), a.out))

    pack_ufo(a.data, Path(a.out).with_name('origins_ufo.bin'))
    if a.check:
        check_sheets(a, rom, tables, images, pals)


# Sonic CD's UFO stages with Tails or Knuckles: Origins' SpecialSonic frames
# (frames_by_character, its own order), one per CD art frame (Sonic's DMA'd
# frame, ObjSonic_LoadArt's oSpriteFlag), matched by pose within the same
# kind of animation (walk, lean, sprint, jump, fan, trip, booster, the turn to
# the camera) and aligned to CD's sprite origin: art -> (frame, dx, dy).
UFO_FRAMES = {
    0: (9, 2, 0), 1: (10, -3, 1), 2: (11, 4, 1), 3: (21, 1, 0), 4: (22, 0, 0), 5: (23, 0, 0),
    6: (24, -1, 0), 7: (25, 0, 0), 8: (26, 0, 0), 9: (39, 3, 1), 10: (40, 0, -1), 11: (41, -1, 0),
    12: (42, -1, 0), 14: (54, 2, 0), 15: (55, 2, 0), 16: (56, -1, 0), 17: (57, 2, 0), 18: (74, 1, 0),
    19: (75, 0, 0), 20: (76, 2, 0), 21: (53, 0, 1), 22: (52, 0, -1), 23: (51, -3, 0), 24: (52, 0, -1),
    25: (44, 2, 0), 26: (45, 1, 0), 27: (43, 0, 0), 28: (46, -4, 2), 29: (47, 3, -1), 30: (47, 6, 0),
    31: (15, 1, 0), 32: (16, 1, 0), 33: (17, 1, 0), 34: (18, 1, 0), 35: (19, 1, 0), 36: (20, 1, 0),
    37: (77, 1, 0), 38: (78, 1, 0), 39: (9, 0, 1), 40: (79, -1, -3), 41: (1, 0, 0), 42: (3, 0, 0),
    43: (2, 0, 0), 44: (2, 0, 0), 45: (5, 0, 0), 46: (6, 0, 0), 47: (7, 0, 0), 48: (8, -1, -1),
    49: (8, 0, -5), 50: (84, 1, 0), 51: (84, 1, 0), 52: (84, 1, 1), 53: (84, 1, 0), 54: (81, 1, 4),
    55: (82, -2, -6), 56: (83, 0, 0), 57: (84, -1, 0), 58: (85, -1, 0), 59: (86, -1, 0),
    60: (87, 2, 0), 61: (88, 3, 0), 62: (89, 1, 0), 63: (90, 1, 0)
}
UFO_CHARS = ('tails', 'knuckles')


def pack_ufo(data, out):
    """origins_ufo.bin: "TUFO", u16 1, u16 chars, u16 64 arts; chars x 64 u16
    image (0xFFFF: none); u32 images; per image i16 w, h, ox, oy (from the
    sprite's position), u16 colours, colours x (r, g, b), w*h bytes (0 clear)"""
    g = Game(data, 'soniccd')
    so = g.j['script_objects']['SpecialSonic']
    fbc = so['frames_by_character']
    images, table = [], []
    for ch in UFO_CHARS:
        row = [0xFFFF] * 64
        for art, (fr, dx, dy) in UFO_FRAMES.items():
            f = fbc[ch][fr]
            im = rgba(g, f)
            cols, px = [], bytearray()
            for p in im.getdata():
                if p[3] < 128:
                    px.append(0)
                    continue
                c = p[:3]
                if c not in cols:
                    cols.append(c)
                px.append(cols.index(c) + 1 if len(cols) < 255 else 1)
            row[art] = len(images)
            images.append((im.width, im.height, f['pivot_x'] + dx, f['pivot_y'] + dy, cols, bytes(px)))
        table.append(row)
    out_b = bytearray(b'TUFO' + struct.pack('>HHH', 1, len(UFO_CHARS), 64))
    for row in table:
        out_b += struct.pack('>64H', *row)
    out_b += struct.pack('>I', len(images))
    for w, h, ox, oy, cols, px in images:
        out_b += struct.pack('>hhhhH', w, h, ox, oy, len(cols))
        for c in cols:
            out_b += bytes(c)
        out_b += px
    Path(out).write_bytes(out_b)
    print('%d UFO-stage pictures -> %s' % (len(images), out))


def check_sheets(a, rom, tables, images, pals):
    """S3K frame beside each style's choice, per character (for review)"""
    import sys
    sys.path.insert(0, str(ROOT / 'scratch'))
    from PIL import ImageDraw
    try:
        import player_frames as pf
    except ImportError:
        return
    Path(a.check).mkdir(parents=True, exist_ok=True)
    for ci, ch in enumerate(CHARS[:3]):
        frames = [f for f in range(1, 251) if any(tables[si * len(SLOTS) + ci][f] != 0xFFFF for si in range(4))]
        cell = 56
        S = Image.new('RGB', (5 * cell, len(frames) * cell), (60, 60, 60))
        d = ImageDraw.Draw(S)
        key = {'Sonic': 'sonic', 'Tails': 'tails', 'Knuckles': 'knux'}[ch]
        for r, f in enumerate(frames):
            im = pf.frame(key, f, cell)
            S.paste(im, (0, r * cell), im)
            d.text((1, r * cell), '%02X' % f, fill=(255, 255, 0))
            for si in range(4):
                t = tables[si * len(SLOTS) + ci][f]
                if t == 0xFFFF:
                    continue
                w, h, ox, oy, colours, px = images[t]
                pic = Image.new('RGBA', (w, h))
                pic.putdata([(0, 0, 0, 0) if v == 0 else colours[v - 1][:3] + (255,) for v in px])
                S.paste(pic, ((si + 1) * cell + cell // 2 + ox, r * cell + cell // 2 + oy), pic)
        S.save(Path(a.check) / ('check_%s.png' % key))


if __name__ == '__main__':
    main()
