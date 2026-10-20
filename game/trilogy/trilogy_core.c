/*
 * trilogy_core.c — expansion bank, hook chain, native code dispatch.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "trilogy_hooks.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_bank;
static uint32_t s_bank_size;
static void scd_bank_init(void);                     /* (the Sonic CD window, below) */

int tri_bank_loaded(void) { return s_bank != NULL; }

/* a file beside the executable (scdss.bin, scd-loudness.txt, ...) */
FILE *tri_fopen_beside_exe(const char *name, const char *mode)
{
    char path[1024];
    char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%s%s", base ? base : "", name);
    SDL_free(base);
    return fopen(path, mode);
}
FILE *tri_open_beside_exe(const char *name) { return tri_fopen_beside_exe(name, "rb"); }

void trilogy_init(void)
{
    if (s_bank)
        return;
    char path[1024];
    char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%strilogy.bin", base ? base : "");
    SDL_free(base);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[trilogy] %s not found — S1/S2 zones unavailable\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0x2040 || n > 0x400000 || (n & 1)) {
        fprintf(stderr, "[trilogy] bad bank size %ld\n", n);
        fclose(f);
        return;
    }
    s_bank = (uint8_t *)malloc((size_t)n);
    if (!s_bank || fread(s_bank, 1, (size_t)n, f) != (size_t)n || memcmp(s_bank, "TRLG", 4) != 0) {
        fprintf(stderr, "[trilogy] bank read/magic failed\n");
        free(s_bank);
        s_bank = NULL;
        fclose(f);
        return;
    }
    fclose(f);
    s_bank_size = (uint32_t)n;
    g_rom_ext = s_bank;
    g_rom_ext_size = s_bank_size;
    tri_screen_register();
    {
        extern void tri_title_init(void), tri_origins_init(void);
        tri_title_init();                            /* (the Data Select's ground) */
        tri_origins_init();                          /* (PLAYER SPRITES) */
    }
    fprintf(stderr, "[trilogy] expansion bank mapped at $400000 (%u bytes)\n", s_bank_size);
    scd_bank_init();
    extern void scd_audio_dev(void);
    scd_audio_dev();                                 /* (development: TRILOGY_SCD_PLAY) */
}

/* ---- reads across ROM / bank / RAM -------------------------------------- */
uint8_t tri_rd8(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x400000u) return g_rom[a];
    if (a - TRI_BANK_BASE < s_bank_size) return s_bank[a - TRI_BANK_BASE];
    if (a >= 0xFF0000u) return g_ram[a & 0xFFFF];
    return 0xFF;
}
uint16_t tri_rd16(uint32_t a) { return (uint16_t)((tri_rd8(a) << 8) | tri_rd8(a + 1)); }
uint32_t tri_rd32(uint32_t a) { return ((uint32_t)tri_rd16(a) << 16) | tri_rd16(a + 2); }

/* ---- Sonic CD: one level at a time in the bank's window ------------------
 * scd.bin (AnthologyBuilder, from the player's disc) holds every Sonic CD level
 * converted like the S1/S2 zones, each a self-contained blob assembled for
 * the same window of the bank. As the Mega-CD loads a level into Word RAM,
 * the level about to start is copied in. Each blob opens with its directory
 * entry (TD_*), then the level's own palette and PLC tables (ids $F0..).
 *   "TSCD", u16 version, u16 levels, u32 window base, u32 window size,
 *   levels x { u16 zone_and_act, u16 flags, u32 offset, u32 size }
 * The entry $FFFF (flags bit 0) is no level but Sonic CD's FM sound set
 * (scd_sfx.py): a 32 KB SFX bank for the S3K driver, then its table. It goes
 * once just past the window, at the next 32 KB boundary (the driver's bank
 * register reaches it there), and trilogy_music.c swaps it in. */
#define SCD_SOUND_ZA    0xFFFFu
#define SCD_SOUND_BANK  0x8000u
static uint8_t *s_scd;
static uint32_t s_scd_size, s_win_base, s_win_size;
static unsigned s_scd_levels;
static uint32_t s_snd_base;                      /* the sound set's bank (68k address), 0: none */
static uint16_t s_win_level = 0xFFFF;
static int s_win_dirty;                          /* (a block written at run time: copy afresh next start) */

void tri_scd_wr16(uint32_t a, uint16_t v)        /* the window's level, changed for this attempt */
{
    a &= 0xFFFFFF;
    if (!s_bank || a < s_win_base || a + 2 > s_win_base + s_win_size) return;
    uint8_t *p = s_bank + (a - TRI_BANK_BASE);
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    s_win_dirty = 1;
}

static uint32_t be32p(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void scd_bank_init(void)
{
    char path[1024];
    char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%sscd.bin", base ? base : "");
    SDL_free(base);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 16 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n || memcmp(d, "TSCD", 4)) {
        fprintf(stderr, "[trilogy] scd.bin unreadable\n");
        free(d);
        fclose(f);
        return;
    }
    fclose(f);
    uint32_t wb = be32p(d + 8), ws = be32p(d + 12);
    unsigned levels = (unsigned)(d[6] << 8 | d[7]);
    if (wb < TRI_BANK_BASE + s_bank_size || wb + ws > 0x7E0000u || 16u + levels * 12u > (uint32_t)n) {
        fprintf(stderr, "[trilogy] scd.bin window $%06X+%X doesn't fit this bank\n", wb, ws);
        free(d);
        return;
    }
    uint32_t snd_base = (wb + ws + SCD_SOUND_BANK - 1) & ~(SCD_SOUND_BANK - 1), snd_off = 0, snd_size = 0;
    for (unsigned i = 0; i < levels; i++) {          /* (the FM sound set) */
        const uint8_t *e = d + 16 + i * 12;
        uint32_t off = be32p(e + 4), size = be32p(e + 8);
        if ((uint16_t)(e[0] << 8 | e[1]) != SCD_SOUND_ZA || !(e[3] & 1)) continue;
        if (off <= (uint32_t)n && size <= (uint32_t)n - off && size > SCD_SOUND_BANK && snd_base + size + 1 <= 0x7E0000u) {
            snd_off = off;
            snd_size = size;
        }
    }
    uint32_t total = (snd_size ? snd_base + ((snd_size + 1) & ~1u) : wb + ws) - TRI_BANK_BASE;
    uint8_t *nb = (uint8_t *)realloc(s_bank, total);
    if (!nb) { free(d); return; }
    memset(nb + s_bank_size, 0, total - s_bank_size);
    s_bank = nb;
    s_bank_size = total;
    g_rom_ext = s_bank;
    g_rom_ext_size = s_bank_size;
    s_scd = d;
    s_scd_size = (uint32_t)n;
    s_win_base = wb;
    s_win_size = ws;
    s_scd_levels = levels;
    if (snd_size) {
        uint8_t *sb = s_bank + (snd_base - TRI_BANK_BASE);
        memcpy(sb, d + snd_off, snd_size);
        sb[SCD_SOUND_BANK] = (uint8_t)(snd_base >> 15);  /* (the table's bank byte: where it is) */
        s_snd_base = snd_base;
    }
    fprintf(stderr, "[trilogy] Sonic CD: %u levels, window $%06X (%u bytes)%s\n", levels - (snd_size != 0), wb, ws,
            snd_size ? ", FM sound set" : "");
}

/* the FM sound set's table (68k address; scd_sfx.py's blob), 0: none */
uint32_t tri_scd_sound(void) { return s_snd_base ? s_snd_base + SCD_SOUND_BANK : 0; }

int tri_scd_available(void) { return s_scd != NULL; }

int tri_scd_map(uint16_t za)
{
    if (!s_scd) return 0;
    if (za == s_win_level && !s_win_dirty) return 1;
    for (unsigned i = 0; i < s_scd_levels; i++) {
        const uint8_t *e = s_scd + 16 + i * 12;
        if ((uint16_t)(e[0] << 8 | e[1]) != za || (e[3] & 1)) continue;   /* (flags bit 0: not a level) */
        uint32_t off = be32p(e + 4), size = be32p(e + 8);
        if (off > s_scd_size || size > s_scd_size - off || size > s_win_size) return 0;
        uint8_t *w = s_bank + (s_win_base - TRI_BANK_BASE);
        memcpy(w, s_scd + off, size);
        memset(w + size, 0, s_win_size - size);
        s_win_level = za;
        s_win_dirty = 0;
        return 1;
    }
    return 0;
}

uint32_t tri_dir_for(int zone, int act)
{
    if (tri_cd_zone(zone))                       /* (the mapped level's entry, at the window) */
        return s_scd && s_win_level == (uint16_t)(zone << 8 | act) ? s_win_base : 0;
    if (!s_bank || zone < TRI_ZONE_BASE || zone >= TRI_ZONE_END || act < 0 || act > 3)
        return 0;
    uint32_t idx = (uint32_t)(zone - TRI_ZONE_BASE) * 4u + (uint32_t)act;
    if (idx >= TRI_DIR_COUNT)
        return 0;
    uint32_t e = TRI_BANK_BASE + TRI_DIR_OFF + idx * TRI_DIR_ENTRY;
    return tri_rd32(e + TD_LLB) ? e : 0;
}

uint32_t tri_dir(void) { return tri_dir_for(tri_zone(), tri_act()); }

/* ---- JSR-exact call into recompiled code -------------------------------- */
void tri_jsr(uint32_t addr)
{
    recomp_push_return(TRILOGY_NATIVE_BASE);      /* return slot (never executed) */
    uint32_t slot = g_cpu.A[7];
    int saved = g_split_sp_popped;
    g_split_sp_popped = 0;
    recomp_call_addr(addr);
    g_split_sp_popped = saved;
    if (g_rte_pending)
        return;
    if (g_cpu.A[7] != slot + 4u)
        g_cpu.A[7] += 4u;
}

/* ---- native routines at pseudo addresses -------------------------------- */
static TriNativeFn s_native[TRILOGY_NATIVE_MAX];
static unsigned s_native_count;

uint32_t tri_native(TriNativeFn fn)
{
    for (unsigned i = 0; i < s_native_count; i++)
        if (s_native[i] == fn)
            return TRILOGY_NATIVE_BASE + 2u * (i + 1u);
    if (s_native_count >= TRILOGY_NATIVE_MAX) {
        fprintf(stderr, "[trilogy] native table full\n");
        abort();
    }
    s_native[s_native_count++] = fn;
    return TRILOGY_NATIVE_BASE + 2u * s_native_count;
}

int trilogy_dispatch(uint32_t addr)
{
    addr &= 0xFFFFFF;
    if (tri_obj_dispatch(addr))
        return 1;
    if (addr < TRILOGY_NATIVE_BASE)
        return 0;
    uint32_t i = (addr - TRILOGY_NATIVE_BASE) / 2u;
    if (i == 0 || i > s_native_count || (addr & 1))
        return 0;
    s_native[i - 1]();
    return 1;
}

/* ---- hook chain --------------------------------------------------------- */
/* The widescreen renderer's hook treats any PC it doesn't list as an object
 * range check and rewrites D0, so our sites must never reach it. */
int trilogy_owns(uint32_t pc)
{
    switch (pc) {
    TH_ALL_CASES return 1;
    default: return 0;
    }
}

int trilogy_instruction(uint32_t pc)
{
    if (!s_bank)
        return 0;
    if (tri_level_hook(pc))
        return 1;
    if (tri_screen_hook(pc))
        return 1;
    if (tri_titlecard_hook(pc))
        return 1;
    if (tri_flow_hook(pc))
        return 1;
    if (tri_water_hook(pc))
        return 1;
    /* Sonic CD's FindLevelFloor(2) .CheckNegFloor: looking down (a3 = $10), a
     * block solid only at its top (a negative height) is a full block with a
     * flat angle; S3K takes its own angle (Palmtree Panic's logs: $80, a
     * ceiling's, which flipped the player off them) */
    if ((pc == TH_FLOOR_NEG || pc == TH_FLOOR2_NEG) && (int16_t)A(3) == 0x10 && tri_cd_zone(tri_zone())) {
        w8(A(4), 0);
        uint16_t d0 = (uint16_t)((D(2) & 0xF) + 0x10);
        D(0) = (D(0) & 0xFFFF0000u) | d0;
        D(1) = (D(1) & 0xFFFF0000u) | (uint16_t)(0xF - d0);
        return 1;                                    /* (rts) */
    }
    /* The ground's wall ahead (CalcRoomInFront: d0 its direction, d1 the room
     * there). At speed the step ahead lands inside a tight curve's steeper part
     * (Stardust Speedway's risers: at $900 and more, a step of 9 px or more):
     * S3K, as Sonic CD, then stops the player dead at its foot. On a slope
     * already rising toward that side, the step is cut short at the wall and the
     * speed kept: the curve takes him up the next frame. Again the next frame,
     * the slope no steeper, it is a wall after all: the stop as ever */
    if ((pc == TH_ROOM_AHEAD_S || pc == TH_ROOM_AHEAD_T || pc == TH_ROOM_AHEAD_K) && (int16_t)D(1) < 0
        && tri_cd_zone(tri_zone())) {
        static uint16_t s_frame[2];
        static uint8_t s_ang[2];
        uint32_t p = A(0);
        int k = p != RAM_Player_1;
        uint8_t ang = r8(p + OBJ_angle), side = (uint8_t)(((uint8_t)D(0) + 0x20) & 0xC0);
        int16_t gv = (int16_t)r16(p + OBJ_ground_vel);
        int up = side == 0xC0 ? gv > 0 && ang >= 0xC1 && ang <= 0xF0
               : side == 0x40 ? gv < 0 && ang >= 0x10 && ang <= 0x3F : 0;
        uint16_t f = r16(RAM_Level_frame_counter);
        if (up && !(s_frame[k] == (uint16_t)(f - 1) && s_ang[k] == ang)) {
            int16_t cut = (int16_t)((uint16_t)D(1) << 8);
            w16(p + OBJ_x_vel, (uint16_t)(r16(p + OBJ_x_vel) + (side == 0xC0 ? cut : -cut)));
            D(1) &= 0xFFFF0000u;                     /* (room enough: on as he was) */
            s_frame[k] = f;
            s_ang[k] = ang;
        }
        return 0;
    }
    {
        extern int scd_ttz_water(uint32_t pc);
        if (scd_ttz_water(pc))                       /* (Tidal Tempest: Sonic CD's water levels) */
            return 1;
    }
    {
        extern void scd_ppz_hook(uint32_t pc), scd_time_hook(uint32_t pc), scd_wwz_hook(uint32_t pc);
        scd_ppz_hook(pc);                            /* (Palmtree Panic's 3D ramps; the menu runs there too) */
        scd_wwz_hook(pc);                            /* (Wacky Workbench's floors) */
        scd_time_hook(pc);                           /* (Sonic CD's time travel) */
        /* (Sonic CD's level events set the camera's bottom outright, as its
         *  bosses' do: S3K's MoveCameraY clamps to it only while it is changing) */
        if (pc == TH_LVL_POST_SPRITES || pc == TH_GAMELOOP) {
            extern void tri_scd_music_follow(int level);
            tri_scd_music_follow(pc == TH_LVL_POST_SPRITES && tri_cd_zone(tri_zone()));   /* (Sonic CD's music) */
        }
        if (pc == TH_LVL_POST_SPRITES && tri_cd_zone(tri_zone()) &&
            (int16_t)r16(RAM_Camera_Y_pos) > (int16_t)r16(RAM_Camera_max_Y_pos))
            w8(RAM_Camera_max_Y_pos_changing, 1);
        extern void scd_mmz_hook(uint32_t pc);
        scd_mmz_hook(pc);                            /* (Metallic Madness: layers, belts, mini Sonic) */
        extern void scd_ssz_hook(uint32_t pc);
        scd_ssz_hook(pc);                            /* (Stardust Speedway: layers, loops, sparks) */
        extern void scd_qqz_hook(uint32_t pc);
        scd_qqz_hook(pc);                            /* (Quartz Quadrant: its belts) */
        extern void scd_qqz3_hook(uint32_t pc);
        scd_qqz3_hook(pc);                           /* (Quartz Quadrant act 3: the boss's events, belt colours) */
        extern void scd_ttz_hook(uint32_t pc);
        scd_ttz_hook(pc);                            /* (Tidal Tempest: its currents, the wave strip) */
        extern void scd_ttz3_hook(uint32_t pc);
        scd_ttz3_hook(pc);                           /* (Tidal Tempest act 3: the boss's level events) */
        extern void scd_ccz_hook(uint32_t pc);
        scd_ccz_hook(pc);                            /* (Collision Chaos: tile bumpers, flipper jumps) */
        extern int scd_time_pal_hook(uint32_t pc);
        if (scd_time_pal_hook(pc))                   /* (arriving from a time warp: white, no card) */
            return 1;
    }
    if (tri_menu_hook(pc))
        return 1;
    {
        extern int tri_title_hook(uint32_t pc);
        if (tri_title_hook(pc))
            return 1;
    }
    {
        if (pc == TH_BOSS_HIT) {
            extern int tri_obj_boss_hit(void);
            return tri_obj_boss_hit();               /* (S1/S2 bosses keep state where S3K's hit writes) */
        }
        if (pc == TH_GAMELOOP) {
            extern void tri_sound_set_tick(void);
            tri_sound_set_tick();                    /* (the S2 sound set survives driver reloads) */
        }
        extern int tri_dfall_hook(uint32_t pc);
        if (tri_dfall_hook(pc))
            return 1;
        extern int tri_s2end_hook(uint32_t pc);
        if (tri_s2end_hook(pc))
            return 1;
        extern int tri_s2ss_hook(uint32_t pc);
        if (tri_s2ss_hook(pc))
            return 1;
        extern int tri_scdss_hook(uint32_t pc);
        if (tri_scdss_hook(pc))                      /* (Sonic CD's UFO stages) */
            return 1;
        extern int tri_scdwarp_hook(uint32_t pc);
        if (tri_scdwarp_hook(pc))                    /* (Sonic CD's time warp cutscene) */
            return 1;
        extern int tri_save_hook(uint32_t pc);
        if (tri_save_hook(pc))
            return 1;
        if (pc == TH_GAMELOOP && r8(RAM_Game_mode) == 0x4C) {   /* Data Select */
            extern void tri_save_dev_clear(void);
            tri_save_dev_clear();
        }
    }
    tri_ext_hook(pc);
    return 0;
}
