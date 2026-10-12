"""Record the Data Select's zone cards from the game itself.

Each zone's act 1 is run headless with TRILOGY_GLIDE=2 (the player runs the
act unseen and unhurt, the camera following as in play) and its frames
are captured, reduced to the 80x56 card (4x4 box average) and packed as
trilogy_previews.bin beside the executable: so a card is exactly what the
level looks like, backgrounds, parallax, palette cycles and all.

  python tools/trilogy/previews.py [build/Release] [--zones 0,1,...]

File: 'TPRV', u16 zones, u16 width, u16 height, u16 0; per zone u32 offset,
u32 size, u16 frames, u16 0; at offset: 256 x RGB palette, then the frames'
palette indices (8 bpp) deflated (zlib).
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
W, H = 80, 56
SHOTS, STEP, SETTLE = 240, 2, 240
S1 = [0x2000, 0x2200, 0x2400, 0x2100, 0x2300, 0x2500, 0x2502]
S2 = [(0x28 + i) << 8 for i in range(11)]
S3K = [0x000, 0x100, 0x200, 0x300, 0x500, 0x600, 0x700, 0x400, 0x800, 0x900, 0x1601, 0xA00, 0xB00, 0xC00]
ZONES = S1 + S2 + S3K


def capture(exe_dir, za, tmp):
    boot = (ROOT / 'scratch' / 'boot.inc').read_text() if (ROOT / 'scratch' / 'boot.inc').exists() else \
        "WAIT 420\nPRESS START 4\nWAIT 150\nPRESS START 4\nWAIT 120\nPRESS LEFT 4\nWAIT 40\nPRESS START 4\n"
    lines = [boot.strip(), "WAIT 90", "HOLD RIGHT", f"WAIT {SETTLE}"]
    for k in range(SHOTS):                           # the player runs right, jumping now and then
        if k % 24 == 12:
            lines.append("PRESS A 8")                # (PRESS starts the press; the WAIT runs the frames)
        lines.append(f"WAIT {STEP}")
        lines.append(f"SCREENSHOT {tmp}/f{k:04d}.png")
    lines.append("EXIT 0")
    script = Path(tmp) / 'script.txt'
    script.write_text('\n'.join(lines) + '\n')
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', TRILOGY_ZONE=f'{za:04X}', TRILOGY_GLIDE='2', TRILOGY_CHAR='2',
               LNG_SCRIPT='1', LNG_TEST_HIDDEN='1')       # (unseen, unheard)
    exe = next((n for n in ('SonicTrilogy.exe', 'Sonic3KRecomp.exe') if (Path(exe_dir) / n).exists()), 'Sonic3KRecomp.exe')
    subprocess.run([str(Path(exe_dir) / exe), '--no-launcher', '--turbo', '--input-script', str(script),
                    'sonic3k.bin'], cwd=exe_dir, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900,
                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    frames = []
    for k in range(SHOTS):
        p = Path(tmp) / f'f{k:04d}.png'
        if not p.exists():
            break
        im = Image.open(p).convert('RGB')
        if im.size != (320, 224):                    # (a widescreen capture: the native middle)
            x0 = (im.size[0] - 320) // 2
            im = im.crop((x0, 0, x0 + 320, 224))
        frames.append(im.reduce(4))
        p.unlink()
    return frames


def pack(frames):
    strip = Image.new('RGB', (W, H * len(frames)))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * H))
    q = strip.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = bytes(q.getpalette()[:768]).ljust(768, b'\0')
    return pal + zlib.compress(q.tobytes(), 9), len(frames)


def main(argv=None, progress=None):
    argv = sys.argv[1:] if argv is None else argv
    args = [a for a in argv if not a.startswith('--')]
    exe_dir = Path(args[0] if args else ROOT / 'build' / 'Release').resolve()
    only = None
    if '--zones' in argv:
        only = {int(x) for x in argv[argv.index('--zones') + 1].split(',')}
    out = exe_dir / 'trilogy_previews.bin'
    old = {}
    if only is not None and out.exists():            # keep the zones not re-recorded
        b = out.read_bytes()
        n = struct.unpack_from('>H', b, 4)[0]
        for i in range(n):
            o, s, f = struct.unpack_from('>IIH', b, 12 + i * 12)
            if s:
                old[i] = (b[o:o + s], f)
    blobs = []
    for i, za in enumerate(ZONES):
        if progress:
            progress(i, len(ZONES))
        if only is not None and i not in only:
            blobs.append(old.get(i, (b'', 0)))
            continue
        with tempfile.TemporaryDirectory() as tmp:
            frames = capture(exe_dir, za, tmp.replace('\\', '/'))
        if not frames:
            print(f"  code {i:2d} ${za:04X}: no frames")
            blobs.append((b'', 0))
            continue
        data, n = pack(frames)
        blobs.append((data, n))
        print(f"  code {i:2d} ${za:04X}: {n} frames, {len(data)} bytes")
    head = bytearray(b'TPRV' + struct.pack('>HHHH', len(ZONES), W, H, 0))
    off = len(head) + 12 * len(ZONES)
    body = bytearray()
    for data, n in blobs:
        head += struct.pack('>IIHH', off + len(body) if data else 0, len(data), n, 0)
        body += data
    out.write_bytes(bytes(head + body))
    print(f"{out}: {len(head) + len(body)} bytes")


if __name__ == '__main__':
    main()
