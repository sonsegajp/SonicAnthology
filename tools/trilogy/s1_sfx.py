"""Sonic 1's sound effects on the retail S3K sound driver: the S1 sound set.

As s2_sfx's Sonic 2 set (trilogy_music.c swaps it in while a Sonic 1 zone
runs): the driver plays every effect from one 32 KB bank through a pointer
table in Z80 RAM (z80_SFXPointers, ids $33-$DF), and here both point at the
bank built below:

  - every S3K id the shared player and object code plays that Sonic 1 has a
    sound for (jump, roll, rings, springs, spikes, skid, splash, drowning,
    explosions, lamppost, signpost, tally...) carries Sonic 1's (S3K_TO_S1);
  - the S3K ids with no Sonic 1 counterpart that can play in a Sonic 1 zone
    (elemental shields and their moves, the spin dash, Tails' flight,
    Knuckles' moves, everything the trilogy's own code plays) keep S3K's,
    moved into this bank (S3K_KEEP, s2_sfx.code_sfx_ids);
  - every other S1 effect ($A0-$CF, the waterfall $D0) gets an S3K id only
    S3K's own zones play, and Sonic 1's objects ask for S1 sound ids
    (o_sfx_s1) through the table in the blob.

S1 effects are SMPS 68k, the format of its songs: their tracks are
re-encoded by s1_music's song rules (flags, rests, PSG +12, the TL bits of
the voices) behind an S3K effect header. S1's PSG envelopes lead the bank at
the offsets every S1 song bank has them (the driver reads an effect's
envelope with this bank switched in).
"""
import struct

import s1_music
from s1_music import be16
from s2_sfx import S3K_KEEP as S2_KEEP, S3kSfx, code_sfx_ids, FIRST, LAST
from s3k_driver import SFX_PTRS, SND_BANK, z80_image, le16

S1_FIRST, S1_LAST = 0xA0, 0xD0          # sfx_Jump .. sfx_Waterfall (SpecSoundIndex)

# S3K id -> S1 sound, for the sounds S3K's shared code plays in an S1 zone
S3K_TO_S1 = {
    0x33: 0xB5, 0x34: 0xCE,             # rings (right / left speaker)
    0x35: 0xA3,                         # death / hurt -> Death
    0x36: 0xA4,                         # skid
    0x37: 0xA6,                         # spike hit -> HitSpikes
    0x38: 0xAD,                         # air bubble -> GetBubble
    0x39: 0xAA, 0x6C: 0xAA,             # splash
    0x3A: 0xAF,                         # shield
    0x3B: 0xB2,                         # drown -> DrownDeath
    0x3C: 0xBE,                         # roll
    0x3D: 0xC1,                         # Obj_Explosion (badniks, monitors) -> BreakItem
    0x52: 0xB6,                         # spike move -> SpikesMove
    0x5B: 0xCD,                         # switch (results tick) -> Switch
    0x62: 0xA0,                         # jump
    0x63: 0xA1,                         # star post -> Lamppost
    0x69: 0xA7,                         # push -> PushBlock
    0x6E: 0xAC,                         # boss hit -> HitBoss
    0x70: 0xAE,                         # lava ball -> Fireball
    0xA9: 0xC2,                         # air countdown ding -> DrownWarning
    0xAA: 0xB4,                         # bumper
    0xAC: 0xBF,                         # continue -> GetContinue
    0xAF: 0xCA,                         # enter special stage -> EnterSS
    0xB0: 0xC5,                         # register (tally end) -> CashRegister
    0xB1: 0xCC,                         # spring
    0xB3: 0xC3,                         # giant ring
    0xB4: 0xC4,                         # big explosion -> Bomb
    0xB8: 0xCF,                         # signpost
    0xB9: 0xC6,                         # ring loss
}
# S3K effects with no Sonic 1 counterpart that can play in an S1 zone: the S2
# set's, and Sonic 1 has no spin dash, super form, flipper or error sound
S3K_KEEP = S2_KEEP + [0xAB, 0xB6, 0x9F, 0xAE, 0xB2]


class S1Sfx(s1_music.Song):
    """One S1 sound effect (SMPS 68k: pointers from its header), decoded by
    s1_music.Song's walk and re-encoded by its rules."""

    def __init__(self, rom, start):
        self.rom, self.start = rom, start
        self.voice_ptr = start + be16(rom, start)
        self.div, n = rom[start + 2], rom[start + 3]
        self.tempo = 0
        self.ins, self.ctx, self.cdiv = {}, {}, {}
        self.max_voice = -1
        self.chans = []
        for i in range(n):
            a = start + 4 + i * 6
            ch = rom[a + 1]
            self.chans.append(('psg' if ch & 0x80 else 'fm', rom[a], ch, start + be16(rom, a + 2), rom[a + 4], rom[a + 5]))
        self.tr_at = {}
        for kind, _, _, ptr, tr, _ in self.chans:
            seen = set(self.ins)
            self._walk(ptr, kind)
            for a in set(self.ins) - seen:
                self.tr_at[a] = tr
        self.uses_e5 = any(r[0] == 0xE5 for r, _, _ in self.ins.values())

    def _encode(self, addr):
        raw, typ, _ = self.ins[addr]
        b = raw[0]
        # nMaxPSG in noise mode: S1's driver $C6, S3K's $C7. An effect spells
        # it against its header transposition, so the note that lands there moves
        if typ == 'byte' and self.ctx[addr] == 'psg' and 0x81 <= b < 0xE0 and self.noise.get(addr):
            if (b + self.tr_at.get(addr, 0)) & 0xFF == 0xC6:
                return bytes([b + 1]), None
            return raw, None
        return super()._encode(addr)

    def assemble(self, base):
        hdr_len = 4 + len(self.chans) * 6
        body, newaddr, fixups = bytearray(), {}, []
        # S3K's stop keys off only if 'do not attack' is clear (s2_sfx's rule)
        held = any(self._encode(a)[0][:1] == b'\xE7' for a in self.ins)
        for a in sorted(self.ins):
            newaddr[a] = base + hdr_len + len(body)
            enc, fix = self._encode(a)
            if held and enc == b'\xF2':
                enc = b'\x80\x01\xF2'
            if fix:
                fixups.append((len(body) + fix[0], fix[1]))
            body += enc
        for off, tgt in fixups:
            if tgt not in newaddr:
                raise ValueError(f"jump into undecoded data at {tgt:X}")
            struct.pack_into('<H', body, off, newaddr[tgt])
        voices = bytearray()
        for v in range(self.max_voice + 1):
            vc = bytearray(self.rom[self.voice_ptr + v * 25:self.voice_ptr + v * 25 + 25])
            alg = vc[0] & 7
            masks = [(alg == 7) << 7, (alg >= 4) << 7, (alg >= 5) << 7, 0x80]   # TL4, TL3, TL2, TL1
            for i in range(4):
                vc[21 + i] = (vc[21 + i] & 0x7F) | masks[i]
            voices += vc
        voice_addr = base + hdr_len + len(body) if voices else 0
        hdr = bytearray(struct.pack('<H', voice_addr)) + bytes([1 if self.uses_e5 else self.div, len(self.chans)])
        for kind, flags, ch, ptr, tr, vol in self.chans:
            if kind == 'psg':
                tr = (tr + 12) & 0xFF
            hdr += bytes([flags, ch]) + struct.pack('<H', newaddr[ptr]) + bytes([tr, vol])
        return bytes(hdr + body + voices)


def build(s1rom, s3krom, bank, sym):
    """-> (table blob, bank bytes used, {S1 sound id: S3K id}). Blob: u8 bank byte, then a
    u16 LE Z80 pointer per S3K id $33-$DF, then a u8 S3K id per S1 sound $A0-$D0."""
    ram = z80_image(s3krom)
    w3 = s3krom[SND_BANK:SND_BANK + 0x8000]
    env = bytearray(b''.join(s1_music.s1_envelopes(s1rom)))   # (where s1_music's song banks have them)
    if len(env) & 1:
        env.append(0)
    out = bytearray(env)
    ptr = {}
    silent = 0x8000 + len(out)                   # one PSG3 track that stops at once
    out += bytes([0, 0, 1, 1, 0x80, 0xC0]) + struct.pack('<H', silent + 10) + bytes([0, 0]) + b'\xF2\x00'
    s1_at = {}                                   # S1 sound id -> Z80 pointer
    for sid in range(S1_FIRST, S1_LAST + 1):
        start = struct.unpack_from('>I', s1rom, sym['SoundIndex'] + (sid - S1_FIRST) * 4)[0] if sid < 0xD0 \
            else struct.unpack_from('>I', s1rom, sym['SpecSoundIndex'])[0]
        s1_at[sid] = 0x8000 + len(out)
        out += S1Sfx(s1rom, start).assemble(0x8000 + len(out))
        if len(out) & 1:
            out.append(0)
    used = code_sfx_ids()
    keep = sorted(set(S3K_KEEP) | {k for k in used if FIRST <= k <= LAST and k not in S3K_TO_S1})
    for k in keep:                               # (S3K's own, moved: nothing the game asks for goes silent)
        p = le16(ram, SFX_PTRS + (k - FIRST) * 2)
        if p < 0x8000:
            continue
        ptr[k] = 0x8000 + len(out)
        out += S3kSfx(w3, p).assemble(0x8000 + len(out))
        if len(out) & 1:
            out.append(0)
    if len(out) > 0x8000:
        raise SystemExit(f"S1 sound set: {len(out)} bytes do not fit a bank")
    for k, s in S3K_TO_S1.items():
        ptr[k] = s1_at[s]
    s1_id = {}
    for k, s in sorted(S3K_TO_S1.items(), reverse=True):
        s1_id[s] = k
    free = [k for k in range(0x46, 0xDC) if k not in ptr and k not in used]   # ids only S3K's own zones play
    for s in range(S1_FIRST, S1_LAST + 1):
        if s in s1_id:
            continue
        k = free.pop(0)
        ptr[k] = s1_at[s]
        s1_id[s] = k
    addr = bank.add(bytes(out), align=0x8000, dedupe=False)
    blob = bytearray([(addr >> 15) & 0xFF])
    for k in range(FIRST, LAST + 1):
        blob += struct.pack('<H', ptr.get(k, silent))
    for s in range(S1_FIRST, S1_LAST + 1):
        blob.append(s1_id.get(s, 0))
    return bytes(blob), len(out), s1_id
