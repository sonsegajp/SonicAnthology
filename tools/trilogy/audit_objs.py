"""Object audit: every Sonic 2 act, object by object, against the real cartridge.

  python tools/trilogy/audit_objs.py run  ZONE ACT [--step N]   both sides for one act
  python tools/trilogy/audit_objs.py cmp  ZONE ACT              the report (scratch/audit/ZONEn.txt)
  python tools/trilogy/audit_objs.py all  [--jobs N]            every act, then every report

ZONE = EHZ CPZ ...; ACT 0-based. Sample points walk the act's object layout
one screen at a time. At each, both games restart the act as if from a star
post there (S2's Obj79_LoadData restore / S3K's, the trilogy's
TRILOGY_AUDIT hook) and at level frame 90 the object tables are taken, with a
screenshot. Layout objects pair up by their respawn entry (each game numbers
every layout object), the others (children: debris, projectiles, parts) by
S2 object id. The report lists, per point: layout objects one game has and
the other not, paired objects whose position, frame or visibility differ,
and child counts that differ.
"""
import ctypes as C
import os
import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import s2_level  # noqa: E402
import s2_objects  # noqa: E402

OUT = ROOT / 'scratch' / 'audit'
EXE = ROOT / 'build' / 'Release'
NATIVE_S2 = s2_level.NATIVE_S2
STEP = 0x180                      # one sample point per this much layout x
FRAME = 90                        # level frame the tables are taken at
SCREEN_W = 0x140


def acts():
    for name, (zone, lst) in s2_level.S2_ZONES.items():
        for a in range(len(lst)):
            yield name, a


def placements(name, act):
    rom = Path('C:/Users/hyper/roms/Sonic The Hedgehog 2 (World).md').read_bytes()
    s = object.__new__(s2_level.Sonic2)
    s.rom = rom
    z, a = s2_level.S2_ZONES[name][1][act]
    return s.placements(z, a)


def points(name, act, step=STEP):
    """(x, y) sample points: one per `step` of the layout, at the first object's height."""
    pts, last = [], -0x10000
    for x, yw, oid, sub in placements(name, act):
        if x - last < step:
            continue
        y = yw & 0xFFF
        pts.append((max(0xA0, x + 0x40), max(0x60, y - 0x20)))
        last = x
    return pts


# ---- the cartridge -----------------------------------------------------------
def run_ref(name, act, pts):
    import s2ref
    sym = s2_objects.symbols()
    rom = Path(s2ref.S2ROM).read_bytes()
    ref = s2ref.S2Ref(s2ref.CORE, s2ref.S2ROM, workdir=str(s2ref.HERE))
    for f in range(1500):                          # SEGA, title, Start into EHZ1
        ref.step('S' if f % 40 < 3 else '')
        if ref.r8(sym['Game_Mode']) == 0x0C and f > 100:
            break
    for _ in range(30):
        ref.step('')
    z, s2act = s2_level.S2_ZONES[name][1][act]
    be16 = lambda a: (rom[a] << 8) | rom[a + 1]
    sa = s2_level.S2['LevelSize'] + (z * 2 + s2act) * 8
    left, right, top, bottom = (be16(sa + i * 2) for i in range(4))
    for i, (x, y) in enumerate(pts):
        ref.w16(sym['Current_ZoneAndAct'], z << 8 | s2act)
        for k, v in (('Saved_x_pos', x), ('Saved_y_pos', y), ('Saved_Ring_count', 0),
                     ('Saved_art_tile', 0x0780), ('Saved_Solid_bits', 0x0C0D),
                     ('Saved_Camera_X_pos', max(left, min(right, x - 0xA0))),
                     ('Saved_Camera_Y_pos', max(top, min(bottom, y - 0x60))),
                     ('Saved_Camera_BG_X_pos', 0), ('Saved_Camera_BG_Y_pos', 0),
                     ('Saved_Camera_BG2_X_pos', 0), ('Saved_Camera_BG2_Y_pos', 0),
                     ('Saved_Camera_BG3_X_pos', 0), ('Saved_Camera_BG3_Y_pos', 0),
                     ('Saved_Camera_Max_Y_pos', bottom)):
            ref.w16(sym[k], v)
        ref.w8(sym['Saved_Last_star_pole_hit'], 1)
        ref.w8(sym['Last_star_pole_hit'], 1)
        ref.w8(sym['Saved_Dynamic_Resize_Routine'], 0)
        ref.w16(sym['Level_Inactive_flag'], 1)
        fc = sym['Level_frame_counter'] & 0xFFFF
        for t in range(1200):                      # the restart, its title card, then level frame FRAME
            ref.w8(0xFFB030, 0x1C)                 # (Sonic can't be hurt)
            ref.step('')
            r = ref.ram()
            if t > 10 and r[sym['Game_Mode'] & 0xFFFF] == 0x0C and (r[fc] << 8 | r[fc + 1]) >= FRAME:
                break
        ram = ref.ram()
        cam = struct.unpack_from('>HH', ram, sym['Camera_X_pos'] & 0xFFFF)
        cam = (cam[0], struct.unpack_from('>H', ram, sym['Camera_Y_pos'] & 0xFFFF)[0])
        lines = ['point %X %X camera %X %X' % (x, y, cam[0], cam[1])]
        for s in range(0x80):
            o = 0xB000 + s * 0x40
            oid = ram[o]
            if not oid:
                continue
            lines.append('%d %02X %02X %04X %04X %02X %02X %02X %02X %02X' % (
                s, oid, ram[o + 0x23], struct.unpack_from('>H', ram, o + 8)[0], struct.unpack_from('>H', ram, o + 0xC)[0],
                ram[o + 0x1A], ram[o + 1], ram[o + 0x24], ram[o + 0x28], ram[o + 0x22]))
        (OUT / f'{name}{act + 1}_ref_{i:02d}.txt').write_text('\n'.join(lines) + '\n')
        ref.png(str(OUT / f'{name}{act + 1}_ref_{i:02d}.png'))


# ---- ours ------------------------------------------------------------------------
def run_ours(name, act, pts):
    zone = s2_level.S2_ZONES[name][0]
    pf = OUT / f'{name}{act + 1}_pts.txt'
    pf.write_text(''.join('%X %X\n' % p for p in pts))
    script = OUT / 'run.txt'
    script.write_text((ROOT / 'scratch' / 'boot.inc').read_text() + 'WAIT 60000\nEXIT 0\n')
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', LNG_SCRIPT='1', LNG_TEST_HIDDEN='1', TRILOGY_GOD='1', TRILOGY_ZONE='%02X%02X' % (zone, act),
               TRILOGY_AUDIT=str(pf), TRILOGY_AUDIT_OUT=str(OUT / f'{name}{act + 1}_ours'))
    subprocess.run([str(EXE / 'Sonic3KRecomp.exe'), '--no-launcher', '--turbo', '--input-script', str(script),
                    'sonic3k.bin'], cwd=str(EXE), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=3600)


# ---- the comparison ----------------------------------------------------------------
def load(path):
    rows = []
    lines = path.read_text().splitlines()
    head = lines[0].split()
    for l in lines[1:]:
        f = l.split()
        rows.append(dict(slot=int(f[0]), code=int(f[1], 16), resp=int(f[2], 16), x=int(f[3], 16), y=int(f[4], 16),
                         frame=int(f[5], 16), render=int(f[6], 16), routine=int(f[7], 16), sub=int(f[8], 16),
                         status=int(f[9], 16)))
    return (int(head[4], 16), int(head[5], 16)), rows


S3K_NAMES = {}


def s3k_name(code):
    if not S3K_NAMES:
        for l in (ROOT / 'game' / 'trilogy' / 'trilogy_syms.h').read_text().splitlines():
            p = l.split()
            if len(p) >= 3 and p[0] == '#define' and p[1].startswith('S3K_Obj'):
                S3K_NAMES[int(p[2].rstrip('u'), 16)] = p[1][4:]
    return S3K_NAMES.get(code, 'S3K:%06X' % code)


def ours_id(code):
    if NATIVE_S2 <= code < NATIVE_S2 + 0x400 and not code & 3:
        return (code - NATIVE_S2) >> 2
    return None


def calibrate(objs, lay, key):
    """layout index offset that best lines respawn numbers up with layout x positions"""
    best, bo = -1, 0
    for off in range(-8, 9):
        n = sum(1 for o in objs if key(o) is not None and 0 <= key(o) + off < len(lay) and lay[key(o) + off][0] == o['x'])
        if n > best:
            best, bo = n, off
    return bo


def compare(name, act):
    z, a = s2_level.S2_ZONES[name][1][act]
    pl = placements(name, act)
    lay_s2 = pl                                         # S2's own order
    s3 = sorted(pl, key=lambda o: o[0])                 # (s2_level.objects: sorted by x)
    pf = OUT / f'{name}{act + 1}_pts.txt'
    pts = [tuple(int(v, 16) for v in l.split()) for l in pf.read_text().splitlines()] if pf.exists() else points(name, act)
    rep = [f'{name} {act + 1}: {len(pts)} points, {len(pl)} layout objects']
    totals = Counter()
    for i in range(len(pts)):
        fr, fo = OUT / f'{name}{act + 1}_ref_{i:02d}.txt', OUT / f'{name}{act + 1}_ours_{i:02d}.txt'
        if not fr.exists() or not fo.exists():
            rep.append(f'  point {i}: missing data ({"ref" if not fr.exists() else "ours"})')
            continue
        camr, ref = load(fr)
        camo, ours = load(fo)
        ref = [o for o in ref if o['code'] not in (0x01, 0x02) and o['slot'] >= 2]          # players
        ours = [o for o in ours if o['slot'] >= 2 and o['code'] not in (0,)]
        # ours: every layout object carries its layout entry (S3K numbers them all),
        # the native ones their S2 id; S2 numbers only the objects that remember
        # state, so its side goes by id. Pairs: same S2 id, nearest position.
        resp_base = min((o['resp'] for o in ours if o['resp']), default=0) & 0xFF00
        oours = calibrate(ours, s3, lambda o: (o['resp'] - resp_base) if o['resp'] else None)
        for o in ours:
            o['id'] = ours_id(o['code'])
            k = o['resp'] - resp_base + oours if o['resp'] else -1
            if 0 <= k < len(s3):
                o['id'] = s3[k][2]                     # (S3K-run layout objects: monitor, spring, star post...)
        for o in ref:
            o['id'] = o['code']
        view = lambda o: abs(o['x'] - camr[0] - SCREEN_W // 2) < 0x180 and abs(o['y'] - camr[1] - 0x70) < 0x140
        R = [o for o in ref if view(o)]
        O = [o for o in ours if view(o) and o['id'] is not None]
        pairs, used = [], set()
        for r in sorted(R, key=lambda o: (o['id'], o['x'], o['y'])):
            best, bd = None, 0x60
            for j, o in enumerate(O):
                if j in used or o['id'] != r['id']:
                    continue
                d = abs(o['x'] - r['x']) + abs(o['y'] - r['y'])
                if d < bd:
                    best, bd = j, d
            if best is None:
                pairs.append((r, None))
            else:
                used.add(best)
                pairs.append((r, O[best]))
        pairs += [(None, o) for j, o in enumerate(O) if j not in used]
        out = []
        for r, o in pairs:
            k = (r or o)['id']
            if r and not o:
                out.append(f'    MISSING in ours  {k:02X} at {r["x"]:X},{r["y"]:X} (frame {r["frame"]:X} sub {r["sub"]:X} routine {r["routine"]:X})')
                totals['missing', k] += 1
            elif o and not r:
                out.append(f'    EXTRA in ours    {k:02X} at {o["x"]:X},{o["y"]:X} (frame {o["frame"]:X})')
                totals['extra', k] += 1
            else:
                d = []
                if abs(r['x'] - o['x']) > 4 or abs(r['y'] - o['y']) > 4:
                    d.append(f'pos ref {r["x"]:X},{r["y"]:X} ours {o["x"]:X},{o["y"]:X}')
                if (r['render'] ^ o['render']) & 0x80:
                    d.append('on-screen ref %d ours %d' % (r['render'] >> 7, o['render'] >> 7))
                if ours_id(o['code']) is not None and r['frame'] != o['frame']:
                    d.append(f'frame ref {r["frame"]:X} ours {o["frame"]:X}')
                if d:
                    out.append(f'    DIFF {k:02X} at {r["x"]:X},{r["y"]:X}: ' + '; '.join(d))
                    totals['diff', k] += 1
        rep.append(f'  point {i} ({pts[i][0]:X},{pts[i][1]:X}) camera ref {camr[0]:X},{camr[1]:X} ours {camo[0]:X},{camo[1]:X}'
                   + ('' if out else '  ok'))
        rep += out
    rep.append('  summary: ' + ', '.join(f'{k[0]} {k[1]:02X} x{n}' for k, n in sorted(totals.items())) if totals else '  summary: clean')
    (OUT / f'{name}{act + 1}.txt').write_text('\n'.join(rep) + '\n')
    return rep


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cmd, args = sys.argv[1], [a for a in sys.argv[2:] if not a.startswith('--')]
    opt = {sys.argv[i]: sys.argv[i + 1] for i in range(2, len(sys.argv) - 1) if sys.argv[i].startswith('--')}
    if cmd in ('run', 'cmp'):
        name, act = args[0], int(args[1])
        if cmd == 'run':
            pts = points(name, act, int(opt.get('--step', '0'), 16) or STEP)
            (OUT / f'{name}{act + 1}_pts.txt').write_text(''.join('%X %X\n' % p for p in pts))
            if opt.get('--side', 'both') in ('both', 'ref'):
                run_ref(name, act, pts)
            if opt.get('--side', 'both') in ('both', 'ours'):
                run_ours(name, act, pts)
        print('\n'.join(compare(name, act)))
    elif cmd == 'all':
        from concurrent.futures import ThreadPoolExecutor
        jobs = int(opt.get('--jobs', '4'))
        todo = list(acts())
        for name, act in todo:
            (OUT / f'{name}{act + 1}_pts.txt').write_text(''.join('%X %X\n' % p for p in points(name, act)))
        with ThreadPoolExecutor(jobs) as ex:          # ours: one game process per act
            list(ex.map(lambda t: run_ours(t[0], t[1], points(t[0], t[1])), todo))
        for name, act in todo:                        # the cartridge (in-process core: one at a time)
            run_ref(name, act, points(name, act))
        for name, act in todo:
            rep = compare(name, act)
            print(rep[0], '|', rep[-1].strip())


if __name__ == '__main__':
    main()
