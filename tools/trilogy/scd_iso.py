"""Sonic CD disc access: the ISO 9660 file system of the data track and the
CD-DA tracks, read straight from the player's own dump (BIN/CUE).

The data track is MODE1/2352 (2048 user bytes at offset 16 of each raw
sector). Audio tracks are raw 44.1 kHz 16-bit little-endian stereo.
Nothing is written into the repository: the converters read the disc in
place and the builder packs what it needs into the bank.
"""
import re
import struct
from pathlib import Path

RAW = 2352
USER = 2048


class Disc:
    def __init__(self, cue):
        self.cue = Path(cue)
        self.tracks = []                       # (number, kind, path, index1 offset in bytes)
        cur = None
        for line in self.cue.read_text(errors='replace').splitlines():
            line = line.strip()
            m = re.match(r'FILE "(.*)" BINARY', line)
            if m:
                cur = self.cue.parent / m.group(1)
                continue
            m = re.match(r'TRACK (\d+) (\S+)', line)
            if m:
                self.tracks.append([int(m.group(1)), m.group(2), cur, 0])
                continue
            m = re.match(r'INDEX 01 (\d+):(\d+):(\d+)', line)
            if m and self.tracks:
                mm, ss, ff = map(int, m.groups())
                self.tracks[-1][3] = ((mm * 60 + ss) * 75 + ff) * RAW
        data = [t for t in self.tracks if t[1].startswith('MODE1')]
        if not data:
            raise ValueError('no MODE1 data track in %s' % cue)
        self._data = data[0][2].read_bytes()
        self.files = {}
        self._walk(self._root(), '')

    # ---- data track ----
    def sector(self, lba, count=1):
        out = bytearray()
        for i in range(count):
            o = (lba + i) * RAW + 16
            out += self._data[o:o + USER]
        return bytes(out)

    def _root(self):
        pvd = self.sector(16)
        if pvd[1:6] != b'CD001':
            raise ValueError('no ISO 9660 volume descriptor')
        return pvd[156:156 + 34]

    def _walk(self, rec, prefix):
        lba, size = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        blob = self.sector(lba, (size + USER - 1) // USER)
        p = 0
        while p < size:
            n = blob[p]
            if n == 0:                          # records never straddle sectors
                p = (p // USER + 1) * USER
                continue
            r = blob[p:p + n]
            name = r[33:33 + r[32]]
            if name not in (b'\x00', b'\x01'):
                name = name.decode('ascii', 'replace').split(';')[0]
                if r[25] & 2:
                    self._walk(r, prefix + name + '/')
                else:
                    self.files[prefix + name] = (struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0])
            p += n

    def read(self, name):
        lba, size = self.files[name]
        return self.sector(lba, (size + USER - 1) // USER)[:size]

    def system_area(self):
        """The boot sectors (IP/SP live in the first 16 sectors)."""
        return self.sector(0, 16)

    # ---- audio tracks ----
    def audio(self, number):
        """Raw 44.1 kHz s16le stereo PCM of a CD-DA track (from INDEX 01)."""
        for n, kind, path, start in self.tracks:
            if n == number and kind == 'AUDIO':
                return path.read_bytes()[start:]
        raise KeyError(number)


if __name__ == '__main__':
    import sys
    d = Disc(sys.argv[1])
    for name, (lba, size) in sorted(d.files.items(), key=lambda kv: kv[1][0]):
        print('%-16s lba %6d  %8d' % (name, lba, size))
    if len(sys.argv) > 2:
        out = Path(sys.argv[2])
        out.mkdir(parents=True, exist_ok=True)
        for name in d.files:
            (out / name).parent.mkdir(parents=True, exist_ok=True)
            (out / name).write_bytes(d.read(name))
        (out / '_SYSTEM.BIN').write_bytes(d.system_area())
