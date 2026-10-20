#pragma once
#include "game_video.h"
extern const GameVideo sonic3_video;
int s3_video_hook(uint32_t pc);
void s3_video_command(int id, const char *json);
void s3_video_vblank(void);
unsigned s3_video_main_cpu_divisor(void);
/* Full custom canvas, or 320 while the opt-in renderer is disabled. */
int s3_video_canvas_width(void);
/* Additive host actors in the custom-width renderer, after every scene sprite.
 * priority bit 0 = high-priority plane pixel, bit 1 = sprite already drawn;
 * `origin` is the output column of camera_x. */
struct GVDP;
typedef void (*S3VideoActorOverlay)(const struct GVDP *v, int line, uint32_t *out, int width,
                                    int origin, const uint32_t *palette, uint8_t *priority,
                                    int camera_x, int camera_y);
void s3_video_set_actor_overlay(S3VideoActorOverlay draw);
/* Final-frame rows (native or custom width) after all rendering; may rewrite
 * the row (pause menu blur/dim). Must not touch guest state. */
typedef void (*S3VideoFrameOverlay)(int line, uint32_t *out, int width);
void s3_video_set_frame_overlay(S3VideoFrameOverlay draw);
/* Level background blur (a depth of field: far layers softest): 0 off, 1 low,
 * 2 medium, 3 high. */
void s3_video_set_bg_blur(int level);
/* Smooth sprite rotation: walking/running players are drawn from their
 * upright frame, turned to the ground angle. */
void s3_video_set_rotation(int on);
/* A window taller than the picture's shape: the rows the renderer adds above
 * and below the 224 native lines this frame (overlays get them as lines
 * -top..-1 and 224..). 0, 0 otherwise. */
void s3_video_canvas_rows(int *top, int *bottom);
/* Zones the game adds itself (not in the retail tables): their ring layout. */
typedef unsigned (*S3VideoRingLayout)(void);
void s3_video_set_ring_layout(S3VideoRingLayout fn);
/* A game's own player art: a picture drawn in place of a frame's pieces, its
 * top-left (ox, oy) from the object's position, mirrored with it. Walking and
 * running frames are asked for upright and turned by the renderer (smoothly,
 * or in the cartridge's 45-degree steps). One byte per pixel, 0 clear:
 *  - without colours: 1-15 a colour of the sprite's palette line, 16-63 an
 *    entry of the whole palette;
 *  - with colours (ncol): n is colour n-1, 6 bytes each: r, g, b, a palette
 *    entry 0-63 and that entry's own colour (a little-endian palette word).
 *    The colour is drawn moved as far as the entry now is from its own
 *    colour, so palette fades, water and flashes carry over.
 * upright: a picture already drawn for the frame's angle, never turned. */
typedef struct { int16_t w, h, ox, oy; const uint8_t *pix; uint16_t ncol; const uint8_t *col; uint8_t upright; } S3VideoArt;
/* obj: RAM offset of a world object drawn with mappings `map` (for a static
 * mapping, the address of its pieces), frame `frame`; NULL keeps the
 * cartridge's frame (w or h 0: draw nothing). */
typedef const S3VideoArt *(*S3VideoPlayerArt)(unsigned obj, unsigned map, unsigned frame);
void s3_video_set_player_art(S3VideoPlayerArt fn);
/* A game's own size for a world object in the scene (RAM offset obj): 256ths
 * about its position, 0 for none (Sonic CD's miniature Sonic). */
typedef unsigned (*S3VideoObjectZoom)(unsigned obj);
void s3_video_set_object_zoom(S3VideoObjectZoom fn);
/* Data Select ground: a full-width ARGB row shown behind the save cards in
 * place of the portrait, or NULL to keep the cartridge's own. Palette fades
 * are the callback's to follow. Must not touch guest state. */
typedef const uint32_t *(*S3VideoGround)(int line, int width);
void s3_video_set_file_select_ground(S3VideoGround fn);
/* 1 while an in-between (interpolated) frame is being drawn: overlays keep
 * their timers to real frames. */
int s3_video_interpolating(void);
