"""AnthologyBuilder — sets Sonic Anthology up from the player's own cartridges.

Asks for Sonic the Hedgehog, Sonic the Hedgehog 2 and Sonic 3 & Knuckles
(the lock-on cart, or Sonic & Knuckles and Sonic 3 separately), checks each
against the dumps the converters are verified on, then writes beside the game:
  sonic3k.bin            the S3K cart the game runs
  trilogy.bin            Sonic 1 and 2's zones, objects, music... converted
  trilogy_previews.bin   the Data Select's zone cards, recorded from the game
And, given a Sonic CD disc (its BIN/CUE dump; optional):
  scd.bin, scdss.bin     Sonic CD's levels and special stages, converted
  scd-disc.txt           the CUE's path (the game reads its music in place)
And, given a Sonic Origins install (optional): the player sprites option's
  origins.bin, origins_ufo.bin   its players, monitors and posts, packed
(the release ships these two already; this repacks them from the player's own
copy). Nothing else from the games ships with Sonic Anthology: it is made here.

  python tools/trilogy/builder/trilogy_builder.py [game dir]
Packaged as AnthologyBuilder.exe by make_builder.py (then the game dir is its own).
"""
import contextlib
import os
import queue
import subprocess
import sys
import tempfile
import threading
import traceback
import zlib
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except ImportError:                                 # (Android: the app is the window, headless() the build)
    tk = None

FROZEN = getattr(sys, 'frozen', False)
TOOLS = Path(sys._MEIPASS) / 'trilogy' if FROZEN else Path(__file__).resolve().parents[1]
if FROZEN:
    GAME_DIR = Path(sys.executable).resolve().parent
else:
    GAME_DIR = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith('--') \
        else TOOLS.parents[1] / 'build' / 'Release'
sys.path.insert(0, str(TOOLS))

GAME_EXE = next((n for n in ('SonicAnthology.exe', 'SonicTrilogy.exe', 'Sonic3KRecomp.exe') if (GAME_DIR / n).exists()),
                'SonicAnthology.exe')
ZONES = 'GHZ,LZ,MZ,SLZ,SYZ,SBZ,EHZ,CPZ,ARZ,CNZ,HTZ,MCZ,OOZ,MTZ,SCZ,WFZ,DEZ'

# CRC32s of the dumps the converters are verified against
S1_REV01 = {0xAFE05EEE: 'REV01', 0x18B1B998: 'REV01 (region check removed)'}
S2_REV01 = {0x24AB4C3A: 'REV01'}
S3K_LOCKON, SK_ALONE, S3_ALONE = 0x63522553, 0x0658F691, 0x9BC192CE


def read_rom(path):
    b = Path(path).read_bytes()
    if len(b) % 0x4000 == 512:                      # (a copier header)
        b = b[512:]
    return b


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def identify(b):
    """(kind, note) of a cart: kind 's1', 's2', 's3k', 'sk', 's3' or None."""
    c = crc(b)
    if c in S1_REV01:
        return 's1', 'Sonic the Hedgehog ' + S1_REV01[c]
    if c in S2_REV01:
        return 's2', 'Sonic the Hedgehog 2 ' + S2_REV01[c]
    if c == S3K_LOCKON:
        return 's3k', 'Sonic 3 & Knuckles (lock-on)'
    if c == SK_ALONE:
        return 'sk', 'Sonic & Knuckles'
    if c == S3_ALONE:
        return 's3', 'Sonic the Hedgehog 3'
    serial = b[0x180:0x18E]
    if serial.startswith(b'GM 00004049') or serial.startswith(b'GM 00001009'):
        return None, 'Sonic the Hedgehog, but not REV01: the REV01 dump is needed'
    if serial.startswith(b'GM 00001051'):
        return None, 'Sonic the Hedgehog 2, but not REV01 (Rev A): that dump is needed'
    return None, 'not a cart Sonic Anthology knows'


class Log(object):
    """stdout/stderr for the build thread: lines go to the window."""
    def __init__(self, q):
        self.q = q

    def write(self, s):
        if s:
            self.q.put(('log', s))
            try:                                    # (and builder.log beside the game)
                with open(GAME_DIR / 'builder.log', 'a', encoding='utf-8') as f:
                    f.write(s)
            except OSError:
                pass

    def flush(self):
        pass


class Builder(tk.Tk if tk else object):
    ROWS = [('s1', 'Sonic the Hedgehog'), ('s2', 'Sonic the Hedgehog 2'),
            ('s3k', 'Sonic 3 & Knuckles'), ('s3', 'Sonic 3 (only with Sonic & Knuckles alone)')]
    CD_ROW = 5                                      # (Sonic CD: optional, its disc's CUE)
    ORIGINS_ROW = 6                                 # (Sonic Origins: optional, its install folder)

    def __init__(self):
        super().__init__()
        self.title('Sonic Anthology — Setup')
        self.resizable(False, False)
        self.q = queue.Queue()
        self.carts = {}                             # kind -> bytes
        self.vars = {}
        self.marks = {}
        pad = dict(padx=8, pady=4)
        ttk.Label(self, text='Sonic Anthology is made from your own cartridges.\n'
                             'Choose a dump of each game (they are checked, never changed).',
                  justify='left').grid(row=0, column=0, columnspan=4, sticky='w', **pad)
        for i, (k, name) in enumerate(self.ROWS, 1):
            ttk.Label(self, text=name).grid(row=i, column=0, sticky='w', **pad)
            v = tk.StringVar()
            self.vars[k] = v
            ttk.Entry(self, textvariable=v, width=52, state='readonly').grid(row=i, column=1, **pad)
            ttk.Button(self, text='Browse…', command=lambda k=k: self.browse(k)).grid(row=i, column=2, **pad)
            m = ttk.Label(self, text='', width=3)
            m.grid(row=i, column=3, **pad)
            self.marks[k] = m
        self.cue = None
        ttk.Label(self, text='Sonic CD (optional: its .cue)').grid(row=self.CD_ROW, column=0, sticky='w', **pad)
        self.vars['scd'] = tk.StringVar()
        ttk.Entry(self, textvariable=self.vars['scd'], width=52, state='readonly').grid(row=self.CD_ROW, column=1, **pad)
        ttk.Button(self, text='Browse…', command=self.browse_cd).grid(row=self.CD_ROW, column=2, **pad)
        self.marks['scd'] = ttk.Label(self, text='', width=3)
        self.marks['scd'].grid(row=self.CD_ROW, column=3, **pad)
        self.origins = None
        ttk.Label(self, text='Sonic Origins (optional: sprites included)').grid(row=self.ORIGINS_ROW, column=0, sticky='w', **pad)
        self.vars['origins'] = tk.StringVar()
        ttk.Entry(self, textvariable=self.vars['origins'], width=52, state='readonly').grid(row=self.ORIGINS_ROW, column=1, **pad)
        ttk.Button(self, text='Browse…', command=self.browse_origins).grid(row=self.ORIGINS_ROW, column=2, **pad)
        self.marks['origins'] = ttk.Label(self, text='', width=3)
        self.marks['origins'].grid(row=self.ORIGINS_ROW, column=3, **pad)
        self.previews = tk.BooleanVar(value=True)
        ttk.Checkbutton(self, text='Record the Data Select zone previews (a few minutes)',
                        variable=self.previews).grid(row=7, column=0, columnspan=3, sticky='w', **pad)
        self.bar = ttk.Progressbar(self, length=560, mode='determinate')
        self.bar.grid(row=8, column=0, columnspan=4, **pad)
        self.text = tk.Text(self, width=80, height=14, state='disabled', font=('Consolas', 9))
        self.text.grid(row=9, column=0, columnspan=4, **pad)
        f = ttk.Frame(self)
        f.grid(row=10, column=0, columnspan=4, sticky='e', **pad)
        self.build_btn = ttk.Button(f, text='Build', command=self.build, state='disabled')
        self.build_btn.pack(side='left', padx=4)
        self.play_btn = ttk.Button(f, text='Play', command=self.play, state='disabled')
        self.play_btn.pack(side='left', padx=4)
        self.scan()
        self.after(100, self.pump)

    # ---- choosing carts ----------------------------------------------------
    def say(self, s):
        self.text.configure(state='normal')
        self.text.insert('end', s)
        self.text.see('end')
        self.text.configure(state='disabled')

    def take(self, path, quiet=False):
        try:
            b = read_rom(path)
        except OSError as e:
            if not quiet:
                messagebox.showerror('Sonic Anthology', f'{path}\n\n{e}')
            return None
        kind, note = identify(b)
        if kind is None:
            if not quiet:
                messagebox.showerror('Sonic Anthology', f'{Path(path).name}\n\nThis is {note}.')
            return None
        if kind == 'sk':                            # S&K alone: S3 joins it on the lock-on slot
            self.carts['sk'] = b
            kind = 's3k'
        elif kind == 's3':
            self.carts['s3'] = b
        else:
            self.carts[kind] = b
        row = 's3' if kind == 's3' else kind
        self.vars[row].set(str(path))
        if not quiet:
            self.say(f'{Path(path).name}: {note}\n')
        self.refresh()
        return kind

    def browse(self, k):
        p = filedialog.askopenfilename(title='Choose a cartridge dump', initialdir=str(GAME_DIR),
                                       filetypes=[('Mega Drive / Genesis', '*.bin *.md *.gen'), ('All files', '*.*')])
        if p:
            self.take(p)

    def take_cd(self, path, quiet=False):
        note = check_cd(path)
        if note:
            if not quiet:
                messagebox.showerror('Sonic Anthology', f'{Path(path).name}\n\n{note}')
            return False
        self.cue = Path(path)
        self.vars['scd'].set(str(path))
        self.marks['scd'].configure(text='✔')
        if not quiet:
            self.say(f'{Path(path).name}: Sonic CD\n')
        return True

    def take_origins(self, path, quiet=False):
        if not origins_ok(path):
            if not quiet:
                messagebox.showerror('Sonic Anthology', f'{path}\n\nNo Sonic Origins data packs (*.rsdk) here.')
            return False
        self.origins = Path(path)
        self.vars['origins'].set(str(path))
        self.marks['origins'].configure(text='✔')
        if not quiet:
            self.say(f'{path}: Sonic Origins\n')
        return True

    def browse_origins(self):
        p = filedialog.askdirectory(title='Choose the Sonic Origins install folder')
        if p:
            self.take_origins(p)

    def browse_cd(self):
        p = filedialog.askopenfilename(title="Choose Sonic CD's .cue", initialdir=str(GAME_DIR),
                                       filetypes=[('CUE sheet', '*.cue'), ('All files', '*.*')])
        if p:
            self.take_cd(p)

    def scan(self):
        """Carts (and a Sonic CD .cue) already beside the game (or in roms/) are picked up."""
        seen = set()
        for d in (GAME_DIR, GAME_DIR / 'roms'):
            if not d.is_dir():
                continue
            for p in sorted(d.iterdir()):
                if p.suffix.lower() == '.cue' and not self.cue and self.take_cd(p, quiet=True):
                    seen.add('Sonic CD')
                    continue
                if p.suffix.lower() in ('.bin', '.md', '.gen') and p.name not in ('trilogy.bin', 'trilogy_previews.bin') \
                        and 0x40000 <= p.stat().st_size <= 0x400200:
                    kind = self.take(p, quiet=True)
                    if kind:
                        seen.add(kind)
        if not self.origins and self.take_origins(DEFAULT_ORIGINS, quiet=True):
            seen.add('Sonic Origins')
        if seen:
            self.say('Found beside the game: ' + ', '.join(sorted(seen)) + '\n')

    def s3k_cart(self):
        if 's3k' in self.carts:
            return self.carts['s3k']
        if 'sk' in self.carts and 's3' in self.carts:
            b = self.carts['sk'] + self.carts['s3']
            return b if crc(b) == S3K_LOCKON else None
        return None

    def refresh(self):
        ok = {'s1': 's1' in self.carts, 's2': 's2' in self.carts, 's3k': self.s3k_cart() is not None}
        for k in ('s1', 's2', 's3k'):
            self.marks[k].configure(text='✔' if ok[k] else ('…' if k == 's3k' and 'sk' in self.carts else ''))
        self.marks['s3'].configure(text='✔' if 's3' in self.carts else '')
        self.build_btn.configure(state='normal' if all(ok.values()) else 'disabled')

    # ---- building ----------------------------------------------------------
    def build(self):
        self.build_btn.configure(state='disabled')
        self.play_btn.configure(state='disabled')
        threading.Thread(target=self.work, daemon=True).start()

    def work(self):
        log = Log(self.q)
        try:
            with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log), \
                    tempfile.TemporaryDirectory() as tmp:
                t = Path(tmp)
                paths = {}
                for k, b in (('s1', self.carts['s1']), ('s2', self.carts['s2']), ('s3k', self.s3k_cart())):
                    paths[k] = t / f'{k}.bin'
                    paths[k].write_bytes(b)
                self.q.put(('bar', 5))
                print('Converting Sonic 1 and Sonic 2 (this takes a minute)…')
                import build_bank
                build_bank.main(['--s1', str(paths['s1']), '--s2', str(paths['s2']), '--s3k', str(paths['s3k']),
                                 '--out', str(t / 'trilogy.bin'), '--zones', ZONES])
                GAME_DIR.mkdir(parents=True, exist_ok=True)
                (GAME_DIR / 'trilogy.bin').write_bytes((t / 'trilogy.bin').read_bytes())
                (GAME_DIR / 'sonic3k.bin').write_bytes(paths['s3k'].read_bytes())
                print(f'Wrote trilogy.bin and sonic3k.bin to {GAME_DIR}')
                self.q.put(('bar', 20))
                if self.cue:
                    print('Converting Sonic CD (this takes a minute)…')
                    optional('Sonic CD', build_scd, self.cue, paths['s3k'], t)
                if self.origins:
                    print('Packing the Sonic Origins sprites…')
                    optional('Sonic Origins', build_origins, self.origins, paths['s3k'], t)
                self.q.put(('bar', 30))
                if self.previews.get():
                    if (GAME_DIR / GAME_EXE).exists():
                        print('Recording the zone previews…')
                        import previews
                        previews.main([str(GAME_DIR)],
                                      progress=lambda i, n: self.q.put(('bar', 30 + 70 * i // n)))
                    else:
                        print(f'(no {GAME_EXE} here: previews skipped)')
                self.q.put(('bar', 100))
                print('\nAll set. Have fun!')
                self.q.put(('done', None))
        except BaseException:                       # (a converter's argparse exits: SystemExit)
            self.q.put(('log', traceback.format_exc()))
            self.q.put(('fail', None))

    def pump(self):
        try:
            while True:
                what, v = self.q.get_nowait()
                if what == 'log':
                    self.say(v)
                elif what == 'bar':
                    self.bar['value'] = v
                elif what == 'done':
                    self.play_btn.configure(state='normal')
                    self.build_btn.configure(state='normal')
                elif what == 'fail':
                    self.build_btn.configure(state='normal')
                    messagebox.showerror('Sonic Anthology', 'The build failed: see the log.')
        except queue.Empty:
            pass
        self.after(100, self.pump)

    def play(self):
        exe = GAME_DIR / GAME_EXE
        if exe.exists():
            subprocess.Popen([str(exe)], cwd=str(GAME_DIR))
            self.destroy()
        else:
            messagebox.showinfo('Sonic Anthology', f'Built. Put {GAME_EXE} in {GAME_DIR} to play.')


def optional(what, fn, *args):
    """An optional part of the build: a failure is reported and skipped, the
    rest (the three carts) stays good"""
    try:
        fn(*args)
        return True
    except BaseException:
        traceback.print_exc()
        print(f'{what} could not be set up (above); the game runs without it.')
        return False


def check_cd(cue):
    """None if the CUE is a Sonic CD disc the converters read, else why not"""
    import scd_iso
    try:
        d = scd_iso.Disc(cue)
    except (OSError, ValueError) as e:
        return f'Not a readable BIN/CUE dump ({e}).'
    if not any(n.upper().startswith('R11A') for n in d.files) or 'SPSS__.BIN' not in d.files:
        return 'This disc is not Sonic CD (no R11A / SPSS__.BIN on its data track).'
    return None


def build_scd(cue, s3k_path, tmp):
    """Sonic CD's levels and special stages -> scd.bin / scdss.bin, and the CUE's
    path for the game's own disc reader (scd-disc.txt)"""
    import scd_iso
    import scd_level
    import scd_special
    d = scd_iso.Disc(cue)
    disc = Path(tmp) / 'scd'
    disc.mkdir(exist_ok=True)
    for name in d.files:
        (disc / name).parent.mkdir(parents=True, exist_ok=True)
        (disc / name).write_bytes(d.read(name))
    scd_level.main(['--disc', str(disc), '--out', str(GAME_DIR / 'scd.bin'), '--s3k', str(s3k_path)])
    scd_special.main(['--disc', str(disc), '--out', str(GAME_DIR / 'scdss.bin')])
    (GAME_DIR / 'scd-disc.txt').write_text(str(Path(cue).resolve()) + '\n')
    print(f'Wrote scd.bin, scdss.bin and scd-disc.txt to {GAME_DIR}')


DEFAULT_ORIGINS = r'C:\Program Files (x86)\Steam\steamapps\common\SonicOrigins'


def origins_ok(folder):
    try:
        return any(Path(folder).rglob('*.rsdk'))
    except OSError:
        return False


def build_origins(folder, s3k_path, tmp):
    """The player sprites option's pack (origins.bin, origins_ufo.bin) from the
    player's own Sonic Origins install"""
    import rsdk
    import origins_pack
    data = Path(tmp) / 'origins'
    rsdk.main(['extract', '--origins', str(folder), '--out', str(data), '--no-contact'])
    origins_pack.main(['--data', str(data), '--rom', str(s3k_path), '--disasm', str(Path(tmp) / 'no-disasm'),
                       '--out', str(GAME_DIR / 'origins.bin')])
    print(f'Wrote origins.bin and origins_ufo.bin to {GAME_DIR}')


def headless(argv):
    """AnthologyBuilder --s1 X --s2 Y --s3k Z [--sk A --s3 B] [--scd CUE] [--origins DIR]
    [--no-previews]: no window; the log goes to builder.log beside the game."""
    with open(GAME_DIR / 'builder.log', 'w', buffering=1) as f, contextlib.redirect_stdout(f),             contextlib.redirect_stderr(f):                  # (line by line: the Android app shows it as it goes)
        carts = {}
        for k in ('s1', 's2', 's3k', 'sk', 's3'):
            if '--' + k in argv:
                b = read_rom(argv[argv.index('--' + k) + 1])
                kind, note = identify(b)
                print(f'{k}: {note}')
                if kind:
                    carts[kind] = b
        if 's3k' not in carts and 'sk' in carts and 's3' in carts and crc(carts['sk'] + carts['s3']) == S3K_LOCKON:
            carts['s3k'] = carts['sk'] + carts['s3']
        missing = [k for k in ('s1', 's2', 's3k') if k not in carts]
        if missing:
            print('missing or wrong: ' + ', '.join(missing))
            return 1
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            for k in ('s1', 's2', 's3k'):
                (t / f'{k}.bin').write_bytes(carts[k])
            import build_bank
            build_bank.main(['--s1', str(t / 's1.bin'), '--s2', str(t / 's2.bin'), '--s3k', str(t / 's3k.bin'),
                             '--out', str(t / 'trilogy.bin'), '--zones', ZONES])
            (GAME_DIR / 'trilogy.bin').write_bytes((t / 'trilogy.bin').read_bytes())
            (GAME_DIR / 'sonic3k.bin').write_bytes(carts['s3k'])
            if '--scd' in argv:
                cue = argv[argv.index('--scd') + 1]
                note = check_cd(cue)
                if note:
                    print(f'scd: {note}')
                    return 1
                optional('Sonic CD', build_scd, cue, t / 's3k.bin', t)
            if '--origins' in argv:
                folder = argv[argv.index('--origins') + 1]
                if not origins_ok(folder):
                    print(f'origins: no Sonic Origins data packs in {folder}')
                    return 1
                optional('Sonic Origins', build_origins, folder, t / 's3k.bin', t)
        if '--no-previews' not in argv and (GAME_DIR / GAME_EXE).exists():
            import previews
            previews.main([str(GAME_DIR)])
        print('done')
    return 0


if __name__ == '__main__':
    if sys.stdout is None:                          # (a windowed build has no console)
        sys.stdout = sys.stderr = open(os.devnull, 'w')
    if '--s1' in sys.argv or '--s3k' in sys.argv:
        sys.exit(headless(sys.argv))
    Builder().mainloop()
