/*
 * trilogy_ss.c — which special stage, and which emerald.
 *
 * One emerald progression runs through the whole trilogy (S3K's own
 * Chaos_emerald_count / Super_emerald_count / Collected_emeralds_array):
 * every special stage is played for the next emerald not yet collected,
 * whatever game the zone comes from. Once all seven Chaos Emeralds are in,
 * special stages award Super Emeralds (array value 2 = Chaos Emerald
 * waiting to become super, 3 = Super Emerald, as S&K's Hidden Palace
 * leaves them).
 *
 * The stage played follows the SPECIAL STAGES option: per game (Sonic 1 and
 * Sonic 3 & Knuckles: Blue Spheres, Sonic 2: its half-pipe), or always one
 * kind. Knuckles has no half-pipe sprites in these cartridges, so he
 * always gets Blue Spheres.
 */
#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>

void tri_s2ss_start(int stage, void (*done)(int got_emerald));
void tri_scdss_start(int stage, uint8_t stones, void (*done)(int got));
int tri_scdss_available(void);

/* -> the emerald slot the next special stage is for (-1: everything collected) */
int tri_ss_next(int *super)
{
    int chaos = r8(RAM_Chaos_emerald_count) < 7;
    if (super) *super = !chaos;
    for (int i = 0; i < 7; i++) {
        uint8_t v = r8(RAM_Collected_emeralds_array + (uint32_t)i);
        if (chaos ? v == 0 : v < 3) return i;
    }
    return -1;
}

int tri_ss_available(void) { return tri_ss_next(NULL) >= 0; }

/* an emerald won in a stage that isn't S3K's own (its results award S3K's) */
static void award(int slot, int super)
{
    if (slot < 0) return;
    uint32_t a = RAM_Collected_emeralds_array + (uint32_t)slot;
    if (super) {
        w8(a, 3);
        if (r8(RAM_Super_emerald_count) < 7) w8(RAM_Super_emerald_count, (uint8_t)(r8(RAM_Super_emerald_count) + 1));
    } else {
        w8(a, 1);
        if (r8(RAM_Chaos_emerald_count) < 7) w8(RAM_Chaos_emerald_count, (uint8_t)(r8(RAM_Chaos_emerald_count) + 1));
    }
    fprintf(stderr, "[trilogy] %s Emerald %d (%d chaos, %d super)\n", super ? "Super" : "Chaos", slot + 1,
            r8(RAM_Chaos_emerald_count), r8(RAM_Super_emerald_count));
    extern void tri_save_emeralds(void);
    tri_save_emeralds();
}

/* S3K's Blue Spheres for the next emerald: SK_special_stage_flag and the
 * stage number its own search (loc_8606) starts from */
void tri_ss_prepare_blue_spheres(void)
{
    int super, slot = tri_ss_next(&super);
    if (super)                                       /* (Hidden Palace's conversion: chaos -> waiting for super) */
        for (int i = 0; i < 7; i++)
            if (r8(RAM_Collected_emeralds_array + (uint32_t)i) == 1) w8(RAM_Collected_emeralds_array + (uint32_t)i, 2);
    w8(RAM_SK_special_stage_flag, (uint8_t)super);
    if (slot >= 0) w8(RAM_Current_special_stage, (uint8_t)slot);
}

enum { SS_KIND_BLUE_SPHERES, SS_KIND_HALF_PIPE, SS_KIND_UFO };

/* the option: 0 per game, 1 Blue Spheres, 2 half-pipe, 3 Sonic CD's UFO stages */
static int kind_for(int game)
{
    extern int tri_option_special_stages(void);
    int opt = tri_option_special_stages();
    int k = opt == 1 ? SS_KIND_BLUE_SPHERES : opt == 2 ? SS_KIND_HALF_PIPE : opt == 3 ? SS_KIND_UFO
          : game == TRI_GAME_S2 ? SS_KIND_HALF_PIPE : game == TRI_GAME_CD ? SS_KIND_UFO : SS_KIND_BLUE_SPHERES;
    if (k == SS_KIND_HALF_PIPE && r16(RAM_Player_mode) == 3) k = SS_KIND_BLUE_SPHERES;   /* (no half-pipe Knuckles) */
    if (k == SS_KIND_UFO && !tri_scdss_available()) k = SS_KIND_BLUE_SPHERES;   /* (no scdss.bin: no disc set up) */
    return k;
}

/* 1 when the game's special stage is one of the trilogy's own scenes (the
 * half-pipe, the UFOs), not S3K's Blue Spheres */
int tri_ss_half_pipe(int game) { return kind_for(game) != SS_KIND_BLUE_SPHERES; }

/* ---- the half-pipe from a level ---------------------------------------------- */
enum { RET_STARPOST, RET_NEXT_ACT, RET_RING };
static int s_slot, s_super, s_ret;
static uint16_t s_next;

static void half_pipe_done(int got)
{
    if (got) award(s_slot, s_super);
    if (s_ret == RET_RING) {                         /* a giant ring: back where it was (Saved2, as S3K's own) */
        w8(RAM_Special_bonus_entry_flag, 1);
        w8(RAM_Respawn_table_keep, 1);
    } else {
        w8(RAM_Special_bonus_entry_flag, 0);
        w8(RAM_Respawn_table_keep, 0);               /* (S2: the level reloads, rings back to 0) */
    }
    if (s_ret == RET_NEXT_ACT) {
        extern uint16_t tri_apparent(uint16_t za);
        w16(RAM_Current_zone_and_act, s_next);
        w16(RAM_Apparent_zone_and_act, tri_apparent(s_next));
        w8(RAM_Last_star_post_hit, 0);
    } else if (s_ret == RET_STARPOST) {
        w8(RAM_Last_star_post_hit, r8(0xFFFE2B));    /* Saved_last_star_post_hit */
    }
    w8(RAM_Game_mode, 0x0C);
}

static void half_pipe(int ret, uint16_t next, int kind)
{
    s_slot = tri_ss_next(&s_super);
    s_ret = ret;
    s_next = next;
    if (kind == SS_KIND_UFO) {                       /* (the emeralds held show as time stones) */
        uint8_t stones = 0;
        for (int i = 0; i < 7; i++) {
            uint8_t v = r8(RAM_Collected_emeralds_array + (uint32_t)i);
            if (s_super ? v >= 3 : v != 0) stones |= (uint8_t)(1u << i);
        }
        tri_scdss_start(s_slot < 0 ? 0 : s_slot, stones, half_pipe_done);
    } else
        tri_s2ss_start(s_slot < 0 ? 0 : s_slot, half_pipe_done);
}

static int zone_game(void) { return tri_dir() ? tri_rd8(tri_dir() + TD_GAME) : TRI_GAME_S3K; }

/* a giant ring's stage, then the act after it (Sonic 1, Sonic CD) */
void tri_ss_half_pipe_then(uint16_t next) { half_pipe(RET_NEXT_ACT, next, kind_for(zone_game())); }

/* ---- Sonic 2's star posts: the star circle at 50 rings ---------------------- */
int tri_ss_stars_wanted(void)
{
    return r16(RAM_Ring_count) >= 50 && tri_ss_available();
}

/* the stars were touched (loc_2D47E): the special stage instead of a bonus stage */
int tri_ss_stars_touch(void)
{
    uint32_t a0 = A(0) & 0xFFFFFFu;
    SET8(a0, OBJ_coll_prop, 0);
    int kind = kind_for(TRI_GAME_S2);
    if (kind != SS_KIND_BLUE_SPHERES) {
        half_pipe(RET_STARPOST, 0, kind);            /* (S2: back to the star post) */
        return 1;
    }
    tri_jsr(S3K_Save_Level_Data2);                   /* Blue Spheres, back to the stars' spot */
    tri_ss_prepare_blue_spheres();
    o_sfx(0xAF);                                     /* sfx_EnterSS */
    w8(RAM_Special_bonus_entry_flag, 1);
    w8(RAM_Game_mode, 0x34);
    w8(RAM_Respawn_table_keep, 1);
    return 1;
}

/* an S3K giant ring with the half-pipe chosen (SSEntryFlash_GoSS): Chaos Emeralds
 * only; with all seven, S&K's rings keep leading to Hidden Palace */
int tri_ss_ring_half_pipe(void)
{
    int kind = kind_for(0);
    if (kind == SS_KIND_BLUE_SPHERES || r8(RAM_Chaos_emerald_count) >= 7) return 0;
    uint32_t a0 = A(0) & 0xFFFFFFu;
    if ((int8_t)OB8(a0, OBJ_subtype) < 0) return 0;   /* (the Hidden Palace ring) */
    o_sfx(0xAF);
    tri_jsr(S3K_Clear_SpriteRingMem);
    tri_jsr(S3K_Save_Level_Data2);
    half_pipe(RET_RING, 0, kind);
    A(0) = RAMPTR(a0);
    tri_jsr(S3K_Go_Delete_SpriteSlotted2);
    return 1;
}

/* development (TRILOGY_S2SS=n) */
void tri_ss_dev(int stage)
{
    s_slot = stage;
    s_super = 0;
    s_ret = RET_NEXT_ACT;
    s_next = r16(RAM_Current_zone_and_act);
    tri_s2ss_start(stage, half_pipe_done);
}

/* development (TRILOGY_SCDSS=n) */
void tri_ss_dev_ufo(int stage)
{
    s_slot = stage;
    s_super = 0;
    s_ret = RET_NEXT_ACT;
    s_next = r16(RAM_Current_zone_and_act);
    tri_scdss_start(stage, 0, half_pipe_done);
}
