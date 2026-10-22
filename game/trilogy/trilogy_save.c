/*
 * trilogy_save.c — save files across the whole Anthology.
 *
 * A save slot (S3K's 10-byte format, Saved_data) keeps its level code in
 * byte 3. The Anthology numbers every zone in story order: Sonic 1's seven
 * (0-6), Sonic CD's seven rounds (7-13), Sonic 2's eleven (14-24), then
 * Sonic 3 & Knuckles' fourteen as S3K numbers them, offset by 25 (25-38); 39
 * is a finished game. S3K's own SaveGame is redone here with that offset;
 * the other games' zones save when a zone is cleared (trilogy_flow.c), and
 * the Data Select reads the codes back through its hooks: the zones a
 * cleared file may pick, the level a code starts, the live thumbnail of the
 * zone (trilogy_thumbs.c).
 *
 * Byte 1, which S3K leaves zero, is the file's experience (trilogy.h): the
 * whole Anthology or one game, Original or Easy, chosen when a new file
 * starts (trilogy_title.c's picker). Bit 7 marks the format: files from
 * before Sonic CD had codes (S2 7-17, S3K 18-31, 32 finished) are moved up
 * once, as the Data Select opens, and become Anthology files.
 */
#include "trilogy_obj.h"
#include "genesis_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_Save_pointer                0xFFE660u
#define RAM_Saved_data                  0xFFE6ACu
#define RAM_SK_alone_flag               0xFFFFAEu
#define RAM_Continue_count              0xFFFE18u
#define RAM_SRAM_mask_interrupts_flag   0xFFEF56u
#define RAM_Ctrl_1_pressed              0xFFF605u

#define CODE_CD         7
#define CODE_S2         14
#define CODE_S3K        25
#define CODE_CLEAR      39
#define OLD_CODE_S2     7              /* (before Sonic CD had codes) */

#define FILE_MARK       0x80           /* byte 1: the Anthology's format */
#define FILE_EASY       0x40
#define FILE_EXP        0x07

#define SLOT_SIZE       10
#define SLOTS           8
#define PAGES           8              /* (of eight files: the Data Select's pages) */

static const uint16_t k_s1_zones[7] = { 0x2000, 0x2200, 0x2400, 0x2100, 0x2300, 0x2500, 0x2502 };   /* GHZ MZ SYZ LZ SLZ SBZ FZ */
static const uint16_t k_s3k_zones[14] = { 0x000, 0x100, 0x200, 0x300, 0x500, 0x600, 0x700, 0x400, 0x800, 0x900,
                                          0x1601, 0xA00, 0xB00, 0xC00 };                         /* LevelList_DA6E */
/* SaveGame_NextLevel (zone*2 + act) */
static const uint8_t k_next_level[24][2] = {
    {1,1},{2,2},{3,3},{4,4},{8,8},{5,5},{6,6},{7,7},{9,9},{0xA,0xA},{0xC,0xC},{0xD,0xD},{0xE,0xE},
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0xA,0xB},{0xD,0} };

/* each experience's codes: from the first, to the finished card (one past) */
static const uint8_t k_lo[TRI_EXP_COUNT] = { 0, 0, CODE_CD, CODE_S2, CODE_S3K };
static const uint8_t k_hi[TRI_EXP_COUNT] = { CODE_CLEAR, CODE_CD, CODE_S2, CODE_S3K, CODE_CLEAR };

uint16_t tri_code_to_za(int code)
{
    if (code < 0) return 0x2000;
    if (code < CODE_CD) return k_s1_zones[code];
    if (code < CODE_S2) return (uint16_t)(TRI_CD_ZONE(code - CODE_CD, TRI_CD_PRESENT) << 8);
    if (code < CODE_S3K) return (uint16_t)((0x28 + code - CODE_S2) << 8);
    if (code < CODE_CLEAR) return k_s3k_zones[code - CODE_S3K];
    return 0x2000;
}

/* the code of the zone an (internal) zone/act belongs to */
int tri_za_to_code(uint16_t za)
{
    extern uint16_t tri_apparent(uint16_t za);
    uint16_t ap = tri_apparent(za);
    int z = ap >> 8;
    static const int8_t s1[7] = { 0, 3, 1, 4, 2, 5, 6 };                /* $20 GHZ, $21 LZ, $22 MZ ... $26 FZ */
    if (z >= 0x20 && z <= 0x26) return s1[z - 0x20];
    if (tri_cd_zone(z)) return CODE_CD + (z - TRI_CD_BASE) / 4;
    if (z >= 0x28 && z <= 0x32) return CODE_S2 + z - 0x28;
    for (int i = 0; i < 14; i++) if ((k_s3k_zones[i] >> 8) == z) return CODE_S3K + i;
    return 0;
}

/* The preview recorded for a code (trilogy_thumbs.c): trilogy_previews.bin
 * keeps the older numbering, Sonic 1, Sonic 2, S3K (0-31), with Sonic CD's
 * rounds after them (32-38). */
int tri_code_preview(int code)
{
    if (code < CODE_CD) return code;
    if (code < CODE_S2) return 32 + code - CODE_CD;
    return code - CODE_S2 + OLD_CODE_S2;
}

/* ---- the file's experience --------------------------------------------- */
static uint8_t s_nosave = FILE_MARK;                 /* No Save play: the picker's choice */

static uint32_t slot(void) { return r32(RAM_Save_pointer) & 0xFFFFFFu; }

static uint8_t file_byte(void)
{
    static int dev = -1;                             /* development: TRILOGY_EXP=s1|cd|s2|3k|all, TRILOGY_EASY=1 */
    if (dev < 0) {
        const char *e = getenv("TRILOGY_EXP"), *ez = getenv("TRILOGY_EASY");
        static const char *const names[TRI_EXP_COUNT] = { "all", "s1", "cd", "s2", "3k" };
        dev = 0;
        for (int i = 0; e && i < TRI_EXP_COUNT; i++) if (!strcmp(e, names[i])) dev = FILE_MARK | i;
        if (ez && *ez == '1') dev |= FILE_MARK | FILE_EASY;
    }
    if (dev) return (uint8_t)dev;
    uint32_t a1 = slot();
    return a1 ? r8(a1 + 1) : s_nosave;
}

static int exp_of(uint8_t b)
{
    int e = b & FILE_EXP;
    return (b & FILE_MARK) && e < TRI_EXP_COUNT ? e : TRI_EXP_ALL;
}
int tri_experience(void) { return exp_of(file_byte()); }
int tri_easy(void) { return (file_byte() & (FILE_MARK | FILE_EASY)) == (FILE_MARK | FILE_EASY); }

/* Easy: each zone's boss act - Sonic 1's act 3 (Scrap Brain, which has no
 * boss of its own, act 1, then the Final Zone), Sonic 2's act 2 (Metropolis
 * 3), Sonic CD's act 3 in the good future, S3K's act 2 (one-act zones and
 * the boss arenas as they are) */
static uint16_t easy_act(uint16_t za)
{
    int z = za >> 8, act = za & 0xFF;
    if (z >= 0x20 && z <= 0x24 && za != 0x2103) return (uint16_t)(z << 8 | 2);
    if (z >= 0x28 && z <= 0x2E) return (uint16_t)(z << 8 | 1);
    if (z == 0x2F) return 0x2F02;
    if (tri_cd_zone(z)) return (uint16_t)(TRI_CD_ZONE((z - TRI_CD_BASE) / 4, TRI_CD_GOOD) << 8 | 2);
    if ((z <= 9 || z == 0xB) && !act) return (uint16_t)(za | 1);
    return za;
}
uint16_t tri_easy_act(uint16_t za) { return tri_easy() ? easy_act(za) : za; }

uint16_t tri_experience_start(int exp, int easy)
{
    static const uint16_t first[TRI_EXP_COUNT] = { 0x2000, 0x2000, 0x4000, 0x2800, 0x0000 };
    uint16_t za = first[exp < TRI_EXP_COUNT ? exp : 0];
    return easy ? easy_act(za) : za;
}

/* the zone number Data Select shows: the code's place in its file's list */
static int code_ok(int exp, int code)
{
    if (code < k_lo[exp] || code > k_hi[exp]) return 0;
    return tri_scd_available() || code < CODE_CD || code >= CODE_S2;
}
static int zone_number(uint8_t b1, int code)
{
    int exp = exp_of(b1), n = 0;
    for (int c = k_lo[exp]; c <= code && c <= k_hi[exp]; c++) n += code_ok(exp, c);
    return n ? n : 1;
}

static void write_save(void)
{
    w8(RAM_SRAM_mask_interrupts_flag, 0xFF);
    tri_jsr(S3K_Write_SaveGame);
}

/* Files from before Sonic CD had codes: S2 and S3K codes up by seven, an
 * Anthology file (once; bit 7 marks it done) */
static void migrate(void)
{
    int changed = 0;
    for (int i = 0; i < SLOTS; i++) {
        uint32_t a1 = RAM_Saved_data + (uint32_t)i * SLOT_SIZE;
        if ((r8(a1) & 0x80) || (r8(a1 + 1) & FILE_MARK)) continue;
        uint8_t code = r8(a1 + 3);
        if (code >= OLD_CODE_S2) w8(a1 + 3, (uint8_t)(code + CODE_CD));
        w8(a1 + 1, FILE_MARK | TRI_EXP_ALL);
        changed = 1;
        fprintf(stderr, "[trilogy] save file %d: level code %u -> %u (Anthology numbering)\n", i + 1, code, r8(a1 + 3));
    }
    if (changed) write_save();
}

/* The pause menu's zone select is earned by a finished Anthology file, on
 * any page of files (the one in Saved_data, the others from their SRAM). */
static int s_page;
static uint8_t page_byte(int p, int i, int k);
static int page_written(int p);
int tri_save_anthology_clear(void)
{
    for (int p = 0; p < PAGES; p++)
        for (int i = 0; i < SLOTS; i++) {
            uint8_t b0, b1;
            if (p == s_page) {
                uint32_t a1 = RAM_Saved_data + (uint32_t)i * SLOT_SIZE;
                b0 = r8(a1); b1 = r8(a1 + 1);
            } else {
                if (!page_written(p)) break;
                b0 = page_byte(p, i, 0); b1 = page_byte(p, i, 1);
            }
            if (!(b0 & 0x80) && (b0 & 3) && exp_of(b1) == TRI_EXP_ALL) return 1;
        }
    return 0;
}

static void save_lives(uint32_t a1)                 /* loc_C4B4 */
{
    uint8_t d0 = r8(RAM_Life_count);
    if (d0 > 99) d0 = 99;
    w8(a1 + 8, d0);
    w8(RAM_Life_count, d0);
    write_save();
}

/* emeralds + continues (SaveGame_SpecialStage), after a stage the trilogy ran itself */
void tri_save_emeralds(void)
{
    if (r16(RAM_SK_alone_flag)) return;
    uint32_t a1 = slot();
    if (!a1) return;
    uint16_t d0 = 0;
    for (int i = 0; i < 7; i++) {
        d0 |= r8(RAM_Collected_emeralds_array + (uint32_t)i) & 3;
        d0 = (uint16_t)(d0 << 2);
    }
    w16(a1 + 6, d0);
    uint8_t c = r8(RAM_Continue_count);
    if (c > 99) c = 99;
    w8(a1 + 9, c);
    write_save();
}

/* a zone cleared (or a game's end): the file resumes at next_za's zone */
void tri_save_progress(uint16_t next_za)
{
    if (r16(RAM_SK_alone_flag)) return;
    uint32_t a1 = slot();
    if (!a1) return;
    int code = tri_za_to_code(next_za);
    if (!(r8(a1) & 3) || code >= r8(a1 + 3)) {     /* (a finished file replaying an earlier zone stays finished) */
        w8(a1, (uint8_t)(r8(a1) & 0xFC));
        w8(a1 + 3, (uint8_t)code);
    }
    w16(a1 + 4, 0);
    save_lives(a1);
    fprintf(stderr, "[trilogy] saved: level code %d ($%04X)\n", code, next_za);
}

/* A game's end (trilogy.h). One game alone: the file finished at its
 * finished card, its cleared state from the emeralds (as SaveGame). */
uint16_t tri_after_game(int game)
{
    int exp = tri_experience(), easy = tri_easy();
    if (exp == TRI_EXP_ALL) {
        uint16_t next = game == TRI_EXP_S1 ? tri_experience_start(tri_scd_available() ? TRI_EXP_CD : TRI_EXP_S2, easy)
                      : game == TRI_EXP_CD ? tri_experience_start(TRI_EXP_S2, easy)
                      : tri_experience_start(TRI_EXP_S3K, 0);       /* (S3K's own start: Easy makes it act 2 as it loads) */
        tri_save_progress(next);
        fprintf(stderr, "[trilogy] game %d complete: on to $%04X\n", game, next);
        return next;
    }
    uint32_t a1 = slot();
    if (a1 && !r16(RAM_SK_alone_flag)) {
        uint8_t c = 1;
        if (r8(RAM_Chaos_emerald_count) >= 7) { c++; if (r8(RAM_Super_emerald_count) >= 7) c++; }
        w8(a1, c);
        w8(a1 + 3, k_hi[exp]);
        w16(a1 + 4, 0);
        save_lives(a1);
    }
    fprintf(stderr, "[trilogy] game %d complete: file finished\n", game);
    return 0;
}

/* SaveGame for Sonic 3 & Knuckles' zones, codes offset by 25 */
static void save_game_s3k(void)
{
    uint32_t a1 = slot();
    if (r16(RAM_SK_alone_flag) || !a1) goto done;
    {
        uint16_t za = r16(RAM_Current_zone_and_act);
        int z = za >> 8;
        int d0 = CODE_S3K + (z < 24 ? k_next_level[z][za & 1] : 0);
        if ((r8(a1) & 3) && d0 < r8(a1 + 3)) { save_lives(a1); goto done; }
        w8(a1, (uint8_t)(r8(a1) & 0xFC));
        w8(a1 + 3, (uint8_t)d0);
        uint16_t pm = r16(RAM_Player_mode);
        int completed;
        if (pm == 3) completed = d0 >= CODE_S3K + 0xC;
        else if (pm == 2) completed = d0 >= CODE_S3K + 0xD;
        else completed = d0 > CODE_S3K + 0xD || (d0 == CODE_S3K + 0xD && r8(RAM_Chaos_emerald_count) < 7);
        if (completed) {
            uint8_t c = 1;
            if (r8(RAM_Chaos_emerald_count) >= 7) { c++; if (r8(RAM_Super_emerald_count) >= 7) c++; }
            w8(a1, c);
        }
        w16(a1 + 4, 0);
        save_lives(a1);
    }
done:
    w32(RAM_Collected_special_ring_array, 0);
}

/* ---- a new file: the experience picker (trilogy_title.c) ---------------- */
void tri_picker_open(uint32_t slot_data);

/* The picker's choice: the file (or No Save play) starts its experience. */
void tri_save_start(uint32_t a1, int exp, int easy)
{
    uint16_t za = tri_experience_start(exp, easy);
    uint8_t b1 = (uint8_t)(FILE_MARK | (easy ? FILE_EASY : 0) | exp);
    if (a1) {
        w8(a1, 0);
        w8(a1 + 1, b1);
        w8(a1 + 3, (uint8_t)tri_za_to_code(za));
        w32(RAM_Save_pointer, 0xFF000000u | a1);
        write_save();
    } else s_nosave = b1;
    w16(RAM_Current_zone_and_act, za);
    w16(RAM_Apparent_zone_and_act, tri_apparent(za));
    w8(RAM_Game_mode, 0x0C);
    fprintf(stderr, "[trilogy] new %s file: experience %d%s from $%04X\n", a1 ? "save" : "no-save",
            exp, easy ? " (easy)" : "", za);
}

void tri_thumb_show(int code);

/* ---- more files: pages of eight ------------------------------------------
 * The Data Select shows eight files; the Anthology keeps PAGES of them. Page
 * 0 is S3K's own (SRAM_SKgame and its backup), the others sit in the SRAM
 * S3K leaves unused ($200400 on), in the same format (eight files, $4244,
 * the checksum; a backup copy $A8 after), read through S3K's own
 * Get_From_SRAM and written by its Write_SaveGame, pointed at the page.
 * Right on DELETE turns to the next page, Left on NO SAVE to the one before;
 * the file being played stays in Saved_data (its page with it) until the
 * Data Select turns again. */
#define RAM_Dataselect_entry 0xFFEF4Bu
#define RAM_DS_selector     0xFFB04Au           /* (Player_2: Obj_SaveScreen_Selector) */
#define RAM_DS_delete_prompt 0xFFEEE2u          /* (Events_bg+$10, +$12: a delete under way) */
#define RAM_DS_delete_armed 0xFFEEE4u
#define DS_SLOT0            0xFFB128u           /* the eight file cards, $4A apart */
#define S3K_Get_From_SRAM   0x00C30Eu
#define S3K_loc_D3B0        0x00D3B0u           /* a file card's reading of its file */
#define S3K_Obj_SaveScreen_Selector 0x00D1B6u

static int s_page;

int tri_save_page(void) { return s_page; }
int tri_save_pages(void) { return PAGES; }

static uint32_t page_sram(int p, int backup)
{
    if (!p) return backup ? 0x20032Du : 0x200281u;               /* SRAM_SKgame(_backup) */
    return 0x200401u + (uint32_t)(p - 1) * 0x150u + (backup ? 0xA8u : 0);
}

/* a page into Saved_data (empty files if it was never written) */
static void load_page(int p)
{
    uint32_t sa[3] = { A(0), A(1), A(2) }, sd[2] = { D(0), D(1) };
    uint16_t sr = g_cpu.SR;
    A(0) = page_sram(p, 0);
    A(1) = page_sram(p, 1);
    A(2) = 0xFF000000u | RAM_Saved_data;
    D(0) = 0x29;                                     /* (the block's words, less one) */
    D(1) = 0x4244;                                   /* (its integrity word) */
    tri_jsr(S3K_Get_From_SRAM);
    int ok = (g_cpu.SR & 4) != 0;                    /* (Z: read and checked) */
    A(0) = sa[0]; A(1) = sa[1]; A(2) = sa[2]; D(0) = sd[0]; D(1) = sd[1];
    g_cpu.SR = sr;
    if (!ok) {                                       /* (as SaveData_SKDefault: eight empty files) */
        for (int i = 0; i < SLOTS; i++) {
            uint32_t a1 = RAM_Saved_data + (uint32_t)i * SLOT_SIZE;
            for (int k = 0; k < SLOT_SIZE; k++) w8(a1 + (uint32_t)k, 0);
            w8(a1, 0x80);
            w8(a1 + 8, 3);
        }
        w16(RAM_Saved_data + 0x50, 0x4244);
    }
    fprintf(stderr, "[trilogy] save files: page %d of %d%s\n", p + 1, PAGES, ok ? "" : " (new)");
}

/* one of another page's files, from its SRAM (the primary copy: a bus byte
 * every other address) */
static uint8_t page_byte(int p, int i, int k)
{
    uint8_t *sram = gbus_sram_buffer(&g_machine.bus);
    uint32_t a = page_sram(p, 0) - 0x200000u + (uint32_t)(i * SLOT_SIZE + k) * 2u;
    return a < 0x4000u ? sram[a] : 0;
}
static int page_written(int p)
{
    return page_byte(p, 8, 0) == 0x42 && page_byte(p, 8, 1) == 0x44;   /* (the $4244 after the files) */
}

/* Right on DELETE, Left on NO SAVE (TH_DS_LOOP: before the cards read the pad) */
static void page_turn(void)
{
    uint8_t press = r8(RAM_Ctrl_1_pressed);
    if (!(press & 0x0C) || (r8(RAM_Game_mode) & 0x7F) != 0x4C) return;
    if (r16(RAM_DS_delete_prompt) || r16(RAM_DS_delete_armed) || r16(RAM_DS_selector + 0x30)) return;
    int e = r8(RAM_Dataselect_entry), to;
    if ((press & 0x08) && e == 9 && s_page < PAGES - 1) to = s_page + 1;
    else if ((press & 0x04) && e == 0 && s_page > 0) to = s_page - 1;
    else return;
    w8(RAM_Ctrl_1_pressed, (uint8_t)(press & ~0x0C));
    write_save();                                    /* (this page as it stands) */
    int from = s_page;
    s_page = to;
    load_page(to);
    for (int i = 0; i < SLOTS; i++) {                /* the cards read their files again */
        uint32_t o = DS_SLOT0 + (uint32_t)i * OBJ_SIZE;
        uint32_t c = r32(o) & 0xFFFFFFu;
        if (c >= 0xD39Eu && c < 0xD6D0u) w32(o, S3K_loc_D3B0);
    }
    w8(RAM_Dataselect_entry, (uint8_t)(to > from ? 1 : 8));   /* (the new page's first file, or last) */
    w32(RAM_DS_selector, S3K_Obj_SaveScreen_Selector);
    w16(RAM_DS_selector + 0x2E, 0);
    w16(RAM_DS_selector + 0x30, 0);
    o_sfx(0x5B);
}

/* development: TRILOGY_DEV_FILES=1 fills the eight slots with one file of
 * each kind (the badges, Easy, finished files' zone picks) */
static void dev_files(void)
{
    static const struct { uint8_t b0, b1, ch, code, lives; uint16_t emeralds; } k[SLOTS] = {
        { 0, FILE_MARK | TRI_EXP_ALL,             0, 15, 5, 0 },               /* Chemical Plant */
        { 0, FILE_MARK | FILE_EASY | TRI_EXP_ALL, 3, 9,  4, 0 },               /* Tidal Tempest, Knuckles */
        { 0, FILE_MARK | TRI_EXP_S1,              1, 3,  3, 0 },               /* Labyrinth */
        { 2, FILE_MARK | FILE_EASY | TRI_EXP_S1,  1, CODE_CD, 7, 0x5554 },   /* finished, the emeralds */
        { 0, FILE_MARK | TRI_EXP_CD,              1, 12, 3, 0 },               /* Stardust Speedway */
        { 0, FILE_MARK | FILE_EASY | TRI_EXP_S2,  2, 19, 6, 0 },               /* Mystic Cave, Tails */
        { 0, FILE_MARK | TRI_EXP_S3K,             0, 31, 3, 0 },               /* Mushroom Hill */
        { 3, FILE_MARK | TRI_EXP_ALL,             1, CODE_CLEAR, 9, 0xFFFC },  /* finished: all super */
    };
    static int done;
    const char *e = getenv("TRILOGY_DEV_FILES");
    if (done || !e || *e != '1') return;
    done = 1;
    for (int i = 0; i < SLOTS; i++) {
        uint32_t a1 = RAM_Saved_data + (uint32_t)i * SLOT_SIZE;
        w8(a1, k[i].b0);
        w8(a1 + 1, k[i].b1);
        w8(a1 + 2, (uint8_t)(k[i].ch << 4));
        w8(a1 + 3, k[i].code);
        w16(a1 + 4, 0);
        w16(a1 + 6, k[i].emeralds);
        w8(a1 + 8, k[i].lives);
        w8(a1 + 9, 1);
    }
    write_save();
    fprintf(stderr, "[trilogy] dev: eight files of every kind\n");
}

int tri_save_hook(uint32_t pc)
{
    switch (pc) {
    case TH_SAVEGAME:
        if (!tri_dir()) save_game_s3k();
        return 1;                                    /* (the other games' zones save on their own zone clears) */
    case TH_DS_INIT:
        migrate();
        dev_files();
        return 0;
    case TH_DS_LOOP:
        page_turn();
        return 0;
    case TH_SRAM_LOAD:                               /* (boot: S3K's own files are the first page) */
        s_page = 0;
        return 0;
    case TH_SAVE_WRITE:                              /* (Write_SaveGame: to the files' page) */
        if (s_page) {
            A(0) = page_sram(s_page, 0);
            A(1) = page_sram(s_page, 1);
        }
        return 0;
    case TH_DS_NOSAVE_START:                         /* (Set_Lives_and_Continues, Game_mode already $0C) */
    case TH_DS_NEWGAME: {                            /* the picker first; the Data Select carries on under it */
        uint32_t a1 = pc == TH_DS_NEWGAME ? A(1) & 0xFFFFFFu : 0;
        const char *dz = getenv("TRILOGY_ZONE");
        if ((dz && *dz) || getenv("TRILOGY_NO_PICKER")) {   /* (development, the previews' recording: no
                                                             *  picker - TRILOGY_EXP / TRILOGY_EASY, else All) */
            uint8_t b = file_byte();
            tri_save_start(a1, exp_of(b), (b & (FILE_MARK | FILE_EASY)) == (FILE_MARK | FILE_EASY));
            return 0;
        }
        if (a1) w8(a1, 0x80);                        /* (written empty until a choice is made) */
        w8(RAM_Game_mode, 0x4C);
        tri_picker_open(a1);
        return 0;
    }
    case TH_DS_ZONE_MAX: {                           /* a finished file picks from its experience's zones */
        uint32_t o = A(0) & 0xFFFFFFu, a1 = A(1) & 0xFFFFFFu;
        int exp = exp_of(r8(a1 + 1)), lo = k_lo[exp], hi = r8(o + 0x3A);
        int s3k_hi = CODE_S3K + (int)(D(6) & 0xFFFF);              /* (S3K's own limit for the character) */
        if (hi > s3k_hi && (exp == TRI_EXP_ALL || exp == TRI_EXP_S3K)) hi = s3k_hi;
        uint8_t press = r8(RAM_Ctrl_1_pressed);
        int step = press & 0x01 ? 1 : press & 0x02 ? -1 : 0;           /* (up: on, down: back, as S3K) */
        if (step && hi > lo) {
            int c = (int16_t)r16(o + 0x36);
            for (int n = 0; n <= hi - lo; n++) {
                c += step;
                if (c > hi) c = lo;
                if (c < lo) c = hi;
                if (code_ok(exp, c)) break;
            }
            w16(o + 0x36, (uint16_t)c);
            o_sfx(0x5B);                                               /* sfx_Switch */
        }
        w8(RAM_Ctrl_1_pressed, (uint8_t)(press & ~0x03));             /* (S3K's own stepping sees nothing) */
        return 0;
    }
    case TH_DS_CODE_TO_ZONE: {                       /* the level a code starts (a1: the file) */
        int code = D(0) & 0xFF;
        uint8_t b1 = r8((A(1) & 0xFFFFFFu) + 1);
        int easy = (b1 & (FILE_MARK | FILE_EASY)) == (FILE_MARK | FILE_EASY);
        if (code < CODE_S3K) {
            uint16_t za = tri_code_to_za(code);
            D(0) = (D(0) & 0xFFFF0000u) | (easy ? easy_act(za) : za);
            return 1;
        }
        D(0) = (D(0) & 0xFFFFFF00u) | (uint8_t)(code - CODE_S3K);   /* (S3K's list; Easy's act 2 as it loads) */
        return 0;
    }
    case TH_DS_ZONE_TENS: {                          /* "ZONE n": its place in the file's list (a0: the file) */
        int n = zone_number(r8((A(0) & 0xFFFFFFu) + 1), (int)((D(0) & 0xFFFF) >> 1));
        if (n / 10) { D(1) = (D(1) & 0xFFFFFF00u) | (uint8_t)(n / 10); g_cpu.SR &= ~8u; }
        else { D(1) = (D(1) & 0xFFFFFF00u) | 0xFF; g_cpu.SR |= 8u; }   /* (blank: the bpl reads N) */
        return 0;
    }
    case TH_DS_ZONE_UNITS: {
        int n = zone_number(r8((A(0) & 0xFFFFFFu) + 1), (int)((D(0) & 0xFFFF) >> 1));
        D(1) = (D(1) & 0xFFFFFF00u) | (uint8_t)(n % 10);
        return 0;
    }
    case TH_RENDER_HUD: {                            /* (no HUD while the zone cards are recorded) */
        static int glide = -1;
        if (glide < 0) glide = getenv("TRILOGY_GLIDE") != NULL;
        if (glide) w8(RAM_Player_1 + 0x34, 0x18);    /* (and no player: flickered out, unhurt - in every zone) */
        return glide;
    }
    case TH_DS_ZONE_ICON:
        tri_thumb_show(D(1) & 0xFF);
        return 1;
    default:
        return 0;
    }
}

/* development: TRILOGY_CLEAR_SLOT=n (1-8) makes save file n a finished
 * Anthology (Sonic 1 through Sonic 3 & Knuckles, every emerald super) */
void tri_save_dev_clear(void)
{
    static int done;
    const char *e = getenv("TRILOGY_CLEAR_SLOT");
    if (done || !e || !*e) return;
    done = 1;
    int n = atoi(e);
    if (n < 1 || n > 8) return;
    uint32_t a1 = RAM_Saved_data + (uint32_t)(n - 1) * SLOT_SIZE;
    w8(a1, 3);                                       /* finished, all Super Emeralds */
    w8(a1 + 1, FILE_MARK | TRI_EXP_ALL);
    w8(a1 + 2, 0x00);                                /* Sonic & Tails */
    w8(a1 + 3, CODE_CLEAR);
    w16(a1 + 4, 0);
    w16(a1 + 6, 0xFFFC);                             /* seven Super Emeralds */
    w8(a1 + 8, 9);
    w8(a1 + 9, 3);
    write_save();
    fprintf(stderr, "[trilogy] save file %d marked finished\n", n);
}
