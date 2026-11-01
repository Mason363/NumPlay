/* Five Nights at Freddy's 2: the game's state and its parts.
 *
 * game.c: the scenes (title, newspaper, how to play, the night's card, 6 AM, the paychecks, game over, the
 * custom night), keys and saves. night.c: a night (time, the music box, the flashlight, the mask, the vents,
 * the cameras and every animatronic) as the game plays it, 60 steps a second. view.c: drawing the office, the
 * cameras and what is around them. */
#ifndef FNAF2_GAME_H
#define FNAF2_GAME_H
#include <stdbool.h>
#include <stdint.h>

#define DT (1.0f / 60)       /* a step */
#define HOUR 68.0f           /* seconds of an hour */
#define OFFICE_PAN 180       /* how far the office view turns (the office is 500 wide, the screen 320) */
#define BOX_FULL 2000        /* the music box, wound up */

/* where everyone can be: the 12 cameras (CAM 01 to 12), then the places the cameras don't show */
enum {
  L_PR1, L_PR2, L_PR3, L_PR4, L_LVENT, L_RVENT, L_MAIN, L_PARTS, L_STAGE, L_GAME, L_PRIZE, L_KIDS,
  L_UNSEEN,     /* Balloon Boy between the Game Area and the vent */
  L_HALL_FAR,   /* the office hallway, far (Toy Freddy) */
  L_HALL,       /* the office hallway */
  L_LSPOT,      /* the left vent's blind spot */
  L_RSPOT,      /* the right vent's blind spot */
  L_OFFICE,
  L_COUNT
};
#define CAM_COUNT 12

/* the animatronics: the ones that walk a path, then Withered Foxy, Golden Freddy and the Puppet */
enum { TF, TB, TC, MANGLE, BB, WF, WB, WC, WALKERS, FOXY = WALKERS, GF, PUPPET, BOTS };

/* the office's visitor (one at a time): how far along */
enum { OCC_NONE, OCC_IN, OCC_SHOWN, OCC_MASKED, OCC_LEAVING, OCC_DOOMED };
/* Golden Freddy */
enum { GF_NONE, GF_HALL, GF_COMING, GF_SHOWN, GF_FADING };

/* scenes */
enum { SC_TITLE, SC_NEWS, SC_KEYS, SC_INTRO, SC_NIGHT, SC_SCARE, SC_SIX, SC_PAPER, SC_OVER, SC_CUSTOM };

/* A night, all of it: kept as it is when Home quits (save.c), so the next start goes on from there. */
typedef struct {
  uint8_t night;         /* 1 to 7 (7: the custom night) */
  uint8_t hour;          /* 0 (12 AM) to 6 */
  int8_t ai[BOTS];       /* 0 to 20 (the caps apply when moving) */
  uint8_t step[WALKERS]; /* how far along its path each walker is */
  uint8_t ready[WALKERS];/* at its last place, it comes in the next time the monitor is up */
  uint8_t pending[BOTS]; /* a move won while stunned: made when the stun ends */
  uint16_t stun[BOTS];   /* frames it can't move (the camera's light) */
  uint8_t occ, occ_state;/* the office's visitor and how far along (OCC_*) */
  uint8_t mangle_in, bb_in;
  uint8_t foxy_hall, foxy_got, foxy_flashes, foxy_off;
  uint8_t gf;            /* GF_* */
  uint8_t puppet;        /* 0 in the box, 1 partly out, 2 mostly out, 3 out, 4 gone from CAM 11 */
  uint8_t puppet_at;     /* gone: where (an L_*) */
  uint8_t monitor;       /* up, or going up */
  uint8_t mask;          /* on, or going on */
  uint8_t cam;           /* the camera picked */
  uint8_t hall_light, vent_light, cam_light, winding;   /* held: the lights (vent: 1 + side), the box */
  int8_t want;           /* a vent light pressed while looking away: turning there first (-1 none) */
  uint8_t warned;        /* the music box's warning, given */
  uint8_t hall_was;      /* the hallway light, the step before */
  uint8_t pad[2];
  float t;               /* seconds since 12 AM */
  float hour_t;
  float box;             /* the music box: 0 to BOX_FULL */
  float battery;         /* frames of light used */
  float move_t[BOTS];    /* until the next movement opportunity */
  float occ_t;           /* the visitor's timer */
  float force_t;         /* a doomed visitor pulls the monitor down or the mask off */
  float blackout;        /* seconds of the office's lights out */
  float flicker;         /* seconds of the office's lights flickering */
  float foxy_d;          /* Withered Foxy's D */
  float foxy_hold;       /* the hallway light held on him */
  float foxy_ten;        /* until the next 10 s mark */
  float foxy_dim;        /* until the next half second (the light down the hall in Parts/Service) */
  float gf_t, gf_lit;
  float pan, pan_v;      /* the office view's left edge */
  float flip;            /* the monitor: 0 down to 1 up */
  float mask_anim;       /* the mask: 0 off to 1 on */
  float glitch;          /* seconds of static on the camera watched */
  float laugh_t;         /* Balloon Boy in the office */
} Night;

typedef struct {
  int scene;
  float t;               /* seconds in the scene */
  uint32_t held, pressed;
  bool running;
  /* the save: the night reached (1 to 5), the stars, whether the keys were shown */
  uint8_t reached, stars, keys_seen;
  uint8_t menu, menu_n;
  uint8_t paused, confirm, pause_item;
  uint8_t custom_sel, custom_mode, paper;
  uint8_t scare, after_keys;
  int8_t custom[10];     /* the custom night's levels (CUSTOM_ORDER) */
  float scare_t;
  float blip;            /* the cameras' white flicker when switched */
  float repeat;          /* a held key repeats */
  const char *cap[4];    /* what is heard, as captions (the newest last) */
  float cap_t[4];
  Night n;
} Game;

extern Game g;
/* the custom night's ten, in the order of its screen: W.Freddy, W.Bonnie, W.Chica, W.Foxy, BB, T.Freddy,
 * T.Bonnie, T.Chica, Mangle, G.Freddy */
extern const uint8_t CUSTOM_ORDER[10];
extern const char *const BOT_NAMES[BOTS];

/* game.c */
void game_init(void);
void game_step(uint32_t keys);
void game_draw(void);
uint32_t rnd(void);
void start_night(int night);
void caption(const char *s);

/* night.c */
void night_begin(int night);
void night_step(void);
int bot_at(int who);            /* where an animatronic is (an L_*), or -1 off the map */
int ai_level(int who);          /* its AI level, capped as the game caps it */
int battery_bars(void);         /* the flashlight: 0 to 4 */
bool lights_work(void);         /* the flashlight and the vent lights (Balloon Boy takes them) */
int battery_frames(int night);
float box_drain(int night, int hour);
bool box_warning(void);
void apply_hour(int night, int hour);

/* view.c */
void view_night(void);
void view_scare(void);
void view_captions(void);

/* save.c */
void save_restore_copy(void);
void save_progress(void);
void save_load(void);
void save_resume(void);     /* the night as it is (Home) */
bool save_resume_load(void);
#endif
