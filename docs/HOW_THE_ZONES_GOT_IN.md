# How Sonic 1 and Sonic 2 got into Sonic 3 & Knuckles

Sonic Anthology plays every zone of Sonic 1 and Sonic 2 in the Sonic 3 & Knuckles
engine, beside S3K's own zones, from one Data Select. This document explains
how: what runs, what was converted, what was rewritten, and the rules that kept
it honest.

## The ground rules

- **The S3K cartridge is never modified.** It runs as a static recompilation
  (mstan's [Sonic3AndKnucklesRecomp](https://github.com/mstan/Sonic3AndKnucklesRecomp)):
  the 68000 code becomes C, compiled natively. S3K's player physics, camera,
  sound driver, results tally, special stages and save files are the original
  code, running unchanged.
- **Sonic 1 and Sonic 2 are not emulated.** Their zones run *inside* the S3K
  engine. Their data is converted into S3K's formats, and their code (objects,
  bosses, level events, the S2 special stage and ending) is transcribed by hand
  into C, routine by routine, from the Sonic Retro disassemblies.
- **Nothing from the games is in this repository.** The converters in
  `tools/trilogy/` run on the player's own cartridges (AnthologyBuilder) and
  write `trilogy.bin`. The repo holds code, plus label addresses
  (`tools/trilogy/data/`).
- **Faithful, not approximate.** Object behaviour is transcribed from the
  original routines (the C keeps the disassembly's routine names in comments)
  and checked against the original games, not rebuilt from memory.

## Two seams into an unmodified cartridge

Everything the anthology adds reaches the S3K code through two engine contracts
(`game/trilogy/trilogy.h`):

1. **Hook sites.** About a hundred instructions in the retail code are marked
   as hooks (`tools/trilogy/hooksites.py` names each one by its routine label
   and instruction, resolves it to a cartridge address, and writes
   `trilogy_hooks.h`). Just before or after such an instruction, our C runs and
   can repoint a register, or replace the routine outright. Most hooks sit where
   S3K fetches a per-zone table entry (level layout, collision, palette, art
   cues, music, object layout, start position...) and point it at the anthology
   zone's data instead. The retail routine then runs as usual on our data.
2. **Native code addresses.** Code addresses from `$7E0000` up are not
   cartridge code but C functions (`dispatch_override`). An object whose code
   pointer is `$7E0000 + id*4` runs Sonic 1 object `id` in C (`$7E1000 + id*4`
   for Sonic 2). Zone screen events, the Sonic 2 special stage and the endings
   are native routines too.

Anthology zones get their own zone numbers (`$20`–`$26` for Sonic 1, `$28`–`$32`
for Sonic 2), so S3K's own zones can never be confused with them.

## The expansion bank

`trilogy.bin` is mapped into the 68000's address space at `$400000`, past the
4 MB cartridge, where it reads like more cartridge. It starts with a directory
of zone/act entries, each pointing at S3K-format structures: level-load block,
layout, collision indexes, level sizes, start positions, object and ring
layouts, angle and height maps, object pointer table, animated-art
descriptors. Palettes, art cues, music banks and dozens of data blobs (boss
paths, conveyor lists, title-card letters, the S2 special stage...) follow
(`TRI_BLOB_*` in `trilogy.h`).

## Levels

**Sonic 2** is already close to S3K: 128 px chunks whose words carry S3K's
flip and two-path solidity bits, 16 px blocks, two collision indexes
(`tools/trilogy/s2_level.py`). Mostly, its interleaved FG/BG layout is
re-laid as S3K row pointers and the art is repacked.

**Sonic 1** is further away. Its chunks are 256 px, so each becomes four S3K
128 px chunks. It has one collision set and decides loops in code (the
chunk under the player plus position and angle, `Sonic_Loops`). The converter
builds a synthetic block wherever a loop's "behind" path uses a different
block: front visuals, front shape as primary collision, behind shape as
secondary. At runtime, "behind the loop" just selects S3K's secondary
solidity (`trilogy_player.c`). S1's collision shapes are mirrored and rebuilt
into S3K's floor and wall arrays where the chunks flip them.

For both games, only the tiles an act can actually show are kept. They are
recoloured per palette line into S3K's conventions and packed under S3K's
level-art limit, leaving one free run of VRAM for object art
(`tilepack.py`).

**Backgrounds.** The foreground is drawn by S3K's own tile streamers on the
converted layout. The background is drawn natively, band by band, into
plane B, then the original game's deformation routine (transcribed) fills the
line-scroll table. So Green Hill's clouds and water and Chemical Plant's
parallax move exactly as they did (`trilogy_screen.c`).

**Camera and events.** Each game's dynamic level events (camera limits, boss
arenas, boss spawns) are transcribed (`trilogy_s1dle.c`, `trilogy_s2dle.c`).
Their variables map onto S3K's (for example S1's `v_limitbtm1` is
`Camera_target_max_Y_pos`), and S3K's own camera eases toward them.

## Objects

Common objects become S3K's own: monitors, springs, star posts, path swappers,
bubble makers, the giant ring. The S3 characters already know how to use them,
shields and all. Everything else is the original game's object, transcribed
into C (`s1_obj_*.c`, `s2_obj_*.c`), running on S3K's engine services:
`SolidObject`, `Touch_Enemy`, `Draw_Sprite` and the rest. That way badniks,
platforms and bosses interact with both S3 players exactly as S3K objects do.

The recurring work in a transcription:

- **Field layout.** S1 and S2 keep object variables at offsets that S3K uses
  for status, subtype and collision. Every file states where its moved fields
  live (usually `$30`–`$47`, S3K's scratch space).
- **Collision sizes.** Where S1 or S2 used a touch box S3K's table lacks, the
  object tests the original box itself.
- **Loading.** S3K's object loader is windowed vertically; S1's and S2's are
  horizontal only. The converter marks every placement to skip the vertical
  test, and moves S2's "remember" flag to another bit. That way objects
  appear wherever the original game would have spawned them.
- **Art.** Each object's art is recoloured to the S3K palette conventions and
  placed in free VRAM, and its mappings are converted to S3K's 6-byte pieces.
  Its animation scripts stay as they are: S2's format is S1's, and `o_animate`
  transcribes it.

Some things in the originals aren't objects at all, and were found the hard
way. Casino Night's wall bumpers, for example, are a separate list the level
reads (`SpecialCNZBumpers`), now its own bank blob, checked every frame like S2
did.

## Colour

S3K reserves palette line 0 for the character: Knuckles recolours it, Super
Sonic cycles it. But S1 and S2 draw badniks, bosses and signposts with the
player's line. So that art keeps its own game's line-0 colours on an
**extended palette line**. The converter flags those tiles, and the renderer
draws them from an extra 16-colour line that fades step for step with the real
palette and has an underwater set (`trilogy_extpal.c`). The same mechanism
covers plane tiles that need it, like the Casino Night slot reels.

Palette cycles (Green Hill's waterfalls, Oil Ocean's oil, Wing Fortress's
fire and belts...) are converted from each game's cycle tables.

## Sound

- **Music.** S1's songs (68k SMPS) and S2's (Z80 SMPS, some Saxman-compressed)
  are re-assembled for S3K's sound driver, following SMPS2ASM's cross-driver
  rules: tempo conversion, coordination-flag re-encoding, FM operator order,
  carrier TL bits, PSG transposition, rest handling (`s1_music.py`,
  `s2_music.py`). When a Sonic 1 or 2 zone wants music, two song slots S3K never
  uses in one-player (the competition themes) are pointed at the converted
  songs. The games' PSG envelopes go into S3K envelope slots nothing else uses
  (`trilogy_music.c`).
- **Drums.** S2's own DAC samples are copied into a bank of their own and
  played through the S3K driver's DAC tables (`s2_dac.py`).
- **Sound effects.** In a Sonic 2 zone the whole effect table is swapped. Every
  sound the shared player code plays that S2 has an equivalent for (jump, roll,
  rings, springs, skid...) plays S2's. S3K-only moves (shields, Tails' flight,
  Knuckles' glide) keep S3K's. Anything else is silent, so no stray sound can
  play (`s2_sfx.py`).

## Flow: title cards, act ends, saves

- **Title cards.** S3K's own title card, with zone names built from its letter
  art (`build_title_cards`). Sonic 1's Act 3 numeral is drawn to match.
- **Act ends.** The original game's signpost or prison capsule (transcribed),
  then S3K's results tally. When it finishes, the next act of the campaign
  loads as a fresh level (`trilogy_flow.c`).
- **Saves.** S3K's 10-byte save slots keep a level code. The anthology numbers
  every zone in campaign order: Sonic 1's seven, Sonic 2's eleven, then S3K's
  fourteen. S3K's `SaveGame` and the Data Select read and write those codes
  through hooks (`trilogy_save.c`). A new game starts at Green Hill.
- **Data Select previews.** Each zone card is recorded from the game itself:
  every act 1 runs hidden while frames are captured, reduced and packed by the
  builder (`previews.py`).

## Special stages and endings

- **One emerald run** spans the whole anthology: every special stage plays for
  the next emerald not yet collected, whichever game the zone comes from. Once
  the seven Chaos Emeralds are in, they become Super Emeralds (`trilogy_ss.c`).
  Players choose per game (Sonic 1 and S3K: Blue Spheres; Sonic 2: its
  half-pipe) or one kind throughout.
- **Sonic 2's half-pipe** is transcribed in full and runs as a native scene
  off S3K's game loop. It uses S2's own object table layout, sprite builder,
  VBlank work and VDP setup, so its objects transcribe offset for offset
  (`trilogy_s2ss.c`).
- **Sonic 2's ending** runs the same way: the photos, Sonic in space, the
  Tornado rescue. Then it leads into Sonic 3's Angel Island intro
  (`trilogy_s2end.c`). Between Launch Base and Mushroom Hill, S&K's own title
  sequence plays the Death Egg falling into the volcano (`trilogy_dfall.c`).

## Characters across games

Sonic, Tails and Knuckles are S3K's characters everywhere, with their own
moves. Where Sonic 1 or 2 assumed Sonic, the anthology extends the rules. For
example, Knuckles breaks Sonic 1's smashable walls by pushing or gliding into
them, as he does in S3K. On top of that sits an achievement and ability layer
(`trilogy_menu.c`): drop dash, peel-out, homing attack, Tails' flight boost,
Knuckles' burrow and wall spin dash, and modifiers. Each is unlocked in play.

## By the numbers

- 49 C files in `game/trilogy/`, about 37,000 lines, mostly object
  transcriptions.
- About 6,000 lines of Python converters in `tools/trilogy/`.
- About 100 hook sites into the unmodified cartridge.
- 18 zones (Sonic 1's seven, Final Zone included, and Sonic 2's eleven) beside
  S3K's 14.

## Reading the code

Start with `game/trilogy/trilogy.h` (zone ids, bank layout, hook contract),
then `trilogy_level.c` (how a level loads), then any object file. Each one
opens with which original objects it covers and where its fields moved. On
the converter side, `tools/trilogy/build_bank.py` drives everything, and each
module opens with what it converts and why.
