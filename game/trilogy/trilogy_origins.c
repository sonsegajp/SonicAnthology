/*
 * trilogy_origins.c — PLAYER SPRITES: the players drawn with Sonic Origins'
 * sprites of Sonic 1, Sonic 2, Sonic CD or Sonic 3 & Knuckles.
 *
 * origins.bin (tools/trilogy/origins_pack.py, from the player's own copy of
 * Sonic Origins; beside the executable, never shipped) holds each style's
 * pictures in their own colours. Each colour names the palette entry it
 * plays the part of, so the cartridge's fades, water, Super palettes and
 * flashes carry over (s3_video_set_player_art). The renderer turns walking
 * frames itself.
 *
 * Animations that loop run on the Origins game's own frames and clock here:
 * walking, running and rolling at the cartridge's speed-driven rate, the
 * rest at their Origins speed; jog and dash take over at speed where the
 * game has them, and a charged drop dash shows the game's own. Everything
 * else follows the cartridge's frame through the pack's frame table.
 *
 * The Sonic 1 / 2 / CD sprites carry Tails' tails in his body frames: his
 * separate tails object then shows only behind the ball (rolling, spindash),
 * where those games draw it too. Monitors (with the icon that rises from a
 * broken one; the 1-up shows the player's own face) and star posts follow
 * the same style.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "sonic3_video.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_KNUCKLES    0x14A8D6u     /* (follows PLC_Tails in the S&K listing) */
#define MAP_TAILS_TAIL  0x144BB8u
#define MAP_MONITOR     0x01DBA2u
#define MAP_MONITOR_END 0x01DC5Cu
#define MAP_STARPOST    0x02D348u
/* Sonic, Tails, Knuckles, Super Sonic, Monitor, Monitor icon, Star post */
enum { S_SONIC, S_TAILS, S_KNUX, S_SUPER, S_MONITOR, S_ICON, S_POST, SLOTS, PLAYERS = 4 };
enum { TRACKS = 64, T_JOG = 0x30, T_DASH = 0x31, T_DROPDASH = 0x32, T_RAMP = 0x33, RAMPS = 7, T_SPINTOP = 0x3A,
       T_LIFT_TIRED = 0x3B };                         /* (Tails tired carrying Sonic: S3K's $24, Mania's own pose) */
enum { K_NONE, K_WALK, K_ROLL, K_FIXED, K_IDLE };
#define IDLE_HOLD 288                 /* (S3K's stand before the foot taps: 48 x 6 frames) */

int tri_option_player_sprites(void);
int tri_drop_dash_ready(void);
int scd_ramp(int player);                 /* scd_ppz.c */
int scd_spin_frame(int player);

typedef struct { uint16_t first; uint8_t count, kind, ticks, loop; } Track;

static uint8_t *s_data;
static const uint8_t *s_tab;          /* styles x SLOTS x 256 u16 */
static Track *s_tracks;               /* styles x PLAYERS x TRACKS */
static const uint8_t *s_seq;          /* u16 images */
static S3VideoArt *s_img;
static uint32_t s_images;
static unsigned s_styles;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p) | (uint32_t)le16(p + 2) << 16; }

static void load(void)
{
    static int tried;
    if (tried) return;
    tried = 1;
    char path[1024];
    char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%sorigins.bin", base ? base : "");
    SDL_free(base);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 8 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n || memcmp(d, "TORG", 4) || le16(d + 4) != 3) {
        fprintf(stderr, "[trilogy] origins.bin unreadable (rebuild it with tools/trilogy/origins_pack.py)\n");
        free(d);
        fclose(f);
        return;
    }
    fclose(f);
    size_t size = (size_t)n;
    unsigned styles = le16(d + 6);
    size_t tab = 8, trk = tab + (size_t)styles * SLOTS * 512u, pos = trk + (size_t)styles * PLAYERS * TRACKS * 8u;
    if (pos + 4 > size) { free(d); return; }
    uint32_t seq_len = le32(d + pos);
    size_t seq = pos + 4;
    pos = seq + (size_t)seq_len * 2u;
    if (pos + 4 > size) { free(d); return; }
    uint32_t count = le32(d + pos);
    pos += 4;
    S3VideoArt *img = (S3VideoArt *)calloc(count ? count : 1, sizeof *img);
    Track *tracks = (Track *)calloc((size_t)styles * PLAYERS * TRACKS, sizeof *tracks);
    for (uint32_t i = 0; img && i < count; i++) {
        if (pos + 10 > size) { free(img); img = NULL; break; }
        img[i].w = (int16_t)le16(d + pos);
        img[i].h = (int16_t)le16(d + pos + 2);
        img[i].ox = (int16_t)le16(d + pos + 4);
        img[i].oy = (int16_t)le16(d + pos + 6);
        img[i].ncol = le16(d + pos + 8);
        pos += 10;
        img[i].col = d + pos;
        pos += (size_t)img[i].ncol * 6u;
        size_t px = (size_t)(img[i].w > 0 ? img[i].w : 0) * (size_t)(img[i].h > 0 ? img[i].h : 0);
        if (pos + px > size) { free(img); img = NULL; break; }
        img[i].pix = d + pos;
        pos += px;
    }
    if (!img || !tracks) { fprintf(stderr, "[trilogy] origins.bin truncated\n"); free(img); free(tracks); free(d); return; }
    for (size_t t = 0; t < (size_t)styles * PLAYERS * TRACKS; t++) {
        const uint8_t *e = d + trk + t * 8u;
        Track k = { le16(e), e[2], e[3], e[4], e[5] };
        if ((uint32_t)k.first + k.count > seq_len) k.kind = K_NONE;
        tracks[t] = k;
        if (k.kind && t % TRACKS >= T_RAMP && t % TRACKS <= T_SPINTOP)    /* (CD's: drawn for their angles) */
            for (unsigned f = 0; f < k.count; f++) {
                uint16_t i = le16(d + seq + ((size_t)k.first + f) * 2u);
                if (i < count) img[i].upright = 1;
            }
    }
    s_data = d;
    s_tab = d + tab;
    s_tracks = tracks;
    s_seq = d + seq;
    s_img = img;
    s_images = count;
    s_styles = styles;
    fprintf(stderr, "[trilogy] Sonic Origins sprites: %u styles, %u pictures\n", styles, count);
}

int tri_origins_available(void) { load(); return s_img != NULL; }

static const S3VideoArt *art(int style, int slot, unsigned frame)
{
    const uint8_t *e = s_tab + (((size_t)(style - 1) * SLOTS + (size_t)slot) * 256u + (frame & 255u)) * 2u;
    uint16_t i = le16(e);
    return i < s_images ? &s_img[i] : NULL;
}

static const Track *track(int style, int slot, int t)
{
    return &s_tracks[((size_t)(style - 1) * PLAYERS + (size_t)slot) * TRACKS + (size_t)t];
}

/* ---- the Origins game's own clock, per player --------------------------- */
typedef struct { int style, slot, t, frame, timer; uint16_t tick; } Clock;
static Clock s_clock[2] = { { 0, 0, -1, 0, 0, 0 }, { 0, 0, -1, 0, 0, 0 } };

static int walkish(int t) { return t == 0 || t == 1 || t == T_JOG || t == T_DASH || (t >= T_RAMP && t < T_RAMP + 6); }

/* Palmtree Panic's 3D ramp (Origins' Player_State_Ramp3D): Sonic CD's own
 * pictures by the angle and facing, the 3D roll curled up the ramp */
static int ramp_track(uint32_t o, int anim)
{
    int a = r8(o + 0x26), left = r8(o + 0x2A) & 1;
    if (anim >= 2)
        return a == 0 || a > 216 ? -1 : T_RAMP + 6;
    return a == 0 || a > 232 ? T_RAMP : a > 216 ? T_RAMP + (left ? 5 : 1)
         : a > 200 ? T_RAMP + (left ? 4 : 2) : T_RAMP + 3;
}

static const S3VideoArt *tracked(uint32_t o, int pi, int style, int slot)
{
    int anim = r8(o + 0x20), gsp = (int16_t)r16(o + 0x1C);
    if (gsp < 0) gsp = -gsp;
    int turn = scd_spin_frame(pi);                    /* (Sonic CD's spinning disc: its turn) */
    if (turn >= 0) {
        const Track *k = track(style, slot, T_SPINTOP);
        if (k->kind && turn < k->count) {
            s_clock[pi].t = -1;
            uint16_t i = le16(s_seq + ((size_t)k->first + (size_t)turn) * 2u);
            return i < s_images ? &s_img[i] : NULL;
        }
    }
    int t = anim < 0x30 ? anim : -1;
    int ramp = anim <= 3 && scd_ramp(pi) ? ramp_track(o, anim) : -1;
    if (ramp >= 0 && track(style, slot, ramp)->kind)
        t = ramp;
    else if (pi == 0 && (slot == S_SONIC || slot == S_SUPER) && tri_drop_dash_ready() && track(style, slot, T_DROPDASH)->kind)
        t = T_DROPDASH;
    else if (slot == S_TAILS && anim == 0x24 && r8(0xFFF73Eu) /* Flying_carrying_Sonic_flag */ &&
             track(style, slot, T_LIFT_TIRED)->kind)
        t = T_LIFT_TIRED;
    else if (anim == 0 || anim == 1) {               /* (S3K keeps "walk" and picks frames by speed: so do we) */
        t = gsp >= 0xA00 && track(style, slot, T_DASH)->kind ? T_DASH
          : gsp >= 0x600 ? 1
          : gsp >= 0x400 && track(style, slot, T_JOG)->kind ? T_JOG : 0;
    }
    Clock *c = &s_clock[pi];
    const Track *k = t >= 0 ? track(style, slot, t) : NULL;
    if (!k || !k->kind || !k->count) { c->t = -1; return NULL; }
    uint16_t tick = r16(RAM_Level_frame_counter);
    if (c->t != t || c->style != style || c->slot != slot) {
        int frame = 0;                                /* (between runs: keep the stride's phase) */
        if (c->t >= 0 && c->style == style && c->slot == slot && walkish(c->t) && walkish(t)) {
            const Track *old = track(style, slot, c->t);
            if (old->count) frame = c->frame * k->count / old->count;
        }
        *c = (Clock){ style, slot, t, frame, 0, tick };
    } else if (tick != c->tick) {
        int steps = (uint16_t)(tick - c->tick);
        c->tick = tick;
        if (steps > 4) steps = 1;
        while (steps-- > 0) {
            int dur = k->kind == K_WALK ? ((0x800 - (gsp < 0x800 ? gsp : 0x800)) >> 8) + 1
                    : k->kind == K_ROLL ? ((0x400 - (gsp < 0x400 ? gsp : 0x400)) >> 8) + 1
                    : k->kind == K_IDLE && c->frame == 0 ? IDLE_HOLD
                    : (k->ticks ? k->ticks : 1);
            if (++c->timer >= dur) {
                c->timer = 0;
                if (++c->frame >= k->count) c->frame = k->loop < k->count ? k->loop : 0;
            }
        }
    }
    if (c->frame >= k->count) c->frame = 0;
    uint16_t i = le16(s_seq + ((size_t)k->first + (size_t)c->frame) * 2u);
    return i < s_images ? &s_img[i] : NULL;
}

static const S3VideoArt *player(unsigned obj, int style, int slot, unsigned frame)
{
    uint32_t o = 0xFF0000u | (obj & 0xFFFFu);
    int pi = o == RAM_Player_1 ? 0 : o == RAM_Player_1 + 0x4A ? 1 : -1;
    if (frame == 0) {                                 /* (S3K's empty frame: the player hidden, as in AIZ's intro
                                                       *  while the plane draws him; whatever his animation) */
        static const S3VideoArt none;
        if (pi >= 0) s_clock[pi].t = -1;
        return &none;
    }
    if (frame >= 0x31 && frame <= 0x54 && r8(o + 0x20) <= 1) {   /* S3K's tumble (Anim_Tumble, under walk / run): */
        if (pi >= 0) s_clock[pi].t = -1;                     /* the style's turns, else the cartridge's own */
        return art(style, slot, frame);
    }
    const S3VideoArt *a = pi >= 0 ? tracked(o, pi, style, slot) : NULL;
    return a ? a : art(style, slot, frame);
}

static const S3VideoArt *player_art(unsigned obj, unsigned map, unsigned frame)
{
    int style = tri_option_player_sprites();
    if (!style) return NULL;
    load();
    if (!s_img || (unsigned)style > s_styles) return NULL;
    if (map == S3K_Map_Sonic) return player(obj, style, S_SONIC, frame);
    if (map == S3K_Map_Tails) return player(obj, style, S_TAILS, frame);
    if (map == MAP_KNUCKLES) return player(obj, style, S_KNUX, frame);
    if (map == S3K_Map_SuperSonic) return player(obj, style, S_SUPER, frame);
    if (map == MAP_STARPOST) return frame < 5 ? art(style, S_POST, frame) : NULL;   /* (4: lit, blinking with 0) */
    if (map >= MAP_MONITOR && map < MAP_MONITOR_END) {   /* (the whole box, or a frame's icon alone) */
        if (frame == 2) {                                 /* (the 1-up: the player's face) */
            uint8_t c = r8(RAM_Player_1 + 0x38);
            frame = c == 1 ? 0x12 : c == 2 ? 0x13 : 2;
        }
        return frame < 0x14 ? art(style, map == MAP_MONITOR ? S_MONITOR : S_ICON, frame) : NULL;
    }
    if (map == MAP_TAILS_TAIL && style < 4) {          /* (the tails are in the body frames) */
        static const S3VideoArt none;
        for (uint32_t body = RAM_Player_1; body <= RAM_Player_1 + 0x4A; body += 0x4A) {
            if (r8(body + 0x38) != 1 || (uint32_t)(r32(body + 0xC) & 0xFFFFFF) != S3K_Map_Tails) continue;
            uint8_t anim = r8(body + 0x20);
            int pi = body == RAM_Player_1 ? 0 : 1;
            if (anim == 2 || anim == 3 || anim == 9) return NULL;
            if (art(style, S_TAILS, r8(body + 0x22))) return &none;
            /* (or drawn from one of the style's own animations, flying's: those carry their tails too) */
            return s_clock[pi].t >= 0 && s_clock[pi].slot == S_TAILS && s_clock[pi].style == style ? &none : NULL;
        }
    }
    if (map == MAP_TAILS_TAIL) {                       /* (on a 3D ramp: Sonic CD's pictures carry them) */
        static const S3VideoArt none;
        for (int n = 0; n < 2; n++) {
            uint32_t body = RAM_Player_1 + (uint32_t)n * 0x4A;
            if (r8(body + 0x38) == 1 && r8(body + 0x20) <= 1 && scd_ramp(n) &&
                track(style, S_TAILS, ramp_track(body, r8(body + 0x20)))->kind)
                return &none;
        }
    }
    return NULL;
}

/* Sonic CD's miniature Sonic (Metallic Madness's shrink ray): player 1 at
 * half size, whichever art he is drawn with */
static unsigned object_zoom(unsigned obj)
{
    extern int scd_mmz_mini(void);
    return obj == 0xB000u && scd_mmz_mini() ? 128u : 0u;
}

void tri_origins_init(void)
{
    s3_video_set_player_art(player_art);
    s3_video_set_object_zoom(object_zoom);
}
