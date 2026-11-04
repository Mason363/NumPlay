/* Deltarune: what the Dark World's cutscenes use (cut.c), named as the original's scripts are: markers
 * (scr_dark_marker), moves over time, the view panning, the party stood in for by markers, the "!" bubble.
 * Things a cutscene makes are found again by their ACT_* name (pointers don't last over a wait). */
#ifndef DR_CUT_H
#define DR_CUT_H
#include "event.h"
#include "rooms.h"
#include "text.h"
#include "world.h"

/* the dialogue box as the original's cutscenes fill it: SAY(face, expression, texts and F_ changes) */
#define SAY TALKF
#define SAY_TOP(...) do { text_side(0); TALKF(__VA_ARGS__); } while (0)
#define SAY_LOW(...) do { text_side(1); TALKF(__VA_ARGS__); } while (0)

/* more actors (rooms.h's ACT_* go on) */
enum { ACT_K = 40, ACT_S, ACT_R, ACT_L, ACT_X1, ACT_X2, ACT_X3, ACT_X4, ACT_X5, ACT_X6, ACT_EXC };

Obj *mk(uint16_t spr, int x, int y, int name);          /* scr_dark_marker, named */
Obj *act(int name);                                     /* obj_get */
void go_hv(int name, int hs16, int vs16, int ispd16);   /* hspeed, vspeed (1/16) */
void move_over(int name, int x, int y, int t);          /* scr_move_to_point_over_time */
void halt_act(int name);                                /* scr_halt */
void set_spr(int name, uint16_t spr, int ispd16);
void gone(int name);                                    /* instance_destroy */

/* the view: scr_pan (dx, dy in 1/16 a frame, for t frames), to a place in t frames, back to Kris */
void cam_pan(int dx16, int dy16, int t);
void cam_pan_to(int x, int y, int t);
bool cam_panning(void);
void cam_follow(void);

/* the party as markers (K, S, R at Kris's and the followers' places, Kris and them hidden), and back */
void party_markers(void);
void party_back(void);                                  /* Kris where K is, the followers on him */
void exc_over(int x, int y);                            /* obj_excblcon: "!" for 20 frames (units) */

void cut_step(void);                                    /* each frame (dw_step) */
void cut_draw(void);
void cut_reset(void);                                   /* (a room entered) */
#endif
