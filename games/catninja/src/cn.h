/* Cat Ninja: the game's state, shared by its logic (game.c, env.c), its drawing (draw.c) and its shell
 * (main.c). The logic is Richman Stewart's ActionScript (CatNinja.swf, 2013) in C, class by class: the
 * same numbers, the same order, the same quirks. It steps 60 times a second, in the original's pixels
 * (tiles of 20), with its Numbers as doubles and its display objects' places in twentieths of a pixel. */
#ifndef CN_H
#define CN_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

#define SOLID 0x80 /* a tile's byte: its type (0 to 91), and this bit when it isn't walkable */
#define TYPE(i) (grid[i] & 0x7F)

/* ---------------------------------------------------------------- tools/images/ColourChanger */
typedef struct {
  double r, g, b, ra, ga, ba, speed, max;
  bool reversed;
} cc_t;
void cc_init(cc_t *c, double speed, double max);
void cc_update(cc_t *c);

/* ---------------------------------------------------------------- the map (maps/Map) */
extern uint8_t grid[CN_MAP_MAX];
extern uint8_t blood[(CN_MAP_MAX + 7) / 8]; /* spikes the cat died on (drawn bloody) */
extern int gw, gh, level, room;
extern int destrotate, currentrotation;
extern bool rotating, hallucinate;
extern cc_t map_tint;
extern int playerpoints[12];

/* the display: Main.world (its turn and size about the screen's middle) and Main.screen (the camera) */
extern double world_rot, world_sx, world_sy, screen_x, screen_y;
extern double fade_alpha; /* Main.fadescreen */
extern bool fade_on;

/* ---------------------------------------------------------------- the cat (entities/Player) */
typedef struct {
  double x, y, speedx, gravity, ACCELERATION, FRICTION, trailfade, rotation;
  int maxspeedx, maxgravity, MAXSPEED, coordx, coordy, scaleX, scaleoffset;
  int spawnx, spawny, spawnrotation, spawnlevel;
  bool dead, stopMovement, doublejump, doublefalling, rolling, forceSlam, hitwall, onground;
  bool hasJetPack, usingjetpack, walkSteps, on_stage;
  int stepAmount, steps, lastX;
  int pic;                                        /* base.bitmapData */
  int a_frame, a_timer, a_speed, a_state, a_paused; /* its AnimateControl */
  bool jet_visible, jet_half;                     /* basejetpack: shown, squashed (ducking) */
  cc_t tint;
} player_t;
extern player_t P;
#define OFFX 11
#define OFFY 10

/* ---------------------------------------------------------------- the things of a level (environment/) */
#define MAX_MINES 48
#define MAX_BUTTONS 8
#define MAX_DIRTS 128
#define MAX_CRATES 160
#define MAX_SHOOTERS 12
#define MAX_FALLS 140
#define MAX_SUNS 16
#define MAX_SPHERES 72
#define MAX_SCREENS 12
#define MAX_LASERS 12
#define MAX_BULLETS 48
#define MAX_BOMBS 24
#define MAX_TRAILS 320
#define MAX_PARTICLES 100
#define MAX_TEXTS 6

typedef struct {
  int x, y, graphic, timer, rate, pic;
  bool setoff, blinkon, off;
} mine_t;
typedef struct {
  double x, y, sx, sy, msx, msy, oy;
  int datanum, pic;
  bool pressed, alive;
} button_t;
typedef struct {
  int x, y, frame, ftimer;
  bool activated, alive, playing;
} dirt_t;
typedef struct {
  int x, y, ox, oy, lx, ly;
  bool alive;
} crate_t;
typedef struct {
  int x, y, orient, timer, startat, frame, ftimer;
  bool homing, reverse, paused;
} shooter_t;
typedef struct {
  double x, y, sx, sy, angle;
  int owner, color, htimer, homingtimer, bspeed; /* owner: a shooter, or -1 the boss */
  bool homing, on;
} bullet_t;
typedef struct {
  int x, y, sx, sy, spx, spy, dir;
  bool fall, alive;
} fall_t;
typedef struct {
  int x, y, sx, sy, lastx, lasty;
} sun_t;
typedef struct {
  double x, y;
  int frame, ftimer;
  bool collectable, alive;
} sphere_t;
typedef struct {
  int x, y, text, rot;
} screen_t;
typedef struct {
  int fx, fy, tx, ty, g1, g2, go1, go2, lg;
  bool on;
} laser_t;
typedef struct {
  double x, y, speedx, gravity, rot;
  bool on;
} bomb_t;
enum { TR_PIC, TR_CIRCLE_RED, TR_DOT }; /* a trail: a picture, the jetpack's red circle, a bullet's dot */
typedef struct {
  double x, y, sx, sy, scale, alpha, fade, rot;
  int kind, pic, color, tint; /* tint: 0 none, 1 the map's, 2 the cat's (as it was) */
  float r;                    /* a circle's radius */
  uint8_t tr, tg, tb;         /* (the cat's tint when it was made, in 32nds) */
} trail_t;
typedef struct {
  double x, y, speedx, gravity, alpha, rot;
  int type, size, delayrate, delaytimer, pic, px0, py0, pw, ph; /* type 0: a piece of the cat (pic, its part) */
  bool onground;
} particle_t;
typedef struct {
  int text;
  double x, y, alpha, fadespeed;
  int destx, fadedelay, fadedelaytimer;
  bool fading, fadein, fadeout, doublefade, faded, visible, logo;
} dtext_t;

extern mine_t mines[MAX_MINES];
extern button_t buttons[MAX_BUTTONS];
extern dirt_t dirts[MAX_DIRTS];
extern crate_t crates[MAX_CRATES];
extern shooter_t shooters[MAX_SHOOTERS];
extern bullet_t bullets[MAX_BULLETS];
extern fall_t falls[MAX_FALLS];
extern sun_t suns[MAX_SUNS];
extern sphere_t spheres[MAX_SPHERES];
extern screen_t screens[MAX_SCREENS];
extern bomb_t bombs[MAX_BOMBS];
extern trail_t trails[MAX_TRAILS];
extern particle_t particles[MAX_PARTICLES];
extern dtext_t texts[MAX_TEXTS];
extern int n_mines, n_buttons, n_dirts, n_crates, n_shooters, n_falls, n_suns, n_spheres, n_screens, n_trails,
    n_particles, n_texts;

/* the big crystal at the end (environment/BigSphere) */
typedef struct {
  bool on, finished;
  double x, y, scale;
  int frame, ftimer, timer, crystals;
} bigsphere_t;
extern bigsphere_t big;

/* the boss (entities/Boss) */
typedef struct {
  bool created, intro, invincible, defeated;
  double x, y, sx, sy, alpha;
  int id, routineid, attackid, ptimer, prate, weapon, bullettimer, maxbullets, bulletsshot, destx, desty, bulletrate,
      speed, invincibletimer, deftimer, particletimer, lineoffset;
  bool homing;
  int jit[4]; /* drawboss()'s four random offsets this frame */
} boss_t;
extern boss_t boss;
extern const int16_t boss_points[];
#define BOSS_POINTS 108

/* ---------------------------------------------------------------- the game */
extern int pdeaths, collected;
extern uint8_t crystals_got[(CN_CRYSTALS + 7) / 8];
extern int hud_deaths, hud_collected; /* (what tools/text/Info shows: it changes once a second) */
extern uint32_t game_ms;              /* getTimer() */
extern bool pausegame;
extern bool exitedmap;
extern int afk_text;                   /* the idle message shown, or -1 */
extern uint32_t rng;
double rnd01(void); /* Math.random() */

/* keys, as the original's KeyboardManager has them */
extern bool key_left, key_right, key_up, key_down;
void key_press(int k);   /* K_* */
void key_release(int k);
enum { GK_LEFT, GK_RIGHT, GK_UP, GK_DOWN, GK_JET, GK_R };

void game_new(void);                /* Main.initiateGame: the menu map */
void game_step(void);               /* one frame of Main.update */
void game_exit_to_menu(void);       /* the pause bar's exit button (Menu.exitclick) */
bool game_can_exit(void);
void game_resume_at(int lvl, int x, int y, int rot); /* a saved game: the menu, its way right to (x, y) */

/* things env.c and game.c share */
void change_tile(int x, int y, int type, bool clear, bool persist, bool reset_node, bool set_walk);
int walk_at(int x, int y); /* grid at (x, y) walkable (out of the map: the original's error, as solid) */
void player_die(void);
void particles_create(double x, double y, int amount, int sx, int sy, int type, bool parts);
void trail_create(double x, double y, int kind, int pic, double fade, double alpha, int tint, double rot, int color,
                  double r);
void shake(int power, int length);
int text_create(int text, int x, int y, double speed, bool fade, int delay);
void text_screenmessage(int first, int count, int delay);
void interp(int page, int op_override, bool next);
void env_clear(void);
void env_update(void);
void env_reset(void);
void env_slam(void);
void boss_create(void);
void boss_remove(void);
void boss_update(void);
void boss_draw_points(int *xy); /* drawboss(): the outline's points this frame (BOSS_POINTS pairs) */
int timer_add(int ms, int kind, int arg, bool repeat);
void timer_remove_kind(int kind);
enum { T_PAGE, T_LOOP, T_LASER, T_RESPAWN, T_DIRT, T_AFK };
void bigsphere_create(int x, int y);
void bigsphere_update(void);
int sphere_create(double x, double y, bool collectable);
void laser_create(int fx, int fy, int tx, int ty, int ms);
void laser_switch(int i);
void dirt_break(int i);
void map_move(int dir); /* moveLeft / moveRight / moveUp / moveDown: 0 1 2 3 */
void info_update(bool force);

/* the menu's credits and the levels' first words fade in */
extern int key_hint_frames;
#endif
