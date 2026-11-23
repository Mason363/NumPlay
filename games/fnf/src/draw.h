/* Drawing: the game's pictures unpacked into RAM and drawn in strips of the screen, with NumPlay's launcher graphics
 * for text and boxes (gfx.h). */
#ifndef FNF_DRAW_H
#define FNF_DRAW_H
#include <stdbool.h>
#include "gfx.h"
#include "game.h"

/* an unpacked picture */
typedef struct {
  uint16_t id, w, h, stride;
  int16_t x, y;
  const uint16_t *pal;
  uint8_t *px;
} pic_t;

/* RAM for pictures: a song's (its stage, characters, notes...) or the menus'. */
void ram_reset(void);
uint8_t *ram_take(uint32_t n);                          /* NULL when full */
bool pic_load(pic_t *p, unsigned sprite, uint8_t *dst, uint32_t cap);
bool pic_new(pic_t *p, unsigned sprite);                /* into RAM taken for it */
uint8_t *bg_pixels(void);                               /* the 320x240 background's (38400 bytes) */
bool bg_load(unsigned sprite);

/* drawing into the strip (gfx_y0..gfx_y1): (x, y) the picture's top left; alpha 0-32 */
void blit(const pic_t *p, int x, int y, int alpha, bool flip_y);
void blit_at(const pic_t *p, int ax, int ay, int alpha);   /* at its anchor */
void blit_scaled(const pic_t *p, int x, int y, int w, int h, int alpha);
void blit_tinted(const pic_t *p, int x, int y, color_t tint, int t32);
void draw_bg(const uint16_t *pal, int dark1000, float zoom);
void push_strip(void);

/* the menus' bold letters (white, outlined): their width, and drawn with their top at y */
bool bold_load(void);
int bold_width(const char *s, int scale256);
void bold_text(int x, int y, const char *s, int alpha, int scale256);

void draw_strip_buffer(color_t *p);                     /* SCREEN_W x STRIP_H, for the frames */
/* a frame: strip_fn draws each strip in turn (gfx_y0..gfx_y1), which is sent to the screen */
void draw_frame(void (*strip_fn)(void *), void *ctx);
extern void (*draw_between)(void);                      /* called between strips (to read the keys) */

/* a song: what it shows, unpacked when it starts; then each frame, prepared and drawn strip by strip */
bool play_load(const fnf_game_t *g);
void play_prepare(const fnf_game_t *g, bool downscroll, int32_t since_start);
void play_strip(void);
void play_actor_strip(const fnf_game_t *g, int i);
void play_icons(const fnf_game_t *g);                   /* (after a SetHealthIcon) */
/* the song's stage and characters darkened (the HUD isn't): v 0-256, or (tenths) v tenths of white taken away */
void play_fade(int v, bool tenths);
void strip_darken(int v, bool tenths);                  /* the whole strip */

/* the game over (GameOverSubState): the player's death, unpacked into the background's RAM, on black; the camera
 * moves from the stage's view to the death's. over_death: the player's (0: none) */
const fnf_death_t *over_death(const fnf_game_t *g);
bool over_begin(const fnf_game_t *g);                   /* false: no death animations (its last frame stays) */
/* anim 0-2 (firstDeath, deathLoop, deathConfirm), ms into it, ms since the death */
void over_prepare(const fnf_game_t *g, int anim, uint32_t anim_ms, uint32_t since_ms);
void over_strip(void);
#endif
