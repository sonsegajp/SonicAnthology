#!/usr/bin/env python3
"""Read the Retro Engine data packs of the user's own Sonic Origins install.

This module reads ``*.rsdk`` packs straight out of the user's own Sonic
Origins installation and decodes the parts needed to redraw the player
characters, monitors / item boxes, star posts / lamp posts and the special
stage players in the Sonic 1, Sonic 2, Sonic CD and Sonic 3 & Knuckles styles.
Nothing extracted here belongs in the repository: the ``__main__`` writes into
a cache folder outside it (default ``C:\\Users\\hyper\\scd_ref\\origins``).

The formats follow the public Retro Engine decompilations:
RSDKv3 (Sonic-CD-11-Decompilation, ``RSDKv3/Reader.cpp`` / ``Animation.cpp``),
RSDKv4 (Sonic-1-2-2013-Decompilation, ``RSDKv4/Reader.cpp`` /
``Animation.cpp``) and RSDKv5(U) (RSDKv5-Decompilation,
``RSDKv5/RSDK/Core/Reader.cpp``, ``Graphics/Animation.cpp``,
``Graphics/Sprite.cpp`` and the ``Legacy`` v3/v4 loaders).

Pack container (all four Origins packs)
---------------------------------------
Origins runs every game on the RSDKv5U engine, so all four packs use the v5
container even for the v3/v4 games; only byte 5 of the signature names the
game's engine::

    "RSDK" 'v' <'3'|'4'|'5'>  u16 fileCount
    fileCount x { u8 hash[16]; u32 offset; u32 size }   (little endian)

``hash`` is the MD5 of the *lower-cased* path (``data/sprites/players/
sonic1.gif``) with each 32-bit word byte-reversed.  Bit 31 of ``size`` flags
an encrypted file (every file in the Origins packs is encrypted).  The
retail RSDKv4 container (``RSDKvB``) has the same layout but derives its keys
from the size only; it is supported for completeness.  The retail Sonic CD
2011 ``Data.rsdk`` (no signature, directory table) is not.

Encryption (v5 / Origins): key A = MD5(upper-cased path), key B =
MD5(decimal size), both word-reversed; ``keyNo = (size / 4) & 0x7F``,
``posA = 0``, ``posB = 8``.  Each byte is ``b ^= keyNo ^ B[posB]``, nibble
swapped while the swap flag is set, then ``b ^= A[posA]``; the positions then
advance as in ``RSDK::DecryptBytes``.  (RSDKvB: key A = MD5(str(size)), key B =
MD5(str(size / 2 + 1)), same stream.)

Sprite sheets
-------------
GIF87a/89a, one image, 8-bit palette indices.  Index 0 is transparent when the
engine draws sprites.  NOTE: the engine ignores the GIF palette when drawing
sprites -- it uses the active palette (v4: GameConfig colours 0x00-0x5F plus
StageConfig 0x60-0x7F; v3: ``Data/Palettes/MasterPalette.act`` plus StageConfig
96-127; v5: GameConfig / StageConfig palette banks), and scripts recolour some
entries at run time (e.g. Knuckles / Amy fur colours in S1/S2).  The GIF's own
palette is what the artists saved and normally matches; :func:`decode_gif`
keeps the raw indices so a different palette can be applied later.

Animation files
---------------
* v3 ``.ani`` (Sonic CD) and v4 ``.ani`` (Sonic 1/2) are byte-identical
  formats (``Data/Animations/<name>.ani``)::

      u8 sheetCount; sheetCount x str  (paths relative to Data/Sprites/)
      u8 animCount;  animCount x {
          str name; u8 frameCount; u8 speed; u8 loopIndex; u8 rotationStyle
          frameCount x { u8 sheet; u8 hitbox; u8 x, y, w, h; s8 pivotX, pivotY }
      }
      u8 hitboxCount; hitboxCount x 8 directions x { s8 left, top, right, bottom }

  (``str`` = u8 length + bytes.)  There is no per-frame duration: every game
  frame the timer gains ``speed`` (scripts may override it, e.g. walking) and
  the frame advances when the timer reaches 240.  rotationStyle 3
  (STATICFRAMES) stores a second, pre-rotated half of the frames.
* v5 ``.bin`` (S3K, ``Data/Sprites/<dir>/<name>.bin``)::

      "SPR\\0"; u32 totalFrames; u8 sheetCount; sheetCount x str
      u8 hitboxCount; hitboxCount x str (hitbox names)
      u16 animCount; animCount x {
          str name; u16 frameCount; u16 speed; u8 loopIndex; u8 rotationStyle
          frameCount x { u8 sheet; s16 duration; u16 unicodeChar;
                         s16 x, y, w, h, pivotX, pivotY;
                         hitboxCount x { s16 left, top, right, bottom } }
      }

  Every game frame the timer gains ``speed``; a frame is shown while the timer
  is not above its ``duration`` (so ticks ~= duration / speed).

Both decode to the common structure returned by :func:`parse_animation`::

    {"format", "path", "sheets": [...], "hitbox_names": [...],
     "hitbox_table": [...],   # legacy only
     "animations": {name: {"index", "speed", "loop_index", "rotation_style",
                           "rotation_style_name", "frame_count", "frames": [
                               {"sheet", "sheet_index", "x", "y", "w", "h",
                                "pivot_x", "pivot_y", "duration",
                                "hitbox_id" | "hitboxes", ...}]}}}

Script sprite frames (v3/v4 monitors, lamp posts, S2/CD special stages)
-------------------------------------------------------------------------
Sonic 1/2/CD objects other than the players define their sprites in script
code (``LoadSpriteSheet`` + ``SpriteFrame(pivotX, pivotY, w, h, x, y)`` in
``ObjectStartup``).  The packs hold that code compiled (v4
``Bytecode/GlobalCode.bin`` / ``Bytecode/<StageFolder>.bin``, v3
``Data/Scripts/ByteCode/...``); :class:`LegacyBytecode` runs an object's
startup event in a small VM (opcode / variable tables from the
decompilations) once per playable character, giving the frame list the game
builds (frame ID = list position).  If the install also has the community text
scripts (``build/main/projects/exec/<Game>u/Scripts``, not part of a stock
install), :func:`parse_script_frames` reads them -- honouring the
``#platform:`` blocks Origins compiles (STANDARD / SW_RENDERING /
USE_ORIGINS) -- and, where they agree with the bytecode, adds each frame's
comment, label, source line and guarding conditions.

Output of ``python rsdk.py extract`` (default ``--out C:\\Users\\hyper\\scd_ref\\origins``)
------------------------------------------------------------------------------------------
``<game>/`` for sonic1, sonic2, soniccd, sonic3k::

    <game>.json                    description (below)
    <game>_contact.png             first frame of every player animation, cut and pivoted
    <game>_contact_<group>.png     same for special_players / monitors / starposts / special / extra_players
    raw/Data/...                   the decrypted original files (.gif, .ani, .bin)
    sheets/<sheet>.png             RGBA, index 0 transparent, GIF palette
    sheets/<sheet>.indexed.png     8-bit palette PNG with the original indices

``<game>.json``: ``game``, ``title``, ``engine`` (3/4/5), ``pack`` (container
stats), ``globals`` (v3/v4 GameConfig variables), ``palettes`` (GameConfig /
MasterPalette colours), ``groups`` {group: {character/object: [animation
file keys or "script:<path>"]}}, ``animation_files`` {key: parse_animation()
structure}, ``script_objects`` {name: {"object", "stage_folder", "bytecode",
"script_index", "sheets", "frames", "frames_by_character" (only where a
character differs), "text_script", "text_matches_bytecode", "all_frames"}},
``sheets`` {sheet path relative to Data/Sprites/: {"png", "indexed_png",
"raw", "width", "height", "palette" (256 RGB), "used_indices",
"transparent_index"}}, ``missing`` / ``missing_optional`` / ``notes``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import zlib

DEFAULT_ORIGINS = r"C:\Program Files (x86)\Steam\steamapps\common\SonicOrigins"
DEFAULT_OUT = r"C:\Users\hyper\scd_ref\origins"
PACK_SUBDIR = os.path.join("image", "x64", "raw", "retro")
SCRIPT_SUBDIR = os.path.join("build", "main", "projects", "exec")

# --------------------------------------------------------------------------
# Hashing / encryption
# --------------------------------------------------------------------------


def _word_reverse(digest: bytes) -> bytes:
    return b"".join(digest[i:i + 4][::-1] for i in range(0, 16, 4))


def path_hash(path: str) -> bytes:
    """The 16 bytes a pack stores for ``path`` (MD5 of lower-cased path, word-reversed)."""
    return _word_reverse(hashlib.md5(path.replace("\\", "/").lower().encode("latin-1")).digest())


def _md5_key(text: str) -> bytes:
    return _word_reverse(hashlib.md5(text.encode("latin-1")).digest())


_NIBBLE_SWAP = bytes(((b << 4) | (b >> 4)) & 0xFF for b in range(256))


def _decrypt(data: bytes, key_a: bytes, key_b: bytes, key_no: int) -> bytes:
    """The RSDK byte stream cipher (identical stepping in v4 and v5)."""
    out = bytearray(len(data))
    pos_a, pos_b, swap = 0, 8, 0
    swap_tab = _NIBBLE_SWAP
    for i, c in enumerate(data):
        c ^= key_no ^ key_b[pos_b]
        if swap:
            c = swap_tab[c]
        out[i] = c ^ key_a[pos_a]
        pos_a += 1
        pos_b += 1
        if pos_a <= 15:
            if pos_b > 12:
                pos_b = 0
                swap ^= 1
        elif pos_b <= 8:
            pos_a = 0
            swap ^= 1
        else:
            key_no = (key_no + 2) & 0x7F
            if swap:
                swap = 0
                pos_a = key_no % 7
                pos_b = (key_no % 12) + 2
            else:
                swap = 1
                pos_a = (key_no % 12) + 3
                pos_b = key_no % 7
    return bytes(out)


def decrypt_v5(data: bytes, path: str) -> bytes:
    """Decrypt a file from a v5-container pack (all Origins packs)."""
    size = len(data)
    return _decrypt(data, _md5_key(path.replace("\\", "/").upper()), _md5_key(str(size)), (size // 4) & 0x7F)


def decrypt_vB(data: bytes) -> bytes:
    """Decrypt a file from a retail RSDKv4 ``RSDKvB`` pack (keys from the size only)."""
    size = len(data)
    return _decrypt(data, _md5_key(str(size)), _md5_key(str((size >> 1) + 1)), ((size & 0x1FC) >> 2) & 0x7F)


# --------------------------------------------------------------------------
# Pack reader
# --------------------------------------------------------------------------


class PackEntry:
    __slots__ = ("index", "hash", "offset", "size", "encrypted", "name")

    def __init__(self, index, hash_, offset, size, encrypted):
        self.index = index
        self.hash = hash_
        self.offset = offset
        self.size = size
        self.encrypted = encrypted
        self.name = None

    def __repr__(self):
        return "PackEntry(#%d %s off=0x%X size=%d%s)" % (
            self.index, self.name or self.hash.hex(), self.offset, self.size, " enc" if self.encrypted else "")


class RSDKPack:
    """A Retro Engine data pack.  ``read(path)`` returns the decrypted file."""

    def __init__(self, filename: str):
        self.filename = filename
        with open(filename, "rb") as f:
            self._data = f.read()
        d = self._data
        if d[:4] != b"RSDK" or d[4:5] != b"v":
            raise ValueError("%s: not a Retro Engine v4/v5 container (no 'RSDKv' signature)" % filename)
        self.signature = d[:6].decode("latin-1")
        self.container = "vB" if d[5:6] == b"B" else "v5"
        # Engine of the game inside: '3', '4', '5' for Origins packs; retail RSDKvB is v4.
        self.engine = 4 if self.container == "vB" else int(d[5:6].decode())
        (count,) = struct.unpack_from("<H", d, 6)
        self.entries = []
        self.by_hash = {}
        for i in range(count):
            base = 8 + i * 24
            h = d[base:base + 16]
            off, size = struct.unpack_from("<II", d, base + 16)
            e = PackEntry(i, h, off, size & 0x7FFFFFFF, bool(size & 0x80000000))
            self.entries.append(e)
            self.by_hash[h] = e

    # -- lookup ------------------------------------------------------------
    def lookup(self, path: str):
        e = self.by_hash.get(path_hash(path))
        if e is not None and e.name is None:
            e.name = path.replace("\\", "/")
        return e

    def has(self, path: str) -> bool:
        return self.lookup(path) is not None

    def resolve(self, candidates) -> list:
        """Name every entry matched by ``candidates``; returns the matched paths."""
        found = []
        for c in candidates:
            e = self.lookup(c)
            if e is not None:
                found.append(e.name)
        return found

    def read(self, path: str) -> bytes:
        e = self.lookup(path)
        if e is None:
            raise KeyError("%s: no entry for %r" % (os.path.basename(self.filename), path))
        return self.read_entry(e, path)

    def read_entry(self, e: PackEntry, path: str = None) -> bytes:
        raw = self._data[e.offset:e.offset + e.size]
        if not e.encrypted:
            return raw
        if self.container == "vB":
            return decrypt_vB(raw)
        path = path or e.name
        if path is None:
            raise ValueError("entry #%d is encrypted with its path as key; its name is unknown" % e.index)
        return decrypt_v5(raw, path)

    def stats(self) -> dict:
        return {
            "file": self.filename,
            "signature": self.signature,
            "container": self.container,
            "engine": self.engine,
            "entries": len(self.entries),
            "encrypted_entries": sum(1 for e in self.entries if e.encrypted),
            "named_entries": sum(1 for e in self.entries if e.name),
            "size": len(self._data),
        }


# --------------------------------------------------------------------------
# Small binary reader
# --------------------------------------------------------------------------


class _Reader:
    def __init__(self, data: bytes, pos: int = 0):
        self.d = data
        self.p = pos

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def s8(self):
        v = self.u8()
        return v - 256 if v >= 128 else v

    def u16(self):
        (v,) = struct.unpack_from("<H", self.d, self.p)
        self.p += 2
        return v

    def s16(self):
        (v,) = struct.unpack_from("<h", self.d, self.p)
        self.p += 2
        return v

    def u32(self):
        (v,) = struct.unpack_from("<I", self.d, self.p)
        self.p += 4
        return v

    def raw(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def string(self):
        n = self.u8()
        s = self.raw(n)
        i = s.find(b"\0")  # v5 lengths count a trailing NUL
        return (s if i < 0 else s[:i]).decode("latin-1")

    def eof(self):
        return self.p >= len(self.d)


# --------------------------------------------------------------------------
# GIF
# --------------------------------------------------------------------------


class IndexedImage:
    """An 8-bit indexed image: ``pixels[y * width + x]`` is a palette index."""

    def __init__(self, width, height, pixels, palette, palette_size, gif_transparent=None):
        self.width = width
        self.height = height
        self.pixels = pixels            # bytes, width*height
        self.palette = palette          # 256 (r, g, b) tuples
        self.palette_size = palette_size  # entries actually stored in the GIF
        self.gif_transparent = gif_transparent  # GIF89a GCE index, if any (engine ignores it)

    def rgba(self, palette=None, transparent_index=0) -> bytes:
        pal = palette or self.palette
        lut = []
        for i in range(256):
            r, g, b = pal[i] if i < len(pal) else (0, 0, 0)
            lut.append(bytes((r, g, b, 0 if i == transparent_index else 255)))
        return b"".join(lut[p] for p in self.pixels)

    def crop_rgba(self, x, y, w, h, palette=None, transparent_index=0) -> bytes:
        pal = palette or self.palette
        lut = [bytes((*(pal[i] if i < len(pal) else (0, 0, 0)), 0 if i == transparent_index else 255))
               for i in range(256)]
        out = bytearray()
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if 0 <= xx < self.width and 0 <= yy < self.height:
                    out += lut[self.pixels[yy * self.width + xx]]
                else:
                    out += b"\0\0\0\0"
        return bytes(out)


def _lzw_decode(min_size: int, data: bytes, npix: int) -> bytearray:
    clear = 1 << min_size
    eoi = clear + 1
    base = [bytes((i,)) for i in range(clear)] + [b"", b""]
    table = list(base)
    code_size = min_size + 1
    out = bytearray()
    prev = None
    bitbuf = 0
    bits = 0
    for byte in data:
        bitbuf |= byte << bits
        bits += 8
        while bits >= code_size:
            code = bitbuf & ((1 << code_size) - 1)
            bitbuf >>= code_size
            bits -= code_size
            if code == clear:
                table = list(base)
                code_size = min_size + 1
                prev = None
                continue
            if code == eoi:
                return out
            if prev is None:
                entry = table[code]
                out += entry
                prev = entry
                continue
            if code < len(table):
                entry = table[code]
                if len(table) < 4096:
                    table.append(prev + entry[:1])
            elif code == len(table):
                entry = prev + prev[:1]
                if len(table) < 4096:
                    table.append(entry)
            else:
                return out  # corrupt stream; keep what we have (the engine also bails out)
            out += entry
            prev = entry
            if len(table) == (1 << code_size) and code_size < 12:
                code_size += 1
        if len(out) >= npix:
            return out
    return out


def decode_gif(data: bytes) -> IndexedImage:
    """Decode the first image of a GIF into palette indices (no colour conversion)."""
    if data[:3] != b"GIF":
        raise ValueError("not a GIF")
    width, height = struct.unpack_from("<HH", data, 6)
    flags = data[10]
    pos = 13
    pal_size = 2 << (flags & 7)
    palette = []
    if flags & 0x80:
        for i in range(pal_size):
            palette.append(tuple(data[pos + i * 3:pos + i * 3 + 3]))
        pos += 3 * pal_size
    else:
        pal_size = 0
    transparent = None
    while pos < len(data):
        b = data[pos]
        pos += 1
        if b == 0x21:  # extension
            label = data[pos]
            pos += 1
            if label == 0xF9 and data[pos] >= 4 and data[pos + 1] & 1:
                transparent = data[pos + 4]
            while data[pos]:
                pos += data[pos] + 1
            pos += 1
        elif b == 0x2C:  # image descriptor
            ix, iy, iw, ih, iflags = struct.unpack_from("<HHHHB", data, pos)
            pos += 9
            if iflags & 0x80:
                lsize = 2 << (iflags & 7)
                palette = [tuple(data[pos + i * 3:pos + i * 3 + 3]) for i in range(lsize)]
                pal_size = lsize
                pos += 3 * lsize
            min_size = data[pos]
            pos += 1
            chunks = []
            while data[pos]:
                n = data[pos]
                chunks.append(data[pos + 1:pos + 1 + n])
                pos += n + 1
            npix = iw * ih
            idx = _lzw_decode(min_size, b"".join(chunks), npix)
            idx = bytes(idx[:npix]) + bytes(max(0, npix - len(idx)))
            if iflags & 0x40:  # interlaced
                rows = [None] * ih
                r = 0
                for start, step in ((0, 8), (4, 8), (2, 4), (1, 2)):
                    for y in range(start, ih, step):
                        rows[y] = idx[r * iw:(r + 1) * iw]
                        r += 1
                idx = b"".join(rows)
            if (ix, iy, iw, ih) != (0, 0, width, height):
                canvas = bytearray(width * height)
                for y in range(ih):
                    if 0 <= iy + y < height:
                        row = idx[y * iw:(y + 1) * iw]
                        canvas[(iy + y) * width + ix:(iy + y) * width + ix + len(row)] = row[:max(0, width - ix)]
                idx = bytes(canvas)
            palette = (palette + [(0, 0, 0)] * 256)[:256]
            return IndexedImage(width, height, idx, palette, pal_size, transparent)
        elif b == 0x3B:
            break
        else:
            raise ValueError("bad GIF block 0x%02X at %d" % (b, pos - 1))
    raise ValueError("GIF has no image")


# --------------------------------------------------------------------------
# PNG writer (no third-party dependency)
# --------------------------------------------------------------------------


def _png_chunk(tag: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)


def png_bytes_rgba(width: int, height: int, rgba: bytes) -> bytes:
    stride = width * 4
    raw = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + _png_chunk(b"IDAT", zlib.compress(raw, 9)) + _png_chunk(b"IEND", b""))


def png_bytes_indexed(img: IndexedImage, transparent_index=0) -> bytes:
    """Palette PNG that keeps the original indices (PLTE = GIF palette, index 0 transparent)."""
    w = img.width
    raw = b"".join(b"\0" + img.pixels[y * w:(y + 1) * w] for y in range(img.height))
    plte = b"".join(bytes(c) for c in img.palette[:256])
    trns = bytes(0 if i == transparent_index else 255 for i in range(256))
    return (b"\x89PNG\r\n\x1a\n"
            + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, img.height, 8, 3, 0, 0, 0))
            + _png_chunk(b"PLTE", plte) + _png_chunk(b"tRNS", trns)
            + _png_chunk(b"IDAT", zlib.compress(raw, 9)) + _png_chunk(b"IEND", b""))


# --------------------------------------------------------------------------
# Animations
# --------------------------------------------------------------------------

LEGACY_ROT_STYLES = {0: "none", 1: "full", 2: "45deg", 3: "static_frames"}
V5_ROT_STYLES = {0: "none", 1: "full", 2: "45deg", 3: "90deg", 4: "180deg", 5: "static_frames"}


def _unique(d: dict, name: str) -> str:
    if name not in d:
        return name
    n = 2
    while "%s#%d" % (name, n) in d:
        n += 1
    return "%s#%d" % (name, n)


def parse_legacy_ani(data: bytes, path: str = None, engine: int = 4) -> dict:
    """Parse a RSDKv3 / RSDKv4 ``.ani`` (identical layouts)."""
    r = _Reader(data)
    sheets = [r.string() for _ in range(r.u8())]
    anims = {}
    for a in range(r.u8()):
        name = r.string()
        count, speed, loop, rot = r.u8(), r.u8(), r.u8(), r.u8()
        frames = []
        for _ in range(count):
            sheet, hb, x, y, w, h = r.u8(), r.u8(), r.u8(), r.u8(), r.u8(), r.u8()
            px, py = r.s8(), r.s8()
            frames.append({
                "sheet": sheets[sheet] if sheet < len(sheets) else None,
                "sheet_index": sheet, "x": x, "y": y, "w": w, "h": h,
                "pivot_x": px, "pivot_y": py, "duration": None, "hitbox_id": hb,
            })
        entry = {
            "index": a, "speed": speed, "loop_index": loop, "rotation_style": rot,
            "rotation_style_name": LEGACY_ROT_STYLES.get(rot, str(rot)),
            "frame_count": count,
            # STATICFRAMES: the engine halves frameCount; the second half are the rotated copies.
            "frame_count_effective": count >> 1 if rot == 3 else count,
            "ticks_per_frame": round(240.0 / speed, 3) if speed else None,
            "frames": frames,
        }
        anims[_unique(anims, name)] = entry
    hitboxes = []
    if not r.eof():
        for _ in range(r.u8()):
            dirs = []
            for _d in range(8):
                dirs.append({"left": r.s8(), "top": r.s8(), "right": r.s8(), "bottom": r.s8()})
            hitboxes.append(dirs)
    return {
        "format": "rsdkv%d-ani" % engine, "path": path, "sheets": sheets, "sheet_root": "Data/Sprites/",
        "hitbox_names": [], "hitbox_table": hitboxes, "animations": anims,
        "timing": "legacy: timer += speed (or script override) each 1/60 s; next frame when timer >= 240",
    }


def parse_v5_spr(data: bytes, path: str = None) -> dict:
    """Parse a RSDKv5 sprite animation (``SPR\\0`` ``.bin``)."""
    r = _Reader(data)
    if r.raw(4) != b"SPR\0":
        raise ValueError("not an RSDKv5 sprite animation")
    total_frames = r.u32()
    sheets = [r.string() for _ in range(r.u8())]
    hb_names = [r.string() for _ in range(r.u8())]
    anims = {}
    for a in range(r.u16()):
        name = r.string()
        count, speed, loop, rot = r.u16(), r.u16(), r.u8(), r.u8()
        frames = []
        for _ in range(count):
            sheet = r.u8()
            duration = r.s16()
            uchar = r.u16()
            x, y, w, h, px, py = (r.s16() for _ in range(6))
            hbs = []
            for hn in hb_names:
                hbs.append({"name": hn, "left": r.s16(), "top": r.s16(), "right": r.s16(), "bottom": r.s16()})
            frames.append({
                "sheet": sheets[sheet] if sheet < len(sheets) else None,
                "sheet_index": sheet, "x": x, "y": y, "w": w, "h": h,
                "pivot_x": px, "pivot_y": py, "duration": duration, "unicode_char": uchar,
                "hitboxes": hbs,
            })
        entry = {
            "index": a, "speed": speed, "loop_index": loop, "rotation_style": rot,
            "rotation_style_name": V5_ROT_STYLES.get(rot, str(rot)),
            "frame_count": count, "frame_count_effective": count, "frames": frames,
        }
        anims[_unique(anims, name)] = entry
    names = list(anims)
    for i, name in enumerate(names):
        if (anims[name]["rotation_style"] == 5 and i + 1 < len(names)
                and names[i + 1].lower() == name.lower() + " angled"):
            # Drawing.cpp STATICFRAMES: 45/135/225/315 deg use frame + frameCount,
            # i.e. the same frame index in the next animation ("Walk" -> "Walk Angled").
            anims[name]["angled_animation"] = names[i + 1]
    return {
        "format": "rsdkv5-spr", "path": path, "total_frames": total_frames, "sheets": sheets,
        "sheet_root": "Data/Sprites/", "hitbox_names": hb_names, "hitbox_table": [], "animations": anims,
        "timing": "v5: timer += speed each 1/60 s; advance while timer > frame.duration (ticks ~= duration/speed)",
    }


def parse_animation(data: bytes, path: str = None, engine: int = None) -> dict:
    if data[:4] == b"SPR\0":
        return parse_v5_spr(data, path)
    return parse_legacy_ani(data, path, engine or 4)


# --------------------------------------------------------------------------
# Game / stage configs and palettes
# --------------------------------------------------------------------------


def parse_act(data: bytes) -> list:
    """An ``.act`` palette: 3 bytes per colour."""
    return [tuple(data[i:i + 3]) for i in range(0, len(data) - 2, 3)]


def parse_gameconfig(data: bytes, engine: int) -> dict:
    """Title, object list (+ script paths for v3/v4), global variables, palette, scenes."""
    r = _Reader(data)
    out = {"engine": engine}
    if engine == 5:
        if r.raw(4) != b"CFG\0":
            raise ValueError("not a v5 GameConfig")
        out["title"], out["subtitle"], out["version"] = r.string(), r.string(), r.string()
        r.u8()
        r.u16()
        out["objects"] = [r.string() for _ in range(r.u8())]
        banks = []
        for _b in range(8):
            rows = r.u16()
            colours = [None] * 256
            for row in range(16):
                if rows >> row & 1:
                    for c in range(16):
                        colours[row * 16 + c] = tuple(r.raw(3))
            banks.append({"active_rows_mask": rows, "colours": colours})
        out["palette_banks"] = banks
        out["sfx"] = []
        for _ in range(r.u8()):
            out["sfx"].append(r.string())
            r.u8()
        r.u16()
        scenes = []
        for _c in range(r.u8()):
            cat = r.string()
            for _s in range(r.u8()):
                name, folder, sid = r.string(), r.string(), r.string()
                r.u8()  # filter (REV02)
                scenes.append({"category": cat, "name": name, "folder": folder, "id": sid})
        out["scenes"] = scenes
        return out
    out["title"] = r.string()
    if engine == 3:
        r.string()  # "Data"
    out["subtitle"] = r.string()
    if engine == 4:
        out["palette"] = [tuple(r.raw(3)) for _ in range(0x60)]
    n = r.u8()
    out["objects"] = [r.string() for _ in range(n)]
    out["object_scripts"] = [r.string() for _ in range(n)]
    gv = {}
    for _ in range(r.u8()):
        name = r.string()
        b = r.raw(4)
        v = struct.unpack("<i" if engine == 4 else ">i", b)[0]
        gv[name] = v
    out["globals"] = gv
    nsfx = r.u8()
    if engine == 4:
        out["sfx_names"] = [r.string() for _ in range(nsfx)]
    out["sfx"] = [r.string() for _ in range(nsfx)]
    out["players"] = [r.string() for _ in range(r.u8())]
    scenes = []
    for cat in ("Presentation", "Regular", "Special", "Bonus"):
        for _ in range(r.u8()):
            folder, sid, name = r.string(), r.string(), r.string()
            r.u8()
            scenes.append({"category": cat, "folder": folder, "id": sid, "name": name})
    out["scenes"] = scenes
    return out


def parse_stageconfig_legacy(data: bytes, engine: int) -> dict:
    """v3/v4 ``StageConfig.bin``: global-script flag, stage palette 0x60-0x7F, sfx, objects."""
    r = _Reader(data)
    out = {"load_globals": r.u8(), "palette_0x60": [tuple(r.raw(3)) for _ in range(0x20)]}
    try:
        if engine == 4:
            n = r.u8()
            out["sfx_names"] = [r.string() for _ in range(n)]
            out["sfx"] = [r.string() for _ in range(n)]
            n = r.u8()
            out["objects"] = [r.string() for _ in range(n)]
            out["object_scripts"] = [r.string() for _ in range(n)]
        else:
            n = r.u8()
            out["objects"] = [r.string() for _ in range(n)]
            out["object_scripts"] = [r.string() for _ in range(n)]
            out["sfx"] = [r.string() for _ in range(r.u8())]
    except (IndexError, struct.error):
        pass
    return out


# --------------------------------------------------------------------------
# Text-script sprite frames (v3/v4)
# --------------------------------------------------------------------------

#: ``#platform:`` tags the Origins RSDKv5U engine compiles (see RetroEngine.cpp,
#: engine.gamePlatform / gameRenderType / gameHapticSetting / releaseType).
ORIGINS_PLATFORM_TAGS = {"standard", "sw_rendering", "use_origins", "no_haptics", "no_f_feedback"}

_RE_CALL = re.compile(r"^\s*(LoadSpriteSheet|SpriteFrame)\s*\((.*)\)\s*(?://\s*(.*))?$", re.I)
_RE_ALIAS = re.compile(r"^\s*(?:(?:public|private)\s+alias|#alias)\s+(-?(?:0x[0-9a-f]+|\d+))\s*:\s*([A-Za-z_][\w.]*)", re.I)
_RE_EVENT = re.compile(r"^\s*(?:event|sub)\s+(\w+)\s*$", re.I)
_RE_END_EVENT = re.compile(r"^\s*end\s+(?:event|sub)\b", re.I)


def _strip_comment(line: str) -> str:
    i = line.find("//")
    return line if i < 0 else line[:i]


def apply_platform_blocks(text: str, tags=ORIGINS_PLATFORM_TAGS) -> list:
    """Return (line_no, line) pairs that survive ``#platform:`` filtering."""
    out = []
    skipping = False
    for no, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        low = s.lower()
        if low.startswith("#platform:"):
            tag = _strip_comment(s[len("#platform:"):]).strip().lower()
            skipping = tag not in tags
            continue
        if low.startswith("#endplatform"):
            skipping = False
            continue
        if not skipping:
            out.append((no, line))
    return out


def _int_or_none(tok: str, consts: dict):
    tok = tok.strip()
    m = re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", tok)
    if m:
        return int(tok, 0) if not tok.startswith("-") else -int(tok[1:], 0)
    low = tok.lower()
    if low in ("true",):
        return 1
    if low in ("false",):
        return 0
    return consts.get(low)


def _eval_cond(expr: str, consts: dict, env: dict):
    """True / False, or None when the condition involves something unknown."""
    m = re.fullmatch(r"\s*(.+?)\s*(==|!=|>=|<=|>|<)\s*(.+?)\s*", expr)
    if not m:
        return None
    lhs, op, rhs = m.groups()
    lv = env.get(lhs.strip().lower())
    if lv is None:
        lv = _int_or_none(lhs, consts)
    rv = _int_or_none(rhs, consts)
    if lv is None or rv is None:
        return None
    return {"==": lv == rv, "!=": lv != rv, ">=": lv >= rv, "<=": lv <= rv, ">": lv > rv, "<": lv < rv}[op]


def parse_script_frames(text: str, consts: dict = None, env: dict = None, event: str = "ObjectStartup",
                        tags=ORIGINS_PLATFORM_TAGS) -> dict:
    """Collect ``LoadSpriteSheet`` / ``SpriteFrame`` calls of one script event.

    Returns ``{"sheets", "frames", "all_frames", "unknown_conditions"}`` where
    ``frames`` is the list the object ends up with at run time for ``env``
    (frame IDs = list positions) and ``all_frames`` every call in source order
    with the ``if`` / ``switch`` conditions guarding it.  Unknown conditions
    take their ``if`` branch and are listed in ``unknown_conditions``.
    """
    consts = {k.lower(): v for k, v in (consts or {}).items()}
    env = {k.lower(): v for k, v in (env or {}).items()}
    lines = apply_platform_blocks(text, tags)
    for _no, line in lines:
        m = _RE_ALIAS.match(line)
        if m:
            consts.setdefault(m.group(2).lower(), int(m.group(1), 0))
    in_event = False
    stack = []  # each: {"kind": "if"/"switch", "text", "active": bool|None, "taken": bool, "parent": bool}
    sheet = None
    sheets = []
    frames, all_frames, unknown = [], [], []
    last_label = None

    def live():
        return all(s["active"] is not False for s in stack)

    for no, line in lines:
        if not in_event:
            m = _RE_EVENT.match(line)
            if m and m.group(1).lower() == event.lower():
                in_event = True
            continue
        if _RE_END_EVENT.match(line):
            break
        s = line.strip()
        code = _strip_comment(s).strip()
        low = code.lower()
        if not s:
            last_label = None  # a block comment labels the frames up to the next blank line
            continue
        if s.startswith("//"):
            last_label = s[2:].strip() or last_label
            continue
        if not code:
            continue
        m = re.match(r"if\s+(.*)$", code, re.I)
        if m:
            v = _eval_cond(m.group(1), consts, env) if live() else False
            unk = None
            if v is None and live():
                unk = "line %d: if %s" % (no, m.group(1))
                v = True
            stack.append({"kind": "if", "text": "if " + m.group(1), "active": v, "taken": bool(v), "unknown": unk})
            continue
        if low.startswith("else"):
            if stack and stack[-1]["kind"] == "if":
                top = stack[-1]
                parent_live = all(x["active"] is not False for x in stack[:-1])
                top["active"] = parent_live and not top["taken"]
                top["text"] = "else of (%s)" % top["text"]
                top["taken"] = True
            continue
        if low.startswith("end if"):
            if stack and stack[-1]["kind"] == "if":
                stack.pop()
            continue
        m = re.match(r"switch\s+(.*)$", code, re.I)
        if m:
            var = m.group(1).strip()
            val = env.get(var.lower())
            stack.append({"kind": "switch", "var": var, "val": val, "text": "switch " + var,
                          "active": False, "taken": False, "fall": False})
            continue
        m = re.match(r"(case\s+(.*)|default)$", code, re.I)
        if m and stack and stack[-1]["kind"] == "switch":
            top = stack[-1]
            if m.group(2) is not None:
                cv = _int_or_none(m.group(2), consts)
                hit = top["fall"] or (top["val"] is not None and cv is not None and cv == top["val"])
                if top["val"] is None and not top["taken"]:
                    hit = True
                    top["unknown"] = "line %d: switch %s / case %s" % (no, top["var"], m.group(2))
                top["text"] = "switch %s case %s" % (top["var"], m.group(2))
            else:
                hit = top["fall"] or not top["taken"]
                top["text"] = "switch %s default" % top["var"]
            top["active"] = hit
            top["fall"] = hit
            top["taken"] = top["taken"] or hit
            continue
        if low == "break" and stack and stack[-1]["kind"] == "switch":
            stack[-1]["fall"] = False
            continue
        if low.startswith("end switch"):
            if stack and stack[-1]["kind"] == "switch":
                stack.pop()
            continue
        m = _RE_CALL.match(s)
        if not m:
            continue
        fn, args, comment = m.group(1), m.group(2), m.group(3)
        conds = [x["text"] for x in stack]
        if live():
            for x in stack:
                if x.get("unknown") and x["unknown"] not in unknown:
                    unknown.append(x["unknown"])
        if fn.lower() == "loadspritesheet":
            path = args.strip().strip('"')
            if live():
                sheet = path
                if path not in sheets:
                    sheets.append(path)
            all_frames.append({"call": "LoadSpriteSheet", "sheet": path, "line": no, "conditions": conds,
                               "live": live()})
            continue
        vals = [_int_or_none(a, consts) for a in args.split(",")]
        if len(vals) != 6 or any(v is None for v in vals):
            all_frames.append({"call": "SpriteFrame", "raw": args, "line": no, "conditions": conds,
                               "live": live(), "error": "unparsed arguments"})
            continue
        px, py, w, h, x, y = vals
        fr = {"sheet": sheet, "x": x, "y": y, "w": w, "h": h, "pivot_x": px, "pivot_y": py,
              "duration": None, "label": last_label, "comment": comment, "line": no, "conditions": conds}
        all_frames.append(dict(fr, call="SpriteFrame", live=live(),
                               frame_id=len(frames) if live() else None))
        if live():
            frames.append(fr)
    return {"event": event, "sheets": sheets, "frames": frames, "all_frames": all_frames,
            "unknown_conditions": unknown}


# --------------------------------------------------------------------------
# Compiled-script sprite frames (v3/v4 bytecode in the pack)
# --------------------------------------------------------------------------
# Opcode ("name:paramCount", in opcode order) and variable tables of the
# Retro Engine v3/v4 script VMs, copied from the decompilations
# (RSDKv5/RSDK/Scene/Legacy/v3|v4/ScriptLegacyv*.cpp functions[] and
# variableNames[]; the optional haptics entries at their ends are omitted).

_V3_FUNCS = (
    "End:0 Equal:2 Add:2 Sub:2 Inc:1 Dec:1 Mul:2 Div:2 ShR:2 ShL:2 And:2 Or:2 Xor:2 Mod:2 FlipSign:1 "
    "CheckEqual:2 CheckGreater:2 CheckLower:2 CheckNotEqual:2 IfEqual:3 IfGreater:3 IfGreaterOrEqual:3 "
    "IfLower:3 IfLowerOrEqual:3 IfNotEqual:3 else:0 endif:0 WEqual:3 WGreater:3 WGreaterOrEqual:3 "
    "WLower:3 WLowerOrEqual:3 WNotEqual:3 loop:0 switch:2 break:0 endswitch:0 Rand:2 Sin:2 Cos:2 Sin256:2 "
    "Cos256:2 SinChange:5 CosChange:5 ATan2:3 Interpolate:4 InterpolateXY:7 LoadSpriteSheet:1 "
    "RemoveSpriteSheet:1 DrawSprite:1 DrawSpriteXY:3 DrawSpriteScreenXY:3 DrawTintRect:4 DrawNumbers:7 "
    "DrawActName:7 DrawMenu:3 SpriteFrame:6 EditFrame:7 LoadPalette:5 RotatePalette:3 SetScreenFade:4 "
    "SetActivePalette:3 SetPaletteFade:7 CopyPalette:2 ClearScreen:1 DrawSpriteFX:4 DrawSpriteScreenFX:4 "
    "LoadAnimation:1 SetupMenu:4 AddMenuEntry:3 EditMenuEntry:4 LoadStage:0 DrawRect:8 "
    "ResetObjectEntity:5 PlayerObjectCollision:5 CreateTempObject:4 BindPlayerToObject:2 "
    "PlayerTileCollision:0 ProcessPlayerControl:0 ProcessAnimation:0 DrawObjectAnimation:0 "
    "DrawPlayerAnimation:0 SetMusicTrack:3 PlayMusic:1 StopMusic:0 PlaySfx:2 StopSfx:1 SetSfxAttributes:3 "
    "ObjectTileCollision:4 ObjectTileGrip:4 LoadVideo:1 NextVideoFrame:0 PlayStageSfx:2 StopStageSfx:1 "
    "Not:1 Draw3DScene:0 SetIdentityMatrix:1 MatrixMultiply:2 MatrixTranslateXYZ:4 MatrixScaleXYZ:4 "
    "MatrixRotateX:2 MatrixRotateY:2 MatrixRotateZ:2 MatrixRotateXYZ:4 TransformVertices:3 CallFunction:1 "
    "EndFunction:0 SetLayerDeformation:6 CheckTouchRect:4 GetTileLayerEntry:4 SetTileLayerEntry:4 "
    "GetBit:3 SetBit:3 PauseMusic:0 ResumeMusic:0 ClearDrawList:1 AddDrawListEntityRef:2 "
    "GetDrawListEntityRef:3 SetDrawListEntityRef:3 Get16x16TileInfo:4 Copy16x16Tile:2 Set16x16TileInfo:4 "
    "GetAnimationByName:2 ReadSaveRAM:0 WriteSaveRAM:0 LoadTextFont:1 LoadTextFile:3 DrawText:7 "
    "GetTextInfo:5 GetVersionNumber:2 SetAchievement:2 SetLeaderboard:2 LoadOnlineMenu:1 EngineCallback:1 "
)
_V3_VARS = (
    "TempValue0 TempValue1 TempValue2 TempValue3 TempValue4 TempValue5 TempValue6 TempValue7 CheckResult "
    "ArrayPos0 ArrayPos1 Global Object.EntityNo Object.Type Object.PropertyValue Object.XPos Object.YPos "
    "Object.iXPos Object.iYPos Object.State Object.Rotation Object.Scale Object.Priority Object.DrawOrder "
    "Object.Direction Object.InkEffect Object.Alpha Object.Frame Object.Animation Object.PrevAnimation "
    "Object.AnimationSpeed Object.AnimationTimer Object.Value0 Object.Value1 Object.Value2 Object.Value3 "
    "Object.Value4 Object.Value5 Object.Value6 Object.Value7 Object.OutOfBounds Player.State "
    "Player.ControlMode Player.ControlLock Player.CollisionMode Player.CollisionPlane Player.XPos "
    "Player.YPos Player.iXPos Player.iYPos Player.ScreenXPos Player.ScreenYPos Player.Speed "
    "Player.XVelocity Player.YVelocity Player.Gravity Player.Angle Player.Skidding Player.Pushing "
    "Player.TrackScroll Player.Up Player.Down Player.Left Player.Right Player.JumpPress Player.JumpHold "
    "Player.FollowPlayer1 Player.LookPos Player.Water Player.TopSpeed Player.Acceleration "
    "Player.Deceleration Player.AirAcceleration Player.AirDeceleration Player.GravityStrength "
    "Player.JumpStrength Player.JumpCap Player.RollingAcceleration Player.RollingDeceleration "
    "Player.EntityNo Player.CollisionLeft Player.CollisionTop Player.CollisionRight "
    "Player.CollisionBottom Player.Flailing Player.Timer Player.TileCollisions Player.ObjectInteraction "
    "Player.Visible Player.Rotation Player.Scale Player.Priority Player.DrawOrder Player.Direction "
    "Player.InkEffect Player.Alpha Player.Frame Player.Animation Player.PrevAnimation "
    "Player.AnimationSpeed Player.AnimationTimer Player.Value0 Player.Value1 Player.Value2 Player.Value3 "
    "Player.Value4 Player.Value5 Player.Value6 Player.Value7 Player.Value8 Player.Value9 Player.Value10 "
    "Player.Value11 Player.Value12 Player.Value13 Player.Value14 Player.Value15 Player.OutOfBounds "
    "Stage.State Stage.ActiveList Stage.ListPos Stage.TimeEnabled Stage.MilliSeconds Stage.Seconds "
    "Stage.Minutes Stage.ActNo Stage.PauseEnabled Stage.ListSize Stage.NewXBoundary1 Stage.NewXBoundary2 "
    "Stage.NewYBoundary1 Stage.NewYBoundary2 Stage.XBoundary1 Stage.XBoundary2 Stage.YBoundary1 "
    "Stage.YBoundary2 Stage.DeformationData0 Stage.DeformationData1 Stage.DeformationData2 "
    "Stage.DeformationData3 Stage.WaterLevel Stage.ActiveLayer Stage.MidPoint Stage.PlayerListPos "
    "Stage.ActivePlayer Screen.CameraEnabled Screen.CameraTarget Screen.CameraStyle Screen.DrawListSize "
    "Screen.CenterX Screen.CenterY Screen.XSize Screen.YSize Screen.XOffset Screen.YOffset Screen.ShakeX "
    "Screen.ShakeY Screen.AdjustCameraY TouchScreen.Down TouchScreen.XPos TouchScreen.YPos Music.Volume "
    "Music.CurrentTrack KeyDown.Up KeyDown.Down KeyDown.Left KeyDown.Right KeyDown.ButtonA "
    "KeyDown.ButtonB KeyDown.ButtonC KeyDown.Start KeyPress.Up KeyPress.Down KeyPress.Left KeyPress.Right "
    "KeyPress.ButtonA KeyPress.ButtonB KeyPress.ButtonC KeyPress.Start Menu1.Selection Menu2.Selection "
    "TileLayer.XSize TileLayer.YSize TileLayer.Type TileLayer.Angle TileLayer.XPos TileLayer.YPos "
    "TileLayer.ZPos TileLayer.ParallaxFactor TileLayer.ScrollSpeed TileLayer.ScrollPos "
    "TileLayer.DeformationOffset TileLayer.DeformationOffsetW HParallax.ParallaxFactor "
    "HParallax.ScrollSpeed HParallax.ScrollPos VParallax.ParallaxFactor VParallax.ScrollSpeed "
    "VParallax.ScrollPos 3DScene.NoVertices 3DScene.NoFaces VertexBuffer.x VertexBuffer.y VertexBuffer.z "
    "VertexBuffer.u VertexBuffer.v FaceBuffer.a FaceBuffer.b FaceBuffer.c FaceBuffer.d FaceBuffer.Flag "
    "FaceBuffer.Color 3DScene.ProjectionX 3DScene.ProjectionY Engine.State Stage.DebugMode Engine.Message "
    "SaveRAM Engine.Language Object.SpriteSheet Engine.OnlineActive Engine.FrameSkipTimer "
    "Engine.FrameSkipSetting Engine.SFXVolume Engine.BGMVolume Engine.PlatformID Engine.TrialMode "
    "KeyPress.AnyStart "
)
_V4_FUNCS = (
    "End:0 Equal:2 Add:2 Sub:2 Inc:1 Dec:1 Mul:2 Div:2 ShR:2 ShL:2 And:2 Or:2 Xor:2 Mod:2 FlipSign:1 "
    "CheckEqual:2 CheckGreater:2 CheckLower:2 CheckNotEqual:2 IfEqual:3 IfGreater:3 IfGreaterOrEqual:3 "
    "IfLower:3 IfLowerOrEqual:3 IfNotEqual:3 else:0 endif:0 WEqual:3 WGreater:3 WGreaterOrEqual:3 "
    "WLower:3 WLowerOrEqual:3 WNotEqual:3 loop:0 ForEachActive:3 ForEachAll:3 next:0 switch:2 break:0 "
    "endswitch:0 Rand:2 Sin:2 Cos:2 Sin256:2 Cos256:2 ATan2:3 Interpolate:4 InterpolateXY:7 "
    "LoadSpriteSheet:1 RemoveSpriteSheet:1 DrawSprite:1 DrawSpriteXY:3 DrawSpriteScreenXY:3 "
    "DrawTintRect:4 DrawNumbers:7 DrawActName:7 DrawMenu:3 SpriteFrame:6 EditFrame:7 LoadPalette:5 "
    "RotatePalette:4 SetScreenFade:4 SetActivePalette:3 SetPaletteFade:6 SetPaletteEntry:3 "
    "GetPaletteEntry:3 CopyPalette:5 ClearScreen:1 DrawSpriteFX:4 DrawSpriteScreenFX:4 LoadAnimation:1 "
    "SetupMenu:4 AddMenuEntry:3 EditMenuEntry:4 LoadStage:0 DrawRect:8 ResetObjectEntity:5 "
    "BoxCollisionTest:11 CreateTempObject:4 ProcessObjectMovement:0 ProcessObjectControl:0 "
    "ProcessAnimation:0 DrawObjectAnimation:0 SetMusicTrack:3 PlayMusic:1 StopMusic:0 PauseMusic:0 "
    "ResumeMusic:0 SwapMusicTrack:4 PlaySfx:2 StopSfx:1 SetSfxAttributes:3 ObjectTileCollision:4 "
    "ObjectTileGrip:4 Not:1 Draw3DScene:0 SetIdentityMatrix:1 MatrixMultiply:2 MatrixTranslateXYZ:4 "
    "MatrixScaleXYZ:4 MatrixRotateX:2 MatrixRotateY:2 MatrixRotateZ:2 MatrixRotateXYZ:4 MatrixInverse:1 "
    "TransformVertices:3 CallFunction:1 return:0 SetLayerDeformation:6 CheckTouchRect:4 "
    "GetTileLayerEntry:4 SetTileLayerEntry:4 GetBit:3 SetBit:3 ClearDrawList:1 AddDrawListEntityRef:2 "
    "GetDrawListEntityRef:3 SetDrawListEntityRef:3 Get16x16TileInfo:4 Set16x16TileInfo:4 Copy16x16Tile:2 "
    "GetAnimationByName:2 ReadSaveRAM:0 WriteSaveRAM:0 LoadTextFile:2 GetTextInfo:5 GetVersionNumber:2 "
    "GetTableValue:3 SetTableValue:3 CheckCurrentStageFolder:1 Abs:1 CallNativeFunction:1 "
    "CallNativeFunction2:3 CallNativeFunction4:5 SetObjectRange:1 GetObjectValue:3 SetObjectValue:3 "
    "CopyObject:3 Print:3 CheckCameraProximity:4 SetScreenCount:1 SetScreenVertices:5 GetInputDeviceID:2 "
    "GetFilteredInputDeviceID:4 GetInputDeviceType:2 IsInputDeviceAssigned:1 AssignInputSlotToDevice:2 "
    "IsInputSlotAssigned:1 ResetInputSlotAssignments:0 "
)
_V4_VARS = (
    "temp0 temp1 temp2 temp3 temp4 temp5 temp6 temp7 checkResult arrayPos0 arrayPos1 arrayPos2 arrayPos3 "
    "arrayPos4 arrayPos5 arrayPos6 arrayPos7 global local object.entityPos object.groupID object.type "
    "object.propertyValue object.xpos object.ypos object.ixpos object.iypos object.xvel object.yvel "
    "object.speed object.state object.rotation object.scale object.priority object.drawOrder "
    "object.direction object.inkEffect object.alpha object.frame object.animation object.prevAnimation "
    "object.animationSpeed object.animationTimer object.angle object.lookPosX object.lookPosY "
    "object.collisionMode object.collisionPlane object.controlMode object.controlLock object.pushing "
    "object.visible object.tileCollisions object.interaction object.gravity object.up object.down "
    "object.left object.right object.jumpPress object.jumpHold object.scrollTracking object.floorSensorL "
    "object.floorSensorC object.floorSensorR object.floorSensorLC object.floorSensorRC "
    "object.collisionLeft object.collisionTop object.collisionRight object.collisionBottom "
    "object.outOfBounds object.spriteSheet object.value0 object.value1 object.value2 object.value3 "
    "object.value4 object.value5 object.value6 object.value7 object.value8 object.value9 object.value10 "
    "object.value11 object.value12 object.value13 object.value14 object.value15 object.value16 "
    "object.value17 object.value18 object.value19 object.value20 object.value21 object.value22 "
    "object.value23 object.value24 object.value25 object.value26 object.value27 object.value28 "
    "object.value29 object.value30 object.value31 object.value32 object.value33 object.value34 "
    "object.value35 object.value36 object.value37 object.value38 object.value39 object.value40 "
    "object.value41 object.value42 object.value43 object.value44 object.value45 object.value46 "
    "object.value47 stage.state stage.activeList stage.listPos stage.timeEnabled stage.milliSeconds "
    "stage.seconds stage.minutes stage.actNum stage.pauseEnabled stage.listSize stage.newXBoundary1 "
    "stage.newXBoundary2 stage.newYBoundary1 stage.newYBoundary2 stage.curXBoundary1 stage.curXBoundary2 "
    "stage.curYBoundary1 stage.curYBoundary2 stage.deformationData0 stage.deformationData1 "
    "stage.deformationData2 stage.deformationData3 stage.waterLevel stage.activeLayer stage.midPoint "
    "stage.playerListPos stage.debugMode stage.entityPos screen.cameraEnabled screen.cameraTarget "
    "screen.cameraStyle screen.cameraX screen.cameraY screen.drawListSize screen.xcenter screen.ycenter "
    "screen.xsize screen.ysize screen.xoffset screen.yoffset screen.shakeX screen.shakeY "
    "screen.adjustCameraY touchscreen.down touchscreen.xpos touchscreen.ypos music.volume "
    "music.currentTrack music.position keyDown.up keyDown.down keyDown.left keyDown.right keyDown.buttonA "
    "keyDown.buttonB keyDown.buttonC keyDown.buttonX keyDown.buttonY keyDown.buttonZ keyDown.buttonL "
    "keyDown.buttonR keyDown.start keyDown.select keyPress.up keyPress.down keyPress.left keyPress.right "
    "keyPress.buttonA keyPress.buttonB keyPress.buttonC keyPress.buttonX keyPress.buttonY "
    "keyPress.buttonZ keyPress.buttonL keyPress.buttonR keyPress.start keyPress.select menu1.selection "
    "menu2.selection tileLayer.xsize tileLayer.ysize tileLayer.type tileLayer.angle tileLayer.xpos "
    "tileLayer.ypos tileLayer.zpos tileLayer.parallaxFactor tileLayer.scrollSpeed tileLayer.scrollPos "
    "tileLayer.deformationOffset tileLayer.deformationOffsetW hParallax.parallaxFactor "
    "hParallax.scrollSpeed hParallax.scrollPos vParallax.parallaxFactor vParallax.scrollSpeed "
    "vParallax.scrollPos scene3D.vertexCount scene3D.faceCount scene3D.projectionX scene3D.projectionY "
    "scene3D.fogColor scene3D.fogStrength vertexBuffer.x vertexBuffer.y vertexBuffer.z vertexBuffer.u "
    "vertexBuffer.v faceBuffer.a faceBuffer.b faceBuffer.c faceBuffer.d faceBuffer.flag faceBuffer.color "
    "saveRAM engine.state engine.language engine.onlineActive engine.sfxVolume engine.bgmVolume "
    "engine.trialMode engine.deviceType screen.currentID camera.enabled camera.target camera.style "
    "camera.xpos camera.ypos camera.adjustY "
)

_BC_TABLES = {}


def _bc_tables(engine: int):
    if engine not in _BC_TABLES:
        funcs = (_V3_FUNCS if engine == 3 else _V4_FUNCS).split()
        names = [f.rsplit(":", 1)[0] for f in funcs]
        sizes = [int(f.rsplit(":", 1)[1]) for f in funcs]
        variables = [v.lower() for v in (_V3_VARS if engine == 3 else _V4_VARS).split()]
        _BC_TABLES[engine] = (names, sizes, variables)
    return _BC_TABLES[engine]


def _read_bc_blocks(r: _Reader) -> list:
    count = r.u32()
    out = []
    while count > 0:
        b = r.u8()
        n = b & 0x7F
        if b >= 0x80:
            vals = struct.unpack_from("<%di" % n, r.d, r.p)
            r.p += 4 * n
            out.extend(vals)
        else:
            out.extend(r.raw(n))
        count -= n
    return out


_BC_COMPARE = ("equal", "greater", "greaterorequal", "lower", "lowerorequal", "notequal")


class LegacyBytecode:
    """A v3 (``Data/Scripts/ByteCode/*.bin``) or v4 (``Bytecode/*.bin``) bytecode file.

    ``events[s]`` holds the (code, jump table) start pointers of object script
    ``s`` (v4: update, draw, startup; v3: main, player interaction, draw,
    startup).  Pointers are absolute in the VM, so a stage file loaded after
    the global code (StageConfig ``load_globals``) must get it as ``prefix``.
    """

    def __init__(self, data: bytes, engine: int, prefix: "LegacyBytecode" = None):
        r = _Reader(data)
        self.engine = engine
        code = _read_bc_blocks(r)
        jumps = _read_bc_blocks(r)
        self.code = (prefix.code if prefix else []) + code
        self.jumps = (prefix.jumps if prefix else []) + jumps
        count = r.u16()
        nev = 4 if engine == 3 else 3
        ptrs = [[r.u32() for _ in range(nev)] for _ in range(count)]
        jptrs = [[r.u32() for _ in range(nev)] for _ in range(count)]
        self.events = [list(zip(p, j)) for p, j in zip(ptrs, jptrs)]

    def run_startup(self, script_index: int, globals_list: list, env: dict = None, max_steps: int = 200000) -> dict:
        """Execute one object's startup event far enough to collect its sprite frames.

        ``if`` / ``while`` / ``switch`` run for real on temp variables, the
        global variables (``globals_list`` = GameConfig ``(name, value)``
        pairs, overridable by name through ``env``) and ``env`` values for
        engine variables such as ``stage.playerListPos``.  Any other variable
        reads as 0, ``foreach`` loops see no entities and ``CallFunction`` is
        not followed.  Returns ``{"sheets", "frames", "notes"}``.
        """
        names, sizes, variables = _bc_tables(self.engine)
        env = {k.lower(): v for k, v in (env or {}).items()}
        gvals = [env.get(g.lower(), v) for g, v in globals_list]
        code, jt = self.code, self.jumps
        start, jstart = self.events[script_index][-1]
        ptr = start
        state = {}
        stack = []
        sheet = None
        sheets, frames, notes = [], [], []

        def read_var(var, idx):
            name = variables[var] if var < len(variables) else "?"
            if name == "global":
                return gvals[idx] if idx is not None and 0 <= idx < len(gvals) else 0
            if name in state:
                return state[name]
            return env.get(name, 0)

        def write_var(var, idx, value):
            name = variables[var] if var < len(variables) else "?"
            if name == "global":
                if idx is not None and 0 <= idx < len(gvals):
                    gvals[idx] = value
            elif name.startswith(("temp", "arraypos", "checkresult")):
                state[name] = value

        for _step in range(max_steps):
            op = code[ptr]
            ptr += 1
            if op >= len(names):
                notes.append("unknown opcode %d at %d" % (op, ptr - 1))
                break
            low = names[op].lower()
            operands, refs = [], []
            for _i in range(sizes[op]):
                t = code[ptr]
                ptr += 1
                if t == 1:  # variable: array type, [index type, index], variable id
                    arr = code[ptr]
                    ptr += 1
                    idx = None
                    if arr in (1, 2, 3):
                        it, v = code[ptr], code[ptr + 1]
                        ptr += 2
                        idx = state.get("arraypos%d" % v, 0) if it == 1 else v
                    var = code[ptr]
                    ptr += 1
                    refs.append((var, idx))
                    operands.append(read_var(var, idx))
                elif t == 2:  # int constant
                    operands.append(code[ptr])
                    ptr += 1
                    refs.append(None)
                elif t == 3:  # string constant, 4 chars per int, big-endian
                    n = code[ptr]
                    ptr += 1
                    chars = [chr(((code[ptr + c // 4] & 0xFFFFFFFF) >> (8 * (3 - c % 4))) & 0xFF) for c in range(n)]
                    ptr += n // 4 + 1
                    operands.append("".join(chars))
                    refs.append(None)
                else:
                    notes.append("bad operand type %d at %d" % (t, ptr - 1))
                    return {"sheets": sheets, "frames": frames, "notes": notes}
            if low in ("end", "endfunction", "return"):
                break
            if (low.startswith("if") and low[2:] in _BC_COMPARE) or (low.startswith("w") and low[1:] in _BC_COMPARE):
                cmp = low[2:] if low.startswith("if") else low[1:]
                a, b = operands[1], operands[2]
                ok = {"equal": a == b, "greater": a > b, "greaterorequal": a >= b, "lower": a < b,
                      "lowerorequal": a <= b, "notequal": a != b}[cmp]
                if low.startswith("if"):
                    if not ok:
                        ptr = start + jt[jstart + operands[0]]
                    stack.append(operands[0])
                elif ok:
                    stack.append(operands[0])
                else:
                    ptr = start + jt[jstart + operands[0] + 1]
                continue
            if low == "else":
                ptr = start + jt[jstart + stack.pop() + 1]
                continue
            if low in ("endif", "endswitch"):
                if stack:
                    stack.pop()
                continue
            if low in ("loop", "next"):
                ptr = start + jt[jstart + stack.pop()]
                continue
            if low in ("foreachactive", "foreachall"):
                ptr = start + jt[jstart + operands[0] + 1]
                continue
            if low == "switch":
                j, v = operands[0], operands[1]
                stack.append(j)
                lo, hi = jt[jstart + j], jt[jstart + j + 1]
                ptr = start + (jt[jstart + j + 2] if v < lo or v > hi else jt[jstart + j + 4 + (v - lo)])
                continue
            if low == "break":
                ptr = start + jt[jstart + stack.pop() + 3]
                continue
            if low == "loadspritesheet":
                sheet = operands[0]
                if sheet not in sheets:
                    sheets.append(sheet)
                continue
            if low == "spriteframe":
                px, py, w, h, x, y = operands
                frames.append({"sheet": sheet, "x": x, "y": y, "w": w, "h": h, "pivot_x": px, "pivot_y": py,
                               "duration": None})
                continue
            if not operands or low == "callfunction" or not all(isinstance(o, int) for o in operands):
                if low.startswith("check"):
                    state["checkresult"] = 0  # e.g. CheckCurrentStageFolder: no stage in a dry run
                continue
            a = operands[0]
            b = operands[1] if len(operands) > 1 else 0
            if low in _BC_ARITH:
                if refs[0] is not None:
                    write_var(refs[0][0], refs[0][1], _BC_ARITH[low](a, b))
            elif low in ("checkequal", "checkgreater", "checklower", "checknotequal"):
                state["checkresult"] = int({"checkequal": a == b, "checkgreater": a > b, "checklower": a < b,
                                            "checknotequal": a != b}[low])
            elif low.startswith("check"):
                state["checkresult"] = 0
        else:
            notes.append("step limit reached")
        return {"sheets": sheets, "frames": frames, "notes": notes}


def _c_div(a: int, b: int) -> int:
    return 0 if b == 0 else (abs(a) // abs(b)) * (1 if (a < 0) == (b < 0) else -1)


def _c_mod(a: int, b: int) -> int:
    return 0 if b == 0 else (abs(a) % abs(b)) * (-1 if a < 0 else 1)


_BC_ARITH = {
    "equal": lambda a, b: b, "add": lambda a, b: a + b, "sub": lambda a, b: a - b,
    "inc": lambda a, b: a + 1, "dec": lambda a, b: a - 1, "mul": lambda a, b: a * b,
    "div": _c_div, "mod": _c_mod, "and": lambda a, b: a & b, "or": lambda a, b: a | b,
    "xor": lambda a, b: a ^ b, "flipsign": lambda a, b: -a,
    "shr": lambda a, b: a >> b if 0 <= b < 32 else a, "shl": lambda a, b: a << b if 0 <= b < 32 else a,
}


# --------------------------------------------------------------------------
# Name discovery (for listing a pack)
# --------------------------------------------------------------------------

_RE_QUOTED_PATH = re.compile(r'"([^"\n]+\.[A-Za-z0-9]{2,4})"')
_PREFIXES = ("", "Data/", "Data/Sprites/", "Data/Animations/", "Data/Palettes/", "Data/Scripts/", "Data/Stages/",
             "Data/Game/", "Data/Music/", "Data/SoundFX/", "Data/Video/")


def candidate_names_from_text(text: str) -> set:
    out = set()
    for m in _RE_QUOTED_PATH.finditer(text):
        s = m.group(1).replace("\\", "/").lstrip("/")
        for p in _PREFIXES:
            out.add(p + s)
    return out


def discover_names(pack: RSDKPack, script_dir: str = None, extra=()) -> list:
    """Name as many pack entries as possible from configs, scripts and animation references."""
    cands = set(extra)
    cands.update(["Data/Game/GameConfig.bin", "Data/Palettes/MasterPalette.act", "Bytecode/GlobalCode.bin",
                  "Data/Scripts/ByteCode/GlobalCode.bin", "Data/Game/Game.xml"])
    if script_dir and os.path.isdir(script_dir):
        for root, _d, files in os.walk(script_dir):
            for fn in files:
                if fn.lower().endswith(".txt"):
                    with open(os.path.join(root, fn), encoding="latin-1") as f:
                        cands |= candidate_names_from_text(f.read())
    pack.resolve(cands)
    if pack.has("Data/Game/GameConfig.bin"):
        try:
            gc = parse_gameconfig(pack.read("Data/Game/GameConfig.bin"), pack.engine)
        except Exception:
            gc = {}
        more = set()
        for sp in gc.get("object_scripts", []):
            more.add("Data/Scripts/" + sp)
        for sc in gc.get("scenes", []):
            base = "Data/Stages/%s/" % sc["folder"]
            for fn in ("StageConfig.bin", "16x16Tiles.gif", "128x128Tiles.bin", "CollisionMasks.bin",
                       "Backgrounds.bin", "TileConfig.bin", "Act%s.bin" % sc["id"], "Scene%s.bin" % sc["id"],
                       "Zone.bin", "ZoneConfig.bin"):
                more.add(base + fn)
            if pack.engine != 5:
                more.add("Bytecode/%s.bin" % sc["folder"])
                more.add("Data/Scripts/ByteCode/%s.bin" % sc["folder"])
        pack.resolve(more)
    # follow animation sheet references until nothing new turns up
    seen = set()
    while True:
        todo = [e for e in pack.entries if e.name and e.name not in seen
                and e.name.lower().endswith((".ani", ".bin")) and ("/animations/" in e.name.lower()
                                                                   or "/sprites/" in e.name.lower())]
        if not todo:
            break
        new = set()
        for e in todo:
            seen.add(e.name)
            try:
                anim = parse_animation(pack.read_entry(e, e.name), e.name, pack.engine)
            except Exception:
                continue
            for s in anim["sheets"]:
                new.add("Data/Sprites/" + s)
        pack.resolve(new)
    return sorted(e.name for e in pack.entries if e.name)


# --------------------------------------------------------------------------
# Extraction plan for the four Origins games
# --------------------------------------------------------------------------

#: What to extract.  Player / special entries are animation files: v3/v4 under
#: ``Data/Animations/``, v5 under ``Data/Sprites/``.  Script entries are text
#: scripts (relative to ``exec/<Game>u/Scripts``) whose ObjectStartup defines
#: the sprite frames.
GAMES = {
    "sonic1": {
        "title": "Sonic 1", "pack": "Sonic1u.rsdk", "scripts": "Sonic1u",
        "players": {
            "Sonic": ["Sonic.ani"], "SuperSonic": ["SuperSonic.ani"], "Tails": ["Tails.ani"],
            "Knuckles": ["Knuckles.ani"], "Amy": ["Amy.ani"],
        },
        "special_players": {
            "Sonic": ["SonicSS.ani"], "Tails": ["TailsSS.ani"], "Knuckles": ["KnucklesSS.ani"], "Amy": ["AmySS.ani"],
        },
        "script_objects": {
            "monitors": {"Monitor": ("Monitor", None, "Global/Monitor.txt"),
                         "BrokenMonitor": ("Broken Monitor", None, "Global/BrokenMonitor.txt")},
            "starposts": {"LampPost": ("Lamp Post", None, "Global/LampPost.txt")},
        },
    },
    "sonic2": {
        "title": "Sonic 2", "pack": "Sonic2u.rsdk", "scripts": "Sonic2u",
        "players": {
            "Sonic": ["Sonic.ani"], "SuperSonic": ["SuperSonic.ani"], "Tails": ["Tails.ani"],
            "Knuckles": ["Knuckles.ani"], "Amy": ["Amy.ani"],
        },
        "special_players": {},
        "script_objects": {
            "monitors": {"Monitor": ("Monitor", None, "Global/Monitor.txt"),
                         "BrokenMonitor": ("Broken Monitor", None, "Global/BrokenMonitor.txt")},
            "starposts": {"StarPost": ("Star Post", None, "Global/StarPost.txt")},
            "special": {"SpecialPlayer": ("Player Object", "Special", "Special/PlayerObject.txt")},
        },
    },
    "soniccd": {
        "title": "Sonic CD", "pack": "SonicCDu.rsdk", "scripts": "SonicCDu",
        "players": {
            "Sonic": ["Sonic.ani"], "Tails": ["Tails.ani"], "Knuckles": ["Knuckles.ani"], "Amy": ["Amy.ani"],
            "MiniSonic": ["MiniSonic.ani"], "MiniTails": ["MiniTails.ani"],
            "MiniKnuckles": ["MiniKnuckles.ani"], "MiniAmy": ["MiniAmy.ani"],
        },
        "notes": ["No SuperSonic.ani in Sonic CD.  Data/Animations/MetalSonic.ani exists (enemy, not extracted)."],
        "special_players": {},
        "script_objects": {
            "monitors": {"Monitor": ("Monitor", None, "Global/Monitor.txt"),
                         "BrokenMonitor": ("Broken Monitor", None, "Global/BrokenMonitor.txt")},
            "starposts": {"LampPost": ("LampPost", None, "Global/LampPost.txt")},
            "special": {"SpecialSonic": ("Sonic", "SS1", "Special/Sonic.txt")},
        },
    },
    "sonic3k": {
        "title": "Sonic 3 & Knuckles", "pack": "Sonic3ku.rsdk", "scripts": None,
        "players": {
            "Sonic": ["3K_Players/Sonic.bin"], "SuperSonic": ["3K_Players/SuperSonic.bin"],
            "Tails": ["3K_Players/Tails.bin", "3K_Players/TailSprite.bin"],
            "Knuckles": ["3K_Players/Knux.bin"], "KnucklesCutscene": ["3K_Players/KnuxCutscene.bin"],
            "Amy": ["3K_Players/Amy.bin"],
        },
        "special_players": {
            "Sonic": ["3K_Special/Sonic.bin"], "Tails": ["3K_Special/Tails.bin"],
            "Knuckles": ["3K_Special/Knuckles.bin"], "Amy": ["3K_Special/Amy.bin"],
        },
        "notes": [
            "Not extracted: Data/Sprites/S1_Players/{Sonic,SuperSonic}.bin and S2_Players/{Sonic,SuperSonic,Knux,"
            "Tails,TailSprite}.bin (Mania animation set; S1 and S2 copies identical) name their sheets as "
            "Players/Sonic1.gif ... which is not in this pack; the sheets sit next to them as "
            "S1_Players/Sonic1-4.gif and S2_Players/Sonic1-4.gif, Tails1-3.gif (Sonic Mania art, not S1/S2 art).",
            "Unreferenced sheets in the pack: 3K_Players/Knuckles.gif and 3K_Players/Knux1.gif (older 512x828 "
            "variants of Knux.gif), 3K_Special/Players.gif (older combined Blue Spheres sheet).",
            "Named in SonicOrigins.exe but absent from the pack: 3K_Special/Mighty.bin, 3K_Special/Ray.bin, "
            "SM_Players/Mighty.bin, SM_Players/Ray.bin.",
        ],
        "objects": {
            "monitors": {"ItemBox": ["3K_Global/ItemBox.bin"]},
            "starposts": {"StarPost": ["3K_Global/StarPost.bin"]},
        },
        # Sonic Mania-style player set that also ships in the S3K pack (decodes fine; optional).
        # (S1_Players/*.bin and S2_Players/*.bin are also there but point at Mania's
        # Players/Sonic1.gif ... sheets, which are in no Origins pack -- see "notes".)
        "extra_players": {
            "SM_Sonic": ["SM_Players/Sonic.bin"], "SM_SuperSonic": ["SM_Players/SuperSonic.bin"],
            "SM_Tails": ["SM_Players/Tails.bin", "SM_Players/TailSprite.bin"],
            "SM_Knuckles": ["SM_Players/Knux.bin"], "SM_Amy": ["SM_Players/Amy.bin"],
        },
    },
}

#: Values the player-dependent script branches are evaluated with.  The
#: numeric value comes from the GameConfig globals (``PLAYER_SONIC`` ...).
CHARACTER_GLOBALS = {
    "sonic": ("PLAYER_SONIC", "PLAYER_SONIC_A"),
    "tails": ("PLAYER_TAILS", "PLAYER_TAILS_A"),
    "knuckles": ("PLAYER_KNUCKLES", "PLAYER_KNUCKLES_A"),
    "amy": ("PLAYER_AMY", "PLAYER_AMY_A"),
}


class Extractor:
    def __init__(self, origins: str, out_dir: str, log=print):
        self.origins = origins
        self.out_dir = out_dir
        self.log = log

    # -- helpers -----------------------------------------------------------
    def _write(self, rel: str, data: bytes):
        path = os.path.join(self.out_dir, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def run(self, games=None, contact=True):
        results = {}
        for key in games or GAMES:
            results[key] = self.extract_game(key, contact)
        return results

    def extract_game(self, key: str, contact=True) -> dict:
        spec = GAMES[key]
        pack_path = os.path.join(self.origins, PACK_SUBDIR, spec["pack"])
        pack = RSDKPack(pack_path)
        self.log("%s: %s (%s container, engine v%d, %d entries)" % (
            key, pack_path, pack.signature, pack.engine, len(pack.entries)))
        script_root = (os.path.join(self.origins, SCRIPT_SUBDIR, spec["scripts"], "Scripts")
                       if spec.get("scripts") else None)
        gdir = key
        game = {
            "game": key, "title": spec["title"], "pack": pack.stats(), "engine": pack.engine,
            "script_root": script_root, "sheets": {}, "animation_files": {}, "groups": {},
            "script_objects": {}, "palettes": {}, "missing": [],
        }
        # global palette
        gc = None
        if pack.has("Data/Game/GameConfig.bin"):
            gc = parse_gameconfig(pack.read("Data/Game/GameConfig.bin"), pack.engine)
            if pack.engine == 4:
                game["palettes"]["GameConfig[0x00-0x5F]"] = [list(c) for c in gc["palette"]]
            elif pack.engine == 5:
                for i, bank in enumerate(gc["palette_banks"]):
                    if bank["active_rows_mask"]:
                        game["palettes"]["GameConfig.bank%d" % i] = {
                            "active_rows_mask": bank["active_rows_mask"],
                            "colours": [list(c) if c else None for c in bank["colours"]]}
            game["globals"] = gc.get("globals", {})
        if pack.engine == 3 and pack.has("Data/Palettes/MasterPalette.act"):
            game["palettes"]["MasterPalette.act"] = [list(c) for c in parse_act(pack.read("Data/Palettes/MasterPalette.act"))]

        anim_root = "Data/Sprites/" if pack.engine == 5 else "Data/Animations/"
        game["missing_optional"] = []

        def load_anim(rel, optional=False):
            if rel in game["animation_files"]:
                return game["animation_files"][rel]
            full = anim_root + rel
            if not pack.has(full):
                game["missing_optional" if optional else "missing"].append(full)
                return None
            data = pack.read(full)
            self._write("%s/raw/%s" % (gdir, full), data)
            anim = parse_animation(data, full, pack.engine)
            game["animation_files"][rel] = anim
            for s in anim["sheets"]:
                self.load_sheet(pack, game, gdir, s, optional)
            return anim

        def group(name, mapping, optional=False):
            g = {}
            for who, files in mapping.items():
                got = [f for f in files if load_anim(f, optional) is not None]
                if got:
                    g[who] = got
            if g:
                game["groups"][name] = g

        group("players", spec.get("players", {}))
        group("special_players", spec.get("special_players", {}))
        for gname, mapping in spec.get("objects", {}).items():
            group(gname, mapping)
        group("extra_players", spec.get("extra_players", {}), optional=True)

        # script-defined frames (v3/v4): run the startup bytecode from the pack,
        # annotate from the text scripts when the install has them
        if spec.get("script_objects"):
            self.extract_script_objects(pack, game, gdir, gc, spec, script_root)
        if spec.get("notes"):
            game["notes"] = spec["notes"]

        js = os.path.join(self.out_dir, gdir, "%s.json" % key)
        os.makedirs(os.path.dirname(js), exist_ok=True)
        with open(js, "w", encoding="utf-8") as f:
            json.dump(_strip_private(game), f, indent=1)
        self.log("  wrote %s (%d animation files, %d sheets, %d script objects, missing %s, optional missing %s)" % (
            js, len(game["animation_files"]), len(game["sheets"]), len(game["script_objects"]),
            game["missing"] or "none", game["missing_optional"] or "none"))
        if contact:
            try:
                for p in render_contact_sheet(game, self.out_dir, gdir):
                    self.log("  contact sheet %s" % p)
            except ImportError:
                self.log("  (Pillow not installed: contact sheet skipped)")
        return game

    def extract_script_objects(self, pack, game, gdir, gc, spec, script_root):
        bc_fmt = "Bytecode/%s.bin" if pack.engine == 4 else "Data/Scripts/ByteCode/%s.bin"
        globals_list = list(gc.get("globals", {}).items())
        consts = dict(gc.get("globals", {}))
        chars = {}
        for who, names in CHARACTER_GLOBALS.items():
            val = next((consts[n] for n in names if n in consts), None)
            if val is not None:
                chars[who] = val
        global_bc = None
        stage_bc = {}

        def strip(frames):
            return [(f["sheet"], f["x"], f["y"], f["w"], f["h"], f["pivot_x"], f["pivot_y"]) for f in frames]

        for gname, objs in spec["script_objects"].items():
            for key, (obj_name, stage, rel) in objs.items():
                entry = {"object": obj_name, "stage_folder": stage, "script": rel}
                # -- bytecode (authoritative, always in the pack) --------------
                try:
                    if global_bc is None:
                        global_bc = LegacyBytecode(pack.read(bc_fmt % "GlobalCode"), pack.engine)
                    if stage is None:
                        bc, index = global_bc, gc["objects"].index(obj_name)
                        entry["bytecode"] = bc_fmt % "GlobalCode"
                    else:
                        st = parse_stageconfig_legacy(pack.read("Data/Stages/%s/StageConfig.bin" % stage), pack.engine)
                        if stage not in stage_bc:
                            stage_bc[stage] = LegacyBytecode(pack.read(bc_fmt % stage), pack.engine,
                                                             global_bc if st["load_globals"] else None)
                        bc, index = stage_bc[stage], st["objects"].index(obj_name)
                        entry["bytecode"] = bc_fmt % stage
                    entry["script_index"] = index
                    runs = {who: bc.run_startup(index, globals_list, {"stage.playerListPos": v})
                            for who, v in chars.items()}
                except (KeyError, ValueError, IndexError) as exc:
                    game["missing"].append("bytecode:%s (%s)" % (obj_name, exc))
                    continue
                base = runs.get("sonic") or next(iter(runs.values()))
                entry["source"] = "bytecode"
                entry["frames"] = base["frames"]
                per = {w: r["frames"] for w, r in runs.items() if strip(r["frames"]) != strip(base["frames"])
                       or [f["sheet"] for f in r["frames"]] != [f["sheet"] for f in base["frames"]]}
                if per:
                    entry["frames_by_character"] = per
                entry["sheets"] = []
                for r in runs.values():
                    for s in r["sheets"]:
                        if s not in entry["sheets"]:
                            entry["sheets"].append(s)
                notes = sorted({n for r in runs.values() for n in r["notes"]})
                if notes:
                    entry["vm_notes"] = notes
                # -- text script (optional): labels, comments, conditions -------
                src = os.path.join(script_root, rel.replace("/", os.sep)) if script_root else None
                if src and os.path.isfile(src):
                    with open(src, encoding="latin-1") as f:
                        text = f.read()
                    entry["text_script"] = src
                    matches = True
                    for who, v in chars.items():
                        tv = parse_script_frames(text, consts, {"stage.playerListPos": v})
                        frames = entry["frames"] if who == "sonic" else entry.get("frames_by_character", {}).get(who)
                        if frames is None:
                            continue
                        if strip(tv["frames"]) != strip(frames):
                            matches = False
                            continue
                        for fr, tf in zip(frames, tv["frames"]):
                            for k in ("label", "comment", "line", "conditions"):
                                fr[k] = tf[k]
                        if who == "sonic":
                            entry["all_frames"] = tv["all_frames"]
                            if tv["unknown_conditions"]:
                                entry["unknown_conditions"] = tv["unknown_conditions"]
                    entry["text_matches_bytecode"] = matches
                for s in entry["sheets"]:
                    self.load_sheet(pack, game, gdir, s)
                game["script_objects"][key] = entry
                game["groups"].setdefault(gname, {})[key] = ["script:" + rel]

    def load_sheet(self, pack, game, gdir, sheet, optional=False):
        if sheet in game["sheets"]:
            return game["sheets"][sheet]
        full = "Data/Sprites/" + sheet
        if not pack.has(full):
            game["missing_optional" if optional else "missing"].append(full)
            game["sheets"][sheet] = None
            return None
        data = pack.read(full)
        self._write("%s/raw/%s" % (gdir, full), data)
        img = decode_gif(data)
        stem = os.path.splitext(sheet)[0]
        rgba_rel = "%s/sheets/%s.png" % (gdir, stem)
        idx_rel = "%s/sheets/%s.indexed.png" % (gdir, stem)
        self._write(rgba_rel, png_bytes_rgba(img.width, img.height, img.rgba()))
        self._write(idx_rel, png_bytes_indexed(img))
        info = {
            "pack_path": full, "raw": "raw/" + full, "png": rgba_rel[len(gdir) + 1:],
            "indexed_png": idx_rel[len(gdir) + 1:], "width": img.width, "height": img.height,
            "transparent_index": 0, "gif_palette_entries": img.palette_size,
            "gif_transparent_index": img.gif_transparent,
            "palette": [list(c) for c in img.palette],
            "used_indices": sorted(set(img.pixels)),
        }
        game["sheets"][sheet] = info
        game.setdefault("_images", {})[sheet] = img
        return info


# --------------------------------------------------------------------------
# Contact sheet
# --------------------------------------------------------------------------


CONTACT_GROUPS = ("players", "special_players", "monitors", "starposts", "special", "extra_players")


def _contact_rows(game: dict, gname: str) -> list:
    rows = []  # (title, [(label, frame-or-None)])
    for who, files in game["groups"].get(gname, {}).items():
        for f in files:
            if f.startswith("script:"):
                obj = next((o for o, e in game["script_objects"].items() if "script:" + e["script"] == f), None)
                if obj is None:
                    continue
                so = game["script_objects"][obj]
                rows.append(("%s / %s / %s (script frames, Sonic)" % (gname, who, f[7:]),
                             [("#%d %s" % (i, fr.get("comment") or fr.get("label") or ""), fr)
                              for i, fr in enumerate(so["frames"])]))
                for ch, frs in so.get("frames_by_character", {}).items():
                    rows.append(("%s / %s (%s)" % (gname, who, ch),
                                 [("#%d" % i, fr) for i, fr in enumerate(frs)]))
                continue
            anim = game["animation_files"][f]
            cells = [(name, a["frames"][0] if a["frames"] else None) for name, a in anim["animations"].items()]
            rows.append(("%s / %s / %s" % (gname, who, f), cells))
    return rows


def render_contact_sheet(game: dict, out_dir: str, gdir: str, groups=CONTACT_GROUPS, scale: int = 2,
                         width: int = 1500) -> list:
    """First frame of every animation, cut from its sheet and drawn about its pivot (red cross).

    Writes ``<game>_contact.png`` (players) and ``<game>_contact_<group>.png``
    for the other groups; returns the paths written.
    """
    from PIL import Image, ImageDraw

    images = game.get("_images", {})
    written = []
    cache = {}
    for gname in groups:
        rows = _contact_rows(game, gname)
        if not rows:
            continue
        layout = []
        y = 0
        for title, cells in rows:
            ext = [8, 8, 8, 8]  # pixels left / above / right / below the pivot
            for _l, fr in cells:
                if fr is None:
                    continue
                ext[0] = max(ext[0], -fr["pivot_x"])
                ext[1] = max(ext[1], -fr["pivot_y"])
                ext[2] = max(ext[2], fr["pivot_x"] + fr["w"])
                ext[3] = max(ext[3], fr["pivot_y"] + fr["h"])
            ext = [min(e, 128) for e in ext]
            cw = max((ext[0] + ext[2]) * scale + 8, 96)
            chh = (ext[1] + ext[3]) * scale + 22
            per_row = max(1, width // cw)
            layout.append((y, title, cells, ext, cw, chh, per_row))
            y += 16 + chh * max(1, (len(cells) + per_row - 1) // per_row)
        sheet = Image.new("RGBA", (width, max(y, 1)), (40, 40, 56, 255))
        draw = ImageDraw.Draw(sheet)
        for y0, title, cells, ext, cw, chh, per_row in layout:
            draw.text((4, y0 + 2), title, fill=(255, 255, 0, 255))
            for i, (label, fr) in enumerate(cells):
                cx = (i % per_row) * cw
                cy = y0 + 16 + (i // per_row) * chh
                draw.rectangle([cx + 1, cy + 1, cx + cw - 2, cy + chh - 2], fill=(84, 84, 110, 255))
                ox = cx + (cw - (ext[0] + ext[2]) * scale) // 2 + ext[0] * scale
                oy = cy + 4 + ext[1] * scale
                img = images.get(fr["sheet"]) if fr else None
                if img is not None and fr["w"] > 0 and fr["h"] > 0:
                    key = (fr["sheet"], fr["x"], fr["y"], fr["w"], fr["h"])
                    if key not in cache:
                        tile = Image.frombytes("RGBA", (fr["w"], fr["h"]),
                                               img.crop_rgba(fr["x"], fr["y"], fr["w"], fr["h"]))
                        cache[key] = tile.resize((fr["w"] * scale, fr["h"] * scale), Image.NEAREST)
                    sheet.alpha_composite(cache[key], (ox + fr["pivot_x"] * scale, oy + fr["pivot_y"] * scale))
                elif fr is None:
                    label += " (0 frames)"
                elif img is None:
                    label += " (no sheet)"
                draw.line([ox - 4, oy, ox + 4, oy], fill=(255, 60, 60, 255))
                draw.line([ox, oy - 4, ox, oy + 4], fill=(255, 60, 60, 255))
                draw.text((cx + 3, cy + chh - 16), label[:max(4, (cw - 4) // 6)], fill=(255, 255, 255, 255))
        suffix = "" if gname == "players" else "_" + gname
        path = os.path.join(out_dir, gdir, "%s_contact%s.png" % (game["game"], suffix))
        sheet.convert("RGB").save(path)
        written.append(path)
    return written


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def _strip_private(game: dict) -> dict:
    return {k: v for k, v in game.items() if not k.startswith("_")}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    ex = sub.add_parser("extract", help="extract players / monitors / star posts / specials (default)")
    ex.add_argument("--origins", default=DEFAULT_ORIGINS, help="Sonic Origins install folder")
    ex.add_argument("--out", default=DEFAULT_OUT, help="output folder (outside the repository)")
    ex.add_argument("--games", default=",".join(GAMES), help="comma list of %s" % ",".join(GAMES))
    ex.add_argument("--no-contact", action="store_true", help="skip the contact sheets")
    ls = sub.add_parser("ls", help="list a pack's entries, naming what can be discovered")
    ls.add_argument("pack")
    ls.add_argument("--scripts", help="text-script folder used for name discovery")
    cat = sub.add_parser("cat", help="write one decrypted file")
    cat.add_argument("pack")
    cat.add_argument("path")
    cat.add_argument("out")
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] not in ("extract", "ls", "cat", "-h", "--help"):
        argv = ["extract"] + argv  # extraction is the default command
    args = ap.parse_args(argv)
    if args.cmd == "extract":
        repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        out = os.path.abspath(args.out)
        # (a checkout only: packaged in AnthologyBuilder, "the repository" is the temp dir)
        if os.path.exists(os.path.join(repo, ".git")) and os.path.commonpath([repo, out]) == repo:
            ap.error("refusing to write extracted game data inside the repository (%s)" % out)
        ext = Extractor(args.origins, out)
        ext.run([g.strip() for g in args.games.split(",") if g.strip()], contact=not args.no_contact)
        return 0
    if args.cmd == "ls":
        pack = RSDKPack(args.pack)
        names = discover_names(pack, args.scripts)
        st = pack.stats()
        print("# %s: %s, %d entries, %d encrypted, %d named" % (
            args.pack, st["signature"], st["entries"], st["encrypted_entries"], len(names)))
        for e in pack.entries:
            print("%5d %10d %s" % (e.index, e.size, e.name or "?" + e.hash.hex()))
        return 0
    if args.cmd == "cat":
        pack = RSDKPack(args.pack)
        with open(args.out, "wb") as f:
            f.write(pack.read(args.path))
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
