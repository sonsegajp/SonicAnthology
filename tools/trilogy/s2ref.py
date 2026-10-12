"""Reference frames from the real Sonic 2 cartridge (PicoDrive libretro core,
debug-only): the audit oracle for the converted S2 zones and objects.

  python tools/trilogy/s2ref.py ZONE ACT X Y [--wait N] [--hold BUTTONS] [--out file.png]

Boots the cartridge into Emerald Hill, then restarts the level as if from a
star post at (X, Y) in the given act (S2's own Obj79_LoadData restore), waits
N frames (default 60) holding BUTTONS (UDLRABCS) and saves the frame.
"""
import ctypes as C
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, 'C:/Users/hyper/Chaotix32XRecomp/tools/ref')
import s2_level  # noqa: E402
import s2_objects  # noqa: E402
from refcore import Ref  # noqa: E402
from refhost import RETRO_MEMORY_SYSTEM_RAM  # noqa: E402

CORE = 'C:/Users/hyper/Chaotix32XRecomp/tools/ref/picodrive_libretro.dll'
S2ROM = 'C:/Users/hyper/roms/Sonic The Hedgehog 2 (World).md'


class S2Ref(Ref):
    def _raw(self):
        return self.core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)

    def w8(self, a, v):                        # the core keeps 68K RAM byte-swapped
        C.memmove(self._raw() + ((a & 0xFFFF) ^ 1), bytes([v & 0xFF]), 1)

    def w16(self, a, v):
        self.w8(a, v >> 8)
        self.w8(a + 1, v)

    def r8(self, a):
        return self.ram()[a & 0xFFFF]


def main():
    argv, args, opt = sys.argv[1:], [], {}
    i = 0
    while i < len(argv):
        if argv[i].startswith('--'):
            opt[argv[i]] = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    name, act, x, y = args[0], int(args[1]), int(args[2], 16), int(args[3], 16)
    out = opt.get('--out', 'scratch/s2ref.png')
    sym = s2_objects.symbols()
    rom = Path(S2ROM).read_bytes()
    ref = S2Ref(CORE, S2ROM, workdir=str(HERE))
    for f in range(1500):                         # SEGA, title, Start into EHZ1
        ref.step('S' if f % 40 < 3 else '')
        if ref.r8(sym['Game_Mode']) == 0x0C and f > 100:
            break
    for _ in range(30):
        ref.step('')
    z, s2act = s2_level.S2_ZONES[name][1][act]
    be16 = lambda a: (rom[a] << 8) | rom[a + 1]
    sa = s2_level.S2['LevelSize'] + (z * 2 + s2act) * 8
    left, right, top, bottom = (be16(sa + i * 2) for i in range(4))
    cx = max(left, min(right, x - 160))
    cy = max(top, min(bottom, y - 96))
    ref.w16(sym['Current_ZoneAndAct'], z << 8 | s2act)
    for k, v in (('Saved_x_pos', x), ('Saved_y_pos', y), ('Saved_Ring_count', 0),
                 ('Saved_art_tile', 0x0780), ('Saved_Solid_bits', 0x0C0D),
                 ('Saved_Camera_X_pos', cx), ('Saved_Camera_Y_pos', cy),
                 ('Saved_Camera_BG_X_pos', 0), ('Saved_Camera_BG_Y_pos', 0),
                 ('Saved_Camera_BG2_X_pos', 0), ('Saved_Camera_BG2_Y_pos', 0),
                 ('Saved_Camera_BG3_X_pos', 0), ('Saved_Camera_BG3_Y_pos', 0),
                 ('Saved_Camera_Max_Y_pos', bottom)):
        ref.w16(sym[k], v)
    fresh = x == 0 and y == 0                     # 0,0: a fresh level start (SCZ: camera rides the Tornado)
    ref.w8(sym['Saved_Last_star_pole_hit'], 0 if fresh else 1)
    ref.w8(sym['Last_star_pole_hit'], 0 if fresh else 1)
    ref.w8(sym['Saved_Dynamic_Resize_Routine'], int(opt.get('--rt', '0'), 16))
    ref.w16(sym['Level_Inactive_flag'], 1)
    for _ in range(600):                          # the restarted level's title card
        ref.step('')
        if ref.r8(sym['Game_Mode']) & 0x80:
            break
    for _ in range(600):
        ref.step('')
        if not ref.r8(sym['Game_Mode']) & 0x80:
            break
    hold = opt.get('--hold', '')
    if '--pmode' in opt:                              # Player_mode (2: Tails alone)
        ref.w16(sym['Player_mode'], int(opt['--pmode'], 16))
    if '--mode' in opt:                               # e.g. 20: EndingSequence from the restarted level
        ref.w8(sym['Game_Mode'], int(opt['--mode'], 16))
    god = '--god' in opt                          # Sonic can't be hurt (MainCharacter invulnerable_time)
    # --seq BUTTONS:FRAMES,... (e.g. A:30,R:40,:300) replaces --wait / --hold
    seq = [(b, int(n)) for b, n in (x.split(':') for x in opt['--seq'].split(','))] if '--seq' in opt         else [(hold, int(opt.get('--wait', '60')))]
    for b, n in seq:
        for _ in range(n):
            if god:
                ref.w8(0xFFB030, 0x1C)
            ref.step(b)
    ref.png(out)
    if '--vram' in opt:                               # 64 KB VRAM (the core keeps words little-endian)
        p = ref.core.retro_get_memory_data(3)
        raw = C.string_at(p, 0x10000)
        sw = bytearray(0x10000)
        sw[0::2], sw[1::2] = raw[1::2], raw[0::2]
        open(opt['--vram'], 'wb').write(bytes(sw))
    ram = ref.ram()
    if '--ram' in opt:                                # 64 KB 68K RAM (big-endian)
        open(opt['--ram'], 'wb').write(bytes(ram))
    print(f"{out}: {name}{act + 1} camera {struct.unpack_from('>H', ram, sym['Camera_X_pos'] & 0xFFFF)[0]:X},"
          f"{struct.unpack_from('>H', ram, sym['Camera_Y_pos'] & 0xFFFF)[0]:X}  sonic "
          f"{struct.unpack_from('>H', ram, 0xB008)[0]:X},{struct.unpack_from('>H', ram, 0xB00C)[0]:X} "
          f"status {ram[0xB022]:X} routine {ram[0xB024]:X}  bg "
          f"{struct.unpack_from('>H', ram, sym['Camera_BG_X_pos'] & 0xFFFF)[0]:X},"
          f"{struct.unpack_from('>H', ram, sym['Camera_BG_Y_pos'] & 0xFFFF)[0]:X}")


if __name__ == '__main__':
    main()
