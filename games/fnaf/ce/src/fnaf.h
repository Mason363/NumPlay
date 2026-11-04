/* Five Nights at Freddy's on the TI-84 Plus CE: what its files share. The game itself (the nights, the
 * animatronics, the scenes' steps) is the NumWorks version's (../src: game.c, night.c, calls.c); this version
 * draws it on the CE's 8-bit screen with the pictures of tools/pack.py. */
#ifndef FNAF_CE_H
#define FNAF_CE_H
#include "../../src/game.h"
#include "../../src/plat.h"
#include "../engine/gfx.h"
#include "assets.h"

/* the RAM: scene (the office, a camera, the title, an animation's frames) and what is left after it */
#define SCENE_W 536
#define SCENE_H 120
#define RAM_NEEDED (SCENE_W * SCENE_H + 26 * 1024)
extern uint8_t *scene, *extra;
extern int scene_pic;        /* the picture scene holds, -1 none */
void scene_want(int pic);    /* scene holds pic: its bases (or the frames before it) decoded first as needed */
extern int extra_pic;        /* what extra holds (a picture: the monitor's frame, a twitch...; or EXTRA_*) */
#define EXTRA_FACES (-2)     /* the custom night's four faces */
void extra_want(int pic, int stride);

/* a picture shown as it is on both screens (the newspaper, the paychecks...): true when they hold it already;
 * else it is drawn now (and kept for the next frame) */
bool still(int pic);
void still_forget(void);
extern bool keep_frame;      /* the next ce_show keeps the frame */

/* the inks of the fixed colours */
extern const uint8_t INK_WHITE[4], INK_GREY[4], INK_SILVER[4], INK_DIM[4], INK_CAPTION[4], INK_CALL[4], INK_MINT[4],
  INK_BLACK[4];
/* an ink fading out: a from 0 (gone) to 32 */
Ink ink_fade(Ink ink, int a);

/* view.c */
void view_paused(void);
const uint8_t *static_phases(int a);   /* the static's four phases this frame, from its strength (of 32) */
void view_reset(void);       /* a new night: the office's pictures again */
int office_pan(void);
/* save.c */
void save_open(void);
/* keys: the CE's as the game's (plat.h) */
uint32_t fnaf_keys(void);
#endif
