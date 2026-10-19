/* Opt-in Sonic 3 family world renderer (combined S3&K first).
 * Stock S&K ROM: Render_Sprites $1AD20, Load_Sprites $1B690.
 * Native collision, decompression and tile streaming remain untouched.
 * Host scene publication follows the actual native SAT upload.
 */
#include "sonic3_video.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include "cmd_server.h"
#include "sonic3_state.h"
#include "sonic3_state_io.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The standalone S3 cartridge has the same object/layout formats, with a
 * different engine address map. S&K and the combined cartridge share code. */
#ifdef SONIC3_STANDALONE
enum { S3_COMPETITION=0xFFD8, S3_PLACEMENTS=0x5E0D8, S3_RINGS=0x5E198,
       S3_HUD_MAP=0xE932, S3_RING_MAP=0xF87E,
       S3_LOAD_INIT=0x19CF2, S3_LOAD_UPDATE=0x19DD0,
       S3_CAPTURE=0x1938E, S3_PUBLISH=0x194B8,
       S3_TITLE_PLANE_MAP=0x4620, S3_FILE_SELECT_MAP=0xBD68,
       S3_AIZ_MINIBOSS_CUTSCENE=0x46442,
       S3_RING_RANGE=0xF6D8, S3_RING_STATUS=0xF6E2,
       S3_ART_BANK=0, S3_PAL_INTRO=0x8C314, S3_PAL_FOREST=0x8C374 };
#else
enum { S3_COMPETITION=0xFFE8, S3_PLACEMENTS=0x1E3D98, S3_RINGS=0x1E3E58,
       S3_HUD_MAP=0xDBB6, S3_RING_MAP=0xEBEE,
       S3_LOAD_INIT=0x1B690, S3_LOAD_UPDATE=0x1B7F2,
       S3_CAPTURE=0x1AD20, S3_PUBLISH=0x1AE56,
       S3_TITLE_PLANE_MAP=0x4FE8, S3_FILE_SELECT_MAP=0xCE0E,
       S3_AIZ_MINIBOSS_CUTSCENE=0x684EC,
       S3_RING_RANGE=0xEA32, S3_RING_STATUS=0xEA3C,
       S3_ART_BANK=0x200000, S3_PAL_INTRO=0xA8B1C, S3_PAL_FOREST=0xA8B7C };
#endif

enum { VIDEO_OFF, VIDEO_FIT, VIDEO_RATIO, VIDEO_STAGE };
static int s_mode;
static double s_ratio = 16.0 / 9.0;
static int s_width, s_left, s_camera, s_stage_width, s_native_x, s_requested_width = 320;
static int s_camera_y;
static unsigned s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors;
static unsigned s_bg_checks, s_bg_errors, s_bg_unstreamed;
static unsigned s_scene_misses, s_scene_holds;
static unsigned s_uninitialized_scroll_lines;
static unsigned s_terrain_unstreamed;
static int s_bad_x,s_bad_y;
static unsigned s_bad_attr,s_bad_expected;
static int s_bad_world_y,s_bad_hscroll;
static int s_bg_bad_x,s_bg_bad_y,s_bg_bad_a,s_bg_bad_expected;
static GVDP s_video_frame;
static uint8_t s_world_frame[0xA800];
static uint8_t s_background_frame[0xA800];
static int s_frame_level,s_frame_special,s_frame_fg_x,s_frame_fg_y,s_frame_bg_y;
static unsigned s_frame_zone,s_frame_act,s_frame_bg_event;
static unsigned s_frame_tree_reveal;
static int s_frame_bg_band[2];
static void scene_mode_changed(int was_enabled);
static void ss_capture(void);

/* AIZ reuses its intro's VRAM and block definitions for the forest, and
 * temporarily installs Knuckles' palette during the emerald cutscene.
 * An expanded view sees both banks simultaneously. Decode their original
 * ROM assets into host-only storage; never preload them into guest memory. */
typedef struct { uint8_t blocks[0x1800]; GVDP video; uint16_t colors[48]; } AizArt;
static AizArt s_aiz_intro,s_aiz_main;
static int s_aiz_art_ready,s_aiz_art_attempted;
typedef struct { const uint8_t *p,*end; unsigned desc,bits; int error; } KosReader;
static unsigned kos_byte(KosReader *r)
{
    if(r->p==r->end){r->error=1;return 0;}
    return *r->p++;
}
static unsigned kos_bit(KosReader *r)
{
    unsigned bit=r->desc&1u;r->desc>>=1;
    if(!--r->bits) {
        unsigned low=kos_byte(r);r->desc=low|(kos_byte(r)<<8);r->bits=16;
    }
    return bit;
}
static size_t art_kos(const uint8_t *src,size_t size,uint8_t *out,size_t capacity,size_t *used)
{
    KosReader r={src,src+size,0,16,0};size_t n=0;
    unsigned low=kos_byte(&r);r.desc=low|(kos_byte(&r)<<8);
    while(!r.error) {
        if(kos_bit(&r)) {
            unsigned value=kos_byte(&r);
            if(r.error || n==capacity)return 0;
            out[n++]=(uint8_t)value;continue;
        }
        unsigned count;int offset;
        if(kos_bit(&r)) {
            unsigned lo=kos_byte(&r),hi=kos_byte(&r);
            offset=(int)(((hi&248u)<<5)|lo)-8192;count=hi&7u;
            if(!count) {
                count=kos_byte(&r);
                if(r.error)return 0;
                if(!count){*used=(size_t)(r.p-src);return n;}
                if(count==1)continue;
                ++count;
            } else count+=2;
        } else {
            count=kos_bit(&r)<<1;count=(count|kos_bit(&r))+2;
            offset=(int)kos_byte(&r)-256;
        }
        if(r.error || (size_t)-offset>n || count>capacity-n)return 0;
        while(count--){out[n]=out[n-(size_t)-offset];++n;}
    }
    return 0;
}
static size_t art_kosm(unsigned address,size_t compressed,uint8_t *out,size_t capacity)
{
    if(compressed<2 || address>0x400000u-compressed)return 0;
    const uint8_t *src=g_rom+address;
    size_t total=(size_t)((src[0]<<8)|src[1]),pos=2,n=0;
    if(total==0xA000)total=0x8000;
    if(total>capacity)return 0;
    while(n<total && pos<compressed) {
        size_t used=0,expected=total-n>4096?4096:total-n;
        size_t got=art_kos(src+pos,compressed-pos,out+n,expected,&used);
        if(got!=expected)return 0;
        n+=got;pos+=used;pos=2+((pos-2+15)&~(size_t)15);
    }
    return n==total?n:0;
}
static void load_aiz_art(void)
{
    if(s_aiz_art_attempted)return;
    s_aiz_art_attempted=1;
    size_t used=0;
    if(art_kos(g_rom+S3_ART_BANK+0x1A374A,0x200,s_aiz_intro.blocks,0x268,&used)!=0x268 ||
       art_kos(g_rom+S3_ART_BANK+0x1A394A,0xC40,s_aiz_intro.blocks+0x268,0x1058,&used)!=0x1058 ||
       art_kosm(S3_ART_BANK+0x1A566A,0xE12,s_aiz_intro.video.vram,0x17C0)!=0x17C0 ||
       art_kosm(S3_ART_BANK+0x1A647C,0x2FD2,s_aiz_intro.video.vram+0x17C0,0x6260)!=0x6260)return;
    memcpy(s_aiz_main.blocks,s_aiz_intro.blocks,0x268);
    memcpy(s_aiz_main.video.vram,s_aiz_intro.video.vram,0x17C0);
    if(art_kos(g_rom+S3_ART_BANK+0x1A458A,0x10E0,s_aiz_main.blocks+0x268,0x14F0,&used)!=0x14F0 ||
       art_kosm(S3_ART_BANK+0x1A944E,0x27F2,s_aiz_main.video.vram+0x17C0,0x4920)!=0x4920)return;
    for(unsigned i=0;i<48;++i) {
        s_aiz_intro.colors[i]=(uint16_t)((g_rom[S3_PAL_INTRO+i*2]<<8)|g_rom[S3_PAL_INTRO+i*2+1]);
        s_aiz_main.colors[i]=(uint16_t)((g_rom[S3_PAL_FOREST+i*2]<<8)|g_rom[S3_PAL_FOREST+i*2+1]);
    }
    s_aiz_art_ready=1;
}

static uint16_t ram16(unsigned a)
{
    a &= 0xFFFFu;
    return (uint16_t)((g_ram[a] << 8) | g_ram[(a + 1) & 0xFFFFu]);
}
static uint16_t vram16(const GVDP *v, unsigned a)
{
    return (uint16_t)((v->vram[a & 0xFFFFu] << 8) | v->vram[(a + 1) & 0xFFFFu]);
}
static int gameplay(void)
{
    int mode=g_ram[0xF600]&127;
    return (mode==8 || mode==12) && (ram16(0xB000)||ram16(0xB002)) && !ram16(S3_COMPETITION);
}
static int stage_width(void)
{
    int w=ram16(0x8000)*128;
    return w>=320 && w<=32768?w:320;
}
static int level_ready(void)
{
    unsigned w=ram16(0x8000),row=ram16(0x8008);
    return gameplay() && !(g_ram[0xF600]&128) && w>=3 && w<=256 && row>=0x8088 && row<0x9000;
}
static int configure(const char *mode)
{
    if (!mode) return 0;
    int was_enabled = s_mode != VIDEO_OFF;
    if (!strcmp(mode, "off")) s_mode = VIDEO_OFF;
    else if (!strcmp(mode, "fit") || !strcmp(mode, "adaptive")) s_mode = VIDEO_FIT;
    else if (!strcmp(mode, "stage")) s_mode = VIDEO_STAGE;
    else {
        char *end;
        double w = strtod(mode, &end);
        if (end == mode || *end != ':') return 0;
        const char *den = end + 1;
        double h = strtod(den, &end);
        if (end == den || *end || !(w > 0) || !(h > 0) ||
            !(w / h > 0) || w / h > (double)INT_MAX / 480.0) return 0;
        s_ratio = w / h; s_mode = VIDEO_RATIO;
    }
    scene_mode_changed(was_enabled);
    return 1;
}
static int enabled(void) { return s_mode != VIDEO_OFF; }
unsigned s3_video_main_cpu_divisor(void)
{
    /* LevelLoop ($650C) still waits for the real V-int every tick.
     * Expanded activation must not inherit the 7.67 MHz object's lag budget.
     * Loading, special stages, native 2P and opt-out keep original timing. */
    int mode=g_ram[0xF600];
    return enabled() && (mode==8 || mode==12) && gameplay() && g_ram[0xF711] ? 4 : 1;
}
static int width(int dw, int dh, int nw, int nh)
{
    if (!enabled()) return 0;
    double result = nw;
    if (s_mode == VIDEO_STAGE) {
        if (gameplay()) s_stage_width = stage_width();
        result = s_stage_width > nw ? s_stage_width : nw;
    }
    else if (s_mode == VIDEO_RATIO) result = nh * s_ratio;
    else if (dw > 0 && dh > 0) result = (double)nh * dw / dh;
    /* Bound by representable dimensions and the SDL texture limit (runner),
     * never by an aspect preset. Even a stage-length viewport is supported. */
    if (result > INT_MAX - 1.0) result = INT_MAX - 1.0;
    int pixels = (int)(result + 0.5);
    s_requested_width = pixels < nw ? nw : pixels;
    return s_requested_width;
}
int s3_video_canvas_width(void) { return enabled()?s_requested_width:320; }

/* Full 16-bit scroll is unwrapped against the live camera's 10-bit VDP
 * position. This avoids the one-frame camera/streaming seam seen in SMB. */
static int unwrap(int value, int reference)
{
    return reference + ((value - reference + 512) & 1023) - 512;
}
static int native_terrain_streamed(int wy, int copied_camera_y)
{
    /* Native foreground streaming uses 16px rows at copied-camera offsets -16 and
     * +224. Live VScroll can run ahead of that copy during a fast fall.
     * Outside this precise coverage, the native name table is not a terrain
     * oracle: the enhanced renderer must still draw the real stage block. */
    int top = (copied_camera_y & ~15) - 16;
    return wy >= top && wy < top + 256;
}
static uint16_t world_attr_blocks(const uint8_t *ram,const uint8_t *blocks,int wx,int wy,int background)
{
    unsigned header=background?0x8002u:0x8000u;
    int width=((ram[header]<<8)|ram[header+1])*128;
    if(width<=0 || (!background && (wx<0 || wx>=width)))return 0;
    unsigned row=((unsigned)wy>>5)&0x7Cu;
    unsigned p=0x8008u+row+(background?2u:0u);
    unsigned rowaddr=(ram[p]<<8)|ram[p+1];
    /* Get_ChunkRow uses a signed chunk offset. A moving background can
     * expose the guard chunks preceding its row (HCZ wall/HPZ/LRZ3);
     * wrapping that offset to the row's far end selects different art. */
    int cell=(int)rowaddr+(wx>>7);
    if(cell<0x8088 || cell>=0x9000)return 0;
    unsigned address=ram[cell]*128u+((unsigned)wy&112u)+((unsigned)wx&112u)/8u;
    uint16_t block=(uint16_t)((ram[address]<<8)|ram[address+1]);
    int tx=(wx>>3)&1,ty=(wy>>3)&1;
    if(block&0x400)tx^=1;
    if(block&0x800)ty^=1;
    address=0x9000u+(block&1023u)*8u+(unsigned)(ty*2+tx)*2u;
    if(address+1>=0xA800)return 0;
    address-=0x9000;
    return (uint16_t)(((blocks[address]<<8)|blocks[address+1])^((block&0xC00u)<<1));
}
static uint16_t world_attr(const uint8_t *ram,int wx,int wy,int background)
{ return world_attr_blocks(ram,ram+0x9000,wx,wy,background); }
static uint8_t pattern_pixel(const GVDP *v, uint16_t attr, int x, int y)
{
    int fx = x & 7, fy = y & 7;
    if (attr & 0x0800) fx ^= 7;
    if (attr & 0x1000) fy ^= 7;
    unsigned a = (attr & 2047u) * 32u + (unsigned)fy * 4u + (unsigned)fx / 2u;
    int byte = v->vram[a & 0xFFFFu];
    int nibble = fx & 1 ? byte & 15 : byte >> 4;
    return nibble ? (uint8_t)(((attr >> 9) & 48) | nibble) : 0;
}
static int plane_tiles(int bits) { return bits == 1 ? 64 : bits == 3 ? 128 : 32; }
static uint16_t plane_attr(const GVDP *v, unsigned base, int x, int y)
{
    int wt = plane_tiles(v->reg[16] & 3), ht = plane_tiles((v->reg[16] >> 4) & 3);
    int col = (x >> 3) & (wt - 1), row = (y >> 3) & (ht - 1);
    return vram16(v, base + (unsigned)(row * wt + col) * 2u);
}
static int file_select_layout(const GVDP *v)
{
    /* Keep the outgoing menu through its fade, after Game_mode changes.
     * SaveScreen alone installs this full-screen Window/128-column B pair. */
    return (v->reg[12]&1) && (v->reg[2]&56)==0x38 && (v->reg[3]&62)==0x38 &&
        (v->reg[4]&7)==6 && v->reg[18]==0x80 && (v->reg[16]&3)==3;
}

/* Enhanced scene data lives on the host: no SAT coordinate wrap, 80-piece
 * ceiling, or 20-sprites-per-line ceiling. Native BuildSprites still executes
 * for the hardware pass and publishes a signature used to align our list
 * with the exact VBlank upload displayed by the VDP. */
/* zoom: scaled about (rx,ry) too, in 256ths (0: none).
 * rot: turn about (rx,ry), 1024 = full circle clockwise on screen (0 none).
 * bank: 0 VRAM tiles, else the frame's own player art (SceneFrame.ptiles). */
/* art: a game-supplied indexed picture drawn in place of tile pieces (its
 * size is the piece size; attr carries palette line, priority and flips). */
typedef struct { int x, y; uint16_t attr; uint8_t size, hud; uint16_t rot; uint8_t bank, slot; int16_t rx, ry; const S3VideoArt *art; uint16_t zoom; } SceneSprite;
enum { SCENE_SPRITES = 32768, SCENE_PLACEMENTS = 1024, PLAYER_TILES = 64, SCENE_SLOTS = 112 };
typedef struct {
    SceneSprite sprites[SCENE_SPRITES];
    /* world position of each drawn object (slot = 1 + RAM index), for
     * frame interpolation; code 0 = not drawn this frame */
    int16_t ox[SCENE_SLOTS], oy[SCENE_SLOTS];
    uint32_t ocode[SCENE_SLOTS];
    uint8_t ptiles[2][PLAYER_TILES*32]; /* P1/P2 upright frames (smooth rotation) */
    unsigned count, serial;
    uint8_t sat[640];
    int scene; /* 1 level, 2 special, 0 menu */
    int camera_x, camera_y; /* coordinates used when capturing world sprites */
} SceneFrame;
static SceneFrame s_build, s_history[3], s_display_frame;
static const SceneFrame *s_display;
static unsigned s_serial, s_scene_tick, s_spawned, s_pool_pressure;
static unsigned s_tick_samples,s_tick_updates,s_tick_lag,s_tick_multi;
static unsigned s_publication_lag;
static int s_tick_was_active;
static unsigned s_tick_last,s_tick_zone,s_tick_serial;
/* Newest publication a V-int could have uploaded (0 = unknown). Only a
 * completed tick sets V_int_routine; VInt_0_Main (lag) leaves the SAT in
 * VRAM alone. With CPU headroom the next tick often publishes before
 * line 0 is drawn: when only the camera-tracked player is on screen its
 * SAT bytes equal the displayed one, and choosing it drew the player a
 * frame ahead of the planes. */
static unsigned s_upload_serial;
void s3_video_vblank(void)
{
    if(g_ram[0xF62A])s_upload_serial=s_serial;
    if(enabled())ss_capture();
    unsigned tick=ram16(0xFE04),zone=ram16(0xFE10);
    int active=enabled() && g_ram[0xF600]==12 && gameplay() &&
        g_ram[0xF711] && g_ram[0xB005]<6 && !ram16(0xF63A);
    /* Sample at the same IRQ entry, not end-of-wall-frame: native variable
     * V-int DMA debt can shift the next tick across that later sample point. */
    if(active && s_tick_was_active && zone==s_tick_zone) {
        unsigned delta=(uint16_t)(tick-s_tick_last);
        ++s_tick_samples;s_tick_updates+=delta;
        s_tick_lag+=delta==0;s_tick_multi+=delta>1;
        s_publication_lag+=s_serial==s_tick_serial;
    }
    s_tick_was_active=active;s_tick_last=tick;s_tick_zone=zone;s_tick_serial=s_serial;
}
static unsigned s_visible_objects[144], s_visible_count;
typedef struct { unsigned address; uint16_t x, y; uint8_t id, subtype, loaded; uint16_t state; } Placement;
static Placement s_placements[SCENE_PLACEMENTS];
static unsigned s_placement_count, s_placement_base;
static int s_loader_active;

static uint8_t scene_read8(unsigned a)
{
    a &= 0xFFFFFFu;
    if (a < 0x400000u) return g_rom[a];
    if (a >= 0xFF0000u) return g_ram[a & 65535u];
    /* the opt-in expansion bank (game-added zones keep their layouts there) */
    return g_rom_ext && a - 0x400000u < g_rom_ext_size ? g_rom_ext[a - 0x400000u] : 0;
}
static uint16_t scene_read16(unsigned a) { return (uint16_t)((scene_read8(a)<<8)|scene_read8(a+1)); }
static uint32_t scene_read32(unsigned a) { return ((uint32_t)scene_read16(a)<<16)|scene_read16(a+2); }
static void write8(unsigned a, unsigned v) { glue_poke8(0xFF0000u|(a&65535u),(uint8_t)v); }
static void write16(unsigned a, unsigned v) { glue_poke16(0xFF0000u|(a&65535u),(uint16_t)v); }
static void write32(unsigned a, unsigned v) { glue_poke32(0xFF0000u|(a&65535u),v); }
static int view_left(int camera, int w)
{
    int end = stage_width()-w, left = camera-(w-320)/2;
    if (end<0) return end/2;
    if (left<0) left=0;
    return left>end ? end : left;
}
static void activation_bounds(int camera,int w,int *lo,int *hi)
{
    int left=view_left(camera,w);
    /* out_of_range tests 128px-rounded positions. OPL must only instantiate
     * cells that this same culler retains. Loading at left+w+320 with an
     * unrounded camera let objects spawn one cell early, get deleted, and
     * remain marked loaded until they left the window (missing platforms). */
    *lo=(left-128)&~127;
    *hi=((left+w+192)&~127)+128;
}

static void scene_mode_changed(int was_enabled)
{
    if(was_enabled!=enabled()) {
        s_display=NULL;s_display_frame.serial=0;
        for(unsigned n=0;n<3;++n)s_history[n].serial=0;
    }
    /* Once adopted, the host loader also maintains a native-width window
     * on opt-out until the next level init, preserving existing object state. */
}
/* Smooth rotation (opt-in), drawn as the Retro Engine does: a walking or
 * running player is its upright frame turned by the whole ground angle, not
 * one of S3K's 45-degree drawings (those would pop at every 45 degrees).
 * Only the displayed frame is in VRAM, so the upright one is rebuilt from
 * the cartridge art through its DPLC. The drawn angle eases toward the
 * ground's, gliding over the steps between collision blocks (a loop is made
 * of them); the faster the run, the closer it follows. */
static int s_rotate;
void s3_video_set_rotation(int on) { s_rotate = on != 0; }
static int s_add_rot,s_add_bank,s_add_slot,s_add_zoom;
/* A game's own size for an object (see s3_video_set_object_zoom). */
static S3VideoObjectZoom s_object_zoom;
void s3_video_set_object_zoom(S3VideoObjectZoom fn) { s_object_zoom = fn; }
static float s_turn[2];
static int s_turn_ok[2];
/* A game's own player art (see s3_video_set_player_art). */
static S3VideoPlayerArt s_player_art;
void s3_video_set_player_art(S3VideoPlayerArt fn) { s_player_art = fn; }
static const S3VideoArt *player_art(unsigned o,unsigned map,unsigned frame)
{
    return s_player_art && s_build.scene==1 ? s_player_art(o,map,frame&255u) : NULL;
}
/* the picture about the object's position (x, y), mirrored like its mapping */
static void add_art(const S3VideoArt *art,unsigned gfx,unsigned flags,int x,int y)
{
    if(!art->w || !art->h || s_build.count>=SCENE_SPRITES)return;
    int dx=(flags&1)?-art->ox-art->w:art->ox,dy=(flags&2)?-art->oy-art->h:art->oy;
    SceneSprite *q=&s_build.sprites[s_build.count++];
    q->x=x+dx;q->y=y+dy;q->attr=(uint16_t)((gfx&0xE000u)|((flags&1)?0x800u:0)|((flags&2)?0x1000u:0));
    q->size=0;q->hud=0;q->rot=(uint16_t)s_add_rot;q->bank=0;q->slot=(uint8_t)s_add_slot;q->zoom=(uint16_t)s_add_zoom;
    q->rx=(int16_t)x;q->ry=(int16_t)y;q->art=art;
}
static void add_mapping(unsigned map,unsigned frame,unsigned gfx,unsigned flags,
                        int x,int y,int anchor,int static_mapping);
#ifndef SONIC3_STANDALONE
/* Sonic/Tails/Knuckles_Load_PLC2's tables (pinned S&K listing) */
enum { S3_PLC_SONIC=0x148182, S3_PLC_SUPER=0x148378, S3_ART_SONIC=0x100000, S3_ART_SONIC_X=0x140060,
       S3_PLC_TAILS=0x14A08A, S3_ART_TAILS=0x3200E0, S3_ART_TAILS_X=0x143D00,
       S3_PLC_KNUX=0x14BD0A, S3_ART_KNUX=0x1200E0, S3_SUPER_FLAG=0xFE19 };
/* the frame's tiles, in the order its Load_PLC2 would DMA them */
static int player_tiles(unsigned o,unsigned frame,uint8_t *dst)
{
    unsigned ch=g_ram[o+0x38],plc,art;
    if(ch==0){plc=g_ram[S3_SUPER_FLAG]?S3_PLC_SUPER:S3_PLC_SONIC;art=frame>=0xDA?S3_ART_SONIC_X:S3_ART_SONIC;}
    else if(ch==1){plc=S3_PLC_TAILS;art=frame>=0xD1?S3_ART_TAILS_X:S3_ART_TAILS;}
    else {plc=S3_PLC_KNUX;art=S3_ART_KNUX;}
    unsigned p=plc+(unsigned)(int16_t)scene_read16(plc+frame*2u);
    int entries=scene_read16(p),tiles=0;
    p+=2;
    if(entries>32)return 0;
    for(int i=0;i<entries;++i,p+=2) {
        unsigned e=scene_read16(p),src=art+(e&0xFFFu)*32u;
        for(unsigned k=0;k<=(e>>12) && tiles<PLAYER_TILES;++k,++tiles)
            for(unsigned b=0;b<32;++b)dst[tiles*32+b]=scene_read8(src+k*32+b);
    }
    return tiles;
}
#endif
/* Player object o (slot 0 P1, 1 P2) turned while it walks or runs.
 * -> 1 when its pieces were added here (upright art); otherwise s_add_rot
 * holds the remainder turn for the displayed frame (0: upright). */
static int player_turned(unsigned o,int slot,unsigned map,unsigned gfx,unsigned flags,int x,int y)
{
    s_add_rot=0;
    /* Animate_Sonic/Tails/Knuckles use angle frames for anim scripts that
     * begin $FF: walk (0) and run (1). Tumbling and pushing use others.
     * A game's own art is always drawn upright and turned here: smoothly,
     * or (rotation off) by the cartridge's own 45-degree steps. */
    const S3VideoArt *mine=player_art(o,map,g_ram[o+0x22]);
    int own=mine!=NULL;
    if(mine && mine->upright) {                              /* (drawn for its angle already) */
        s_turn_ok[slot]=0;
        add_art(mine,gfx,(flags&~3u)|(g_ram[o+0x2A]&1u),x,y);
        return 1;
    }
    if((!s_rotate && !own) || g_ram[o+0x20]>1 || g_ram[o+0x27] || (int8_t)g_ram[o+0x2D]<0 ||
       (g_ram[o+0x2A]&0x20) || g_ram[o+0x38]>2){s_turn_ok[slot]=0;return 0;}
    int a=g_ram[o+0x26];
    if(a>0 && a<128)--a;
    int left=g_ram[o+0x2A]&1;
#ifndef SONIC3_STANDALONE
    {
        /* The frame set Animate chose: the sector after its facing mirror,
         * times the script's stride per set (walk 4, run 2, Tails' top 1). */
        int set=(((left?a:(~a&255))+0x10)&255)>>4&6;
        unsigned spd=(unsigned)abs((int16_t)ram16(o+0x1C));
        if((int8_t)g_ram[o+0x2B]<0)spd*=2;
        int ch=g_ram[o+0x38],stride;
        if(ch==1)stride=spd<0x600?4:spd<0x700?2:1;
        else stride=spd<(ch==0 && g_ram[S3_SUPER_FLAG]?0x800u:0x600u)?4:2;
        int upright=(int)g_ram[o+0x22]-set*stride;
        const S3VideoArt *art=upright>=0?player_art(o,map,(unsigned)upright):NULL;
        if(!s_rotate) {                                      /* (the cartridge's 45-degree sets) */
            if(!art){s_turn_ok[slot]=0;return 0;}
            s_add_rot=((a+16)&0xE0)*4&1023;
            add_art(art,gfx,(flags&~3u)|(unsigned)left,x,y);
            s_add_rot=0;
            return 1;
        }
        if(upright>=0 && (art || player_tiles(o,(unsigned)upright,s_build.ptiles[slot])>0)) {
            int sa=(int8_t)(uint8_t)a;
            float target=sa>=-2 && sa<=2?0.f:(float)a;      /* level-ish ground stays upright */
            if(!s_turn_ok[slot])s_turn[slot]=target;
            float d=target-s_turn[slot];
            while(d>=128)d-=256;
            while(d<-128)d+=256;
            float k=.5f+(float)spd/(float)0x1800;
            s_turn[slot]+=d*(k>1?1:k);
            if(s_turn[slot]<0)s_turn[slot]+=256;
            if(s_turn[slot]>=256)s_turn[slot]-=256;
            s_turn_ok[slot]=1;
            s_add_rot=(int)(s_turn[slot]*4+.5f)&1023;
            if(art)add_art(art,gfx,(flags&~3u)|(unsigned)left,x,y);
            else {
                s_add_bank=slot+1;
                add_mapping(map,(unsigned)upright,gfx&0xE000u,(flags&~3u)|(unsigned)left,x,y,0,0);
            }
            s_add_rot=s_add_bank=0;
            return 1;
        }
    }
#endif
    s_turn_ok[slot]=0;
    /* Near-level ground stays upright: tiny bumps would only wobble. */
    if((int8_t)(uint8_t)a>=-4 && (int8_t)(uint8_t)a<=4)return 0;
    /* The frame set shows the 32-unit sector chosen after the facing
     * mirror (not.b when facing right); the remainder is the rest. */
    int q=left?((a+16)&0xE0):(-((15-a)&0xE0));
    s_add_rot=((int8_t)(uint8_t)(a-q)*4)&1023;
    return 0;
}
static void add_mapping(unsigned map,unsigned frame,unsigned gfx,unsigned flags,
                        int x,int y,int anchor,int static_mapping)
{
    unsigned p=map;int count=1;
    if(!static_mapping) {
        p+=(int16_t)scene_read16(p+(frame&255u)*2u);
        count=scene_read16(p);p+=2;
    }
    if(count>1024)return;
    for(int n=0;n<count && s_build.count<SCENE_SPRITES;++n,p+=6) {
        int dy=(int8_t)scene_read8(p),size=scene_read8(p+1)&15;
        int dx=(int16_t)scene_read16(p+4);
        uint16_t attr=(uint16_t)(scene_read16(p+2)+gfx);
        if(flags&1){dx=-dx-(((size>>2)&3)+1)*8;attr^=0x800;}
        if(flags&2){dy=-dy-((size&3)+1)*8;attr^=0x1000;}
        if(!s_add_rot && (y+dy>=224 || y+dy+((size&3)+1)*8<=0))continue;
        SceneSprite *q=&s_build.sprites[s_build.count++];
        q->x=x+dx;q->y=y+dy;q->attr=attr;q->size=(uint8_t)size;q->hud=(uint8_t)anchor;
        q->rot=(uint16_t)s_add_rot;q->bank=(uint8_t)s_add_bank;q->slot=(uint8_t)s_add_slot;q->zoom=(uint16_t)s_add_zoom;
        q->rx=(int16_t)x;q->ry=(int16_t)y;q->art=NULL;
    }
}

static void load_placements(void)
{
    unsigned zone=ram16(0xFE10),index=(zone>>8)*8u+(zone&1u)*4u;
    unsigned base=scene_read32(S3_PLACEMENTS+index);
    if(base==s_placement_base && s_placement_count)return;
    s_placement_base=base;s_placement_count=0;
    if(base>=0x400000u)return;
    for(unsigned p=base;p+6<=0x400000u && s_placement_count<768;p+=6) {
        unsigned x=scene_read16(p);if(x==65535u)break;
        Placement *q=&s_placements[s_placement_count];
        q->address=p;q->x=(uint16_t)x;q->y=scene_read16(p+2);
        q->id=scene_read8(p+4);q->subtype=scene_read8(p+5);
        q->state=(uint16_t)(0xEB00+s_placement_count++);q->loaded=0;
    }
}
static unsigned placement_code(const Placement *p)
{
#ifdef SONIC3_STANDALONE
    return scene_read32(0x5CC96u+p->id*4u);
#else
    return scene_read32(scene_read32(0xFFEF5Au)+p->id*4u);
#endif
}
static void spawn_scene(void)
{
    load_placements();s_loader_active=1;
    int camera=ram16(0xEE78),cy=ram16(0xEE7C),w=enabled()?s_requested_width:320;
    int lo,hi;activation_bounds(camera,w,&lo,&hi);
    int player=ram16(0xB010);s_pool_pressure=0;
    int ylo=(cy&~127)-128,yhi=(cy&~127)+512;
    for(unsigned i=0;i<s_placement_count;++i) {
        Placement *p=&s_placements[i];
        if((int)p->x<lo || (int)p->x>=hi)p->loaded=0;
    }
    for(unsigned pass=0;pass<s_placement_count;++pass) {
        int best=-1,distance=INT_MAX;
        for(unsigned i=0;i<s_placement_count;++i) {
            Placement *p=&s_placements[i];
            if(p->loaded || (g_ram[p->state]&128) || (int)p->x<lo || (int)p->x>=hi)continue;
            /* This cutscene's initializer loads PLC $5A into the same VRAM
             * bank that AIZ1_Resize subsequently uses for PLC $0C at camera
             * $2E00. Early wide activation reverses their native order and
             * overwrites the miniboss with vines/trees. Keep this scripted
             * object inside Load_Sprites' native 128px-aligned interval.
             * Addresses: Obj_AIZMinibossCutscene in the pinned S3/S&K lists. */
            if(((int)p->x<((camera-128)&~127) || (int)p->x>=(camera&~127)+640) &&
                placement_code(p)==S3_AIZ_MINIBOSS_CUTSCENE)continue;
            int y=p->y&4095;
            if(!(p->y&0x8000)) {
                if((int16_t)ram16(0xEE18)<0) {
                    int mask=ram16(0xEEAA),dy=(y-ylo)&mask;
                    if(dy>640)continue;
                } else if(y<ylo || y>yhi)continue;
            }
            int d=abs((int)p->x-player);
            if(d<distance){best=(int)i;distance=d;}
        }
        if(best<0)break;
        Placement *p=&s_placements[best];unsigned o;
        for(o=0xB0DE;o<0xCAE2 && scene_read32(0xFF0000u+o);o+=0x4A){}
        if(o>=0xCAE2){++s_pool_pressure;break;}
        write16(o+0x10,p->x);write16(o+0x14,p->y&4095);
        unsigned flip=(p->y>>13)&3;
        write8(o+4,flip);write8(o+0x2A,flip);write16(o+0x48,p->state);
        write8(o+0x2C,p->subtype);write8(p->state,g_ram[p->state]|128);
        write32(o,placement_code(p));
        p->loaded=1;++s_spawned;
    }
    unsigned r=0,l=0;
    while(r<s_placement_count && s_placements[r].x<(unsigned)((camera&~127)+640))++r;
    while(l<s_placement_count && (int)s_placements[l].x<((camera-128)&~127))++l;
    write32(0xF772,s_placement_base+r*6);write32(0xF776,s_placement_base+l*6);
    write16(0xF77A,0xEB00+r);write16(0xF77C,0xEB00+l);
    write16(0xF76E,camera&0xFF80);write16(0xF770,cy&0xFF80);
    write16(0xF7DA,(camera-128)&0xFF80);write16(0xEF64,(cy-128)&0xFF80);
}
static S3VideoRingLayout s_ring_layout;
void s3_video_set_ring_layout(S3VideoRingLayout fn) { s_ring_layout = fn; }
static S3VideoGround s_file_select_ground;
static uint32_t *s_fs_expect,*s_fs_repl;   /* per column: the planes' native colour, and its stand-in */
static int s_fs_capacity;
void s3_video_set_file_select_ground(S3VideoGround fn) { s_file_select_ground = fn; }
static unsigned rings_base(void)
{
    unsigned own=s_ring_layout?s_ring_layout():0;
    if(own)return own;
    unsigned index=(g_ram[0xFE10]*8u)+((g_ram[0xFE11]&1u)*4u);
    /* Ring layouts begin with an x=0 sentinel, native status index zero. */
    return scene_read32(S3_RINGS+index);
}
static unsigned rings_end(void)
{
    unsigned p=rings_base();
    if(p>=0x400000u+(g_rom_ext?g_rom_ext_size:0u))return p;
    for(unsigned n=0;n<512 && scene_read16(p)!=65535u;++n)p+=4;
    return p;
}
static void capture_objects(void)
{
    s_build.count=0;s_visible_count=0;++s_scene_tick;
    memset(s_build.ocode,0,sizeof s_build.ocode);
    s_build.scene=level_ready()?1:0;
    int cam=ram16(0xEE80),cy=ram16(0xEE84);
    s_build.camera_x=cam;s_build.camera_y=cy;
    int left=gameplay()?view_left(cam,s_requested_width):cam-(s_requested_width-320)/2;
    int mask=ram16(0xEEAA);
    if(gameplay() && g_ram[0xF711]) {
        unsigned blink=(g_ram[0xFE05]&8)==0;
        unsigned frame=blink*((ram16(0xFE20)==0?1:0)+(g_ram[0xFE23]==9?2:0));
        int hudx=(int8_t)g_ram[0xF711];if(hudx<0)hudx+=8;
        add_mapping(S3_HUD_MAP,frame,0x86CA,0,15+hudx,136,1,0);
        unsigned base=rings_base(),end=rings_end();
        for(unsigned p=base+4;p<end;p+=4) {
            unsigned status=0xE700+(p-base)/2;
            if(ram16(status)&0x8000)continue;
            int x=scene_read16(p),y=((scene_read16(p+2)-cy+8)&mask)-8;
            if(x<left-16 || x>=left+s_requested_width+16 || y<-16 || y>=240)continue;
            unsigned fr=g_ram[status+1]?g_ram[status+1]:g_ram[0xFEB3];
            unsigned m=S3_RING_MAP+fr*8;
            if(s_build.count<SCENE_SPRITES) {
                SceneSprite *q=&s_build.sprites[s_build.count++];
                q->x=x-cam+(int16_t)scene_read16(m+6);q->y=y+(int16_t)scene_read16(m);
                q->attr=scene_read16(m+4);q->size=(uint8_t)scene_read16(m+2);q->hud=0;q->rot=0;q->bank=0;q->slot=0;q->art=NULL;q->zoom=0;
            }
        }
    }
    for(unsigned pri=0;pri<8;++pri) {
        unsigned queue=0xAC00+pri*128,bytes=ram16(queue);if(bytes>126)bytes=126;
        for(unsigned k=2;k<=bytes;k+=2) {
            unsigned o=ram16(queue+k);if(o<0xB000 || o>=0xCFE0 || !scene_read32(0xFF0000u+o))continue;
            unsigned flags=g_ram[o+4],multi=flags&64;
            int x,y,anchor=0;
            if(flags&4) {
                x=(int16_t)(ram16(o+0x10)-cam);
                int radius=g_ram[o+7],height=g_ram[o+6];
                y=((ram16(o+0x14)-cy+height)&mask)-height;
                if(x+radius<left-cam || x-radius>=left-cam+s_requested_width || y+height<0 || y-height>=224)continue;
                if(s_visible_count<144)s_visible_objects[s_visible_count++]=o;
            } else {x=(int16_t)ram16(o+0x10)-128;y=(int16_t)ram16(o+0x14)-128;anchor=2;}
            unsigned map=scene_read32(0xFF0000u+o+0xC),gfx=ram16(o+0xA);
            /* The title plane is queued even beyond the hardware viewport.
             * Retain its signed position instead of the wrapped/cut SAT. */
            if((g_ram[0xF600]&127)==4 && map==S3_TITLE_PLANE_MAP)anchor=3;
            /* Data Select queues every card, including those culled by the
             * native SAT builder. Preserve their signed carousel positions. */
            if(map==S3_FILE_SELECT_MAP)anchor=4;
            s_add_rot=0;
            s_add_slot=0;
            s_add_zoom=(flags&4) && s_object_zoom && s_build.scene==1 ? (int)s_object_zoom(o) : 0;
            if((flags&4) && (o-0xB000u)/0x4Au+1<SCENE_SLOTS) {   /* (a world object: interpolation can follow it) */
                unsigned k=(o-0xB000u)/0x4Au+1;
                s_add_slot=(int)k;
                s_build.ox[k]=(int16_t)ram16(o+0x10);s_build.oy[k]=(int16_t)ram16(o+0x14);
                s_build.ocode[k]=scene_read32(0xFF0000u+o);
            }
            if((flags&4) && !multi && (o==0xB000 || o==0xB04A) && s_build.scene==1 &&
               player_turned(o,o!=0xB000,map,gfx,flags,x,y)){s_add_zoom=0;continue;}
            if(multi) {
                if(g_ram[o+0x22])add_mapping(map,g_ram[o+0x22],gfx,flags,x,y,anchor,0);
                unsigned count=ram16(o+0x16);if(count>8)count=8;
                for(unsigned n=0;n<count;++n) {
                    unsigned child=o+0x18+n*6;
                    int sx=(int16_t)ram16(child)-128,sy=(int16_t)ram16(child+2)-128;
                    if(flags&4){sx=(int16_t)(ram16(child)-cam);sy=((ram16(child+2)-cy+128)&mask)-128;}
                    add_mapping(map,g_ram[child+5],gfx,flags,sx,sy,anchor,0);
                }
            } else {
                const S3VideoArt *art=(flags&4)?player_art(o,map,g_ram[o+0x22]):NULL;
                if(art)add_art(art,gfx,flags,x,y);
                else add_mapping(map,g_ram[o+0x22],gfx,flags,x,y,anchor,(flags&32)!=0);
            }
            s_add_rot=0;s_add_slot=0;s_add_zoom=0;
        }
    }
}

static void publish_sprites(void)
{
    for(unsigned n=0;n<s_visible_count;++n) {
        unsigned o=s_visible_objects[n]; if(scene_read32(0xFF0000u+o))write8(o+4,g_ram[o+4]|128);
    }
    s_build.serial=++s_serial;memcpy(s_build.sat,g_ram+0xF800,640);
    SceneFrame *dst=&s_history[s_serial%3];
    dst->count=s_build.count;dst->serial=s_serial;dst->scene=s_build.scene;
    dst->camera_x=s_build.camera_x;dst->camera_y=s_build.camera_y;
    memcpy(dst->sat,s_build.sat,640);
    memcpy(dst->sprites,s_build.sprites,s_build.count*sizeof(SceneSprite));
    memcpy(dst->ptiles,s_build.ptiles,sizeof dst->ptiles);
    memcpy(dst->ox,s_build.ox,sizeof dst->ox);memcpy(dst->oy,s_build.oy,sizeof dst->oy);
    memcpy(dst->ocode,s_build.ocode,sizeof dst->ocode);
}
int s3_video_hook(uint32_t pc)
{
    if(pc==S3_LOAD_INIT && !g_ram[0xF76C]) {
        s_placement_base=s_placement_count=0;s_loader_active=0;
    }
    if(pc==S3_LOAD_UPDATE && gameplay() && (enabled() || s_loader_active) && g_ram[0xFE10]<19) {
        spawn_scene();return 1;
    }
    if(!enabled() || ram16(S3_COMPETITION))return 0;
    switch(pc) {
    case S3_LOAD_INIT:return 0;
    case S3_CAPTURE:capture_objects();return 0;
    case S3_PUBLISH:publish_sprites();return 0;
    case S3_RING_RANGE:
        if(gameplay()) {g_cpu.A[1]=rings_base()+4;g_cpu.A[2]=rings_end();}
        return 0;
    case S3_RING_STATUS:
        if(gameplay())g_cpu.A[4]=0xFFFFE702u;
        return 0;
    default:
        if(gameplay()) {
            int x=(uint16_t)((uint16_t)g_cpu.D[0]+ram16(0xF7DA));
            int lo,hi;activation_bounds(ram16(0xEE78),s_requested_width,&lo,&hi);
            g_cpu.D[0]=(g_cpu.D[0]&0xFFFF0000u)|((x<lo || x>=hi)?641u:320u);
        }
        return 0;
    }
}

#include "sonic3_blue_spheres.inc"

static uint8_t *s_priority;
static int s_priority_capacity;
/* Frame interpolation (opt-in; the runner presents in-between frames on a
 * high-refresh display). Each real frame records what its scanlines used:
 * per-line scroll and CRAM, the native rows, and every object's world
 * position. An in-between frame re-renders the latest frame's art with the
 * camera, background parallax and each object (matched by RAM slot and code)
 * eased from the previous frame's values. Jumps (scene changes, teleports)
 * snap to the latest frame instead. Guest state is never touched. */
enum { IX_LINES = 240, IX_NATIVE = 320 };
typedef struct {
    int16_t hs_a[IX_LINES], hs_b[IX_LINES];
    uint16_t vs_a, vs_b;
    uint16_t cram[IX_LINES][64];
    int16_t ox[SCENE_SLOTS], oy[SCENE_SLOTS];
    uint32_t ocode[SCENE_SLOTS], serial;
    int lines, width;
} IxFrame;
static IxFrame s_ix[2];                   /* [0] previous frame, [1] latest */
static uint32_t s_ix_native[IX_LINES][IX_NATIVE];
static int s_ix_nw, s_interp;
static float s_ix_t;
int s3_video_interpolating(void) { return s_interp; }
static int ix_lerp(int a, int b, int limit)   /* a -> b at s_ix_t; a jump snaps to b */
{
    int d=(int16_t)(uint16_t)(b-a);
    if(d>limit || d<-limit)return b;
    return b-d+(int)((float)d*s_ix_t+(d<0?-.5f:.5f));
}
static S3VideoActorOverlay s_actor_overlay;
void s3_video_set_actor_overlay(S3VideoActorOverlay draw) { s_actor_overlay=draw; }
static void select_scene(const GVDP *v)
{
    s_display=NULL;
    unsigned sat=(v->reg[5]&127u)<<9;
    /* Keep the front buffer's publication alive even if the producer cycles
     * through every history slot before the next native sprite DMA.
     * A publication newer than the last V-int upload cannot be on screen;
     * the unbounded pass covers SAT writes made outside V-int (loading). */
    for(int bounded=s_upload_serial!=0;bounded>=0 && !s_display;--bounded)
    for(unsigned n=0;n<4;++n) {
        const SceneFrame *p=n==3?&s_display_frame:&s_history[n];
        if(!p->serial || (s_display && p->serial<s_display->serial))continue;
        if(bounded && p->serial>s_upload_serial)continue;
        int matches=1;
        for(unsigned k=0;k<640;++k)
            if(p->sat[k]!=v->vram[(sat+k)&65535u]) {matches=0;break;}
        if(matches)s_display=p;
    }
    if(!s_display) {
        ++s_scene_misses;
        int scene=level_ready()?1:(g_ram[0xF600]&127)==0x34?2:0;
        /* An in-progress native SAT is not a custom-scene publication. Keep
         * the completed scene until its replacement is ready, without
         * flashing the native centered HUD or dropping host-only rings. */
        if(scene && s_display_frame.serial && s_display_frame.scene==scene) {
            s_display=&s_display_frame;++s_scene_holds;
        }
    }
    /* Keep the selected front buffer immutable for all displayed scanlines. */
    if(s_display && s_display!=&s_display_frame) {
        s_display_frame.count=s_display->count;s_display_frame.scene=s_display->scene;
        s_display_frame.serial=s_display->serial;
        s_display_frame.camera_x=s_display->camera_x;s_display_frame.camera_y=s_display->camera_y;
        memcpy(s_display_frame.sat,s_display->sat,sizeof s_display_frame.sat);
        memcpy(s_display_frame.sprites,s_display->sprites,s_display->count*sizeof(SceneSprite));
        memcpy(s_display_frame.ptiles,s_display->ptiles,sizeof s_display_frame.ptiles);
        memcpy(s_display_frame.ox,s_display->ox,sizeof s_display_frame.ox);
        memcpy(s_display_frame.oy,s_display->oy,sizeof s_display_frame.oy);
        memcpy(s_display_frame.ocode,s_display->ocode,sizeof s_display_frame.ocode);
        s_display=&s_display_frame;
    }
}
/* A coloured picture's pixel (ix/iy already flipped) -> 1 and its colour */
static inline int art_colour(const S3VideoArt *a,int ix,int iy,const uint32_t *palette,uint32_t *out)
{
    if(ix<0 || iy<0 || ix>=a->w || iy>=a->h)return 0;
    unsigned v=a->pix[iy*a->w+ix];
    if(!v || v>a->ncol)return 0;
    const uint8_t *e=a->col+(v-1)*6u;
    uint32_t now=palette[e[3]&63],own=genesis_dac_cram_to_argb((uint16_t)(e[4]|e[5]<<8),GENESIS_DAC_NORMAL);
    int r=e[0]+(int)((now>>16)&255)-(int)((own>>16)&255);
    int g=e[1]+(int)((now>>8)&255)-(int)((own>>8)&255);
    int b=e[2]+(int)(now&255)-(int)(own&255);
    r=r<0?0:r>255?255:r;g=g<0?0:g>255?255:g;b=b<0?0:b>255?255:b;
    *out=0xFF000000u|(uint32_t)r<<16|(uint32_t)g<<8|(uint32_t)b;
    return 1;
}
/* A piece's pixel (cell without flip bits; ix/iy already flipped). */
static inline uint8_t sprite_pixel(const GVDP *v,const SceneSprite *s,uint16_t cell,int ix,int iy)
{
    if(s->art) {
        if(ix<0 || iy<0 || ix>=s->art->w || iy>=s->art->h)return 0;
        uint8_t v=s->art->pix[iy*s->art->w+ix]&63;
        return v>15?v:v?(uint8_t)(((s->attr>>9)&48)|v):0;
    }
    if(!s->bank)return pattern_pixel(v,cell,ix,iy);
    unsigned t=cell&2047u;
    if(t>=PLAYER_TILES)return 0;
    uint8_t b=s_display->ptiles[s->bank-1][t*32u+(unsigned)(iy&7)*4u+(unsigned)(ix&7)/2u];
    int nib=(ix&1)?b&15:b>>4;
    return nib?(uint8_t)(((cell>>9)&48)|nib):0;
}
static void draw_scene_sprites(const GVDP *v,int line,uint32_t *out,int width,
                               int origin,const uint32_t *palette)
{
    if(!s_display)return;
    for(unsigned n=0;n<s_display->count;++n) {
        const SceneSprite *s=&s_display->sprites[n];
        int menu=s_display->scene==0;
        if(menu && s->hud!=(file_select_layout(v)?4:3))continue;
        int cw=((s->size>>2)&3)+1,ch=(s->size&3)+1;
        int pw=s->art?s->art->w:cw*8,ph=s->art?s->art->h:ch*8;
        int world=!s->hud && s_display->scene==1;
        int y=s->y+(world?s_display->camera_y-s_camera_y:0);
        if(!s->rot && !s->zoom && !s_interp && (line<y || line>=y+ph))continue;
        int base=s->x+(s->hud==1?0:s->hud>=2?(width-320)/2:origin);
        /* Native camera scroll may advance while a completed host frame is
         * retained. Reproject its world coordinates, not its old screen
         * coordinates; otherwise stationary rings jitter with the camera. */
        if(world)base+=s_display->camera_x-s_camera;
        if(s_interp && world && s->slot && s_ix[0].ocode[s->slot]==s_display->ocode[s->slot] &&
           s_display->ocode[s->slot]) {
            int cx=s_display->ox[s->slot],cy=s_display->oy[s->slot];
            int dx=ix_lerp(s_ix[0].ox[s->slot],cx,48)-cx,dy=ix_lerp(s_ix[0].oy[s->slot],cy,48)-cy;
            base+=dx;y+=dy;
        }
        if(s_interp && !s->rot && !s->zoom && (line<y || line>=y+ph))continue;
        if(s->rot || s->zoom) {
            /* Nearest-neighbour turn of this piece about the object's
             * centre: map each output pixel back into the upright piece. */
            static float sn[1024],cs[1024];static int ready;
            if(!ready){for(int k=0;k<1024;++k){sn[k]=(float)sin(k*6.2831853/1024);cs[k]=(float)cos(k*6.2831853/1024);}ready=1;}
            float c=cs[s->rot&1023],si=sn[s->rot&1023],zf=s->zoom?(float)s->zoom/256.f:1.f;
            float cx=(float)(s->rx+(base-s->x)),cy=(float)(s->ry+(y-s->y));
            float w=(float)pw,h=(float)ph,dy=(float)line+.5f-cy;
            float lo=1e9f,hi=-1e9f,ylo=1e9f,yhi=-1e9f;
            for(int k=0;k<4;++k) {
                float px=(float)base+((k&1)?w:0)-cx,py=(float)y+((k&2)?h:0)-cy;
                float ry=(px*si+py*c)*zf;
                if(ry<ylo)ylo=ry;
                if(ry>yhi)yhi=ry;
            }
            if(dy<ylo || dy>yhi)continue;
            /* the piece's span on this line, from its four edges */
            for(int k=0;k<4;++k) {
                float px=(float)base+((k&1)?w:0)-cx,py=(float)y+((k&2)?h:0)-cy;
                float rx=(px*c-py*si)*zf;
                if(rx<lo)lo=rx;
                if(rx>hi)hi=rx;
            }
            int x0=(int)floorf(cx+lo),x1=(int)ceilf(cx+hi);
            if(x0<0)x0=0;
            if(x1>width)x1=width;
            for(int x=x0;x<x1;++x) {
                if(s_priority[x]&2)continue;
                if(menu && x>=origin && x<origin+320)continue;
                float dx=(float)x+.5f-cx;
                float ux=(dx*c+dy*si)/zf+cx-(float)base,uy=(-dx*si+dy*c)/zf+cy-(float)y;
                if(ux<0 || uy<0 || ux>=w || uy>=h)continue;
                int ix=(int)ux,jy=(int)uy;
                if(s->attr&0x1000)jy=ph-1-jy;
                if(s->attr&0x800)ix=pw-1-ix;
                if(s->art && s->art->ncol) {
                    uint32_t col;
                    if(!art_colour(s->art,ix,jy,palette,&col))continue;
                    s_priority[x]|=2;
                    if((s->attr&0x8000) || !(s_priority[x]&1))out[x]=col;
                    continue;
                }
                uint16_t cell=(uint16_t)((s->attr&0xE000u)|
                    (((s->attr&2047u)+(unsigned)(ix/8*ch+jy/8))&2047u));
                uint8_t p=sprite_pixel(v,s,cell,ix,jy);
                if(!p)continue;
                s_priority[x]|=2;
                int ext=p<16 && !s->bank && !s->art?gvdp_ext_line(cell&2047u):0;
                uint32_t col=ext?genesis_dac_cram_to_argb(gvdp_ext_color(line,ext,p),GENESIS_DAC_NORMAL):palette[p];
                if(s->hud==1 || s->hud==2 || (s->attr&0x8000) || !(s_priority[x]&1))out[x]=col;
            }
            continue;
        }
        int iy=line-y;
        if(s->attr&0x1000)iy=ph-1-iy;
        for(int i=0;i<pw;++i) {
            int x=base+i;
            if(x<0 || x>=width || (s_priority[x]&2))continue;
            if(menu && x>=origin && x<origin+320)continue;
            int ix=(s->attr&0x800)?pw-1-i:i;
            if(s->art && s->art->ncol) {
                uint32_t col;
                if(!art_colour(s->art,ix,iy,palette,&col))continue;
                s_priority[x]|=2;
                if((s->attr&0x8000) || !(s_priority[x]&1))out[x]=col;
                continue;
            }
            uint16_t cell=(uint16_t)((s->attr&0xE000u)|
                (((s->attr&2047u)+(unsigned)(ix/8*ch+iy/8))&2047u));
            uint8_t p=sprite_pixel(v,s,cell,ix,iy);
            if(!p)continue;
            s_priority[x]|=2;
            /* extended sprite lines (game-added art keeping its own colours) */
            int ext=p<16 && !s->bank && !s->art?gvdp_ext_line(cell&2047u):0;
            uint32_t c=ext?genesis_dac_cram_to_argb(gvdp_ext_color(line,ext,p),GENESIS_DAC_NORMAL):palette[p];
            if(s->hud==1 || s->hud==2 || (s->attr&0x8000) || !(s_priority[x]&1))out[x]=c;
        }
    }
}
static int attributes_differ(const GVDP *v,uint16_t a,uint16_t b,int x,int y)
{
    if(a==b)return 0;
    /* Compare visible pixels/priority, not irrelevant transparent-tile flags. */
    for(int i=0;i<8;++i) {
        uint8_t ap=pattern_pixel(v,a,x+i,y),bp=pattern_pixel(v,b,x+i,y);
        if(ap!=bp || (ap && ((a^b)&0x8000)))return 1;
    }
    return 0;
}
static void aiz_palette(const AizArt *art,const GVDP *v,uint32_t *out)
{
    /* Follow fades without borrowing the cutscene's unrelated colour banks.
     * AIZ's common black/white entries also cover fades to/from white. */
    for(unsigned i=0;i<64;++i) {
        unsigned color=i<16?v->cram[i]:art->colors[i-16],faded=0;
        for(unsigned shift=0;shift<12;shift+=4) {
            int lo=(v->cram[16]>>shift)&14,hi=(v->cram[17]>>shift)&14;
            int c=lo+(int)((color>>shift)&14)*(hi-lo)/14;
            faded|=(unsigned)c<<shift;
        }
        out[i]=genesis_dac_cram_to_argb((uint16_t)faded,GENESIS_DAC_NORMAL);
    }
}


/* Plane B for one line, sampled identically by the composite and by the
 * background blur's look-ahead (lines below the beam, from the frame's
 * latched VDP snapshot). Frame-wide flags come from the line-0 copies. */
typedef struct {
    int level,special,file_select,bg_world,bg_canvas,aiz_fire,aiz_banks;
    int origin,nw,hs_b,by,bg_width;
    unsigned base_b;
} BgLine;
static void bg_line_setup(const GVDP *v,int line,int level,int special,int file_select,
                          int origin,int nw,BgLine *L)
{
    int bg_world=level && !special;
    L->level=level;L->special=special;L->file_select=file_select;L->origin=origin;L->nw=nw;
    L->base_b=(v->reg[4]&7u)<<13;
    unsigned hsbase=(v->reg[13]&63u)<<10;
    int hmode=v->reg[11]&3,hi=hmode==0?0:hmode==2?line&~7:line;
    int hs_b=(int16_t)vram16(v,hsbase+(unsigned)hi*4+2);
    if(s_interp && line<IX_LINES)hs_b=ix_lerp(s_ix[0].hs_b[line],s_ix[1].hs_b[line],128);
    /* The fire is a streamed Plane B composition, not the background at
     * the world's current layout coordinates. AIZ1BGE_FireTransition
     * writes rows from layout X=$1000 while replacing foreground blocks;
     * AIZ2BGE_WaitFire continues at X=$200 before a gradual BG redraw.
     * Follow the uploaded name table through that redraw so the flames
     * cover the same in-progress art replacement as on the native VDP. */
    int aiz_fire=bg_world && s_frame_zone==0 &&
        ((s_frame_act==0 && s_frame_bg_event>=0xC && s_frame_bg_event<=0x14) ||
         (s_frame_act==1 && s_frame_bg_event<=8));
    /* FBZ streams complete 512px rows from one of two layout canvases
     * (X=0 indoors, X=$200 outdoors). Its doorway events replace that
     * canvas a row/column at a time. The uploaded plane is authoritative
     * for both the selected bank and every mixed transition frame. */
    int fbz_canvas=bg_world && s_frame_zone==4;
    /* ICZ1's outdoor bank starts at layout X=$1880; the snow and cave
     * entrance then replace it in strips. ICZ2 also swaps complete indoor
     * and outdoor canvases. Keep those uploaded compositions through each
     * redraw rather than treating their source banks as adjacent scenery. */
    int icz_canvas=bg_world && s_frame_zone==5 &&
        (s_frame_act==1 || s_frame_bg_event<0x10);
    /* SSZ1's cloud band is a separate 512px bank at X=$1C00; entry and
     * exit replace it progressively. DEZ3's auto-scrolling runway is a
     * live Plane B canvas edited by the boss, independent of its layout. */
    int ssz_canvas=bg_world && s_frame_zone==10 && !s_frame_act && s_frame_bg_event;
    int dez3_canvas=bg_world && s_frame_zone==23;
    int bg_canvas=fbz_canvas || icz_canvas || ssz_canvas || dez3_canvas;
    int aiz_banks=bg_world && !s_frame_zone && !s_frame_act &&
        s_frame_bg_event<=8 && s_aiz_art_ready;
    int vs_b=s_interp?ix_lerp(s_ix[0].vs_b,s_ix[1].vs_b,128):v->vsram[1];
    int by=bg_world?unwrap(vs_b,s_frame_bg_y)+line:line+(vs_b&1023);
    int bg_width=bg_world?((s_world_frame[0x8002]<<8)|s_world_frame[0x8003])*128:0;
    /* AIZ1's upper parallax bands are a fixed 512px canvas. Its layout
     * also stores the intro coastline and fire art farther right: those
     * are separate compositions, not the repeat period of the forest. */
    if(bg_world && s_frame_zone==0 && s_frame_act==0) {
        if(by<0x220)bg_width=512;
        else if(by<0x380)bg_width=30*128; /* lake section, before the fire banks */
        /* The flying intro starts with a negative virtual BG camera.
         * Its streamer clamps that camera to zero, leaving the first 512px
         * canvas in VRAM while hardware HScroll wraps through it. */
        if(s_frame_bg_event==0 && hs_b>0)bg_width=512;
    }
    /* AIZ2BGE_Normal streams whole 512px rows from layout X=0 (d1=0,
     * d6=$20), then scrolls that repeating canvas for its heat shimmer.
     * The rest of the layout contains the outgoing fire and other scenes;
     * treating it as one wide world reads unrelated tile banks. */
    if(bg_world && s_frame_zone==0 && s_frame_act==1 && s_frame_bg_event>=8)
        bg_width=512;
    /* SOZ1's desert also uses Draw_TileRow(X=0, d6=$20). The rest
     * of its layout belongs to the rising pyramid/boss presentation. */
    if(bg_world && s_frame_zone==8 && s_frame_act==0 && s_frame_bg_event==0)
        bg_width=512;
    /* CNZ's normal city parallax is another X=0, 32-block row. Its other
     * layout banks are used as collidable scenery during the act-1 boss. */
    if(bg_world && s_frame_zone==3 &&
       ((!s_frame_act && !s_frame_bg_event) || (s_frame_act==1 && s_frame_bg_event>=4)))
        bg_width=512;
    /* HCZ2's crushing wall is a moving world, not a repeating panorama.
     * Negative coordinates to its left must remain blank. */
    if(bg_world && ((s_frame_zone==1 && s_frame_act==1 && s_frame_bg_event<=4) ||
       s_frame_zone==22))
        bg_width=0;
    /* LRZ3's distant lava cavern occupies the first 512px. Its upper
     * parallax camera stays within that panorama; the remaining layout
     * is the close, collidable boss arena entered by the later events. */
    if(bg_world && s_frame_zone==22 && !s_frame_act && !s_frame_bg_event)
        bg_width=512;
    L->bg_world=bg_world;L->aiz_fire=aiz_fire;L->bg_canvas=bg_canvas;L->aiz_banks=aiz_banks;
    L->hs_b=hs_b;L->by=by;L->bg_width=bg_width;
}
/* -> pixel (0 transparent); *attr its cell, *pal 0 live, 1 AIZ intro, 2 AIZ forest */
static uint8_t bg_sample(const GVDP *v,const BgLine *L,int x,int line,uint16_t *attr,int *pal)
{
    int nx=x-L->origin,nw=L->nw,by=L->by;
    int reference_bx=nx-L->hs_b,bx=reference_bx;
    /* Title/menu backgrounds only initialize the visible 40 tile
     * columns; repeat that composition, not the stale 512px name table. */
    if(!L->level && !L->special && !L->file_select){bx%=nw;if(bx<0)bx+=nw;}
    int background_x=bx;
    if(L->bg_width>0) { background_x%=L->bg_width;if(background_x<0)background_x+=L->bg_width; }
    uint16_t b=L->bg_world?world_attr(s_background_frame,background_x,by,1):plane_attr(v,L->base_b,bx,by);
    uint8_t bp=pattern_pixel(v,b,L->bg_world?background_x:bx,by);
    *pal=0;
    if(L->file_select && (bx<0 || bx>=1024))bp=0;
    if(L->bg_canvas) {
        b=plane_attr(v,L->base_b,reference_bx,by);
        bp=pattern_pixel(v,b,reference_bx,by);
    }
    if(L->aiz_fire) {
        unsigned vs=1;
        if(v->reg[11]&4) {
            /* AIZTrans_WavyFlame advances its 16-entry wave by two
             * per 16px column: extend that eight-column period into
             * either margin. Keep every native column verbatim. */
            unsigned col=nx>=0 && nx<nw?(unsigned)nx/16:((unsigned)nx/16)&7;
            vs=col*2+1;
        }
        int fire_y=line+(v->vsram[vs]&1023);
        b=plane_attr(v,L->base_b,reference_bx,fire_y);
        bp=pattern_pixel(v,b,reference_bx,fire_y);
    }
    if(L->aiz_banks && (nx<0 || nx>=nw) && s_frame_bg_event==0) {
        /* The coast behind the hollow tree and the forest ahead need their
         * own decoded banks beyond the native viewport (see scanline). */
        const AizArt *art=by>=0x380?&s_aiz_intro:&s_aiz_main;
        b=world_attr_blocks(s_background_frame,art->blocks,background_x,by,1);
        bp=pattern_pixel(&art->video,b,background_x,by);
        *pal=by>=0x380?1:2;
    }
    *attr=b;
    return bp;
}

/* Background blur (trilogy option): a depth of field. Plane B and the
 * backdrop are softened by a 2D Gaussian behind the level, before the
 * sprites go on; the level's own plane stays sharp. Each line's strength
 * follows its parallax, measured from how far its scroll moves against the
 * camera: scenery that barely moves (far mountains, sky) gets the whole
 * radius, a band keeping pace with the foreground stays almost sharp.
 * Lines below the beam come from the frame's latched VDP snapshot through
 * bg_sample, exactly as the composite will draw them. Their colours resolve
 * with the palette live when they are used, so a mid-frame palette change
 * (the water line) only affects the few lines the kernel overlaps. */
enum { BLUR_LINES=224, BLUR_R=9 };
static int s_bg_blur;
void s3_video_set_bg_blur(int level) { s_bg_blur = level < 0 ? 0 : level > 3 ? 3 : level; }
/* Per line and column: the background sample (reused by the composite), then
 * its horizontally blurred colour as three planes, so both passes are plain
 * multiply-adds over arrays (vectorised). */
static uint16_t *s_blur_idx;                  /* (palette set << 8) | pixel, 0 backdrop */
static uint16_t *s_blur_attr;                 /* the cell (priority) of that sample */
static uint8_t *s_blur_h[3];                  /* r, g, b after the horizontal pass */
static uint8_t *s_blur_mask;                  /* this line: background visible */
static uint16_t *s_blur_pad[3];               /* one line of colour, edge-padded by BLUR_R */
static uint32_t *s_blur_acc[3];
static int s_blur_cap,s_blur_sampled,s_blur_active;
static unsigned s_blur_frame;                  /* s_frames when this frame's look-ahead began */
static unsigned s_blur_gen,s_blur_hgen[BLUR_LINES];
static uint32_t s_blur_pal[3*64],s_blur_backdrop;
static float s_blur_depth[BLUR_LINES];        /* 0 far .. 1 moves with the foreground */
static int s_blur_prev_bg[BLUR_LINES],s_blur_prev_cam,s_blur_dcam,s_blur_zone=-1;

static int blur_alloc(int width)
{
    if(width<=s_blur_cap)return 1;
    size_t n=(size_t)width*BLUR_LINES;
    int ok=1;
#define BLUR_GROW(ptr,count) do{void *q_=realloc(ptr,(count)*sizeof *(ptr));if(q_)(ptr)=q_;else ok=0;}while(0)
    BLUR_GROW(s_blur_idx,n);BLUR_GROW(s_blur_attr,n);BLUR_GROW(s_blur_mask,(size_t)width);
    for(int c=0;c<3;++c){BLUR_GROW(s_blur_h[c],n);BLUR_GROW(s_blur_pad[c],(size_t)width+2*BLUR_R);BLUR_GROW(s_blur_acc[c],(size_t)width);}
#undef BLUR_GROW
    if(!ok)return 0;
    s_blur_cap=width;
    return 1;
}
static float blur_sigma(int line)
{
    static const float base[4]={0,1.3f,2.4f,3.8f};      /* LOW, MEDIUM, HIGH: farthest layer */
    float k=s_blur_depth[line];
    float f=1-(k<0?0:k>1?1:k);
    return base[s_bg_blur]*(f<.15f?.15f:f);
}
/* normalised Gaussian taps (16.16) for sigma, cached by quarter pixel */
static const uint32_t *blur_kernel(float sigma,int *radius)
{
    static uint32_t taps[64][2*BLUR_R+1];
    static int8_t r[64],ready[64];
    int q=(int)(sigma*4+.5f);
    if(q<1)q=1;
    if(q>63)q=63;
    if(!ready[q]) {
        float sg=q/4.0f,w[2*BLUR_R+1],sum=0;
        int rr=(int)ceilf(sg*2.5f);
        if(rr>BLUR_R)rr=BLUR_R;
        for(int i=-rr;i<=rr;++i){w[i+rr]=expf(-(float)(i*i)/(2*sg*sg));sum+=w[i+rr];}
        uint32_t total=0;
        for(int i=0;i<=2*rr;++i){taps[q][i]=(uint32_t)(w[i]/sum*65536+.5f);total+=taps[q][i];}
        taps[q][rr]+=65536u-total;                            /* exact unit gain */
        r[q]=(int8_t)rr;ready[q]=1;
    }
    *radius=r[q];
    return taps[q];
}
static void blur_sample_line(const GVDP *v,int l,int width,const BgLine *cur)
{
    BgLine L;
    bg_line_setup(v,l,cur->level,cur->special,cur->file_select,cur->origin,cur->nw,&L);
    uint16_t *dst=s_blur_idx+(size_t)l*width,*at=s_blur_attr+(size_t)l*width;
    for(int x=0;x<width;++x) {
        int pal;
        uint8_t p=bg_sample(v,&L,x,l,&at[x],&pal);
        dst[x]=p?(uint16_t)((pal<<8)|p):0;
    }
}
static void blur_hline(int l,int width)
{
    const uint16_t *src=s_blur_idx+(size_t)l*width;
    uint16_t *pr=s_blur_pad[0]+BLUR_R,*pg=s_blur_pad[1]+BLUR_R,*pb=s_blur_pad[2]+BLUR_R;
    for(int x=0;x<width;++x) {
        uint16_t i=src[x];
        uint32_t c=i?s_blur_pal[(i>>8)*64+(i&63)]:s_blur_backdrop;
        pr[x]=(uint16_t)((c>>16)&255);pg[x]=(uint16_t)((c>>8)&255);pb[x]=(uint16_t)(c&255);
    }
    for(int k=1;k<=BLUR_R;++k) {
        pr[-k]=pr[0];pg[-k]=pg[0];pb[-k]=pb[0];
        pr[width-1+k]=pr[width-1];pg[width-1+k]=pg[width-1];pb[width-1+k]=pb[width-1];
    }
    int r;const uint32_t *k=blur_kernel(blur_sigma(l),&r);
    for(int c=0;c<3;++c) {
        uint32_t *__restrict acc=s_blur_acc[c];
        const uint16_t *__restrict in=s_blur_pad[c]+BLUR_R;
        uint8_t *__restrict out=s_blur_h[c]+(size_t)l*width;
        for(int x=0;x<width;++x)acc[x]=32768u;
        for(int i=-r;i<=r;++i) {
            uint32_t w=k[i+r];
            const uint16_t *__restrict p=in+i;
            for(int x=0;x<width;++x)acc[x]+=w*p[x];
        }
        for(int x=0;x<width;++x)out[x]=(uint8_t)(acc[x]>>16);
    }
    s_blur_hgen[l]=s_blur_gen;
}
/* Before the composite of `line`: track its parallax, and make sure every
 * line the vertical kernel reaches has been sampled and blurred across. */
static int blur_prepare(const GVDP *v,int line,int width,const BgLine *L,int camera,
                        const uint32_t *palette,const uint32_t *intro,const uint32_t *forest,uint32_t backdrop)
{
    s_blur_active=0;
    if(!blur_alloc(width) || line>=BLUR_LINES)return 0;
    if(line==0 && s_interp) {
        s_blur_sampled=-1;s_blur_frame=s_frames;
        ++s_blur_gen;
    } else if(line==0) {
        s_blur_sampled=-1;s_blur_frame=s_frames;
        s_blur_dcam=camera-s_blur_prev_cam;
        s_blur_prev_cam=camera;
        if((int)s_frame_zone!=s_blur_zone || abs(s_blur_dcam)>64) {   /* new stage or a jump: start over */
            s_blur_zone=(int)s_frame_zone;s_blur_dcam=0;
            for(int i=0;i<BLUR_LINES;++i)s_blur_depth[i]=.5f;
        }
        ++s_blur_gen;
    } else if(s_blur_frame!=s_frames || s_blur_sampled<line)return 0;   /* (switched on mid-frame) */
    /* parallax of this line: background scroll moved per camera pixel */
    int bgx=-L->hs_b;
    if(s_blur_dcam && !s_interp) {
        float k=(float)(int16_t)(bgx-s_blur_prev_bg[line])/(float)s_blur_dcam;
        k=k<0?0:k>1.5f?1.5f:k;
        s_blur_depth[line]+=(k-s_blur_depth[line])*.2f;
    }
    if(!s_interp)s_blur_prev_bg[line]=bgx;
    /* the palette generation: a change mid-frame re-resolves lines from here down */
    int changed=s_blur_backdrop!=backdrop || memcmp(s_blur_pal,palette,64*sizeof *palette) ||
        (intro && memcmp(s_blur_pal+64,intro,64*sizeof *intro)) ||
        (forest && memcmp(s_blur_pal+128,forest,64*sizeof *forest));
    if(changed) {
        memcpy(s_blur_pal,palette,64*sizeof *palette);
        if(intro)memcpy(s_blur_pal+64,intro,64*sizeof *intro);
        if(forest)memcpy(s_blur_pal+128,forest,64*sizeof *forest);
        s_blur_backdrop=backdrop;
        if(line)++s_blur_gen;
    }
    int rv;blur_kernel(blur_sigma(line),&rv);
    int last=line+BLUR_R;
    if(last>=BLUR_LINES)last=BLUR_LINES-1;
    while(s_blur_sampled<last)blur_sample_line(v,++s_blur_sampled,width,L);
    /* Lines above were each blurred across when they were the current line,
     * with their own palette; only this line and those below can be stale. */
    for(int l=line;l<=last && l<=line+rv;++l)
        if(s_blur_hgen[l]!=s_blur_gen)blur_hline(l,width);
    s_blur_active=1;
    return 1;
}
/* The composite's own background sample for this line (taken by the look-ahead). */
static inline uint8_t blur_cached(int line,int x,int width,uint16_t *attr,int *pal)
{
    size_t i=(size_t)line*width+x;
    uint16_t v=s_blur_idx[i];
    *attr=s_blur_attr[i];*pal=v>>8;
    return (uint8_t)(v&255);
}
/* After the composite: the vertical half, on the background's pixels only. */
static void blur_apply(int line,uint32_t *out,int width)
{
    int r;const uint32_t *k=blur_kernel(blur_sigma(line),&r);
    for(int c=0;c<3;++c) {
        uint32_t *__restrict acc=s_blur_acc[c];
        for(int x=0;x<width;++x)acc[x]=32768u;
        for(int j=-r;j<=r;++j) {
            int l=line+j;l=l<0?0:l>=BLUR_LINES?BLUR_LINES-1:l;
            uint32_t w=k[j+r];
            const uint8_t *__restrict h=s_blur_h[c]+(size_t)l*width;
            for(int x=0;x<width;++x)acc[x]+=w*h[x];
        }
    }
    const uint32_t *ar=s_blur_acc[0],*ag=s_blur_acc[1],*ab=s_blur_acc[2];
    for(int x=0;x<width;++x)
        if(s_blur_mask[x])
            out[x]=(out[x]&0xFF000000u)|((ar[x]>>16)<<16)|((ag[x]>>16)<<8)|(ab[x]>>16);
}

static void scanline(const GVDP *v, int line, const uint32_t *native, int nw,
                     uint32_t *out, int width)
{
    /* Publish one immutable art/scroll snapshot per frame. Palettes stay
     * live so water-line HBlank colour changes are retained. */
    uint32_t palette[64],shadow[64],intro_palette[64],forest_palette[64];
    int uniform_palette=1,ixl=line<IX_LINES?line:IX_LINES-1;
    const uint16_t *cram=s_interp?s_ix[1].cram[ixl]:v->cram;
    for(int i=0;i<64;++i) {
        palette[i]=genesis_dac_cram_to_argb(cram[i],GENESIS_DAC_NORMAL);
        shadow[i]=genesis_dac_cram_to_argb(cram[i],GENESIS_DAC_SHADOW);
        if(palette[i]!=palette[0])uniform_palette=0;
    }
    if(!s_interp) {                          /* (record what this real frame uses) */
        if(line==0)s_ix[0]=s_ix[1];
        memcpy(s_ix[1].cram[ixl],v->cram,sizeof s_ix[1].cram[ixl]);
        if(nw<=IX_NATIVE)memcpy(s_ix_native[ixl],native,(size_t)nw*sizeof *native);
        s_ix_nw=nw;s_ix[1].lines=line+1;s_ix[1].width=width;
    }
    if(line==0 && !s_interp) {
        memcpy(&s_video_frame,v,sizeof s_video_frame);
        s_ix[1].vs_a=s_video_frame.vsram[0];s_ix[1].vs_b=s_video_frame.vsram[1];
        memcpy(s_world_frame,g_ram,sizeof s_world_frame);
        /* HUD visibility is not level readiness: AIZ hides it for the whole
         * landing/emerald cutscene while displaying the real stage planes. */
        s_frame_level=level_ready();
        /* The native special-stage exit changes mode before its 60-frame
         * fade. Keep the globe until its shadow/highlight video mode ends. */
        s_frame_special=(g_ram[0xF600]&127)==0x34 ||
            (s_frame_special && (v->reg[12]&8) && (v->reg[1]&64));
        if(s_frame_special)ss_begin(v,width);
        s_frame_zone=g_ram[0xFE10];
        s_frame_act=g_ram[0xFE11];s_frame_bg_event=ram16(0xEEC2);
        s_frame_tree_reveal=ram16(0xEEC4);
        if(s_frame_level && !s_frame_zone && !s_frame_act)load_aiz_art();
        /* The intro queues the main-level block definitions before its
         * two-row-at-a-time name-table refresh. Keep the outgoing background
         * definitions intact while Kosinski is overwriting their RAM. */
        int replacing_intro=s_frame_level && s_frame_zone==0 && s_frame_act==0 &&
            s_frame_bg_event==0 && ram16(0xEEC6);
        if(!replacing_intro)memcpy(s_background_frame,s_world_frame,sizeof s_background_frame);
        s_frame_fg_x=ram16(0xEE80);s_frame_fg_y=ram16(0xEE84);s_frame_bg_y=ram16(0xEE90);
        s_frame_bg_band[0]=(int16_t)ram16(0xA800);
        s_frame_bg_band[1]=(int16_t)ram16(0xA804);
        s_width=width;s_requested_width=width;
        s_terrain_checks=s_terrain_errors=s_bg_checks=s_bg_errors=s_bg_unstreamed=0;
        s_uninitialized_scroll_lines=s_terrain_unstreamed=0;
        s_bad_x=s_bad_y=-1;s_bad_attr=s_bad_expected=0;
        s_bg_bad_x=s_bg_bad_y=-1;s_bg_bad_a=s_bg_bad_expected=0;
        if(width>s_priority_capacity) {
            uint8_t *p=(uint8_t *)realloc(s_priority,(size_t)width);
            if(p){s_priority=p;s_priority_capacity=width;}
        }
        select_scene(v);
        if(s_display) {
            memcpy(s_ix[1].ox,s_display->ox,sizeof s_ix[1].ox);
            memcpy(s_ix[1].oy,s_display->oy,sizeof s_ix[1].oy);
            memcpy(s_ix[1].ocode,s_display->ocode,sizeof s_ix[1].ocode);
            s_ix[1].serial=s_display->serial;
        } else memset(s_ix[1].ocode,0,sizeof s_ix[1].ocode);
    }
    v=&s_video_frame;
    uint32_t backdrop=palette[v->reg[7]&63];
    /* During a completed fade the game can clear/reload name tables before
     * retiring its old player/display list. A flat palette displays no stage
     * geometry and is not a valid tile-streaming comparison. Keep the full
     * canvas flat; never expose in-progress stage decompression. */
    if(uniform_palette && !(v->reg[12]&8)) {
        for(int x=0;x<width;++x)out[x]=palette[0];
        if(line==0 && !s_interp){++s_frames;s_native_x=(width-nw)/2;}
        return;
    }
    /* Display-off/fades fill the same full-width canvas, not black sidebars.
     * A rare unsupported mode also retains the selected output dimensions. */
    if((nw!=320 && nw!=256) || !(v->reg[1]&64) || (v->reg[12]&6) ||
       ((v->reg[12]&8) && !s_frame_special) || s_priority_capacity<width) {
        uint32_t fill=!(v->reg[1]&64)?native[0]:backdrop;
        for(int x=0;x<width;++x)out[x]=fill;
        int x0=(width-nw)/2;
        if(x0>=0)memcpy(out+x0,native,(size_t)nw*sizeof(uint32_t));
        if(line==0 && !s_interp){++s_fallbacks;s_native_x=x0;}
        return;
    }
    unsigned hsbase=(v->reg[13]&63u)<<10;
    int hmode=v->reg[11]&3,hi=hmode==0?0:hmode==2?line&~7:line;
    int hs_a=(int16_t)vram16(v,hsbase+(unsigned)hi*4);
    int hs_b=(int16_t)vram16(v,hsbase+(unsigned)hi*4+2);
    if(!s_interp){s_ix[1].hs_a[ixl]=(int16_t)hs_a;s_ix[1].hs_b[ixl]=(int16_t)hs_b;}
    else {hs_a=ix_lerp(s_ix[0].hs_a[ixl],hs_a,128);hs_b=ix_lerp(s_ix[0].hs_b[ixl],hs_b,128);}
    if(s_frame_special && ss_ready) {
        if(line==0 && !s_interp){++s_frames;s_native_x=(width-nw)/2;}
        ss_scanline(v,line,out,width,palette,shadow,hs_b);
        return;
    }
    int level=s_frame_level,special=s_frame_special;
    /* The next player object can exist while the title-card SAT is still
     * displayed. Do not expose the incoming level before its own display
     * list/planes reach VBlank. */
    if(s_display && s_display->scene!=1)level=0;
    int missing_scroll=0;
    int camera=level?unwrap(-hs_a,s_frame_fg_x):ram16(0xEE78);
    int camera_y=unwrap(s_interp?ix_lerp(s_ix[0].vs_a,s_ix[1].vs_a,128):v->vsram[0],s_frame_fg_y);
    if(line==0) {
        if(level)s_stage_width=stage_width();
        s_camera=camera;s_camera_y=camera_y;
        if(!s_interp)++s_frames;
        s_left=level?view_left(camera,width):camera-(width-nw)/2;
        if(level && !s_interp && s_display && s_display->scene==1 && getenv("TRILOGY_SPRLOG") &&
           (s_display->camera_x!=camera || s_display->camera_y!=camera_y))   /* development */
            fprintf(stderr,"[SPR] f%u serial %u/%u cam %d,%d shown %d,%d misses %u holds %u\n",s_frames,s_display->serial,s_serial,
                    s_display->camera_x,s_display->camera_y,camera,camera_y,s_scene_misses,s_scene_holds);
    }
    int origin=level?s_camera-s_left:(width-nw)/2;
    s_native_x=origin;
    unsigned base_a=(v->reg[2]&56u)<<10,base_b=(v->reg[4]&7u)<<13;
    /* SaveScreen reverses the usual plane roles: the 320px portrait lives
     * on the full-screen Window ($E000, 64 tiles per row), while B ($C000)
     * holds the complete 1024px card strip (128 tiles per row).
     * Use the installed layout through both entry and exit fades. */
    int file_select=!level && file_select_layout(v);
    int wy=camera_y+line;
    BgLine L;
    bg_line_setup(v,line,level,special,file_select,origin,nw,&L);
    int bg_world=L.bg_world,aiz_fire=L.aiz_fire,bg_canvas=L.bg_canvas,aiz_banks=L.aiz_banks;
    int by=L.by,bg_width=L.bg_width;
    /* FireRefresh builds a temporary foreground from layout X=$180,
     * then AIZ2BGE_FireRedraw installs the incoming stage. RAM's current
     * chunks no longer describe the displayed foreground in between. */
    int aiz_fire_fg=aiz_fire &&
        ((s_frame_act==0 && s_frame_bg_event>=0x10) ||
         (s_frame_act==1 && s_frame_bg_event==0));
    if(aiz_banks){aiz_palette(&s_aiz_intro,v,intro_palette);aiz_palette(&s_aiz_main,v,forest_palette);}
    int blur=s_bg_blur && level && blur_prepare(v,line,width,&L,camera,palette,
        aiz_banks?intro_palette:NULL,aiz_banks?forest_palette:NULL,backdrop);
    /* A game-supplied Data Select ground stands in for the Window (the
     * portrait and its DATA SELECT title) and the backdrop. The card strip
     * and every sprite stay the cartridge's: in the native picture, a pixel
     * is replaced only where the planes showed Window or backdrop and no
     * sprite covered it. */
    const uint32_t *ground=file_select && s_file_select_ground?s_file_select_ground(line,width):NULL;
    if(ground && width>s_fs_capacity) {
        uint32_t *e=(uint32_t *)realloc(s_fs_expect,(size_t)width*4u),*r=e?(uint32_t *)realloc(s_fs_repl,(size_t)width*4u):NULL;
        if(e)s_fs_expect=e;
        if(r){s_fs_repl=r;s_fs_capacity=width;}
    }
    if(width>s_fs_capacity)ground=NULL;
    for(int x=0;x<width;++x) {
        int nx=x-origin,wx=level?nx+camera:nx-hs_a;
        int reference_bx=nx-hs_b;
        uint16_t a=level?world_attr(s_world_frame,wx,wy,0):plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
        uint16_t b;int bpal;
        uint8_t bp=blur?blur_cached(line,x,width,&b,&bpal):bg_sample(v,&L,x,line,&b,&bpal);
        uint8_t ap=pattern_pixel(v,a,level?wx:nx-hs_a,level?wy:line+(v->vsram[0]&1023));
        if(file_select) {
            /* Reflect only the portrait at its edges, retaining its native
             * pixel scale and a continuous backdrop at every aspect ratio.
             * The carousel is finite: never wrap Delete back to No Save. */
            int ax=nx%(2*nw);if(ax<0)ax+=2*nw;
            if(ax>=nw)ax=2*nw-1-ax;
            a=vram16(v,0xE000u+(unsigned)(line/8*64+ax/8)*2u);
            ap=pattern_pixel(v,a,ax,line);
            if(ground) {
                int fg=ap && ((a&0x8000) || !bp || !(b&0x8000));
                uint8_t p=fg?ap:bp;
                s_fs_expect[x]=p?palette[p]:backdrop;
                ap=0;fg=0;                          /* the Window (portrait, title) goes */
                s_fs_repl[x]=fg?palette[ap]:bp?palette[bp]:ground[x];
                s_priority[x]=(uint8_t)(((a&0x8000)&&ap)||((b&0x8000)&&bp));
                out[x]=s_fs_repl[x];
                continue;
            }
        }
        const uint32_t *apalette=palette;
        const uint32_t *bpalette=bpal==1?intro_palette:bpal==2?forest_palette:palette;
        if(aiz_fire_fg) {
            a=plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
            ap=pattern_pixel(v,a,nx-hs_a,line+(v->vsram[0]&1023));
        }
        if(aiz_banks && (nx<0 || nx>=nw)) {
            /* The hollow tree at $1300 shares primary blocks across both
             * banks. The coast behind it and forest ahead need independent
             * art. Only extend beyond the native viewport; its live animated
             * art, palette changes and scripted reveals remain authoritative. */
            int intro=wx<0x1300;
            const AizArt *art=intro?&s_aiz_intro:&s_aiz_main;
            if(intro || s_frame_bg_event<8) {
                a=world_attr_blocks(s_world_frame,art->blocks,wx,wy,0);
                ap=pattern_pixel(&art->video,a,wx,wy);
                apalette=intro?intro_palette:forest_palette;
            }
            /* (its background half is in bg_sample) */
        }
        /* AIZ1_ScreenEvent / AIZ_TreeReveal selectively upload the hollow
         * tree's 16px blocks before committing whole 128px layout chunks.
         * Its locked camera keeps the entire 256px reveal strip resident
         * in Plane A. Use that published composition, including its mask
         * and priority, instead of jumping between the coarse chunk edits.
         * The rest of the expanded world still comes from its own layout. */
        if(bg_world && !s_frame_zone && !s_frame_act && s_frame_tree_reveal &&
           wx>=0x2C80 && wx<0x2D80 && wy>=0x280 && wy<0x480) {
            int ax=nx-hs_a,ay=line+(v->vsram[0]&1023);
            a=plane_attr(v,base_a,ax,ay);
            ap=pattern_pixel(v,a,ax,ay);
            apalette=palette;
        }
        if(level && (wx<0 || wx>=s_stage_width))ap=0;
        /* Menus have a centered foreground composition, with full-width
         * scenery underneath. Special-stage planes are repeating art. */
        if(!level && !file_select && (nx<0 || nx>=nw))ap=0;
        s_priority[x]=(uint8_t)(((a&0x8000)&&ap)||((b&0x8000)&&bp));
        /* The authored transition viewport has no high-priority tree
         * canopy in front of its flame curtain. Expanded margins do: let
         * the opaque high-priority flames cover that extra scenery too.
         * Keep native overlap ordering and transparent flame edges intact. */
        int fire_curtain=aiz_fire && (nx<0 || nx>=nw) && (b&0x8000) && bp;
        int foreground=ap && !fire_curtain && ((a&0x8000) || !bp || !(b&0x8000));
        uint8_t p=foreground?ap:bp;
        out[x]=p?(foreground?apalette[p]:bpalette[p]):backdrop;
        if(p && p<16) {   /* extended line on a line-0 plane tile (game-added art keeping its own colours) */
            int ext=gvdp_ext_line((foreground?a:b)&2047u);
            if(ext)out[x]=genesis_dac_cram_to_argb(gvdp_ext_color(line,ext,p),GENESIS_DAC_NORMAL);
        }
        if(blur)s_blur_mask[x]=(uint8_t)!foreground;
        /* Outside a native special-stage projection only planes are drawn,
         * so plane priority alone
         * determines shadow; native operator sprites remain in the center. */
        if(special && (v->reg[12]&8) && !s_priority[x])
            out[x]=shadow[p?p:v->reg[7]&63];
        if(nx>=0 && nx<nw && !(x&7) && !missing_scroll && !s_interp) {
            if(level && !native_terrain_streamed(wy,s_frame_fg_y)) {
                ++s_terrain_unstreamed;
            } else if(level && !aiz_fire_fg) {
                uint16_t expected=plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
                ++s_terrain_checks;
                int bad=attributes_differ(v,a,expected,wx,wy);
                if(bad && s_bad_y<0){s_bad_x=wx;s_bad_y=line;s_bad_attr=a;s_bad_expected=expected;s_bad_world_y=wy;s_bad_hscroll=hs_a;}
                s_terrain_errors+=bad;
            }
            if(bg_world && s_frame_zone==0 && s_frame_act==0 && s_frame_bg_event==4) {
                /* Native intro->forest refresh uploads two rows per frame.
                 * Until finished, the old name table is not an oracle for
                 * the new world layout, which the opt-in renderer shows. */
                ++s_bg_unstreamed;
            } else if(bg_canvas) {
                /* A streamed composition has no single world-layout bank
                 * to compare against during its directional wipes. */
                ++s_bg_unstreamed;
            } else if(bg_world && s_frame_zone==22 && s_frame_act==1 &&
                      (reference_bx<(s_frame_bg_band[by<512?0:1]&~15) ||
                       reference_bx>=(s_frame_bg_band[by<512?0:1]&~15)+336)) {
                /* HPZ_BGDrawArray streams two bands (split at Y=$200),
                 * while HPZ_BGDeformArray scrolls ten finer bands. Some
                 * fine-band pixels can see outside Draw_TileColumn's
                 * 21-block interval. Those stale native cells are not a
                 * reference for the expanded world's actual layout. */
                ++s_bg_unstreamed;
            } else if(bg_world && !aiz_fire) {
                int rx=reference_bx;
                if(bg_width>0){rx%=bg_width;if(rx<0)rx+=bg_width;}
                uint16_t expected=plane_attr(v,base_b,reference_bx,by);
                ++s_bg_checks;
                uint16_t actual=world_attr(s_background_frame,rx,by,1);
                int bad=attributes_differ(v,actual,expected,reference_bx,by);
                if(bad && s_bg_bad_x<0){s_bg_bad_x=rx;s_bg_bad_y=by;s_bg_bad_a=actual;s_bg_bad_expected=expected;}
                s_bg_errors+=bad;
            }

        }
    }
    if(blur)blur_apply(line,out,width);
    if(!level) {
        /* Logos/copyright/menus remain centered at their original pixel
         * scale. The full-width background follows the live scene palette. */
        if(origin>=0)memcpy(out+origin,native,(size_t)nw*sizeof(uint32_t));
        if(ground)
            for(int x=origin<0?0:origin;x<origin+nw && x<width;++x)
                if(out[x]==s_fs_expect[x])out[x]=s_fs_repl[x];
        if(((g_ram[0xF600]&127)==4 || file_select) && s_display && s_display->scene==0)
            draw_scene_sprites(v,line,out,width,origin,palette);
        if((g_ram[0xF600]&127)==4 && s_actor_overlay)
            s_actor_overlay(v,line,out,width,origin,palette,s_priority,s_camera,s_camera_y);
    } else if(s_display) {
        draw_scene_sprites(v,line,out,width,origin,palette);
        if(s_actor_overlay)s_actor_overlay(v,line,out,width,origin,palette,s_priority,s_camera,s_camera_y);
    } else {
        /* No matching DMA yet (e.g. load/fade): preserve the live native
         * scene while the host display list catches its next publication. */
        int first=origin<0?-origin:0,last=origin+nw>width?width-origin:nw;
        if(last>first)memcpy(out+origin+first,native+first,(size_t)(last-first)*sizeof(uint32_t));
    }
}

void s3_video_command(int id, const char *json)
{
    (void)json;
    char reply[2048];
    snprintf(reply, sizeof reply,
        "{\"id\":%d,\"enabled\":%d,\"width\":%d,\"view_left\":%d,\"camera\":%d,"
        "\"native_x\":%d,\"stage_width\":%d,\"frames\":%u,\"fallback_frames\":%u,"
        "\"terrain_checks\":%u,\"terrain_errors\":%u,\"background_checks\":%u,\"background_errors\":%u,\"background_unstreamed\":%u,"
        "\"sprites\":%u,\"scene\":%d,\"spawned\":%u,\"pool_pressure\":%u,\"native_uninitialized_scroll_lines\":%u,"
        "\"native_unstreamed_terrain_samples\":%u,\"first_bad_x\":%d,\"first_bad_y\":%d,\"first_bad_attr\":%u,\"first_bad_expected\":%u,\"first_bad_world_y\":%d,\"first_bad_hscroll\":%d,"
        "\"scene_match_misses\":%u,\"scene_held_frames\":%u,\"main_cpu_divisor\":%u,"
        "\"aiz_art_ready\":%d,\"aiz_main_palette\":%u,\"aiz_main_attr\":%u,\"aiz_main_pixel\":%u,"
        "\"bg_bad_x\":%d,\"bg_bad_y\":%d,\"bg_bad_attr\":%d,\"bg_bad_expected\":%d,\"bg_event\":%u,"
        "\"tick_samples\":%u,\"tick_updates\":%u,\"tick_lag\":%u,\"tick_multi\":%u,\"publication_lag\":%u,"
        "\"blue_spheres\":%d,\"special_spheres\":%u,\"special_margin_spheres\":%u}",
        id, enabled(), s_width, s_left, s_camera, s_native_x, s_stage_width,
        s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors,s_bg_checks,s_bg_errors,s_bg_unstreamed,
        s_display?s_display->count:0,s_display?s_display->scene:-1,s_spawned,s_pool_pressure,s_uninitialized_scroll_lines,
        s_terrain_unstreamed,s_bad_x,s_bad_y,s_bad_attr,s_bad_expected,s_bad_world_y,s_bad_hscroll,
        s_scene_misses,s_scene_holds,s3_video_main_cpu_divisor(),
        s_aiz_art_ready,s_aiz_main.colors[2],world_attr_blocks(s_world_frame,s_aiz_main.blocks,0x1300,0x390,0),
        pattern_pixel(&s_aiz_main.video,0x582C,0x1300,0x390),
        s_bg_bad_x,s_bg_bad_y,s_bg_bad_a,s_bg_bad_expected,s_frame_bg_event,
        s_tick_samples,s_tick_updates,s_tick_lag,s_tick_multi,s_publication_lag,
        s_frame_special && ss_ready,ss_count,ss_margin_count);
    cmd_send_response(reply);
}
static void scene_frame_state(S3StateIO *io, SceneFrame *frame)
{
    static const S3VideoArt none;
    unsigned count = frame->count;
    s3_state_peek(io, offsetof(SceneFrame, count), &count, sizeof count);
    if (count > SCENE_SPRITES) io->ok = 0;
    S3_STATE(io, *frame);
    if (io->mode == 2)                       /* (a game's pictures: redrawn at the next capture) */
        for (unsigned n = 0; n < frame->count && n < SCENE_SPRITES; ++n)
            if (frame->sprites[n].art) frame->sprites[n].art = &none;
}
void s3_video_state(S3StateIO *io)
{
    int mode = s_mode; double ratio = s_ratio;
    if (io->mode) {
        if (io->pos > io->size || sizeof mode + sizeof ratio > io->size - io->pos) { io->ok = 0; return; }
        memcpy(&mode, io->data + io->pos, sizeof mode);
        memcpy(&ratio, io->data + io->pos + sizeof mode, sizeof ratio);
        if (mode != s_mode || ratio != s_ratio) io->ok = 0;
    }
    S3_STATE(io, mode); S3_STATE(io, ratio);
    /* Published scene frames and loader ownership affect future frames.
     * Scanline scratch and decoded ROM caches are rebuilt on demand. */
    scene_frame_state(io, &s_build);
    for (unsigned i = 0; i < 3; ++i) scene_frame_state(io, &s_history[i]);
    scene_frame_state(io, &s_display_frame);
    int display = s_display != NULL; S3_STATE(io, display);
    S3_STATE(io, s_serial); S3_STATE(io, s_scene_tick);
    S3_STATE(io, s_tick_was_active); S3_STATE(io, s_tick_last);
    S3_STATE(io, s_tick_zone); S3_STATE(io, s_tick_serial);
    S3_STATE(io, s_tick_samples); S3_STATE(io, s_tick_updates);
    S3_STATE(io, s_tick_lag); S3_STATE(io, s_tick_multi); S3_STATE(io, s_publication_lag);
    S3_STATE(io, s_placements);
    if (s3_state_peek_unsigned(io, s_placement_count) > SCENE_PLACEMENTS) io->ok = 0;
    S3_STATE(io, s_placement_count); S3_STATE(io, s_placement_base);
    S3_STATE(io, s_loader_active); S3_STATE(io, s_visible_objects);
    if (s3_state_peek_unsigned(io, s_visible_count) > 144) io->ok = 0;
    S3_STATE(io, s_visible_count); S3_STATE(io, s_background_frame);
    if (io->mode == 2) {
        s_display = display ? &s_display_frame : NULL;
        s_frame_special = ss_ready = ss_latched = 0;
        s_upload_serial = 0;                 /* (relatched at the next V-int) */
    }
}
/* Optional full-frame overlay owned by a game layer (trilogy pause menu). */
static S3VideoFrameOverlay s_frame_overlay;
void s3_video_set_frame_overlay(S3VideoFrameOverlay draw) { s_frame_overlay = draw; }
static void overlay_scanline(const struct GVDP *v, int line, uint32_t *out, int width)
{
    (void)v;
    if (s_frame_overlay) s_frame_overlay(line, out, width);
}
/* An in-between frame at phase t (0 = the previous frame, 1 = the latest),
 * at the custom width, into `frame` (rows `stride` pixels apart). */
static int interpolate(double t, uint32_t *frame, int stride, int height)
{
    if(!enabled() || s_ix[1].lines<=0 || s_ix[0].lines!=s_ix[1].lines ||
       s_ix[0].width!=s_ix[1].width || s_ix[1].width!=s_width || s_ix_nw>IX_NATIVE)return 0;
    int lines=height<s_ix[1].lines?height:s_ix[1].lines;
    s_interp=1;s_ix_t=(float)(t<0?0:t>1?1:t);
    for(int line=0;line<lines;++line) {
        uint32_t *row=frame+(size_t)line*stride;
        scanline(&s_video_frame,line,s_ix_native[line],s_ix_nw,row,s_width);
        overlay_scanline(&s_video_frame,line,row,s_width);
    }
    s_interp=0;
    {   /* development: S3_INTERP_DUMP=<dir> writes in-between frames 1200..1209 as PPM */
        static const char *dir;static int checked;static unsigned n;
        if(!checked){dir=getenv("S3_INTERP_DUMP");checked=1;}
        if(dir && *dir && s_frames>=1200 && s_frames<1210) {
            char path[1024];snprintf(path,sizeof path,"%s/f%04u_%03u.ppm",dir,s_frames,(unsigned)(t*1000));
            FILE *f=fopen(path,"wb");
            if(f) {
                fprintf(f,"P6 %d %d 255\n",s_width,lines);
                for(int y=0;y<lines;++y)for(int x=0;x<s_width;++x) {
                    uint32_t c=frame[(size_t)y*stride+x];uint8_t rgb[3]={(uint8_t)(c>>16),(uint8_t)(c>>8),(uint8_t)c};
                    fwrite(rgb,1,3,f);
                }
                fclose(f);++n;
            }
        }
    }
    return 1;
}
const GameVideo sonic3_video = { configure, enabled, width, scanline, overlay_scanline, interpolate };
