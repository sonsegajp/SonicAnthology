/*
 * trilogy_menu.c — pause menu, achievements, unlocks and abilities.
 *
 * Start pauses as usual (S3K Pause_Game); while paused, a menu on the right
 * of the screen runs from the pause loop (PAUSE_CHK) and the scene behind
 * it is blurred and darkened by the frame overlay. Text uses S3K's own
 * level-select font, decoded from the ROM into the bank.
 *
 * Achievements are earned in play and each one unlocks an ability or a
 * modifier, which can then be switched on or off in the menu. Progress is
 * kept in trilogy-progress.ini beside the executable.
 */
#include "trilogy_obj.h"
#include "sonic3_video.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Unlocks and achievements
 * ---------------------------------------------------------------------- */
enum {
    U_DROPDASH, U_PEELOUT, U_HOMING,                       /* abilities */
    U_MAGNET, U_SHIELD, U_SHOES, U_AIR, U_LIVES, U_RINGS, U_SUPER, U_MOON, U_HIGHJUMP,
    U_AIRDASH, U_CHARGEFLY, U_BURROW, U_WALLDASH,           /* Tails' and Knuckles' (saved by index: append) */
    U_COUNT
};
/* the two menu pages (save indices stay as above) */
static const int k_abil_list[] = { U_DROPDASH, U_PEELOUT, U_HOMING, U_AIRDASH, U_BURROW, U_WALLDASH };
/* the abilities page shows the playing character's own */
static const int k_abil_char[3][3] = {
    { U_DROPDASH, U_PEELOUT, U_HOMING },        /* Sonic */
    { U_PEELOUT, U_AIRDASH, -1 },               /* Tails (U_CHARGEFLY retired: its save slot stays) */
    { U_PEELOUT, U_BURROW, U_WALLDASH },        /* Knuckles */
};
static int abil_char(void)
{
    int ch = r8(0xFFB038u);                     /* Player_1 character_id */
    return ch < 3 ? ch : 0;
}
static int abil_at(int i) { return k_abil_char[abil_char()][i]; }
static int abil_count(void)
{
    int n = 0;
    while (n < 3 && k_abil_char[abil_char()][n] >= 0) n++;
    return n;
}
static const int k_mod_list[] = { U_MAGNET, U_SHIELD, U_SHOES, U_AIR, U_LIVES, U_RINGS, U_SUPER, U_MOON, U_HIGHJUMP };
#define N_ABIL ((int)(sizeof k_abil_list / sizeof k_abil_list[0]))
#define N_MODS ((int)(sizeof k_mod_list / sizeof k_mod_list[0]))

static const char *const k_onoff[] = { "OFF", "ON" };
static const char *const k_shields[] = { "OFF", "FIRE", "BUBBLE", "ELECTRIC" };
static const char *const k_ringstart[] = { "OFF", "10", "50" };

static const struct {
    const char *name, *desc;
    const char *const *values;
    int count;
} k_unlock[U_COUNT] = {
    { "DROP DASH",      "PRESS JUMP AGAIN IN THE AIR AND HOLD IT. LAND TO DASH OFF.", k_onoff, 2 },
    { "SUPER PEEL OUT", "HOLD UP AND PRESS JUMP. KEEP UP HELD TO REV. LET GO TO RUN.", k_onoff, 2 },
    { "HOMING ATTACK",  "PRESS JUMP AGAIN IN THE AIR NEAR A BADNIK TO DIVE AT IT.", k_onoff, 2 },
    { "RING MAGNET",    "RINGS FLY TO YOU AS IF YOU HAD THE ELECTRIC SHIELD.", k_onoff, 2 },
    { "SHIELD START",   "BEGIN EVERY LIFE WITH THE CHOSEN SHIELD.", k_shields, 4 },
    { "SPEED START",    "BEGIN EVERY LIFE WITH SPEED SHOES.", k_onoff, 2 },
    { "INFINITE AIR",   "NEVER RUN OUT OF AIR UNDERWATER.", k_onoff, 2 },
    { "INFINITE LIVES", "YOUR LIVES NEVER DROP BELOW THREE.", k_onoff, 2 },
    { "RING START",     "BEGIN EVERY LIFE WITH RINGS IN HAND.", k_ringstart, 3 },
    { "SUPER ANYTIME",  "TURN SUPER WITH 50 RINGS EVEN WITHOUT THE EMERALDS.", k_onoff, 2 },
    { "MOON GRAVITY",   "FALL SLOWLY AND JUMP HIGHER THAN EVER.", k_onoff, 2 },
    { "HIGH JUMP",      "EVERY JUMP GOES A QUARTER HIGHER.", k_onoff, 2 },
    { "FLIGHT BOOST",   "TAILS: WHILE FLYING, HOLD DOWN AND PRESS JUMP FOR A BURST OF SPEED. EACH ONE USES UP SOME FLIGHT TIME.", k_onoff, 2 },
    { "CHARGED FLIGHT", "TAILS: WHILE FLYING, HOLD JUMP TO WIND UP YOUR TAILS. LET GO TO ROCKET UPWARD.", k_onoff, 2 },
    { "BURROW",         "KNUCKLES: HOLD DOWN AND PRESS JUMP IN THE AIR TO DIVE INTO THE GROUND AND DIG ALONG, UNDER BADNIKS AND MONITORS. JUMP TO BURST OUT: THE LONGER YOU DIG, THE HIGHER YOU GO. JUMP AGAIN TO GLIDE.", k_onoff, 2 },
    { "WALL DASH",      "KNUCKLES: WHILE CLIMBING A WALL, HOLD TOWARDS IT AND PRESS JUMP TO SPIN DASH STRAIGHT UP IT.", k_onoff, 2 },
};

enum {
    A_GHZ, A_MZ, A_SYZ, A_LZ, A_SLZ, A_SBZ, A_FZ,
    A_BADNIKS, A_RINGS, A_MONITORS, A_NOHIT, A_SPEED,
    A_TAILS, A_TAILSFLY, A_KNUX, A_KNUXWALLS,               /* (saved as bits: append) */
    A_COUNT
};
static const struct { const char *name, *desc; int unlock; } k_ach[A_COUNT] = {
    { "GREEN HILL HERO",   "CLEAR GREEN HILL ZONE.",               U_DROPDASH },
    { "MARBLE MASTER",     "CLEAR MARBLE ZONE.",                   U_PEELOUT },
    { "SPRING YARD STAR",  "CLEAR SPRING YARD ZONE.",              U_RINGS },
    { "LABYRINTH DIVER",   "CLEAR LABYRINTH ZONE.",                U_AIR },
    { "STAR LIGHT RUNNER", "CLEAR STAR LIGHT ZONE.",               U_HIGHJUMP },
    { "SCRAP BRAIN HEART", "CLEAR SCRAP BRAIN ZONE.",              U_MOON },
    { "FINAL BLOW",        "DEFEAT EGGMAN IN THE FINAL ZONE.",     U_SUPER },
    { "BADNIK BUSTER",     "DESTROY 100 BADNIKS.",                 U_HOMING },
    { "RING HOARDER",      "HOLD 200 RINGS AT ONCE.",              U_MAGNET },
    { "MONITOR SMASHER",   "BREAK 50 MONITORS.",                   U_SHIELD },
    { "UNTOUCHABLE",       "CLEAR AN ACT WITHOUT GETTING HIT.",    U_LIVES },
    { "SPEED DEMON",       "CLEAR AN ACT IN UNDER 45 SECONDS.",    U_SHOES },
    { "FOX ON THE RUN",    "CLEAR AN ACT AS TAILS.",               U_AIRDASH },
    { "SKY HIGH",          "AS TAILS, FLY UNTIL YOU GET TIRED.",   -1 },
    { "GUARDIAN",          "CLEAR AN ACT AS KNUCKLES.",            U_BURROW },
    { "ROCK BREAKER",      "BREAK 10 WALLS AS KNUCKLES.",          U_WALLDASH },
};

/* saved by index: new options go at the end */
enum { O_BLUR, O_DIM, O_POPUPS, O_BRIGHT, O_SAT, O_FILTER, O_DITHER, O_WIDE, O_FULL, O_SPECIAL, O_BGBLUR, O_CRT, O_COUNT };
static const char *const k_dim[] = { "OFF", "LOW", "HIGH" };
static const char *const k_bgblur[] = { "OFF", "LOW", "MEDIUM", "HIGH" };
static const char *const k_crt[] = { "OFF", "SCANLINES", "PVM", "ARCADE", "TV" };
static const char *const k_bright[] = { "1", "2", "3", "4", "5" };
static const char *const k_sat[] = { "GREY", "LOW", "NORMAL", "VIVID" };
static const char *const k_filter[] = { "NONE", "WARM", "COOL", "SEPIA", "NIGHT" };
static const char *const k_wide[] = { "OFF", "ADAPTIVE", "16:9", "21:9" };
static const char *const k_special[] = { "PER GAME", "BLUE SPHERES", "HALF-PIPE" };
static const struct { const char *name, *desc; const char *const *values; int count; } k_opt[O_COUNT] = {
    { "PAUSE BLUR",       "BLUR THE GAME BEHIND THE PAUSE MENU.",   k_onoff, 2 },
    { "PAUSE DIM",        "DARKEN THE GAME BEHIND THE PAUSE MENU.", k_dim, 3 },
    { "ACHIEVEMENT POPUPS", "SHOW A BANNER WHEN YOU EARN ONE.",     k_onoff, 2 },
    { "BRIGHTNESS",       "3 IS THE ORIGINAL PICTURE.",             k_bright, 5 },
    { "SATURATION",       "HOW STRONG THE COLOURS ARE.",            k_sat, 4 },
    { "COLOUR FILTER",    "TINT THE WHOLE PICTURE.",                k_filter, 5 },
    { "DITHER BLEND",     "BLEND DITHERED PIXELS AS A TV WOULD. WATERFALLS AND SHADOWS LOOK SEE THROUGH.", k_onoff, 2 },
    { "WIDESCREEN",       "SHOW MORE OF THE LEVEL AT THE SIDES. ADAPTIVE FILLS THE WINDOW.", k_wide, 4 },
    { "FULLSCREEN",       "FILL THE WHOLE DISPLAY.",                k_onoff, 2 },
    { "SPECIAL STAGES",   "PER GAME: SONIC 1 AND 3 PLAY BLUE SPHERES, SONIC 2 ITS HALF-PIPE.", k_special, 3 },
    { "BACKGROUND BLUR",  "SOFTEN THE FAR BACKGROUND WHILE YOU PLAY, LIKE A CAMERA FOCUSED ON THE ACTION.", k_bgblur, 4 },
    { "CRT FILTER",       "LOOK LIKE A TUBE TV. PVM IS A SHARP STUDIO MONITOR. TV IS A CURVED HOME SET.", k_crt, 5 },
};
static const int k_video_opts[] = { O_BLUR, O_DIM, O_BRIGHT, O_SAT, O_FILTER, O_DITHER, O_BGBLUR, O_CRT, O_WIDE, O_FULL };
static const int k_game_opts[] = { O_POPUPS, O_SPECIAL };
#define N_VIDEO_OPTS ((int)(sizeof k_video_opts / sizeof k_video_opts[0]))
#define N_GAME_OPTS  ((int)(sizeof k_game_opts / sizeof k_game_opts[0]))

static struct {
    uint32_t ach;                 /* earned bits */
    uint8_t  value[U_COUNT];      /* current setting of each unlock */
    uint8_t  opt[O_COUNT];
    uint32_t badniks, monitors, walls;
} s_prog = { 0, { 0 }, { 1, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };

static int s_unlock_all = -1;      /* TRILOGY_UNLOCK_ALL=1: testing, never saved */

static int unlocked(int u)
{
    if (s_unlock_all < 0) { const char *e = getenv("TRILOGY_UNLOCK_ALL"); s_unlock_all = e && *e == '1'; }
    if (s_unlock_all) return 1;
    for (int a = 0; a < A_COUNT; a++)
        if (k_ach[a].unlock == u && (s_prog.ach & (1u << a))) return 1;
    return 0;
}
static int on(int u) { return unlocked(u) && s_prog.value[u]; }
static void prog_load(void);
/* SPECIAL STAGES: 0 per game, 1 Blue Spheres, 2 half-pipe (trilogy_ss.c) */
int tri_option_special_stages(void)
{
    prog_load();
    const char *e = getenv("TRILOGY_SPECIAL");      /* development override */
    if (e && *e) return atoi(e);
    return s_prog.opt[O_SPECIAL];
}

/* ---- persistence ---- */
static char s_path[1024];

static void prog_path(void)
{
    if (s_path[0]) return;
    char *base = SDL_GetBasePath();
    snprintf(s_path, sizeof s_path, "%strilogy-progress.ini", base ? base : "");
    SDL_free(base);
}

static void prog_save(void)
{
    if (getenv("LNG_SCRIPT")) return;              /* (scripted runs: tests, the builder's previews) */
    prog_path();
    FILE *f = fopen(s_path, "w");
    if (!f) return;
    fprintf(f, "achievements=%u\nbadniks=%u\nmonitors=%u\nwalls=%u\n", s_prog.ach, s_prog.badniks, s_prog.monitors, s_prog.walls);
    for (int u = 0; u < U_COUNT; u++) fprintf(f, "unlock%d=%d\n", u, s_prog.value[u]);
    for (int o = 0; o < O_COUNT; o++) fprintf(f, "option%d=%d\n", o, s_prog.opt[o]);
    fclose(f);
}

static void prog_load(void)
{
    static int loaded;
    if (loaded) return;
    loaded = 1;
    prog_path();
    FILE *f = fopen(s_path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        unsigned v; int i;
        if (sscanf(line, "achievements=%u", &v) == 1) s_prog.ach = v & ((1u << A_COUNT) - 1);
        else if (sscanf(line, "badniks=%u", &v) == 1) s_prog.badniks = v;
        else if (sscanf(line, "monitors=%u", &v) == 1) s_prog.monitors = v;
        else if (sscanf(line, "walls=%u", &v) == 1) s_prog.walls = v;
        else if (sscanf(line, "unlock%d=%u", &i, &v) == 2 && i >= 0 && i < U_COUNT && (int)v < k_unlock[i].count) s_prog.value[i] = (uint8_t)v;
        else if (sscanf(line, "option%d=%u", &i, &v) == 2 && i >= 0 && i < O_COUNT && (int)v < k_opt[i].count) s_prog.opt[i] = (uint8_t)v;
    }
    fclose(f);
}

/* first look at progress: file, then the testing switch (abilities on) */
void genesis_present_set_crt(int preset);                   /* runner/crt_present.h */

static void prog_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    prog_load();
    if (s_prog.opt[O_BGBLUR]) {                              /* (the saved blur, from the first frame) */
        extern void s3_video_set_bg_blur(int level);
        if (!sonic3_video.enabled()) sonic3_video.configure("4:3");
        s3_video_set_bg_blur(s_prog.opt[O_BGBLUR]);
    }
    genesis_present_set_crt(s_prog.opt[O_CRT]);              /* (the saved CRT filter) */
    if (unlocked(U_DROPDASH) && getenv("TRILOGY_UNLOCK_ALL"))
        for (int k = 0; k < N_ABIL; k++)
            if (!s_prog.value[k_abil_list[k]]) s_prog.value[k_abil_list[k]] = 1;
}

/* ---- earning ---- */
static int s_toast_ach = -1, s_toast_time;

static void earn(int a)
{
    if (s_prog.ach & (1u << a)) return;
    s_prog.ach |= 1u << a;
    int u = k_ach[a].unlock;
    if (u >= 0 && !s_prog.value[u]) s_prog.value[u] = 1;   /* new toys start switched on */
    prog_save();
    fprintf(stderr, "[trilogy] achievement: %s\n", k_ach[a].name);
    if (s_prog.opt[O_POPUPS]) {
        s_toast_ach = a;
        s_toast_time = 240;
        o_sfx(0xAC);                                       /* sfx_Continue */
    }
}

static int s_hit;                  /* hurt this act */

/* Act cleared (results tally done, or the Final Zone escape). */
void tri_ach_act_clear(uint16_t za)
{
    prog_load();
    switch (za) {
    case 0x2002: earn(A_GHZ); break;
    case 0x2202: earn(A_MZ); break;
    case 0x2402: earn(A_SYZ); break;
    case 0x2102: earn(A_LZ); break;
    case 0x2302: earn(A_SLZ); break;
    case 0x2103: earn(A_SBZ); break;
    case 0x2502: earn(A_FZ); break;
    default: break;
    }
    if (!s_hit) earn(A_NOHIT);
    if (za != 0x2502 && !r8(RAM_Timer + 1) && r8(RAM_Timer + 2) < 45) earn(A_SPEED);
    if (r8(RAM_Player_1 + 0x38) == 1) earn(A_TAILS);            /* character_id */
    if (r8(RAM_Player_1 + 0x38) == 2) earn(A_KNUX);
}

/* a breakable wall broke (S1 SmashWall, ...): Knuckles' tally */
void tri_ach_wall_broken(void)
{
    prog_load();
    if (r8(RAM_Player_1 + 0x38) != 2) return;
    if (++s_prog.walls >= 10) earn(A_KNUXWALLS);
    prog_save();
}

/* ------------------------------------------------------------------------
 * Abilities and modifiers (P1), around Process_Sprites
 * ---------------------------------------------------------------------- */
#define P_ROUTINE   0x05
#define P_ANIM      0x20
#define P_ANIMTIME  0x24
#define P_ANGLE     0x26
#define P_AIR       0x2C
#define P_STATUS2   0x2B
#define P_OBJCTRL   0x2E
#define P_DJUMP     0x2F
#define P_SHOES     0x36
#define P_CHAR      0x38
#define P_SPINDASH  0x3D
#define P_JUMPING   0x40

#define BTN_UP      0x01
#define BTN_LEFT    0x04
#define BTN_RIGHT   0x08
#define BTN_JUMP    0x70

static int level_mode(void)
{
    return (r8(RAM_Game_mode) & 0x7F) == 0x0C && !r16(0xFFFFE8u);   /* Competition_mode */
}

static int s_frames;               /* frames since the level (re)started */
static int s_prev_air, s_prev_jumping;
static int s_dd;                   /* drop dash: 0 idle, 1 charging, 2 ready */
static int s_dd_charge;
static int s_po, s_po_charge;      /* peel-out */
static int s_dig, s_dig_t;         /* Knuckles' burrow: 0, 1 diving, 2 underground */
static int s_dig_time;             /* frames underground: the jump out grows */
static int s_boost_cd;             /* Tails' flight boost: frames until the next */
#define BOOST_COST 0x30                 /* flight time a boost uses (a full flight is $F0) */
static int s_wd, s_wrev, s_wspd, s_wacc;   /* Knuckles' wall spin dash: 1 revving, 2 rolling up */
static int s_homing;               /* homing dive in progress */
static uint32_t s_home_target, s_home_code;
static int s_home_time, s_home_bounce;

#define HOMING_SPEED 0x900

static void home_aim(uint32_t p, uint32_t t)
{
    float dx = (float)((int16_t)OB16(t, OBJ_x_pos) - (int16_t)OB16(p, OBJ_x_pos));
    float dy = (float)((int16_t)OB16(t, OBJ_y_pos) - (int16_t)OB16(p, OBJ_y_pos));
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 1) d = 1;
    SET16(p, OBJ_x_vel, (uint16_t)(int16_t)(dx / d * HOMING_SPEED));
    SET16(p, OBJ_y_vel, (uint16_t)(int16_t)(dy / d * HOMING_SPEED));
}

/* after a homing hit: pop up off the target, ready to dive again */
static void home_bounce(uint32_t p)
{
    SET16(p, OBJ_x_vel, (uint16_t)((int16_t)OB16(p, OBJ_x_vel) >> 2));
    SET16(p, OBJ_y_vel, (uint16_t)-0x500);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 6);               /* in the air, curled */
    SET8(p, P_JUMPING, 1);
    SET8(p, P_ANIM, 2);
    SET8(p, P_DJUMP, 0);
    s_homing = 0;
    s_home_bounce = 0;
}
static int s_rings_given;

static void level_entry(void)
{
    prog_init();
    s_frames = 0;
    s_hit = 0;
    s_dd = s_po = s_homing = s_home_bounce = 0;
    s_dig = s_dig_time = s_boost_cd = s_wd = 0;
    s_prev_air = s_prev_jumping = 0;
    s_rings_given = 0;
}

static void give_shield(uint32_t p, int kind)
{
    static const uint32_t obj[4] = { 0, S3K_Obj_FireShield, S3K_Obj_BubbleShield, S3K_Obj_LightningShield };
    static const uint8_t bit[4] = { 0, 4, 6, 5 };
    if (OB8(p, P_STATUS2) & 0x71) return;                       /* already shielded */
    if (kind == 1 && (OB8(p, OBJ_status) & 0x40)) return;      /* no fire underwater */
    SET8(p, P_STATUS2, (OB8(p, P_STATUS2) & 0x8E) | 1 | (1 << bit[kind]));
    w32(RAM_Shield, obj[kind]);
    w16(RAM_Shield + 0x42, (uint16_t)p);                       /* parent */
}

static void give_shoes(uint32_t p)
{
    SET8(p, P_STATUS2, OB8(p, P_STATUS2) | 4);                 /* Status_SpeedShoes */
    SET8(p, P_SHOES, (20 * 60) / 8);
    w16(RAM_Max_speed, 0xC00);
    w16(RAM_Acceleration, 0x18);
    w16(RAM_Deceleration, 0x80);
    D(0) = 8;
    tri_jsr(S3K_Change_Music_Tempo);
}

/* the nearest badnik / boss / monitor ahead of Sonic, 0 if none */
static uint32_t homing_target(uint32_t p)
{
    int px = (int16_t)OB16(p, OBJ_x_pos), py = (int16_t)OB16(p, OBJ_y_pos);
    int dir = (OB8(p, OBJ_status) & 1) ? -1 : 1;
    uint32_t best = 0;
    long bestd = 0;
    for (int i = 3; i < 110; i++) {
        uint32_t o = o_slot(i);
        uint32_t code = r32(o);
        if (!code) continue;
        uint8_t c = OB8(o, OBJ_coll_flags);
        int enemy = c && !(c & 0xC0);
        int monitor = code == S3K_Obj_Monitor && (c & 0xC0) == 0x40;
        if (!enemy && !monitor) continue;
        if (!(OB8(o, OBJ_render) & 0x80)) continue;          /* on screen only */
        int dx = ((int16_t)OB16(o, OBJ_x_pos) - px) * dir;
        int dy = (int16_t)OB16(o, OBJ_y_pos) - py;
        if (dx < -8 || dx > 0xB0 || dy < -0x40 || dy > 0xA0) continue;
        long d = (long)dx * dx + (long)dy * dy;
        if (!best || d < bestd) { best = o; bestd = d; }
    }
    return best;
}

static void peel_pre(void);

/* ---- Tails' and Knuckles' moves ------------------------------------------ */
#define BTN_DOWN    0x02
#define P_DJPROP    0x25                /* double_jump_property: Tails' flight time / 2 */
#define P_INVULN    0x34

static void dirt_puff(uint32_t p, int dx)            /* one of the dust object's skid puffs (loc_18CE4) */
{
    if (!r32(RAM_Dust)) return;
    /* the skid frames ($11-$14) use 16 tiles the dust loads with its frame $15 (DPLC $F0AA): load them */
    tri_vram_queue(0x18A604u + 0xAAu * 32u, OB16(RAM_Dust, 0x40), 16u * 32u);   /* ArtUnc_DashDust -> vram_art */
    uint32_t a1 = o_alloc();
    if (!a1) return;
    w32(a1, r32(RAM_Dust));
    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(p, OBJ_x_pos) + dx));
    SET16(a1, OBJ_y_pos, (uint16_t)(OB16(p, OBJ_y_pos) + 4));
    SET8(a1, OBJ_status, 0);
    SET8(a1, OBJ_anim, 3);
    SET8(a1, OBJ_routine, 2);
    w32(a1 + 0x0C, r32(RAM_Dust + 0x0C));                       /* mappings */
    SET8(a1, OBJ_render, OB8(RAM_Dust, OBJ_render));
    SET16(a1, OBJ_priority, 0x80);
    SET8(a1, OBJ_width_px, 4);
    SET16(a1, 0x0A, OB16(RAM_Dust, 0x0A) | 0x8000);            /* art_tile: in front of grass and soil */
    SET16(a1, 0x42, OB16(RAM_Dust, 0x42));                      /* parent */
    SET16(a1, 0x40, OB16(RAM_Dust, 0x40));                      /* vram_art: its frames load where the dust's do */
    SET8(a1, 0x34, 0xFF);                                       /* (first frame always loads) */
}

/* ---- Knuckles' wall spin dash ---------------------------------------------
 * Climbing, hold towards the wall + jump revs a spin dash (more presses, more
 * speed, as on the ground); letting go of the direction rolls him straight up
 * the wall. His climb's own checks (GetDistanceFromWall, the ceiling) steer it:
 * over the top he pops onto the ledge, a ceiling stops him, and slowed right
 * down he takes hold of the wall again. */
#define S3K_GetDistanceFromWall 0x016F4Eu
#define RAM_Player_prev_frame   0xFFF766u
/* TRI_BLOB_KNUX_WALLDASH: his spin dash frames ($86-$8B: 0-5) and the spin
 * dash dust ($0A-$10: 6-12) turned to face up the wall (knux_walldash.py) */
static const uint8_t k_wd_knux[10] = { 0, 1, 0, 2, 0, 3, 0, 4, 0, 5 };   /* AniKnuckles09's order */
static int s_wfr, s_dfr;
static uint32_t s_wmap;                                 /* his own mappings, while ours are on */

static uint32_t wd_blob(void)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + TRI_BLOB_KNUX_WALLDASH * 4u) : 0;
}

/* show turned frame f on object o, its tiles copied to the art at vram */
static void wd_frame(uint32_t o, uint32_t b, int f, uint32_t vram)
{
    uint32_t e = b + 4u + (uint32_t)f * 6u;
    SET32(o, 0x0C, b + tri_rd16(b));                    /* mappings */
    SET8(o, OBJ_frame, (uint8_t)f);
    tri_vram_queue(b + tri_rd32(e), vram, (uint32_t)tri_rd16(e + 4) * 32u);
}

static void wd_restore(uint32_t p)                      /* his own mappings back */
{
    if (!s_wmap) return;
    SET32(p, 0x0C, s_wmap);
    s_wmap = 0;
    w8(RAM_Player_prev_frame, 0xFF);                    /* (his DPLC reloads whatever frame is next) */
    if (OB8(p, P_DJUMP) == 4) SET8(p, OBJ_frame, 0xB7);
}

/* the dust under him, turned the same way (Obj_DashDust's spin dash dust) */
static void wd_dust_obj(void)
{
    uint32_t o = A(0) & 0xFFFFFFu, p = RAM_Player_1, b = wd_blob();
    if (s_wd != 1 || !b || !r32(RAM_Dust)) { o_delete(o); return; }
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    SET8(o, OBJ_render, (uint8_t)(4 | (OB8(p, OBJ_status) & 1)));   /* (mirrored on a left wall) */
    SET16(o, 0x0A, (uint16_t)((OB16(RAM_Dust, 0x0A) & 0x7FF) | (OB16(p, 0x0A) & 0x8000)));
    SET16(o, OBJ_priority, 0x100);
    SET8(o, OBJ_width_px, 0x20);
    SET8(o, OBJ_height_px, 0x20);
    wd_frame(o, b, 6 + (s_dfr++ >> 1) % 7, OB16(RAM_Dust, 0x40));  /* (2 frames each, as Ani_DashSplashDrown 2) */
    w8(RAM_Dust + 0x34, 0xFF);                          /* (the dust's own frame reloads after) */
    o_draw(o);
}

static void wall_collision(uint32_t p)                  /* as Knuckles_Wall_Climb sets it */
{
    w32(RAM_Collision_addr, r32(OB8(p, 0x47) == 0x0D ? RAM_Primary_collision_addr : RAM_Secondary_collision_addr));
}

static void wall_grab(uint32_t p)                       /* Knuckles_Gliding_HitWall .success */
{
    SET8(p, OBJ_status, (uint8_t)((OB8(p, OBJ_status) | 2) & ~4));
    SET8(p, OBJ_y_radius, 0x0A);
    SET8(p, 0x1F, 0x0A);
    SET16(p, OBJ_ground_vel, 0);
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, 0);
    SET8(p, P_DJUMP, 4);
    SET8(p, OBJ_frame, 0xB7);
    SET8(p, P_ANIMTIME, 0x7F);
    SET8(p, 0x23, 0);                                   /* anim_frame */
    SET8(p, P_DJPROP, 3);
    SET16(p, 0x12, OB16(p, OBJ_x_pos));                 /* x_pos+2: where he latched on */
    SET16(p, P_ANIM, 0x2020);                           /* anim, prev_anim: glide/climb */
}

static void wall_off(uint32_t p, int16_t xv, int16_t yv)      /* rolling, in the air */
{
    SET8(p, P_OBJCTRL, (uint8_t)(OB8(p, P_OBJCTRL) & ~1));
    SET8(p, OBJ_status, (uint8_t)(OB8(p, OBJ_status) | 2 | 4));
    SET16(p, OBJ_x_vel, (uint16_t)xv);
    SET16(p, OBJ_y_vel, (uint16_t)yv);
    SET16(p, OBJ_ground_vel, 0);
    SET8(p, P_JUMPING, 1);                              /* (a glide can follow) */
    SET8(p, P_ANIM, 2);
    s_wd = 0;
}

static void wall_dash_pre(void)
{
    uint32_t p = RAM_Player_1;
    if (s_wd != 1) return;
    if (OB8(p, P_CHAR) != 2 || OB8(p, P_ROUTINE) != 2 || OB8(p, P_OBJCTRL) || OB8(p, P_DJUMP) != 4) {
        s_wd = 0;                                       /* (knocked or dropped off the wall) */
        wd_restore(p);
        return;
    }
    int dir = (OB8(p, OBJ_status) & 1) ? -1 : 1;
    uint8_t held = r8(RAM_Ctrl_1), pressed = r8(RAM_Ctrl_1 + 1);
    w8(RAM_Ctrl_1, (uint8_t)(held & ~(BTN_UP | BTN_DOWN)));            /* held in place on the wall */
    w8(RAM_Ctrl_1 + 1, (uint8_t)(pressed & ~(BTN_JUMP | BTN_UP | BTN_DOWN)));
    if (held & (dir < 0 ? BTN_LEFT : BTN_RIGHT)) {
        s_wrev -= s_wrev >> 5;                          /* (the rev winds down, as Sonic's does) */
        if (pressed & BTN_JUMP) {
            s_wrev += 0x200;
            if (s_wrev > 0x800) s_wrev = 0x800;
            o_sfx(0xAB);
        }
        return;
    }
    s_wd = 2;                                           /* let go: up the wall */
    wd_restore(p);
    s_wspd = 0x800 + (s_wrev >> 8) * 0x80;              /* the spin dash's own speeds, $800-$C00 */
    s_wacc = 0;
    SET8(p, P_OBJCTRL, (uint8_t)(OB8(p, P_OBJCTRL) | 1));   /* (his own code holds still; we move him) */
    SET8(p, OBJ_status, (uint8_t)(OB8(p, OBJ_status) | 2 | 4));
    SET8(p, OBJ_y_radius, 0x0E);
    SET8(p, 0x1F, 7);
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, 0);
    SET16(p, OBJ_ground_vel, (uint16_t)s_wspd);
    SET8(p, P_ANIM, 2);
    o_sfx(0xB6);                                        /* sfx_Dash */
}

static void wall_dash_post(void)
{
    uint32_t p = RAM_Player_1;
    if (s_wd == 1 && OB8(p, P_DJUMP) == 4) {           /* revving: the spin dash, turned up the wall */
        uint32_t b = wd_blob();
        if (b) {
            if (!s_wmap) s_wmap = r32(p + 0x0C);
            int f = k_wd_knux[s_wfr++ % 10];
            wd_frame(p, b, f, (uint32_t)(OB16(p, 0x0A) & 0x7FF) * 32u);
            w8(RAM_Player_prev_frame, (uint8_t)f);      /* (his own DPLC loader leaves it be) */
        }
        return;
    }
    if (s_wd != 2) return;
    if (OB8(p, P_CHAR) != 2 || OB8(p, P_ROUTINE) != 2 || !(OB8(p, P_OBJCTRL) & 1)) {
        if (OB8(p, P_ROUTINE) != 2) SET8(p, P_OBJCTRL, (uint8_t)(OB8(p, P_OBJCTRL) & ~1));
        s_wd = 0;
        return;
    }
    int dir = (OB8(p, OBJ_status) & 1) ? -1 : 1;
    s_wacc += s_wspd;
    int steps = s_wacc >> 8;
    s_wacc &= 0xFF;
    for (int i = 0; i < steps; i++) {
        SET16(p, OBJ_y_pos, (uint16_t)(OB16(p, OBJ_y_pos) - 1));
        wall_collision(p);
        if (o_ceiling_dist(p) < 0) {                    /* a ceiling: stop under it */
            SET16(p, OBJ_y_pos, (uint16_t)(OB16(p, OBJ_y_pos) + 1));
            wall_off(p, 0, 0);
            return;
        }
        wall_collision(p);
        A(0) = RAMPTR(p);
        D(2) = (D(2) & 0xFFFF0000u) | OB16(p, OBJ_y_pos);
        tri_jsr(S3K_GetDistanceFromWall);
        if ((int16_t)D(1) >= 4) {                       /* over the top: onto the ledge */
            int up = s_wspd * 3 / 4;
            if (up < 0x400) up = 0x400;
            wall_off(p, (int16_t)(dir * 0x200), (int16_t)-up);
            return;
        }
    }
    SET16(p, OBJ_ground_vel, (uint16_t)s_wspd);         /* (the roll spins with its speed) */
    SET8(p, P_ANIM, 2);
    if (!(++s_dfr & 3)) dirt_puff(p, dir * 10);         /* dust off the wall behind him */
    s_wspd -= 0x20;                                     /* climbing against gravity */
    if (s_wspd < 0x200) {                               /* spent: take hold of the wall */
        SET8(p, P_OBJCTRL, (uint8_t)(OB8(p, P_OBJCTRL) & ~1));
        wall_grab(p);
        s_wd = 0;
    }
}

/* jump presses the moves take are removed before the player's own code sees them */
static void moves_pre(void)
{
    uint32_t p = RAM_Player_1;
    wall_dash_pre();
    if (s_wd) return;
    if (OB8(p, P_ROUTINE) != 2 || OB8(p, P_OBJCTRL)) return;
    uint8_t ch = OB8(p, P_CHAR);
    uint8_t held = r8(RAM_Ctrl_1), pressed = r8(RAM_Ctrl_1 + 1);   /* (the player copies these to the logical pad) */
    int air = (OB8(p, OBJ_status) & 2) != 0;
    int dir = (OB8(p, OBJ_status) & 1) ? -1 : 1;
    if (ch == 2 && on(U_WALLDASH) && OB8(p, P_DJUMP) == 4 && (pressed & BTN_JUMP)   /* Knuckles, climbing: */
        && (held & (dir < 0 ? BTN_LEFT : BTN_RIGHT))) {                               /* towards the wall + jump */
        w8(RAM_Ctrl_1 + 1, (uint8_t)(pressed & ~BTN_JUMP));                         /* (not a jump off it) */
        s_wd = 1;                                               /* rev a spin dash on the wall */
        s_wrev = s_wfr = s_dfr = 0;
        uint32_t a1 = o_alloc();
        if (a1) SET32(a1, OBJ_code, tri_native(wd_dust_obj));
        o_sfx(0xAB);                                            /* sfx_SpinDash */
        return;
    }
    if (!air || !(held & BTN_DOWN) || !(pressed & BTN_JUMP)) return;
    if (ch == 2 && on(U_BURROW) && !s_dig && OB8(p, P_JUMPING) && !OB8(p, P_DJUMP)) {   /* Knuckles: dive */
        w8(RAM_Ctrl_1 + 1, (uint8_t)(pressed & ~BTN_JUMP));                 /* (no glide) */
        SET16(p, OBJ_x_vel, (uint16_t)((int16_t)OB16(p, OBJ_x_vel) >> 1));
        SET16(p, OBJ_y_vel, 0x800);
        SET8(p, P_ANIM, 2);
        s_dig = 1;
        o_sfx(0x3C);                                            /* sfx_Roll */
    } else if (ch == 1 && on(U_AIRDASH) && OB8(p, P_DJUMP)) {   /* Tails, flying: boost */
        w8(RAM_Ctrl_1 + 1, (uint8_t)(pressed & ~BTN_JUMP));    /* (not a flap) */
        if (s_boost_cd || OB8(p, P_DJPROP) < BOOST_COST) return;   /* (too soon, or too tired) */
        SET8(p, P_DJPROP, (uint8_t)(OB8(p, P_DJPROP) - BOOST_COST));   /* it costs flight time */
        int xv = (int16_t)OB16(p, OBJ_x_vel) + dir * 0x400;
        if (xv * dir < 0x400) xv = dir * 0x400;
        if (xv * dir > 0x800) xv = dir * 0x800;
        SET16(p, OBJ_x_vel, (uint16_t)xv);
        if ((int16_t)OB16(p, OBJ_y_vel) > 0) SET16(p, OBJ_y_vel, 0);   /* (holds its height for the burst) */
        s_boost_cd = 30;
        o_sfx(0xB6);                                            /* sfx_Dash */
        dirt_puff(p, -dir * 12);
    }
}

static void moves_post(void)
{
    uint32_t p = RAM_Player_1;
    wall_dash_post();
    uint8_t ch = OB8(p, P_CHAR);
    int air = (OB8(p, OBJ_status) & 2) != 0;
    int ok = OB8(p, P_ROUTINE) == 2 && !OB8(p, P_OBJCTRL);

    /* Tails, flying: the achievement for flying until tired; the boost's cooldown */
    if (ch == 1 && ok && air && OB8(p, P_DJUMP) && !OB8(p, P_DJPROP)) earn(A_TAILSFLY);
    if (s_boost_cd) s_boost_cd--;

    /* Knuckles: the dive, then digging along out of sight */
    if (s_dig && (ch != 2 || !ok)) s_dig = 0;
    if (s_dig == 1) {
        if (air) {
            if ((int16_t)OB16(p, OBJ_y_vel) < 0) s_dig = 0;     /* bounced off something */
            else if ((int16_t)OB16(p, OBJ_y_vel) < 0x800) SET16(p, OBJ_y_vel, 0x800);
        } else {                                                /* into the ground */
            s_dig = 2;
            s_dig_t = s_dig_time = 0;
            o_sfx(0x49);                                        /* sfx_GroundSlide (Knuckles' thump) */
            dirt_puff(p, -8); dirt_puff(p, 0); dirt_puff(p, 8);
        }
    }
    if (s_dig == 2) {
        if (air) {                                              /* jumped (or dropped) out */
            if (OB8(p, P_JUMPING) && (int16_t)OB16(p, OBJ_y_vel) < 0) {   /* the longer dug, the higher */
                int bonus = s_dig_time * 8;
                if (bonus > 0x600) bonus = 0x600;
                SET16(p, OBJ_y_vel, (uint16_t)((int16_t)OB16(p, OBJ_y_vel) - bonus));
            }
            s_dig = 0;
            dirt_puff(p, -8); dirt_puff(p, 0); dirt_puff(p, 8);
            o_sfx(0x49);
        } else {
            SET8(p, OBJ_frame, 0);                              /* out of sight (frame 0 is blank) */
            if (OB8(p, P_INVULN) < 2) SET8(p, P_INVULN, 2);     /* nothing hits him underground */
            if (OB16(p, OBJ_ground_vel) && !(++s_dig_t % 6)) dirt_puff(p, 0);
            s_dig_time++;
        }
    }

    {                                                          /* development: TRILOGY_MOVELOG */
        static int log = -1, last = -1;
        if (log < 0) log = getenv("TRILOGY_MOVELOG") != NULL;
        int st = s_dig * 16 + (int)(air ? 1 : 0) + (OB8(p, P_DJUMP) << 1)
               + (r8(RAM_Super_Sonic_Knux_flag) << 8) + (r8(0xFFF667u) << 16)    /* (+ Super_Tails_flag) */
               + ((OB8(p, OBJ_status) & 0x20) << 2);                            /* (pushing: $80) */
        if (log && st != last) {
            fprintf(stderr, "[MOVE] super %X ch %d dig %d air %d dj %d x %X y %X xv %X yv %X frame %X\n", st >> 8, ch, s_dig, air,
                    OB8(p, P_DJUMP), OB16(p, OBJ_x_pos), OB16(p, OBJ_y_pos), OB16(p, OBJ_x_vel), OB16(p, OBJ_y_vel), OB8(p, 0x22));
            last = st;
        }
    }
}

/* Homing Attack first: a press that has a target is taken before Sonic's own
 * code makes it the insta-shield (which the next press, without one, still is) */
static int s_home_req;
static void homing_pre(void)
{
    uint32_t p = RAM_Player_1;
    s_home_req = 0;
    if (!on(U_HOMING) || OB8(p, P_CHAR) != 0 || OB8(p, P_ROUTINE) != 2 || OB8(p, P_OBJCTRL)) return;
    uint8_t pressed = r8(RAM_Ctrl_1 + 1);
    if (!(pressed & BTN_JUMP) || !(OB8(p, OBJ_status) & 2) || !OB8(p, P_JUMPING) || !s_prev_air) return;
    if (OB8(p, P_DJUMP) || (OB8(p, P_STATUS2) & 0x70) || r8(RAM_Super_Sonic_Knux_flag)) return;
    if (!homing_target(p)) return;
    w8(RAM_Ctrl_1 + 1, (uint8_t)(pressed & ~BTN_JUMP));        /* (no insta-shield this time) */
    s_home_req = 1;
}

static void pre_sprites(void)
{
    homing_pre();
    moves_pre();
    peel_pre();
}

static void peel_pre(void)
{
    uint32_t p = RAM_Player_1;
    if (!s_po) {
        if (!on(U_PEELOUT) || OB8(p, P_ROUTINE) != 2 || OB8(p, P_OBJCTRL)) return;   /* (any character) */
        uint16_t ctrl = r16(RAM_Ctrl_1);
        if ((OB8(p, OBJ_status) & 6) || OB16(p, OBJ_ground_vel) || OB8(p, P_SPINDASH)) return;
        if ((ctrl >> 8) & BTN_UP && (ctrl & BTN_JUMP)) {        /* rev up */
            s_po = 1;
            s_po_charge = 0;
            SET8(p, P_OBJCTRL, 1);                              /* held in place, still animated */
            SET8(p, P_ANIM, 0);
            w8(RAM_Dust + 0x20, 2);                             /* spindash dust, */
            SET8(p, P_SPINDASH, 1);                             /* which follows a spin dash */
            o_sfx(0xAB);                                        /* sfx_Spindash */
        }
        return;
    }
    /* revving */
    int dir = (OB8(p, OBJ_status) & 1) ? -1 : 1;
    if (OB8(p, P_ROUTINE) != 2 || (OB8(p, OBJ_status) & 2) || OB8(p, P_OBJCTRL) != 1) {
        if (OB8(p, P_OBJCTRL) == 1) SET8(p, P_OBJCTRL, 0);
        SET16(p, OBJ_ground_vel, 0);
        w8(RAM_Dust + 0x20, 0);
        SET8(p, P_SPINDASH, 0);
        s_po = 0;
        return;
    }
    if ((r8(RAM_Ctrl_1) & BTN_UP)) {
        if (s_po_charge < 30) s_po_charge++;
        SET16(p, OBJ_ground_vel, (uint16_t)(dir * (0x300 + s_po_charge * 0x30)));  /* legs only */
        SET8(p, P_ANIM, 0);
        return;
    }
    SET8(p, P_OBJCTRL, 0);                                      /* let go */
    w8(RAM_Dust + 0x20, 0);
    SET8(p, P_SPINDASH, 0);
    s_po = 0;
    if (s_po_charge < 30) { SET16(p, OBJ_ground_vel, 0); return; }
    SET16(p, OBJ_ground_vel, (uint16_t)(dir * 0xC00));
    SET16(p, OBJ_x_vel, (uint16_t)(dir * 0xC00));
    o_sfx(0xB6);                                                /* sfx_Dash */
}

static void drop_dash_launch(uint32_t p)
{
    int super = r8(RAM_Super_Sonic_Knux_flag) != 0;
    int speed = super ? 0xC00 : 0x800, cap = super ? 0xD00 : 0xC00;
    int left = OB8(p, OBJ_status) & 1;
    int xv = (int16_t)OB16(p, OBJ_x_vel);
    int gv;
    if (!left) {
        if (xv >= 0) { gv = (xv >> 2) + speed; if (gv > cap) gv = cap; }
        else gv = OB8(p, P_ANGLE) ? (xv >> 1) + speed : speed;
    } else {
        if (xv <= 0) { gv = (xv >> 2) - speed; if (gv < -cap) gv = -cap; }
        else gv = OB8(p, P_ANGLE) ? (xv >> 1) - speed : -speed;
    }
    if (!(OB8(p, OBJ_status) & 4)) {                           /* Sonic_Roll */
        SET8(p, OBJ_status, OB8(p, OBJ_status) | 4);
        SET8(p, OBJ_y_radius, 0x0E);
        SET8(p, 0x1F, 7);
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 5);
    }
    SET8(p, P_ANIM, 2);
    SET16(p, OBJ_ground_vel, (uint16_t)gv);
    SET16(p, OBJ_x_vel, (uint16_t)gv);
    o_sfx(0xB6);                                                /* sfx_Dash */
}

static void post_sprites(void)
{
    uint32_t p = RAM_Player_1;
    static uint16_t last_fc;
    uint16_t fc = r16(RAM_Level_frame_counter);              /* reset by every level (re)load */
    if (fc < last_fc) level_entry();
    last_fc = fc;
    s_frames++;
    int air = (OB8(p, OBJ_status) & 2) != 0;
    int jumping = OB8(p, P_JUMPING) != 0;
    uint8_t pressed = r8(RAM_Ctrl_1_held_logical + 1), held = r8(RAM_Ctrl_1_held_logical);

    /* ---- achievements in play ---- */
    if (OB8(p, P_ROUTINE) == 4) s_hit = 1;
    if (r16(RAM_Ring_count) >= 200) earn(A_RINGS);
    if (s_toast_time) s_toast_time--;

    /* ---- modifiers ---- */
    if (s_frames == 2 && OB8(p, P_ROUTINE) == 2) {
        if (on(U_SHIELD)) give_shield(p, s_prog.value[U_SHIELD]);
        if (on(U_SHOES)) give_shoes(p);
    }
    if (on(U_RINGS) && !s_rings_given && s_frames >= 2) {
        int n = s_prog.value[U_RINGS] == 1 ? 10 : 50;
        if (r16(RAM_Ring_count) < n) { w16(RAM_Ring_count, (uint16_t)n); w8(RAM_Update_HUD_ring_count, 1); }
        s_rings_given = 1;
    }
    if (on(U_AIR)) {
        SET8(RAM_Player_1, P_AIR, 30);
        SET8(RAM_Player_2, P_AIR, 30);
    }
    if (on(U_LIVES) && r8(RAM_Life_count) < 3) {
        w8(RAM_Life_count, 3);
        w8(RAM_Update_HUD_life_count, 1);
    }
    if (on(U_HIGHJUMP) && jumping && !s_prev_jumping && (int16_t)OB16(p, OBJ_y_vel) < 0) {
        int16_t s, c;
        o_sincos((uint8_t)(OB8(p, P_ANGLE) - 0x40), &s, &c);
        SET16(p, OBJ_x_vel, (uint16_t)((int16_t)OB16(p, OBJ_x_vel) + ((c * 0x180) >> 8)));
        SET16(p, OBJ_y_vel, (uint16_t)((int16_t)OB16(p, OBJ_y_vel) + ((s * 0x180) >> 8)));
    }
    if (on(U_MOON) && air && OB8(p, P_ROUTINE) == 2 && !OB8(p, P_OBJCTRL))
        SET16(p, OBJ_y_vel, (uint16_t)((int16_t)OB16(p, OBJ_y_vel) - ((OB8(p, OBJ_status) & 0x40) ? 0x08 : 0x1C)));

    /* ---- Sonic's abilities (second jump press; shields keep their own moves) ---- */
    int sonic = OB8(p, P_CHAR) == 0 && OB8(p, P_ROUTINE) == 2 && !OB8(p, P_OBJCTRL);
    if (sonic && air && jumping && s_prev_air && ((pressed & BTN_JUMP) || s_home_req) && !(OB8(p, P_STATUS2) & 0x70)) {
        uint32_t t = on(U_HOMING) ? homing_target(p) : 0;
        if (t) {
            home_aim(p, t);
            s_homing = 1;
            s_home_target = t;
            s_home_code = r32(t);
            s_home_time = 40;
            s_dd = 0;
            o_sfx(0xB6);
        } else if (on(U_DROPDASH)) {
            s_dd = 1;
            s_dd_charge = 0;
        }
    }
    if (s_dd) {
        if (!sonic || !(held & BTN_JUMP)) s_dd = 0;
        else if (air) {
            if (s_dd == 1 && ++s_dd_charge >= 20) { s_dd = 2; o_sfx(0xAB); }
            /* the ball spins faster: every other frame charging, every frame when ready */
            if (OB8(p, P_ANIM) == 2 && (s_dd == 2 || (s_frames & 1))) SET8(p, P_ANIMTIME, 0);
        } else {                                               /* landed */
            if (s_dd == 2 && s_prev_air) drop_dash_launch(p);
            s_dd = 0;
        }
    }
    if (s_home_bounce) home_bounce(p);                         /* hit something on the way */
    else if (s_homing) {
        uint32_t t = s_home_target;
        if (!air || !sonic || --s_home_time <= 0 || r32(t) != s_home_code || !OB8(t, OBJ_coll_flags))
            s_homing = 0;
        else if (s_home_code == S3K_Obj_Monitor) {
            int dx = (int16_t)OB16(t, OBJ_x_pos) - (int16_t)OB16(p, OBJ_x_pos);
            int dy = (int16_t)OB16(t, OBJ_y_pos) - (int16_t)OB16(p, OBJ_y_pos);
            if (dx > -0x18 && dx < 0x18 && dy > -0x20 && dy < 0x20) {
                if (OB8(t, OBJ_routine) == 2) {                 /* Touch_Monitor .okaytodestroy */
                    SET8(t, OBJ_routine, 4);
                    SET16(t, 0x42, (uint16_t)p);
                }
                home_bounce(p);
            } else home_aim(p, t);
        } else home_aim(p, t);                                 /* follow a moving badnik */
    }

    moves_post();
    s_prev_air = air;
    s_prev_jumping = jumping;
}

/* ------------------------------------------------------------------------
 * Pause menu
 * ---------------------------------------------------------------------- */
enum { PG_MAIN, PG_ABIL, PG_MODS, PG_ACH, PG_OPT, PG_VIDEO, PG_GAME, PG_ZONE, PG_COUNT };
static const char *const k_optmenu[2] = { "VIDEO", "GAME" };
enum { M_CONTINUE, M_RESTART, M_ZONE, M_ABIL, M_MODS, M_ACH, M_OPT, M_EXIT, M_COUNT };
static const char *const k_main[M_COUNT] = {
    "CONTINUE", "RESTART ACT", "ZONE SELECT", "ABILITIES", "UNLOCKS", "ACHIEVEMENTS", "OPTIONS", "EXIT TO TITLE"
};

/* zone select: every imported zone, act chosen with left / right */
static const struct { const char *name; uint16_t act[3]; int acts; } k_zones[] = {
    { "GREEN HILL",     { 0x2000, 0x2001, 0x2002 }, 3 },
    { "MARBLE",         { 0x2200, 0x2201, 0x2202 }, 3 },
    { "SPRING YARD",    { 0x2400, 0x2401, 0x2402 }, 3 },
    { "LABYRINTH",      { 0x2100, 0x2101, 0x2102 }, 3 },
    { "STAR LIGHT",     { 0x2300, 0x2301, 0x2302 }, 3 },
    { "SCRAP BRAIN",    { 0x2500, 0x2501, 0x2103 }, 3 },
    { "FINAL ZONE",     { 0x2502 }, 1 },
    { "EMERALD HILL",   { 0x2800, 0x2801 }, 2 },
    { "CHEMICAL PLANT", { 0x2900, 0x2901 }, 2 },
    { "AQUATIC RUIN",   { 0x2A00, 0x2A01 }, 2 },
    { "CASINO NIGHT",   { 0x2B00, 0x2B01 }, 2 },
    { "HILL TOP",       { 0x2C00, 0x2C01 }, 2 },
    { "MYSTIC CAVE",    { 0x2D00, 0x2D01 }, 2 },
    { "OIL OCEAN",      { 0x2E00, 0x2E01 }, 2 },
    { "METROPOLIS",     { 0x2F00, 0x2F01, 0x2F02 }, 3 },
    { "SKY CHASE",      { 0x3000 }, 1 },
    { "WING FORTRESS",  { 0x3100 }, 1 },
    { "DEATH EGG",      { 0x3200 }, 1 },
};
#define N_ZONES ((int)(sizeof k_zones / sizeof k_zones[0]))
static int s_zone_act[N_ZONES];
static const char *const k_actname[3] = { "ACT 1", "ACT 2", "ACT 3" };

static void zone_warp(int i)
{
    uint16_t za = k_zones[i].act[s_zone_act[i]];
    w16(RAM_Current_zone_and_act, za);
    w16(RAM_Apparent_zone_and_act, tri_apparent(za));
    w8(RAM_Last_star_post_hit, 0);
    w16(RAM_Restart_level_flag, 1);
    fprintf(stderr, "[trilogy] zone select -> $%04X\n", za);
}

static int s_open, s_page, s_sel[PG_COUNT], s_fade, s_dirty;
static int s_tick;                  /* menu frames, for scrolling names */

static int page_count(int pg)
{
    switch (pg) {
    case PG_MAIN: return M_COUNT;
    case PG_ABIL: return abil_count();
    case PG_MODS: return N_MODS;
    case PG_ACH:  return A_COUNT;
    case PG_OPT:  return 2;
    case PG_VIDEO: return N_VIDEO_OPTS;
    case PG_ZONE: return N_ZONES;
    default:      return N_GAME_OPTS;
    }
}

static int opt_at(int pg, int i) { return pg == PG_VIDEO ? k_video_opts[i] : k_game_opts[i]; }

/* the window the game is presented in */
static SDL_Window *game_window(void)
{
    SDL_Window *w = SDL_GetKeyboardFocus();
    for (Uint32 id = 1; !w && id < 16; id++) {
        SDL_Window *c = SDL_GetWindowFromID(id);
        if (c && (SDL_GetWindowFlags(c) & SDL_WINDOW_SHOWN)) w = c;
    }
    return w;
}

/* options that live outside the menu: read them back when it opens */
static void sync_live_options(void)
{
    if (!sonic3_video.enabled()) s_prog.opt[O_WIDE] = 0;
    else if (!s_prog.opt[O_WIDE] && !s_prog.opt[O_BGBLUR]) s_prog.opt[O_WIDE] = 1;   /* (blur alone runs it at 4:3) */
    SDL_Window *w = game_window();
    s_prog.opt[O_FULL] = w && (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) ? 1 : 0;
}

static void apply_live_option(int o)
{
    if (o == O_WIDE || o == O_BGBLUR) {
        static const char *const mode[4] = { "off", "fit", "16:9", "21:9" };
        /* the blur needs the layered renderer: without widescreen it runs at the native 4:3 */
        sonic3_video.configure(s_prog.opt[O_WIDE] ? mode[s_prog.opt[O_WIDE] & 3] : s_prog.opt[O_BGBLUR] ? "4:3" : "off");
        s3_video_set_bg_blur(s_prog.opt[O_BGBLUR]);
    } else if (o == O_CRT) {
        genesis_present_set_crt(s_prog.opt[O_CRT]);
    } else if (o == O_FULL) {
        SDL_Window *w = game_window();
        if (w) SDL_SetWindowFullscreen(w, s_prog.opt[O_FULL] ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
}

static void change(int pg, int i, int delta)
{
    if (pg == PG_ZONE) {
        int n = k_zones[i].acts;
        s_zone_act[i] = (s_zone_act[i] + delta + n) % n;
        return;
    }
    if (pg == PG_ABIL || pg == PG_MODS) {
        int u = pg == PG_ABIL ? abil_at(i) : k_mod_list[i];
        if (!unlocked(u)) return;
        int n = k_unlock[u].count;
        s_prog.value[u] = (uint8_t)((s_prog.value[u] + delta + n) % n);
        if (u == U_PEELOUT && !s_prog.value[u] && s_po) {      /* switched off mid-rev */
            if (OB8(RAM_Player_1, P_OBJCTRL) == 1) SET8(RAM_Player_1, P_OBJCTRL, 0);
            w8(RAM_Dust + 0x20, 0);
            SET8(RAM_Player_1, P_SPINDASH, 0);
            s_po = 0;
        }
        s_dirty = 1;
    } else if (pg == PG_VIDEO || pg == PG_GAME) {
        int o = opt_at(pg, i), n = k_opt[o].count;
        s_prog.opt[o] = (uint8_t)((s_prog.opt[o] + delta + n) % n);
        apply_live_option(o);
        s_dirty = 1;
    }
}

/* runs once per paused frame; returns 1 to resume */
static int menu_tick(uint8_t pressed)
{
    if (!s_open) {
        prog_init();
        s_open = 1;
        s_page = PG_MAIN;
        s_sel[PG_MAIN] = 0;
        s_fade = 0;
        sync_live_options();
    }
    if (s_fade < 12) s_fade++;
    s_tick++;
    int n = page_count(s_page), *sel = &s_sel[s_page];
    if (pressed & 0x0F) s_tick = 0;                             /* new row: scroll from the start */
    if (pressed & 0x80) return 1;                               /* Start: back to the game */
    if (pressed & 0x01) *sel = (*sel + n - 1) % n;
    if (pressed & 0x02) *sel = (*sel + 1) % n;
    if (pressed & 0x04) change(s_page, *sel, -1);
    if (pressed & 0x08) change(s_page, *sel, +1);
    if (pressed & 0x10) {                                       /* B: back */
        if (s_page == PG_MAIN) return 1;
        s_page = (s_page == PG_VIDEO || s_page == PG_GAME) ? PG_OPT : PG_MAIN;
        return 0;
    }
    if (pressed & 0x60) {                                       /* A / C: select */
        if (s_page == PG_OPT) {
            s_page = *sel ? PG_GAME : PG_VIDEO;
            s_sel[s_page] = 0;
            return 0;
        }
        if (s_page == PG_ZONE) { zone_warp(*sel); return 1; }
        if (s_page != PG_MAIN) { change(s_page, *sel, +1); return 0; }
        switch (*sel) {
        case M_CONTINUE: return 1;
        case M_RESTART:  w16(RAM_Restart_level_flag, 1); return 1;
        case M_ZONE:     s_page = PG_ZONE; break;
        case M_ABIL:     s_page = PG_ABIL; s_sel[PG_ABIL] = 0; break;
        case M_MODS:     s_page = PG_MODS; s_sel[PG_MODS] = 0; break;
        case M_ACH:      s_page = PG_ACH;  s_sel[PG_ACH] = 0; break;
        case M_OPT:      s_page = PG_OPT;  s_sel[PG_OPT] = 0; break;
        case M_EXIT:     w8(RAM_Game_mode, 0x04); return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Drawing (frame overlay; reads guest state only)
 * ---------------------------------------------------------------------- */
#define PANEL_W   176
#define LINE_H    10

static const uint8_t *font(void)
{
    static uint8_t f[40 * 32];
    static int loaded;
    if (!loaded) {
        uint32_t a = tri_rd32(tri_rd32(TRI_BANK_BASE + TB_BLOBS) + TRI_BLOB_MENU_FONT * 4u);
        for (int i = 0; i < (int)sizeof f; i++) f[i] = tri_rd8(a + (uint32_t)i);
        loaded = 1;
    }
    return f;
}

static int glyph(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 14 + c - 'A';
    if (c == '*') return 10;
    if (c == ':') return 12;
    if (c == '.') return 13;
    return -1;
}

enum { C_NORMAL, C_SELECT, C_DIM, C_TITLE, C_GOOD };
static const uint32_t k_ramp[5][3] = {      /* font colours 1 (shade), 6 (face), 7 (light) */
    { 0x202040, 0xE0E0E0, 0xFFFFFF },
    { 0x604000, 0xFFE020, 0xFFFFA0 },
    { 0x101018, 0x686878, 0x8C8C9C },
    { 0x002060, 0x40A0FF, 0xC0E0FF },
    { 0x004010, 0x40E060, 0xC0FFC0 },
};

/* one 8px text row into `out`, clipped to [x0, x1) */
static void text_clip(uint32_t *out, int row, int x, const char *s, int col, int x0, int x1);
static void text_line(uint32_t *out, int row, int x, const char *s, int col, int x1)
{
    text_clip(out, row, x, s, col, 0, x1);
}


/* text in [x, x1); too long, it scrolls along to show its end, then back */
static void text_scroll(uint32_t *out, int row, int x, const char *s, int col, int x1)
{
    int over = (int)strlen(s) * 8 - (x1 - x);
    int off = 0;
    if (over > 0) {
        int travel = over * 2, period = 60 + travel + 60;
        int t = s_tick % period;
        off = t < 60 ? 0 : t < 60 + travel ? (t - 60) / 2 : over;
    }
    text_clip(out, row, x - off, s, col, x, x1);
}

static void text_clip(uint32_t *out, int row, int x, const char *s, int col, int x0, int x1)
{
    const uint8_t *f = font();
    if (!f || row < 0 || row > 7) return;
    for (; *s; s++, x += 8) {
        int g = glyph(*s);
        if (g < 0) continue;
        for (int xx = 0; xx < 8; xx++) {
            int X = x + xx;
            if (X < x0 || X >= x1) continue;
            uint8_t b = f[g * 32 + row * 4 + xx / 2];
            int v = (xx & 1) ? b & 15 : b >> 4;
            if (v == 1) out[X] = k_ramp[col][0];
            else if (v == 6) out[X] = k_ramp[col][1];
            else if (v == 7) out[X] = k_ramp[col][2];
        }
    }
}

static int text_w(const char *s) { return (int)strlen(s) * 8; }

/* word-wrap s into lines of at most `cols` characters; returns line `n` */
static int wrap_line(const char *s, int cols, int n, char *buf)
{
    int line = 0;
    while (*s) {
        while (*s == ' ') s++;
        const char *start = s, *brk = NULL;
        int len = 0;
        while (s[len] && len < cols) { if (s[len] == ' ') brk = s + len; len++; }
        const char *end = s[len] == 0 ? s + len : (brk ? brk : s + len);
        if (line == n) {
            int k = (int)(end - start);
            memcpy(buf, start, (size_t)k);
            buf[k] = 0;
            return 1;
        }
        s = end;
        line++;
    }
    return 0;
}

static uint32_t *s_hb;              /* horizontally blurred clean rows */
static int s_hb_w;

static inline uint32_t mix(uint32_t a, uint32_t b, int t, int tmax)   /* a->b by t/tmax */
{
    uint32_t r = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int ca = (a >> sh) & 255, cb = (b >> sh) & 255;
        r |= (uint32_t)(ca + (cb - ca) * t / tmax) << sh;
    }
    return r;
}

/* ---- the emerald tracker (pause page): S3K's Data Select emeralds --------
 * The save screen's own sprites (ArtKos_SaveScreenMisc + ArtKos_SaveScreen, Map_SaveScreen frames
 * $10-$16 Chaos and $1C-$22 Super, palette line 2 of Pal_Save_Chars) in their
 * Data Select ring, decoded once from the cartridge. Missing ones are dimmed. */
#define EM_N 14
static uint32_t s_em_px[EM_N][16 * 16];
static int s_em_x[EM_N], s_em_y[EM_N], s_em_w[EM_N], s_em_h[EM_N];
static int s_em_state;                                       /* 0 not loaded, 1 ready, -1 failed */

static uint32_t md_rgb(uint16_t c)
{
    uint32_t r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
    return (r * 36) << 16 | (g * 36) << 8 | (b * 36);
}

static void emerald_load(void)
{
    extern uint32_t tri_kos_decode(uint32_t a, uint8_t *out, uint32_t cap);
    if (s_em_state) return;
    s_em_state = -1;
    uint8_t *art = (uint8_t *)malloc(0x10000);
    if (!art) return;
    uint32_t n = tri_kos_decode(S3K_ArtKos_SaveScreenMisc, art, 0x10000);   /* ArtTile_Save_Misc ($29F) */
    if (n == (0x454u - 0x29Fu) * 32u)                    /* then ArtTile_Save_Extra ($454): the Super Emeralds */
        n += tri_kos_decode(S3K_ArtKos_SaveScreen, art + n, 0x10000 - n);
    uint16_t pal[4][16] = { { 0 } };
    for (int i = 0; i < 16; i++) pal[0][i] = tri_rd16(S3K_Pal_SaveMenuBG + (uint32_t)i * 2u);
    for (int i = 0; i < 32; i++) pal[1 + i / 16][i % 16] = tri_rd16(S3K_Pal_Save_Chars + (uint32_t)i * 2u);
    for (int e = 0; e < EM_N; e++) {
        int f = e < 7 ? 0x10 + e : 0x1C + e - 7;
        uint32_t m = S3K_Map_SaveScreen + tri_rd16(S3K_Map_SaveScreen + (uint32_t)f * 2u);
        int y = (int8_t)tri_rd8(m + 2), size = tri_rd8(m + 3), x = (int16_t)tri_rd16(m + 6);
        uint16_t tile = tri_rd16(m + 4);
        int tw = ((size >> 2) & 3) + 1, th = (size & 3) + 1;
        int ti = tile & 0x7FF, pl = (tile >> 13) & 3, hf = (tile >> 11) & 1, vf = (tile >> 12) & 1;
        if (tw > 2 || th > 2) continue;
        s_em_x[e] = x;
        s_em_y[e] = y;
        s_em_w[e] = tw * 8;
        s_em_h[e] = th * 8;
        for (int py = 0; py < th * 8; py++)
            for (int px = 0; px < tw * 8; px++) {
                int sx = hf ? tw * 8 - 1 - px : px, sy = vf ? th * 8 - 1 - py : py;
                uint32_t t = (uint32_t)(ti + (sx / 8) * th + sy / 8);      /* (sprite tiles run down the columns) */
                uint32_t a = t * 32u + (uint32_t)(sy % 8) * 4u + (uint32_t)(sx % 8) / 2u;
                if (a >= n) continue;
                int nib = (sx & 1) ? art[a] & 15 : art[a] >> 4;
                s_em_px[e][py * 16 + px] = nib ? 0xFF000000u | md_rgb(pal[pl][nib]) : 0;
            }
    }
    free(art);
    s_em_state = 1;
}

/* one ring (Chaos or Super) centred on cx, cy */
static void emerald_ring(int line, uint32_t *out, int x1, int cx, int cy, int super)
{
    for (int i = 0; i < 7; i++) {
        int e = super ? 7 + i : i;
        uint8_t v = r8(RAM_Collected_emeralds_array + (uint32_t)i);
        int have = super ? v == 3 : v != 0;
        int y = cy + s_em_y[e];
        if (line < y || line >= y + s_em_h[e]) continue;
        const uint32_t *row = &s_em_px[e][(line - y) * 16];
        int x = cx + s_em_x[e];
        for (int k = 0; k < s_em_w[e]; k++) {
            uint32_t c = row[k];
            if (!c || x + k < 0 || x + k >= x1) continue;
            c &= 0xFFFFFF;
            if (!have) {                                     /* still to find: a dark outline of it */
                uint32_t l = ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255)) / 3;
                c = mix(l << 16 | l << 8 | l, 0x101020, 3, 4);
            }
            out[x + k] = c;
        }
    }
}

static void emerald_tracker(int line, uint32_t *out, int tx, int x1, int y0)
{
    if (line < y0 || line >= y0 + 90) return;
    emerald_load();
    if (s_em_state != 1) return;
    int cy = y0 + 54;
    emerald_ring(line, out, x1, tx + 38, cy, 0);
    emerald_ring(line, out, x1, tx + 118, cy, 1);
    int ly = cy + 22;
    if (line >= ly && line < ly + 8) {
        char a[16], b[16];
        snprintf(a, sizeof a, "CHAOS %d", r8(RAM_Chaos_emerald_count));
        snprintf(b, sizeof b, "SUPER %d", r8(RAM_Super_emerald_count));
        text_line(out, line - ly, tx + 38 - text_w(a) / 2, a, C_TITLE, x1);
        text_line(out, line - ly, tx + 118 - text_w(b) / 2, b, C_TITLE, x1);
    }
}

static void blur_dim_row(int line, uint32_t *out, int width)
{
    if (width != s_hb_w) {
        free(s_hb);
        s_hb = (uint32_t *)calloc((size_t)width * 224, sizeof *s_hb);
        s_hb_w = s_hb ? width : 0;
        if (!s_hb) return;
    }
    if (line < 0 || line >= 224) return;
    /* horizontal box blur (radius 3) of this clean row */
    uint32_t *h = s_hb + (size_t)line * width;
    for (int x = 0; x < width; x++) {
        int r = 0, g = 0, b = 0, n = 0;
        for (int k = -3; k <= 3; k++) {
            int X = x + k;
            if (X < 0 || X >= width) continue;
            uint32_t c = out[X];
            r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
        }
        h[x] = ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
    }
    int blur = s_prog.opt[O_BLUR];
    int dim = s_prog.opt[O_DIM] == 0 ? 256 : s_prog.opt[O_DIM] == 1 ? 192 : 128;
    int t = s_fade, tmax = 12;
    for (int x = 0; x < width; x++) {
        uint32_t c = out[x];
        if (blur) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int k = -3; k <= 3; k++) {
                int L = line + k;
                if (L < 0 || L >= 224) continue;
                uint32_t v = s_hb[(size_t)L * width + x];
                r += (v >> 16) & 255; g += (v >> 8) & 255; b += v & 255; n++;
            }
            uint32_t bl = ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
            c = mix(c, bl, t, tmax);
        }
        int d = 256 - (256 - dim) * t / tmax;
        out[x] = (((((c >> 16) & 255) * d) >> 8) << 16) | (((((c >> 8) & 255) * d) >> 8) << 8) | (((c & 255) * d) >> 8);
    }
}

static void panel_row(int line, uint32_t *out, int width)
{
    int x0 = width - PANEL_W;
    if (x0 < 0) x0 = 0;
    /* panel: darker glass with a light edge, sliding in */
    int slide = (12 - s_fade) * PANEL_W / 12;
    x0 += slide;
    for (int x = x0; x < width; x++) {
        uint32_t c = out[x];
        out[x] = mix(c, 0x000010, 3, 5);
    }
    if (x0 < width) out[x0] = 0x4060C0;
    if (x0 + 1 < width) out[x0 + 1] = 0x203060;

    int tx = x0 + 10, x1 = width;
    static const char *const titles[PG_COUNT] = { "PAUSE", "ABILITIES", "UNLOCKS", "ACHIEVEMENTS", "OPTIONS", "VIDEO", "GAME", "ZONE SELECT" };
    int y = 14;
    if (line >= y && line < y + 8) text_line(out, line - y, tx, titles[s_page], C_TITLE, x1);
    if (line == y + 10) for (int x = tx; x < x1 - 10; x++) out[x] = 0x4060C0;
    y += 18;

    int n = page_count(s_page), sel = s_sel[s_page];
    const char *desc = NULL;
    char extra[48] = "";
    for (int i = 0; i < n; i++, y += LINE_H) {
        if (line < y || line >= y + 8) continue;
        int row = line - y, col = i == sel ? C_SELECT : C_NORMAL;
        const char *name = "", *value = NULL;
        switch (s_page) {
        case PG_MAIN: name = k_main[i]; break;
        case PG_ABIL: case PG_MODS: {
            int u = s_page == PG_ABIL ? abil_at(i) : k_mod_list[i];
            name = k_unlock[u].name;
            if (unlocked(u)) value = k_unlock[u].values[s_prog.value[u]];
            else { value = "LOCKED"; if (i != sel) col = C_DIM; }
            break;
        }
        case PG_ACH:
            name = k_ach[i].name;
            if (s_prog.ach & (1u << i)) { if (i != sel) col = C_GOOD; text_line(out, row, tx - 9, "*", C_SELECT, x1); }
            else if (i != sel) col = C_DIM;
            break;
        case PG_OPT:
            name = k_optmenu[i];
            break;
        case PG_ZONE:
            name = k_zones[i].name;
            if (k_zones[i].acts > 1) value = k_actname[s_zone_act[i]];
            break;
        default: {
            int o = opt_at(s_page, i);
            name = k_opt[o].name;
            value = k_opt[o].values[s_prog.opt[o]];
        }
        }
        if (i == sel && s_page != PG_ACH) text_line(out, row, tx - 9, "*", C_SELECT, x1);
        if (value) {
            /* the value sits right-aligned; long names share the row */
            int vx = x1 - 8 - text_w(value);
            text_scroll(out, row, tx, name, col, vx - 6);
            text_line(out, row, vx, value, col, x1);
        } else text_scroll(out, row, tx, name, col, x1 - 4);
    }
    /* description of the selection, at the bottom */
    switch (s_page) {
    case PG_ABIL: case PG_MODS: {
        int u = s_page == PG_ABIL ? abil_at(sel) : k_mod_list[sel];
        if (unlocked(u)) desc = k_unlock[u].desc;
        else for (int a = 0; a < A_COUNT; a++)
            if (k_ach[a].unlock == u) { snprintf(extra, sizeof extra, "EARN %s", k_ach[a].name); desc = extra; }
        break;
    }
    case PG_ACH: {
        desc = k_ach[sel].desc;
        if (k_ach[sel].unlock >= 0) snprintf(extra, sizeof extra, "UNLOCKS %s", k_unlock[k_ach[sel].unlock].name);
        break;
    }
    case PG_VIDEO: case PG_GAME: desc = k_opt[opt_at(s_page, sel)].desc; break;
    default: break;
    }
    int cols = (x1 - tx - 6) / 8;
    int dy = 32 + n * LINE_H + 8;                               /* below the list */
    if (desc) {
        char buf[64];
        int k = 0;
        for (; k < 4 && wrap_line(desc, cols, k, buf); k++)
            if (line >= dy + k * 10 && line < dy + k * 10 + 8) text_line(out, line - dy - k * 10, tx, buf, C_NORMAL, x1);
        if (s_page == PG_ACH && extra[0]) {
            int ey = dy + k * 10 + 2;
            for (int j = 0; j < 2 && wrap_line(extra, cols, j, buf); j++)
                if (line >= ey + j * 10 && line < ey + j * 10 + 8) text_line(out, line - ey - j * 10, tx, buf, C_SELECT, x1);
        }
    }
    if (s_page == PG_MAIN) emerald_tracker(line, out, tx, x1, 32 + n * LINE_H + 8);
    if (s_page != PG_ACH && line >= 224 - 14 && line < 224 - 6)
        text_line(out, line - (224 - 14), tx, s_page == PG_MAIN ? "A SELECT" : s_page == PG_OPT ? "A SELECT  B BACK" : s_page == PG_ZONE ? "A GO  B BACK" : "A CHANGE  B BACK", C_DIM, x1);
    if (s_page == PG_ACH && line >= 224 - 14 && line < 224 - 6) {
        int got = 0;
        for (int a = 0; a < A_COUNT; a++) got += (s_prog.ach >> a) & 1;
        char line_s[24];                                        /* the font has no slash */
        snprintf(line_s, sizeof line_s, "%d OF %d", got, A_COUNT);
        text_line(out, line - (224 - 14), tx, line_s, C_TITLE, x1);
    }
}

static void toast_row(int line, uint32_t *out, int width)
{
    if (s_toast_ach < 0 || !s_toast_time) return;
    const char *name = k_ach[s_toast_ach].name;
    int w = text_w(name) + 20;
    if (w < 120) w = 120;
    int in = s_toast_time > 225 ? (240 - s_toast_time) : s_toast_time < 15 ? s_toast_time : 15;
    int x0 = width - in * w / 15, y0 = 8;
    if (line < y0 || line >= y0 + 26) return;
    for (int x = x0 < 0 ? 0 : x0; x < width; x++) out[x] = mix(out[x], 0x000010, 3, 4);
    if (x0 >= 0 && x0 < width) out[x0] = 0x4060C0;
    if (line >= y0 + 3 && line < y0 + 11) text_line(out, line - y0 - 3, x0 + 8, "ACHIEVEMENT", C_TITLE, width);
    if (line >= y0 + 15 && line < y0 + 23) text_line(out, line - y0 - 15, x0 + 8, name, C_SELECT, width);
}

static void frame_overlay(int line, uint32_t *out, int width);
static void register_overlay(void)
{
    static int done;
    if (!done) { s3_video_set_frame_overlay(frame_overlay); done = 1; }
}

/* picture options, on every frame */
static void video_fx(uint32_t *out, int width)
{
    static const int bright[5] = { 180, 218, 256, 294, 332 };
    static const int sat[4] = { 0, 128, 256, 368 };
    static const int tint[5][3] = { { 256, 256, 256 }, { 276, 256, 225 }, { 232, 256, 282 }, { 256, 256, 256 }, { 150, 180, 270 } };
    int b = bright[s_prog.opt[O_BRIGHT] % 5], st = sat[s_prog.opt[O_SAT] % 4], f = s_prog.opt[O_FILTER] % 5;
    if (s_prog.opt[O_DITHER]) {
        uint32_t prev = out[0];
        for (int x = 0; x + 1 < width; x++) {
            uint32_t a = prev, c = out[x + 1];
            prev = c;
            out[x] = (((a >> 1) & 0x7F7F7F) + ((c >> 1) & 0x7F7F7F)) + (a & c & 0x010101);
        }
    }
    if (b == 256 && st == 256 && f == 0) return;
    for (int x = 0; x < width; x++) {
        uint32_t c = out[x];
        int r = (c >> 16) & 255, g = (c >> 8) & 255, bl = c & 255;
        int y = (r * 77 + g * 150 + bl * 29) >> 8;
        if (f == 3) { r = y * 280 >> 8; g = y * 236 >> 8; bl = y * 180 >> 8; }   /* sepia */
        else {
            r = y + (((r - y) * st) >> 8); g = y + (((g - y) * st) >> 8); bl = y + (((bl - y) * st) >> 8);
            r = r * tint[f][0] >> 8; g = g * tint[f][1] >> 8; bl = bl * tint[f][2] >> 8;
        }
        r = r * b >> 8; g = g * b >> 8; bl = bl * b >> 8;
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        bl = bl < 0 ? 0 : bl > 255 ? 255 : bl;
        out[x] = (c & 0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
    }
}

static void frame_overlay(int line, uint32_t *out, int width)
{
    extern void tri_thumb_overlay(int line, uint32_t *out, int width);
    tri_thumb_overlay(line, out, width);                         /* (the Data Select's recorded zone cards) */
    video_fx(out, width);
    if (s_open && r16(RAM_Game_paused)) {
        blur_dim_row(line, out, width);
        panel_row(line, out, width);
    }
    toast_row(line, out, width);
}

/* ------------------------------------------------------------------------
 * Hooks
 * ---------------------------------------------------------------------- */
int tri_menu_hook(uint32_t pc)
{
    static int started;
    if (!started) { started = 1; prog_init(); register_overlay(); }   /* video options everywhere */
    switch (pc) {
    case TH_PAUSE_CHK: {
        if (!level_mode()) return 0;
        register_overlay();
        if (menu_tick(r8(RAM_Ctrl_1_pressed))) {
            s_open = 0;
            if (s_dirty) { prog_save(); s_dirty = 0; }
            w8(RAM_Ctrl_1_pressed, 0x80);                       /* resume */
        } else
            w8(RAM_Ctrl_1_pressed, 0);
        return 0;
    }
    case TH_LVL_PRE_SPRITES:
        if (level_mode()) { prog_init(); pre_sprites(); }
        return 0;
    case TH_LVL_POST_SPRITES:
        if (level_mode()) { register_overlay(); post_sprites(); }
        return 0;
    case TH_RING_ATTRACT: case TH_RING_ATTRACT2: case TH_RING_ATTRACT3:
        if (on(U_MAGNET)) g_cpu.SR &= ~4u;                      /* as if the shield bit were set */
        return 0;
    case TH_KILL_ENEMY:
        prog_load();
        if (++s_prog.badniks >= 100) earn(A_BADNIKS);
        if (s_homing) s_home_bounce = 1;                        /* bounce off it (post_sprites) */
        if (!(s_prog.badniks % 10)) prog_save();
        return 0;
    case TH_MONITOR_BREAK:
        if (s_homing) s_home_bounce = 1;
        prog_load();
        if (++s_prog.monitors >= 50) earn(A_MONITORS);
        if (!(s_prog.monitors % 10)) prog_save();
        return 0;
    case TH_MON_SOLID:                                          /* (beq: rolling, no solidity) */
        if (s_dig == 2 && (A(1) & 0xFFFFFFu) == RAM_Player_1) g_cpu.SR |= 4u;   /* burrowed: under it */
        return 0;
    case TH_SUPER_CHK: case TH_SUPER_CHK_K: case TH_SUPER_CHK_T:   /* Sonic, Knuckles, Tails */
        if (on(U_SUPER)) g_cpu.SR &= ~1u;                       /* emeralds check passes */
        return 0;
    default:
        return 0;
    }
}
