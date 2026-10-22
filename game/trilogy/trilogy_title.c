/*
 * trilogy_title.c — the Sonic Anthology title screen and its options page.
 *
 * S3K's Title_Screen plays only its SEGA logo. Once the logo has faded
 * (TH_TITLE_TAKEOVER, the end of its fade loop) the routine returns to
 * GameLoop, and this title runs in its place as a scene of its own
 * (TH_GAMELOOP, Game_mode 4): none of Sonic 3's title is ever loaded, so
 * nothing of it can show through. The scene keeps a black screen under this
 * drawing, plays the title music and owns the controls:
 *
 *   PRESS START  ->  START GAME / COMPETITION / OPTIONS
 *
 * START GAME and COMPETITION fade out and leave as Sonic 3's title does
 * (Data Select $4C / Competition $38, new-game counters). OPTIONS opens a
 * full-screen page of the pause menu's Video and Game settings. Idle on
 * PRESS START, the cartridge's own attract demo starts (loc_4278).
 *
 * The look is the Japanese Mega Drive box fronts': paper-white ground, flat
 * primary wedges and discs, Memphis confetti, saw-tooth bands. Art comes from
 * assets/trilogy/ beside the executable, as uncompressed 32-bit TGA (the
 * launcher's format), made by tools/trilogy/title_art.py:
 *   title_bg.tga    the ground, 4:3 picture centred, wider for widescreen
 *   title_side.tga  the left margin's black band (16:9 and wider)
 *   title_logo.tga  the pre-rendered logo (logo.blend), alpha blended
 *   menu_bg.tga     the options page's ground
 *   ds_tile.tga     the Data Select's pattern (player sprites stamped in)
 *   ds_band.tga     the Data Select's title band
 *   title_text.tga  the band's lettering (the boxes' typeface)
 * Without them a plain backdrop and the text stand in.
 */
#include "trilogy_obj.h"
#include "sonic3_video.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_Ctrl_2_pressed       0xFFF607u
#define RAM_Title_screen_option  0xFFFF86u
#define RAM_Life_count_P2        0xFFFEC6u
#define RAM_Continue_count       0xFFFE18u
#define RAM_Score                0xFFFE26u
#define RAM_Ring_count_P2        0xFFFED0u
#define RAM_Timer_P2             0xFFFED2u
#define RAM_Score_P2             0xFFFED6u
#define RAM_Next_extra_life      0xFFFFC0u
#define RAM_Next_extra_life_P2   0xFFFFC4u
#define S3K_TITLE_DEMO  0x004278u     /* (Title_Screen's attract-demo start, loc_4278) */
#define MUS_TITLE       0x25
#define CMD_FADEOUT     0xE1
#define GM_TITLE  0x04
#define BTN_UP    0x01
#define BTN_DOWN  0x02
#define BTN_LEFT  0x04
#define BTN_RIGHT 0x08
#define BTN_B     0x10
#define BTN_AC    0x60
#define BTN_START 0x80
#define SFX_SWITCH 0x5B
#define SFX_RING   0x33

int tri_opt_count(void);
const char *tri_opt_label(int i);
const char *tri_opt_value(int i);
const char *tri_opt_desc(int i);
void tri_opt_open(void);
void tri_opt_change(int i, int delta);
void tri_opt_close(void);
void tri_text(uint32_t *out, int row, int x, const char *s, int col, int x1);
int tri_text_w(const char *s);
int tri_text_wrap(const char *s, int cols, int n, char *buf);
enum { C_NORMAL, C_SELECT, C_DIM, C_TITLE, C_GOOD, C_INK, C_PALE, C_DARK, C_RED };

/* ---- images ------------------------------------------------------------- */
typedef struct { int w, h; uint32_t *px; } Img;      /* ARGB, top row first */

static Img *img_load(const char *name)
{
    char *base = SDL_GetBasePath(), path[1024];
    snprintf(path, sizeof path, "%sassets/trilogy/%s", base ? base : "", name);
    SDL_free(base);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t h[18];
    Img *img = NULL;
    if (fread(h, 1, 18, f) == 18 && h[2] == 2 && h[16] == 32) {    /* uncompressed true colour, 32 bpp */
        int w = h[12] | h[13] << 8, hh = h[14] | h[15] << 8, top = (h[17] & 0x20) != 0;
        fseek(f, 18 + h[0], SEEK_SET);
        img = (Img *)calloc(1, sizeof *img);
        uint8_t *raw = (uint8_t *)malloc((size_t)w * hh * 4);
        if (img && raw && w > 0 && hh > 0 && fread(raw, 4, (size_t)w * hh, f) == (size_t)w * hh) {
            img->w = w;
            img->h = hh;
            img->px = (uint32_t *)malloc((size_t)w * hh * 4);
            for (int y = 0; y < hh && img->px; y++) {
                const uint8_t *r = raw + (size_t)(top ? y : hh - 1 - y) * w * 4;
                for (int x = 0; x < w; x++)
                    img->px[y * w + x] = (uint32_t)r[x * 4 + 3] << 24 | r[x * 4 + 2] << 16 | r[x * 4 + 1] << 8 | r[x * 4];
            }
        }
        free(raw);
        if (img && !img->px) { free(img); img = NULL; }
    }
    fclose(f);
    return img;
}

static Img *s_bg, *s_conf, *s_conf_open, *s_side, *s_logo, *s_menu_bg, *s_ds_tile, *s_ds_band, *s_text;
/* the new file's picker and the Data Select's badges: each experience's logo
 * (game_*.tga, the picker's size; badge_*.tga, a file card's), and EASY */
static Img *s_game[TRI_EXP_COUNT], *s_badge[TRI_EXP_COUNT], *s_badge_easy;
static const char *const k_exp_file[TRI_EXP_COUNT] = { "all", "s1", "cd", "s2", "s3k" };
/* title_text.tga: rows of TEXT_H, white then gold, for these words */
enum { TX_PRESS, TX_START, TX_COMPETITION, TX_OPTIONS, TX_COUNT, TEXT_H = 18 };
static int s_text_w[TX_COUNT];
static void load_art(void)
{
    static int done;
    if (done) return;
    done = 1;
    s_bg = img_load("title_bg.tga");
    s_conf = img_load("title_confetti.tga");
    s_conf_open = img_load("title_confetti_open.tga");
    s_side = img_load("title_side.tga");
    s_logo = img_load("title_logo.tga");
    s_menu_bg = img_load("menu_bg.tga");
    s_ds_tile = img_load("ds_tile.tga");
    s_ds_band = img_load("ds_band.tga");
    s_text = img_load("title_text.tga");
    for (int i = 0; i < TRI_EXP_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof name, "game_%s.tga", k_exp_file[i]);
        s_game[i] = img_load(name);
        snprintf(name, sizeof name, "badge_%s.tga", k_exp_file[i]);
        s_badge[i] = img_load(name);
    }
    s_badge_easy = img_load("badge_easy.tga");
    if (s_text && s_text->h < TX_COUNT * 2 * TEXT_H) { free(s_text->px); free(s_text); s_text = NULL; }
    for (int i = 0; s_text && i < TX_COUNT; i++)          /* each word's width, from its alpha */
        for (int y = 0; y < TEXT_H; y++)
            for (int x = s_text->w - 1; x >= s_text_w[i]; x--)
                if (s_text->px[(size_t)(i * 2 * TEXT_H + y) * s_text->w + x] >> 24) { s_text_w[i] = x + 1; break; }
}

static uint32_t blend(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24;
    if (a == 255) return src | 0xFF000000u;
    if (!a) return dst;
    uint32_t r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * (255 - a)) / 255;
    uint32_t g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * (255 - a)) / 255;
    uint32_t b = ((src & 255) * a + (dst & 255) * (255 - a)) / 255;
    return 0xFF000000u | r << 16 | g << 8 | b;
}
static uint32_t shade(uint32_t c, int k)          /* k/256 brightness */
{
    uint32_t r = ((c >> 16) & 255) * (uint32_t)k >> 8, g = ((c >> 8) & 255) * (uint32_t)k >> 8, b = (c & 255) * (uint32_t)k >> 8;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

/* ---- state -------------------------------------------------------------- */
enum { ST_PRESS, ST_MENU, ST_OPTIONS, ST_LEAVE };
static const char *const k_items[] = { "START GAME", "COMPETITION", "OPTIONS" };
#define N_ITEMS 3
static int s_on, s_started, s_state, s_sel, s_opt, s_t, s_leave, s_choice;

/* The Konami code on PRESS START (up up down down left right left right B A)
 * unlocks everything (trilogy_menu.c) to the Chaos Emerald's jingle; the
 * title theme comes back after it. */
#define MUS_EMERALD  0x2B             /* mus_Emerald */
#define JINGLE_TIME  (5 * 60)
static const uint8_t k_konami[] = { BTN_UP, BTN_UP, BTN_DOWN, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_B, 0x40 };
static int s_konami, s_jingle;
void tri_konami_unlock(void);

static int konami(uint8_t press)                     /* 1: the code's last press (taken) */
{
    if (!press) return 0;
    if (press == k_konami[s_konami]) s_konami++;
    else s_konami = press == k_konami[0];
    if (s_konami < (int)sizeof k_konami) return 0;
    s_konami = 0;
    tri_konami_unlock();
    D(0) = MUS_EMERALD;
    tri_jsr(S3K_Play_Music);
    s_jingle = JINGLE_TIME;
    return 1;
}

static int title_mode(void) { return (r8(RAM_Game_mode) & 0x7F) == GM_TITLE; }

/* Sonic 3's title leaving for a new game: its counters, then the mode */
static void start_game(int choice)
{
    w8(RAM_Life_count, 3);
    w8(RAM_Life_count_P2, 3);
    w16(RAM_Ring_count, 0);
    w32(RAM_Timer, 0);
    w32(RAM_Score, 0);
    w16(RAM_Ring_count_P2, 0);
    w32(RAM_Timer_P2, 0);
    w32(RAM_Score_P2, 0);
    w8(RAM_Continue_count, 0);
    w32(RAM_Next_extra_life, 5000);
    w32(RAM_Next_extra_life_P2, 5000);
    w8(RAM_Title_screen_option, (uint8_t)choice);
    w8(RAM_Game_mode, choice ? 0x38 : 0x4C);          /* Competition / Data Select */
}

/* the scene: one call per frame from GameLoop */
static void title_scene(void)
{
    if (!s_started) {
        s_started = 1;
        for (uint32_t a = 0; a < 0x80; a += 2) w16(RAM_Normal_palette + a, 0);   /* (black under the drawing) */
        D(0) = MUS_TITLE;
        tri_jsr(S3K_Play_Music);
        w16(RAM_Demo_timer, 15 * 60);
        s_state = ST_PRESS;
        s_t = 0;
        s_konami = s_jingle = 0;
    }
    w8(RAM_V_int_routine, 2);                         /* (VInt_2: the pads, the palette, the demo timer) */
    tri_jsr(S3K_Wait_VSync);
    uint8_t press = r8(RAM_Ctrl_1_pressed) | r8(RAM_Ctrl_2_pressed);
    if (s_state != ST_PRESS || press || s_jingle) w16(RAM_Demo_timer, 15 * 60);   /* (no demo mid-code) */
    else if (!r16(RAM_Demo_timer)) {                  /* (the attract demo, as Sonic 3's title) */
        s_on = 0;
        tri_jsr(S3K_TITLE_DEMO);
        return;
    }
    if (s_jingle && !--s_jingle) {                    /* the jingle over: the title theme again */
        D(0) = MUS_TITLE;
        tri_jsr(S3K_Play_Music);
    }
    switch (s_state) {
    case ST_PRESS:
        if (konami(press)) break;
        if (press & (BTN_START | BTN_AC)) { s_state = ST_MENU; s_sel = 0; o_sfx(SFX_RING); }
        break;
    case ST_MENU:                                      /* (a row along the band) */
        if (press & (BTN_UP | BTN_LEFT)) { s_sel = (s_sel + N_ITEMS - 1) % N_ITEMS; o_sfx(SFX_SWITCH); }
        if (press & (BTN_DOWN | BTN_RIGHT)) { s_sel = (s_sel + 1) % N_ITEMS; o_sfx(SFX_SWITCH); }
        if (press & BTN_B) s_state = ST_PRESS;
        else if (press & (BTN_START | BTN_AC)) {
            o_sfx(SFX_RING);
            if (s_sel == 2) { tri_opt_open(); s_state = ST_OPTIONS; s_opt = 0; }
            else { s_choice = s_sel; s_state = ST_LEAVE; s_leave = 0; }
        }
        break;
    case ST_OPTIONS: {
        int n = tri_opt_count();
        if (press & BTN_UP) { s_opt = (s_opt + n - 1) % n; o_sfx(SFX_SWITCH); }
        if (press & BTN_DOWN) { s_opt = (s_opt + 1) % n; o_sfx(SFX_SWITCH); }
        if (press & BTN_LEFT) { tri_opt_change(s_opt, -1); o_sfx(SFX_SWITCH); }
        if (press & (BTN_RIGHT | BTN_AC)) { tri_opt_change(s_opt, +1); o_sfx(SFX_SWITCH); }
        if (press & (BTN_B | BTN_START)) { tri_opt_close(); s_state = ST_MENU; }
        break;
    }
    case ST_LEAVE:                                     /* fade out, then the new game */
        if (++s_leave >= 16) {
            D(0) = CMD_FADEOUT;
            tri_jsr(S3K_Play_SFX);
            s_on = 0;
            start_game(s_choice);
        }
        break;
    }
}

static int ds_active(void);
static int s_pick_on;
static void picker_input(void);

int tri_title_hook(uint32_t pc)
{
    if (pc == TH_DS_LOOP || pc == TH_DS_INIT) {       /* the new ground carries the title */
        uint32_t o = RAM_Player_1;                    /* (SaveScreen's first object: DATA SELECT) */
        if (ds_active() && r32(o) && r16(o + 0x10) == 0x120 && r16(o + 0x14) == 0x14C && r8(o + 0x22) == 3)
            w32(o, 0);
        if (pc == TH_DS_INIT) s_pick_on = 0;
        else if (s_pick_on) picker_input();           /* (a new file's picker: before the objects read the pads) */
        return 0;
    }
    if (pc == TH_TITLE_TAKEOVER) {                    /* the SEGA logo has faded: Sonic 3's title ends here */
        if (getenv("TRILOGY_CLASSIC_TITLE")) return 0;
        load_art();
        s_on = 1;
        s_started = 0;
        return 1;                                     /* (back to GameLoop, as its rts) */
    }
    if (pc == TH_GAMELOOP) {
        if (s_on && title_mode()) A(0) = tri_native(title_scene);
        else if (!title_mode()) s_on = 0;
    }
    return 0;
}

/* ---- drawing ------------------------------------------------------------ */
#define BAND_TOP 184                                  /* (title_bg's blue band) */
#define MENU_BAND 202                                 /* (menu_bg's) */

/* a ground picture, its centre on the screen's; past its edges the edge
 * columns carry on, and the bottom row; above its top (a taller screen) it
 * is reflected */
static void draw_ground(const Img *img, int line, uint32_t *out, int width)
{
    int y = line < 0 ? -1 - line : line;
    const uint32_t *row = img->px + (size_t)(y < img->h ? y : img->h - 1) * img->w;
    int x0 = (width - img->w) / 2;
    for (int x = 0; x < width; x++) {
        int sx = x - x0;
        out[x] = row[sx < 0 ? 0 : sx >= img->w ? img->w - 1 : sx] | 0xFF000000u;
    }
}

static void draw_img(const Img *img, int line, uint32_t *out, int width, int x0, int y0)
{
    int y = line - y0;
    if (!img || y < 0 || y >= img->h) return;
    const uint32_t *row = img->px + (size_t)y * img->w;
    for (int x = 0; x < img->w; x++)
        if (x0 + x >= 0 && x0 + x < width) out[x0 + x] = blend(out[x0 + x], row[x]);
}

/* The title's motion: the confetti rises (wrapping: title_confetti.tga),
 * the band's blue teeth run right, the green teeth down the right edge run
 * down; s_t counts the frames */
#define CONF_Y0 15                                    /* (title_confetti's first row) */
static int wrap(int v, int n) { v %= n; return v < 0 ? v + n : v; }

static void draw_confetti(int line, uint32_t *out, int width)
{
    if (!s_conf || line < CONF_Y0 || line >= CONF_Y0 + s_conf->h) return;
    const uint32_t *row = s_conf->px + (size_t)wrap(line - CONF_Y0 + s_t / 3, s_conf->h) * s_conf->w;
    int x0 = (width - s_conf->w) / 2;
    for (int x = x0 < 0 ? 0 : x0; x < width && x - x0 < s_conf->w; x++)
        if (row[x - x0] >> 24) out[x] = blend(out[x], row[x - x0]);
}

static void draw_band_teeth(int line, uint32_t *out, int width)   /* (10 px apart, 5 high) */
{
    int dy = BAND_TOP - line;
    if (dy < 1 || dy > 5) return;
    int x0 = (width - (s_bg ? s_bg->w : width)) / 2;
    for (int x = 0; x < width; x++) {
        int k = wrap(x - x0 - s_t / 2, 10);
        if (k >= dy && k <= 10 - dy) out[x] = 0xFF3157AEu;
    }
}

static void draw_bg(int line, uint32_t *out, int width)
{
    if (s_bg && s_bg->h > 0) {
        draw_ground(s_bg, line, out, width);
        draw_confetti(line, out, width);
        draw_band_teeth(line, out, width);
        if (width >= 384) {                           /* widescreen margins (Sonic 2's box) */
            draw_img(s_side, line, out, width, 0, 0);
            if (line < BAND_TOP - 6) {                /* green teeth down the right */
                int y = wrap(line - s_t / 2, 14);
                int depth = 13 - (y < 7 ? 7 - y : y - 7) * 13 / 7;
                for (int x = width - depth; x < width; x++) out[x] = 0xFF57AE57u;
            }
        }
        return;
    }
    /* stand-in: a deep blue sky band over a checkerboard */
    for (int x = 0; x < width; x++) {
        uint32_t c;
        if (line < 150) {
            int k = line * 255 / 150;
            c = 0xFF000000u | (uint32_t)(16 + k / 8) << 16 | (uint32_t)(32 + k / 4) << 8 | (uint32_t)(112 + k / 2);
        } else {
            int check = (((x + s_t / 2) >> 4) + ((line - 150) >> 4)) & 1;
            c = check ? 0xFF2050C0u : 0xFF1838A0u;
        }
        out[x] = c;
    }
}

static void draw_logo(int line, uint32_t *out, int width)
{
    int y0 = 16;
    if (s_logo) {
        draw_img(s_logo, line, out, width, (width - s_logo->w) / 2, y0);
        return;
    }
    static const char *const words[2] = { "SONIC", "ANTHOLOGY" };
    for (int k = 0; k < 2; k++) {
        int y = y0 + 24 + k * 14;
        if (line >= y && line < y + 8) tri_text(out, line - y, (width - tri_text_w(words[k])) / 2, words[k], k ? C_TITLE : C_SELECT, width);
    }
}

static void centred(int line, uint32_t *out, int width, int y, const char *s, int col)
{
    if (line >= y && line < y + 8) tri_text(out, line - y, (width - tri_text_w(s)) / 2, s, col, width);
}

/* word i of title_text.tga (gold: the choice) with its top-left at x, y */
static void draw_word(int line, uint32_t *out, int width, int i, int gold, int x, int y)
{
    int r = line - y;
    if (!s_text || r < 0 || r >= TEXT_H) return;
    const uint32_t *row = s_text->px + (size_t)((i * 2 + gold) * TEXT_H + r) * s_text->w;
    for (int k = 0; k < s_text_w[i]; k++)
        if (x + k >= 0 && x + k < width) out[x + k] = blend(out[x + k], row[k]);
}

/* the boxes' header strip ("Mega Drive Cartridge"), in the game's own words */
static void draw_header(int line, uint32_t *out, int width)
{
    if (line < 3 || line >= 11 || !s_bg) return;
    int cx = width / 2;
    tri_text(out, line - 3, cx - 154, "SONIC ANTHOLOGY", C_INK, width);
    tri_text(out, line - 3, cx + 154 - tri_text_w("1991 TO 1994"), "1991 TO 1994", C_INK, width);
}

/* the menu: a row along the band, the choice in gold, bobbing */
static void draw_menu(int line, uint32_t *out, int width, int y)
{
    if (s_text) {
        static const int word[N_ITEMS] = { TX_START, TX_COMPETITION, TX_OPTIONS };
        int gap = 20, total = -gap;
        for (int i = 0; i < N_ITEMS; i++) total += s_text_w[word[i]] + gap;
        int x = (width - total) / 2;
        for (int i = 0; i < N_ITEMS; i++) {
            int bob = i == s_sel && (s_t / 10) & 1;
            draw_word(line, out, width, word[i], i == s_sel, x, y - 5 - bob);
            x += s_text_w[word[i]] + gap;
        }
        return;
    }
    if (line < y || line >= y + 8) return;
    int gap = 24, total = -gap;
    for (int i = 0; i < N_ITEMS; i++) total += tri_text_w(k_items[i]) + gap;
    int x = (width - total) / 2;
    for (int i = 0; i < N_ITEMS; i++) {
        if (i == s_sel && (s_t / 8) % 4) tri_text(out, line - y, x - 11, "*", C_SELECT, width);
        tri_text(out, line - y, x, k_items[i], i == s_sel ? C_SELECT : C_NORMAL, width);
        x += tri_text_w(k_items[i]) + gap;
    }
}

static void put(uint32_t *out, int width, int x, uint32_t c) { if (x >= 0 && x < width) out[x] = c; }

/* Sonic 3's logo slab: black, a white keyline inset 3 px */
static void draw_slab(int line, uint32_t *out, int width, int x0, int x1, int y0, int y1)
{
    if (line < y0 || line >= y1) return;
    for (int x = x0; x < x1; x++) put(out, width, x, 0xFF000000u);
    if (line == y0 + 3 || line == y1 - 4)
        for (int x = x0 + 3; x < x1 - 3; x++) put(out, width, x, 0xFFFFFFFFu);
    else if (line > y0 + 3 && line < y1 - 4) {
        put(out, width, x0 + 3, 0xFFFFFFFFu);
        put(out, width, x1 - 4, 0xFFFFFFFFu);
    }
}

static void draw_options(int line, uint32_t *out, int width)
{
    if (s_menu_bg) draw_ground(s_menu_bg, line, out, width);
    else draw_bg(line, out, width);
    int n = tri_opt_count(), vis = 12, first = s_opt - vis / 2;
    if (first > n - vis) first = n - vis;
    if (first < 0) first = 0;
    int x0 = width / 2 - 150, x1 = width / 2 + 150;
    draw_slab(line, out, width, x0, x1, 6, 196);
    int tag = tri_text_w("OPTIONS") + 12;
    if (line >= 10 && line < 22)                      /* the heading's gold tag, its end slanted */
        for (int x = x0 + 8; x < x0 + 8 + tag + (21 - line) / 2; x++) put(out, width, x, 0xFFFFCE31u);
    if (line >= 12 && line < 20) tri_text(out, line - 12, x0 + 14, "OPTIONS", C_DARK, x1);
    for (int i = first, y = 30; i < first + vis && i < n; i++, y += 11) {
        if (line < y - 2 || line >= y + 9) continue;
        const char *v = tri_opt_value(i);
        int sel = i == s_opt, row = line - y;
        if (sel)                                      /* the slanted gold bar */
            for (int x = x0 + 10 + (y + 9 - line) / 3; x < x1 - 14 + (y + 9 - line) / 3; x++) put(out, width, x, 0xFFFFCE31u);
        if (row < 0 || row >= 8) continue;
        if (sel) tri_text(out, row, x0 + 6, "*", C_RED, x1);
        tri_text(out, row, x0 + 16, tri_opt_label(i), sel ? C_DARK : C_NORMAL, x1 - 16 - tri_text_w(v) - 8);
        tri_text(out, row, x1 - 16 - tri_text_w(v), v, sel ? C_RED : C_SELECT, x1);
    }
    if (line == 162)
        for (int x = x0 + 12; x < x1 - 12; x++) put(out, width, x, 0xFF3157AEu);
    char buf[64];
    int cols = (x1 - x0 - 24) / 8;
    for (int k = 0; k < 3 && tri_text_wrap(tri_opt_desc(s_opt), cols, k, buf); k++) {
        int y = 166 + k * 9;
        if (line >= y && line < y + 8) tri_text(out, line - y, x0 + 12, buf, C_NORMAL, x1);
    }
    centred(line, out, width, MENU_BAND + 9, "A CHANGE  B BACK", C_PALE);
}

/* ---- a taller screen -----------------------------------------------------
 * sonic3_video's extent adds rows above and below the 224 native lines (an
 * unfolded foldable, a tablet held upright). The title then lays itself out
 * on the whole canvas, as the Android setup screen does: the header at its
 * top, the band at its foot, the logo in the middle of the sky. title_bg's
 * flat shapes are drawn here at any height; the confetti (its open layer)
 * and the left band run the whole length. r: the canvas row. */
#define TC_RED    0xFFFF3131u
#define TC_GREEN  0xFF57AE57u
#define TC_YELLOW 0xFFFFFF57u
#define TC_BLUE   0xFF3157AEu
#define TC_BLACK  0xFF000000u
#define TC_WHITE  0xFFFFFFFFu

static void span(uint32_t *out, int width, int x0, int x1, uint32_t c)
{
    if (x0 < 0) x0 = 0;
    if (x1 > width) x1 = width;
    for (int x = x0; x < x1; x++) out[x] = c;
}

/* the ring echoing the sun (title_art.py: a 3 px blue outline in the box
 * x0, y0 .. +40, a 1 px black one a pixel down and right) */
static void tall_ring(int r, uint32_t *out, int width, int x0, int y0)
{
    for (int k = 0; k < 2; k++) {
        float cx = (float)(x0 + 20 + k), cy = (float)(y0 + 20 + k), dy = (float)r - cy, in = k ? 19.5f : 17.5f;
        if (dy < -21 || dy > 21) continue;
        for (int x = x0 + k; x <= x0 + 41; x++) {
            float dx = x - cx, d = sqrtf(dx * dx + dy * dy);
            if (d >= in && d <= 20.5f) put(out, width, x, k ? TC_BLACK : TC_BLUE);
        }
    }
}

static void tall_ground(int r, uint32_t *out, int width, int band, int sy)
{
    int cx = width / 2;
    if (r >= band) {                                  /* the band, two white blocks a side */
        span(out, width, 0, width, TC_BLUE);
        if (r >= band + 30 && r < band + 34)
            for (int k = 0; k < 2; k++) {
                int sx = k ? cx + 140 : cx - 152;
                span(out, width, sx, sx + 5, TC_WHITE);
                span(out, width, sx + 7, sx + 12, TC_WHITE);
            }
        return;
    }
    span(out, width, 0, width, TC_WHITE);
    span(out, width, cx + 186 - 110 * r / band, width, TC_RED);   /* (its edge: cx+186 at the top, cx+76 at the band) */
    if (r >= band - 80)                                         /* (green: -40 at band-80, cx-96 at the band) */
        span(out, width, 0, -40 + (cx - 56) * (r - (band - 80)) / 80 + 1, TC_GREEN);
    int dy = r - (82 + sy);
    if (dy >= -76 && dy <= 76) {                                /* the sun */
        int hw = (int)sqrtf((float)(76 * 76 - dy * dy));
        span(out, width, cx - 34 - hw, cx - 34 + hw + 1, TC_YELLOW);
    }
    tall_ring(r, out, width, cx + 132, 120 + sy);
    if (r >= band - 14 && r < band - 9)
        for (int i = 0; i < 7; i++) span(out, width, cx - 150 + i * 7, cx - 145 + i * 7, TC_BLACK);
    if (r >= band - 10 && r < band - 7) span(out, width, cx + 108, cx + 151, TC_BLACK);
    if (r == 13) span(out, width, cx - 154, cx + 154, TC_BLUE);  /* the header strip's rule */
}

/* the left band down the whole canvas: its black and teeth repeated (whole
 * teeth, in step), the word in the middle of the sky, the squiggle from
 * above the band to the foot */
static void tall_side(int r, uint32_t *out, int width, int band)
{
    if (!s_side) return;
    int shift = band - BAND_TOP, ws = shift / 2 / 10 * 10;
    int sr = r >= 16 + ws && r < BAND_TOP - 30 + ws ? r - ws : 6 + wrap(r - 6 - ws, 10);
    const uint32_t *row = s_side->px + (size_t)sr * s_side->w;
    for (int x = 0; x < s_side->w && x < width; x++) out[x] = blend(out[x], row[x]);
    if (r >= band - 30) {                             /* (title_art.py: x = 14 + 4 sin(row / 2), 2 px) */
        float a = 14 + 4 * sinf((r - 1 - shift) * 0.5f), b = 14 + 4 * sinf((r - shift) * 0.5f);
        int x0 = (int)floorf(a < b ? a : b), x1 = (int)ceilf(a < b ? b : a) + 1;
        for (int x = x0; x <= x1; x++) put(out, width, x, TC_YELLOW);
    }
}

static void title_tall(int r, uint32_t *out, int width, int band)
{
    int sy = (band - BAND_TOP) / 2;
    tall_ground(r, out, width, band, sy);
    const Img *conf = s_conf_open ? s_conf_open : s_conf;
    if (conf && r >= CONF_Y0 && r < band) {           /* rising, wrapped */
        const uint32_t *row = conf->px + (size_t)wrap(r - CONF_Y0 + s_t / 3, conf->h) * conf->w;
        int x0 = (width - conf->w) / 2;
        for (int x = x0 < 0 ? 0 : x0; x < width && x - x0 < conf->w; x++)
            if (row[x - x0] >> 24) out[x] = blend(out[x], row[x - x0]);
    }
    int dy = band - r;                                /* the band's teeth */
    if (dy >= 1 && dy <= 5)
        for (int x = 0, x0 = (width - 640) / 2; x < width; x++) {
            int k = wrap(x - x0 - s_t / 2, 10);
            if (k >= dy && k <= 10 - dy) out[x] = TC_BLUE;
        }
    if (width >= 384) {
        tall_side(r, out, width, band);
        if (r < band - 6) {                           /* green teeth down the right */
            int y = wrap(r - s_t / 2, 14);
            int depth = 13 - (y < 7 ? 7 - y : y - 7) * 13 / 7;
            for (int x = width - depth; x < width; x++) out[x] = TC_GREEN;
        }
    }
    draw_header(r, out, width);
    if (s_logo) draw_img(s_logo, r, out, width, (width - s_logo->w) / 2, 16 + sy);
    else draw_logo(r - sy, out, width);
    if (s_state == ST_PRESS) {
        if ((s_t / 30) & 1 || s_t < 30) {
            if (s_text) draw_word(r, out, width, TX_PRESS, 0, (width - s_text_w[TX_PRESS]) / 2, band + 5);
            else centred(r, out, width, band + 10, "PRESS START BUTTON", C_NORMAL);
        }
    } else
        draw_menu(r, out, width, band + 10);
    centred(r, out, width, band + 26, "@ SEGA", C_PALE);
}

/* Called for every output row (trilogy_menu.c's frame overlay). */
void tri_title_frame(int line, uint32_t *out, int width)
{
    extern int s3_video_interpolating(void);
    if (line == 0 && !s3_video_interpolating() && s_on && s_started) s_t++;
    if (!s_on || !s_started || !title_mode()) return;
    int top, bottom;
    s3_video_canvas_rows(&top, &bottom);
    if (s_state == ST_OPTIONS) draw_options(line, out, width);
    else if ((top || bottom) && s_bg) title_tall(line + top, out, width, 224 + top + bottom - (224 - BAND_TOP));
    else {
        draw_bg(line, out, width);
        draw_header(line, out, width);
        draw_logo(line, out, width);
        if (s_state == ST_PRESS) {
            if ((s_t / 30) & 1 || s_t < 30) {
                if (s_text) draw_word(line, out, width, TX_PRESS, 0, (width - s_text_w[TX_PRESS]) / 2, BAND_TOP + 5);
                else centred(line, out, width, BAND_TOP + 10, "PRESS START BUTTON", C_NORMAL);
            }
        } else
            draw_menu(line, out, width, BAND_TOP + 10);
        centred(line, out, width, BAND_TOP + 26, "@ SEGA", C_PALE);
    }
    /* fade in from black, and out when leaving */
    int k = s_t < 20 ? s_t * 256 / 20 : 256;
    if (s_state == ST_LEAVE) k = (16 - s_leave) * 256 / 16;
    if (k < 256) for (int x = 0; x < width; x++) out[x] = shade(out[x], k < 0 ? 0 : k);
}

/* ---- the Data Select's ground -------------------------------------------
 * Behind the save cards, in place of the cartridge's portrait (the renderer
 * keeps the cards and sprites): a slowly drifting pattern of the boxes'
 * shapes (ds_tile.tga) and the three players, drawn from the cartridge's own
 * frames in grey at low strength, over the title band (ds_band.tga). It
 * follows the cartridge's fades: the live palette against the fade's target. */
#define DS_BAND_Y 194
#define DS_STRENGTH 96                                /* /256, the sprites' ink */
/* Map, DPLC, art (+ the frames from which the extra art bank serves), palette.
 * Map_Knuckles directly follows PLC_Tails in the S&K listing. */
static const struct { uint32_t map, plc, art, art_x; unsigned extra; uint32_t pal; } k_players[3] = {
    { S3K_Map_Sonic, S3K_PLC_Sonic, S3K_ArtUnc_Sonic, S3K_ArtUnc_Sonic_Extra, 0xDA, S3K_Pal_SonicTails },
    { S3K_Map_Tails, S3K_PLC_Tails, S3K_ArtUnc_Tails, S3K_ArtUnc_Tails_Extra, 0xD1, S3K_Pal_SonicTails },
    { 0x14A8D6u, 0x14BD0Au, 0x1200E0u, 0x1200E0u, 0x100, S3K_Pal_Knuckles },
};
/* the tile's sprite cells (tools/trilogy/title_art.py's ds_cells): centre,
 * player, frame */
static const struct { int x, y, ch, frame; } k_ds_cells[8] = {
    { 32, 26, 0, 0xBA }, { 160, 26, 1, 0xAD }, { 288, 26, 2, 0x56 }, { 416, 26, 0, 0x21 },
    { 128, 78, 1, 0xA0 }, { 256, 78, 2, 0xC0 }, { 384, 78, 0, 0xA4 }, { 0, 78, 1, 0x21 },
};

/* one frame of a player, mapped about (cx, cy), in grey over the tile */
static void ds_stamp(Img *t, int ch, unsigned frame, int cx, int cy)
{
    uint32_t art = frame < k_players[ch].extra ? k_players[ch].art : k_players[ch].art_x;
    uint32_t p = k_players[ch].plc + (uint32_t)(int16_t)tri_rd16(k_players[ch].plc + frame * 2u);
    uint32_t src[64];                                 /* the frame's tiles, in DPLC order */
    int tiles = 0;
    for (int i = 0, n = tri_rd16(p); i < n && i < 32; i++) {
        uint16_t e = tri_rd16(p + 2u + (uint32_t)i * 2u);
        for (unsigned k = 0; k <= (unsigned)(e >> 12) && tiles < 64; k++)
            src[tiles++] = art + ((e & 0xFFFu) + k) * 32u;
    }
    uint8_t grey[16];
    for (int i = 0; i < 16; i++) {
        uint16_t c = tri_rd16(k_players[ch].pal + (uint32_t)i * 2u);
        int r = (c >> 1 & 7) * 36, g = (c >> 5 & 7) * 36, b = (c >> 9 & 7) * 36;
        grey[i] = (uint8_t)(40 + ((r * 77 + g * 150 + b * 29) >> 8) * 5 / 8);   /* (white stays off the paper) */
    }
    uint32_t m = k_players[ch].map + (uint32_t)(int16_t)tri_rd16(k_players[ch].map + frame * 2u);
    for (int i = 0, n = tri_rd16(m); i < n && i < 32; i++) {
        uint32_t q = m + 2u + (uint32_t)i * 6u;
        int y = (int8_t)tri_rd8(q), size = tri_rd8(q + 1), x = (int16_t)tri_rd16(q + 4);
        uint16_t attr = tri_rd16(q + 2);
        int cw = (size >> 2 & 3) + 1, chh = (size & 3) + 1;
        for (int tx = 0; tx < cw; tx++)
            for (int ty = 0; ty < chh; ty++) {
                int tile = (attr & 0x7FF) + tx * chh + ty;
                if (tile >= tiles) continue;
                for (int py = 0; py < 8; py++)
                    for (int px = 0; px < 8; px++) {
                        uint8_t b = tri_rd8(src[tile] + (uint32_t)(py * 4 + px / 2));
                        int nib = px & 1 ? b & 15 : b >> 4;
                        if (!nib) continue;
                        int X = attr & 0x800 ? (cw - 1 - tx) * 8 + 7 - px : tx * 8 + px;
                        int Y = attr & 0x1000 ? (chh - 1 - ty) * 8 + 7 - py : ty * 8 + py;
                        X = ((cx + x + X) % t->w + t->w) % t->w;
                        Y = ((cy + y + Y) % t->h + t->h) % t->h;
                        uint32_t *d = &t->px[(size_t)Y * t->w + X];
                        int ink = 255 - ((255 - grey[nib]) * DS_STRENGTH >> 8);
                        uint32_t c = *d;                  /* (multiplied over the paper) */
                        uint32_t r = (c >> 16 & 255) * ink / 255, g = (c >> 8 & 255) * ink / 255, bl = (c & 255) * ink / 255;
                        *d = 0xFF000000u | r << 16 | g << 8 | bl;
                    }
            }
    }
}

static int fade_level(void)                           /* 256 = the full picture */
{
    /* the median colour's brightness against its fade target: the cards'
     * blinking selection frame and emeralds cycle a few entries, which a
     * plain sum would read as a fade */
    int ratio[64], n = 0;
    for (uint32_t a = 0; a < 0x80; a += 2) {
        uint16_t c = r16(RAM_Normal_palette + a), t = r16(RAM_Target_palette + a);
        int now = (c >> 1 & 7) + (c >> 5 & 7) + (c >> 9 & 7);
        int target = (t >> 1 & 7) + (t >> 5 & 7) + (t >> 9 & 7);
        if (target) ratio[n++] = (now * 256 + target / 2) / target;
    }
    if (!n) return 256;
    for (int i = 1; i < n; i++)                       /* (insertion sort: 64 at most) */
        for (int j = i; j > 0 && ratio[j - 1] > ratio[j]; j--) { int s = ratio[j]; ratio[j] = ratio[j - 1]; ratio[j - 1] = s; }
    return ratio[n / 2];
}

static int ds_active(void)
{
    load_art();
    return s_ds_tile && !getenv("TRILOGY_CLASSIC_DATA_SELECT");
}

static const uint32_t *ds_ground(int line, int width)
{
    static uint32_t *row;
    static int row_w, k, stamped;
    static unsigned t;
    if (!ds_active()) return NULL;
    if (!stamped) {
        stamped = 1;
        for (int i = 0; i < 8; i++)
            ds_stamp(s_ds_tile, k_ds_cells[i].ch, (unsigned)k_ds_cells[i].frame, k_ds_cells[i].x, k_ds_cells[i].y);
    }
    if (width > row_w) {
        uint32_t *n = (uint32_t *)realloc(row, (size_t)width * sizeof *row);
        if (!n) return NULL;
        row = n;
        row_w = width;
    }
    if (line == 0) {
        k = fade_level();
        if (!s3_video_interpolating()) t++;
    }
    /* the pattern drifts up and to the left, a pixel every 4 frames */
    const Img *ti = s_ds_tile;
    const uint32_t *src = ti->px + (size_t)wrap(line + (int)(t / 4), ti->h) * ti->w;
    int ox = (int)(t / 4) % ti->w - (width - 320) / 2;
    for (int x = 0; x < width; x++) row[x] = src[((x + ox) % ti->w + ti->w) % ti->w];
    int top, bottom;
    s3_video_canvas_rows(&top, &bottom);              /* (a taller screen: at its foot) */
    if (s_ds_band) draw_img(s_ds_band, line, row, width, (width - s_ds_band->w) / 2, DS_BAND_Y + bottom);
    if (k < 256)
        for (int x = 0; x < width; x++) row[x] = shade(row[x], k < 0 ? 0 : k);
    else if (k > 256)                                 /* (a fade from white) */
        for (int x = 0; x < width; x++) {
            uint32_t c = row[x], w = (uint32_t)(k - 256 > 256 ? 256 : k - 256);
            uint32_t r = (c >> 16 & 255) + ((255 - (c >> 16 & 255)) * w >> 8);
            uint32_t g = (c >> 8 & 255) + ((255 - (c >> 8 & 255)) * w >> 8);
            uint32_t b = (c & 255) + ((255 - (c & 255)) * w >> 8);
            row[x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    return row;
}

/* ---- a new file: the experience picker ----------------------------------
 * Starting an empty file (or No Save) opens it over the Data Select, which
 * carries on beneath (trilogy_save.c reverts the cartridge's start): first
 * the game - the whole Anthology or one of its four - then Original or Easy.
 * Its input is taken before the Data Select's objects see the pads. */
static const char *const k_exp_desc[TRI_EXP_COUNT] = {
    "EVERY GAME IN STORY ORDER FROM SONIC 1 TO SONIC 3 AND KNUCKLES.",
    "SONIC THE HEDGEHOG ON ITS OWN.",
    "SONIC CD ON ITS OWN.",
    "SONIC THE HEDGEHOG 2 ON ITS OWN.",
    "SONIC 3 AND KNUCKLES ON ITS OWN.",
};
static const char *const k_mode_name[2] = { "ORIGINAL", "EASY" };
static const char *const k_mode_desc[2] = {
    "EVERY ACT AS THE GAMES WERE MADE.",
    "ONE ACT PER ZONE WITH ITS BOSS. BOSSES TAKE THREE HITS.",
};
/* the window, about the screen's centre; the carousel's logos SPACING apart */
#define PW 144                                         /* (half its width) */
#define PY0 30
#define PY1 194
#define SPACING 232
#define PAPER 0xFFF6F2E6u
static int s_pick_step, s_pick_ti, s_pick_pos, s_pick_easy, s_pick_t;   /* (s_pick_pos: 1/16 px) */
static uint32_t s_pick_slot;

void tri_save_start(uint32_t a1, int exp, int easy);
int tri_scd_available(void);

/* the games on offer, in order: the Anthology, then story order (Sonic CD
 * only with its disc); i wraps */
static int pick_count(void) { return tri_scd_available() ? TRI_EXP_COUNT : TRI_EXP_COUNT - 1; }
static int pick_exp(int i)
{
    static const int order[TRI_EXP_COUNT] = { TRI_EXP_ALL, TRI_EXP_S1, TRI_EXP_CD, TRI_EXP_S2, TRI_EXP_S3K };
    int n = pick_count();
    i = (i % n + n) % n;
    if (!tri_scd_available() && i >= 2) i++;
    return order[i];
}

void tri_picker_open(uint32_t slot_data)
{
    load_art();
    s_pick_on = 1;
    s_pick_step = 0;
    s_pick_ti = s_pick_pos = 0;
    s_pick_easy = 0;
    s_pick_t = 0;
    s_pick_slot = slot_data;
    fprintf(stderr, "[trilogy] new file: experience picker (slot $%06X)\n", (unsigned)slot_data);
}

static void picker_input(void)
{
    uint8_t press = r8(RAM_Ctrl_1_pressed);
    w8(RAM_Ctrl_1_pressed, 0);                         /* (the Data Select sees no presses; the held
                                                        *  buttons stay, the pad's edges come from them) */
    s_pick_t++;
    int target = s_pick_ti * SPACING * 16;             /* (the carousel slides on) */
    s_pick_pos += (target - s_pick_pos) / 4;
    if (abs(target - s_pick_pos) < 16) s_pick_pos = target;
    if (s_pick_t < 8) return;                          /* (opening) */
    if (s_pick_step == 0) {
        int d = press & BTN_LEFT ? -1 : press & BTN_RIGHT ? 1 : 0;
        if (d) { s_pick_ti += d; o_sfx(SFX_SWITCH); }
        if (press & BTN_B) { s_pick_on = 0; o_sfx(SFX_SWITCH); }
        else if (press & (BTN_START | BTN_AC)) { s_pick_step = 1; o_sfx(SFX_RING); }
    } else {
        if (press & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) { s_pick_easy ^= 1; o_sfx(SFX_SWITCH); }
        if (press & BTN_B) { s_pick_step = 0; o_sfx(SFX_SWITCH); }
        else if (press & (BTN_START | BTN_AC)) {
            s_pick_on = 0;
            o_sfx(SFX_RING);
            tri_save_start(s_pick_slot, pick_exp(s_pick_ti), s_pick_easy);
        }
    }
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, int k)    /* a toward b by k/256 */
{
    uint32_t r = ((a >> 16 & 255) * (256 - k) + (b >> 16 & 255) * k) >> 8;
    uint32_t g = ((a >> 8 & 255) * (256 - k) + (b >> 8 & 255) * k) >> 8;
    uint32_t bl = ((a & 255) * (256 - k) + (b & 255) * k) >> 8;
    return 0xFF000000u | r << 16 | g << 8 | bl;
}

/* an image blended in between columns c0..c1, faded toward the paper by k/256 */
static void draw_clip(const Img *img, int line, uint32_t *out, int width, int x0, int y0, int c0, int c1, int k)
{
    int y = line - y0;
    if (!img || y < 0 || y >= img->h) return;
    const uint32_t *row = img->px + (size_t)y * img->w;
    for (int x = 0; x < img->w; x++) {
        int X = x0 + x;
        if (X < c0 || X >= c1 || X < 0 || X >= width || !(row[x] >> 24)) continue;
        uint32_t c = blend(out[X], row[x]);
        out[X] = k ? mix_rgb(c, PAPER, k) : c;
    }
}

/* text centred across x0..x1, wrapped to n lines from y */
static void wrapped(int line, uint32_t *out, int width, int x0, int x1, int y, const char *s, int col, int n)
{
    char buf[64];
    int cols = (x1 - x0) / 8;
    for (int k = 0; k < n && tri_text_wrap(s, cols, k, buf); k++)
        if (line >= y + k * 10 && line < y + k * 10 + 8)
            tri_text(out, line - y - k * 10, (x0 + x1 - tri_text_w(buf)) / 2, buf, col, width);
}

/* A window over the dimmed Data Select: the games' logos in a carousel (the
 * neighbours peeping in at its sides), dots for the place, the choice's
 * line; once a game is chosen, Original or Easy. */
static void picker_draw(int line, uint32_t *out, int width)
{
    for (int x = 0; x < width; x++) out[x] = shade(out[x], 112);   /* (the Data Select, dimmed) */
    int cx = width / 2, x0 = cx - PW, x1 = cx + PW;
    int open = s_pick_t < 8 ? s_pick_t * 256 / 8 : 256, mid = (PY0 + PY1) / 2;
    int y0 = mid - (mid - PY0) * open / 256, y1 = mid + (PY1 - mid) * open / 256;
    if (line < y0 || line >= y1) return;
    /* the frame: navy, a gold keyline, the paper */
    int edge = line < y0 + 3 || line >= y1 - 3;
    for (int x = x0; x < x1; x++) {
        uint32_t c = PAPER;
        if (edge || x < x0 + 3 || x >= x1 - 3) c = 0xFF102060u;
        else if (line == y0 + 4 || line == y1 - 5 || x == x0 + 4 || x == x1 - 5) c = 0xFFFFCE31u;
        put(out, width, x, c);
    }
    if (open < 256) return;
    int tag = tri_text_w("NEW FILE") + 12;             /* (the heading's gold tag, its end slanted) */
    if (line >= PY0 + 8 && line < PY0 + 20)
        for (int x = x0 + 10; x < x0 + 10 + tag + (PY0 + 19 - line) / 2; x++) put(out, width, x, 0xFFFFCE31u);
    if (line >= PY0 + 10 && line < PY0 + 18) tri_text(out, line - PY0 - 10, x0 + 16, "NEW FILE", C_DARK, x1);
    /* the carousel */
    int n = pick_count(), sel = s_pick_ti, exp = pick_exp(sel), cy = 86;   /* (between the tag and the dots) */
    int pos = s_pick_pos / 16;
    int first = (pos >= 0 ? pos / SPACING : -((-pos + SPACING - 1) / SPACING)) - 1;
    for (int k = first; k <= first + 3; k++) {
        int xc = cx + k * SPACING - pos;
        const Img *g = s_game[pick_exp(k)];
        int fade = k == sel ? 0 : s_pick_step ? 224 : 150;
        if (g) draw_clip(g, line, out, width, xc - g->w / 2, cy - g->h / 2, x0 + 5, x1 - 5, fade);
        else if (k == sel && line >= cy - 4 && line < cy + 4) {
            static const char *const names[TRI_EXP_COUNT] = { "SONIC ANTHOLOGY", "SONIC 1", "SONIC CD", "SONIC 2", "SONIC 3 AND KNUCKLES" };
            tri_text(out, line - cy + 4, xc - tri_text_w(names[exp]) / 2, names[exp], C_INK, x1);
        }
    }
    if (s_pick_step == 0 && line >= cy - 6 && line < cy + 7) {  /* (red arrows at the sides, nudging) */
        int r = line - cy, w = 6 - (r < 0 ? -r : r), nudge = (s_pick_t / 8) & 1;
        for (int k = 0; k < w; k++) {
            put(out, width, x0 + 10 - nudge + k, 0xFFE00000u);
            put(out, width, x1 - 11 + nudge - k, 0xFFE00000u);
        }
    }
    if (line >= 124 && line < 127)                    /* (the dots: which of them) */
        for (int i = 0; i < n; i++) {
            int dx = cx + (i * 2 - (n - 1)) * 5;
            uint32_t c = ((sel % n + n) % n) == i ? 0xFFE00000u : 0xFFB0B0C0u;
            for (int x = dx - 1; x <= dx + 1; x++) put(out, width, x, c);
        }
    wrapped(line, out, width, x0 + 12, x1 - 12, 134, s_pick_step ? k_mode_desc[s_pick_easy] : k_exp_desc[exp], C_INK, 2);
    if (line == 157)
        for (int x = x0 + 14; x < x1 - 14; x++) put(out, width, x, 0xFF3157AEu);
    /* Original / Easy: a gold bar under the choice once the game is chosen */
    int my = 164;
    for (int m = 0; m < 2; m++) {
        int w = tri_text_w(k_mode_name[m]), mx = cx + (m ? 30 : -30 - w);
        int chosen = m == s_pick_easy;
        if (s_pick_step == 1 && chosen && line >= my - 3 && line < my + 11)
            for (int x = mx - 6 + (my + 11 - line) / 3; x < mx + w + 6 + (my + 11 - line) / 3; x++) put(out, width, x, 0xFFFFCE31u);
        if (line >= my && line < my + 8)
            tri_text(out, line - my, mx, k_mode_name[m], s_pick_step == 1 ? (chosen ? C_RED : C_INK) : C_DIM, width);
    }
    const char *hint = s_pick_step ? "A START  B BACK" : "A CHOOSE  B BACK";
    if (line >= 179 && line < 187) tri_text(out, line - 179, cx - tri_text_w(hint) / 2, hint, C_DIM, x1);
}

/* The Data Select's file cards: each started file's experience as a badge
 * on its card (and EASY), over the screen the cartridge draws. The cards sit
 * 104 px apart from x 144, scrolled by the camera ($FFEE80). */
#define RAM_DS_camera_x 0xFFEE80u
#define RAM_Saved_data_ 0xFFE6ACu
#define BADGE_Y         10                            /* (the badges' centre row: astride the card's top) */

/* an image blended in, darkened with the screen's fade (k/256) */
static void draw_faded(const Img *img, int line, uint32_t *out, int width, int x0, int y0, int k)
{
    int y = line - y0;
    if (!img || y < 0 || y >= img->h) return;
    const uint32_t *row = img->px + (size_t)y * img->w;
    for (int x = 0; x < img->w; x++) {
        if (x0 + x < 0 || x0 + x >= width) continue;
        uint32_t c = row[x];
        if (k < 256) c = (c & 0xFF000000u) | (shade(c, k < 0 ? 0 : k) & 0xFFFFFFu);
        out[x0 + x] = blend(out[x0 + x], c);
    }
}

static void ds_badges(int line, uint32_t *out, int width)
{
    static uint8_t b0[8], b1[8];
    static int camx;
    if (line == 0) {
        camx = (int16_t)r16(RAM_DS_camera_x);
        for (int i = 0; i < 8; i++) {
            b0[i] = r8(RAM_Saved_data_ + (uint32_t)i * 10u);
            b1[i] = r8(RAM_Saved_data_ + (uint32_t)i * 10u + 1);
        }
    }
    int k = fade_level();
    {   /* which page of files, on the title band's right (trilogy_save.c) */
        extern int tri_save_page(void), tri_save_pages(void);
        int top, bottom, y = DS_BAND_Y + 9;
        s3_video_canvas_rows(&top, &bottom);
        y += bottom;
        if (line >= y && line < y + 8 && k > 128) {   /* (right of the band's lettering; 4:3: no total) */
            char s[24];
            if (width >= 380) snprintf(s, sizeof s, "PAGE %d OF %d", tri_save_page() + 1, tri_save_pages());
            else snprintf(s, sizeof s, "PAGE %d", tri_save_page() + 1);
            tri_text(out, line - y, width / 2 + 98, s, C_SELECT, width);
        }
    }
    if (line < BADGE_Y - 16 || line >= BADGE_Y + 16) return;
    for (int i = 0; i < 8; i++) {
        if ((b0[i] & 0x80) || !(b1[i] & 0x80) || (b1[i] & 7) >= TRI_EXP_COUNT) continue;
        int cx = 144 + 104 * i - camx + (width - 320) / 2;
        if (cx < -80 || cx > width + 80) continue;
        const Img *b = s_badge[b1[i] & 7];
        if (b) draw_faded(b, line, out, width, cx - 44, BADGE_Y - b->h / 2, k);
        if ((b1[i] & 0x40) && s_badge_easy)
            draw_faded(s_badge_easy, line, out, width, cx + 44 - s_badge_easy->w, BADGE_Y - s_badge_easy->h / 2, k);
    }
}

/* the frame overlay's Data Select part (trilogy_menu.c): the badges, the picker */
void tri_picker_frame(int line, uint32_t *out, int width)
{
    if ((r8(RAM_Game_mode) & 0x7F) != 0x4C) { s_pick_on = 0; return; }
    load_art();
    if (s_pick_on) picker_draw(line, out, width);
    else ds_badges(line, out, width);
}

void tri_title_init(void)
{
    s3_video_set_file_select_ground(ds_ground);
}
