/* Five Nights at Freddy's 3: the game's state and its parts.
 *
 * game.c: the scenes (title, extras, the night's card, 6 AM, the static and game over, the endings), keys and saves.
 * night.c: a night as the game's own events run it (Springtrap, the audio lure, the vents, the three systems and
 * their errors, the phantoms), 60 steps a second, in the events' order. view.c: drawing the office, the monitor
 * and the maintenance panel. The game's names for its counters are kept in the comments. */
#ifndef FNAF3_GAME_H
#define FNAF3_GAME_H
#include <stdbool.h>
#include <stdint.h>

#define XMIN 392   /* the view's centre, in the game's 1200-wide office ("scroll" X: 392..812 in steps of 20) */
#define XMAX 812

/* where Springtrap can be ("dhfgh" overlapping a marker): CAM 01 to 10 (1..10), the vents CAM 11 to 15
   (11..15), then the office's approach ("attack stage 1" to 4) and the two lunges ("GOT YOU", "GOT YOU 2") */
enum { L_NONE, L_CAM1, L_CAM10 = 10, L_VENT11, L_VENT15 = 15, L_ST1, L_ST2, L_ST3, L_ST4, L_GOT, L_GOT2, L_OFF };

/* the scares that end a phantom's sighting with a white flash (the "white flash" animation each uses) */
enum { PH_NONE, PH_BB, PH_FREDDY, PH_CHICA, PH_FOXY };

/* scenes */
enum { SC_TITLE, SC_EXTRAS, SC_DAY, SC_NIGHT, SC_STATIC, SC_OVER, SC_SIX, SC_END };

/* A night, all of it: kept as it is when Home quits (save.c), so the next start goes on from there. */
typedef struct {
  uint8_t night;           /* "night number": 1 to 6 */
  uint8_t ai;              /* "AI" */
  uint8_t tlimit;          /* "time limit": the phantoms' stare limit, in frames */
  uint8_t hour;            /* "hour": 0 (12 AM) to 6 */
  /* Springtrap */
  uint8_t loc;             /* L_* */
  uint8_t moves;           /* "move counter" */
  uint8_t turns;           /* "total turns" */
  uint8_t action;          /* "action selected" (0: none) */
  uint8_t aggressive;      /* "aggresive?" */
  uint8_t latch;           /* "dhfgh" s17: stage 1's move to stage 2 waits for a screen */
  uint8_t pic_random;      /* "pic random" */
  uint8_t lure_to;         /* "dhfgh" s11: the camera a lure pulls him to (0 none) */
  uint8_t ran_past, hid;   /* (ONCE a night: "run past", "hide") */
  uint8_t window, head;    /* "inwindow" and "head" shown */
  uint8_t got_you;         /* "got you": 0, 1 (the door lunge), 2 (from the right) */
  uint8_t force_move, target;   /* "force move", "target location" */
  uint8_t walk_was;        /* "action selected" > 1 the step before (the walk cue's edge) */
  int16_t lure_t;          /* "dhfgh" s12: frames until the lure's move */
  /* the view, the monitor, the panel */
  int16_t x;               /* "scroll" X */
  int8_t want;             /* a screen asked for while looking away: 1 the monitor, 2 the panel, 0 none */
  uint8_t viewing;         /* "viewing": 0 down, 1 going up, 2 up */
  uint8_t mon_s0;          /* "flip it out" s0: 5 ready, 2..4 up */
  uint8_t mon_anim;        /* frames into the flip animation (out or in) */
  uint8_t mon_down;        /* the flip-in animation playing */
  uint8_t panel;           /* "screen two flipper" s0: 0 down, 1 rising, 2..3 up */
  uint8_t panel_anim, panel_dropping;
  uint8_t cursor;          /* the panel's item: 1 audio, 2 camera, 3 ventilation, 4 reboot all, 5 exit */
  uint8_t rebooting;       /* "rebooting": 0, or the item */
  uint8_t reboot_p;        /* "cursor" s1 */
  uint8_t cam;             /* "C": the last camera (1..10) */
  uint8_t vent;            /* "D": the last vent camera (11..15) */
  uint8_t vents_map;       /* "toggle button" s0: the vent map shown */
  uint8_t toggle_cd;       /* "toggle button" s1 */
  uint8_t closed;          /* "what vent is closed" (0 none) */
  uint8_t sealing;         /* "going to seal" (0 none) */
  int16_t seal_t;          /* "seal vent button" s19 */
  uint8_t play;            /* "play counter": 1..7 (7: the lure is ready) */
  uint8_t sdfg;            /* frames left of the lure's sound wave on the map (0 none) */
  uint8_t lure_cam;        /* where it is */
  /* the systems: 0 OK down to -10 (an error) */
  int8_t audio, camera, vent_h;
  uint8_t cam_wear;        /* "camera text" s5: seconds of monitor up */
  uint8_t idle;            /* "ventilation text" s6: seconds with no screen up */
  uint16_t vent_err;       /* "ventilation text" s1: frames in error */
  int16_t halluc_t;        /* "ventilation text" s2 */
  uint8_t red;             /* "foreground" s0 */
  int16_t alarm;           /* "foreground" s3 */
  uint8_t red_on;          /* the office red this tick */
  uint16_t halluc;         /* cams 01..10 showing him (bit n-1) */
  uint8_t hallucinating;
  int16_t black;           /* "blackout" s1: 0..255+ */
  uint8_t black_up;        /* "blackout" s0: 1 fading back */
  int16_t black2;          /* "blackout 2" (the Puppet) */
  int16_t static_s4;       /* "smallstatic" s4 */
  int16_t static_s5;       /* "smallstatic" s5 */
  uint8_t static_s1;
  uint8_t trail_n;         /* the "trail"s: up to 3, their places and ages */
  uint8_t trail_at[3], trail_age[3];
  uint8_t nearby;          /* "nearby" s0: 0, 1, 2 */
  /* the phantoms */
  uint8_t bb, mangle, puppet, chica, freddy;   /* "BB", "mangle", "puppet", "chica", "golden freddy" */
  uint8_t forced;          /* the forced appearances done (bits: BB, Mangle, Marionette, Chica, Golden Freddy) */
  uint8_t bb_pending, bb_shown;
  int16_t bb_stare;        /* "BBpeek" s3 */
  int16_t mangle_stare;    /* "mangle 2" s0 */
  int16_t mangle_win;      /* frames into Mangle's rise in the window (0 none) */
  int16_t puppet_stare;    /* "phantom head" s2 */
  int16_t head_t;          /* "phantom head" s1 */
  int16_t head_x;          /* the head's place (screen) */
  uint8_t head_anim, head_out;
  int16_t chica_stare;     /* "chica 2" s0 (never reset) */
  uint8_t chica_in;        /* shown in the office */
  uint8_t freddy_pending;  /* "BBpeek" s1 */
  uint8_t fwalk;           /* walking */
  int16_t fwalk_t;         /* frames into his walk */
  int16_t fwalk_stare;     /* "fwalk" s0 */
  uint8_t flunge;          /* frames into his lunge + 1 (0 none) */
  uint8_t fscare_ready;    /* "BBpeek" s2 */
  uint8_t foxy;            /* "foxy" s0 = 1: shown */
  uint8_t foxy_done;
  uint8_t scare;           /* the phantom scare playing (PH_*) */
  int16_t scare_t;         /* frames into it */
  uint8_t cooldown;        /* "scare cooldown" */
  uint8_t frozen;          /* "frozen" */
  uint8_t frozen_was;
  int16_t flash;           /* frames of the white flash left */
  int16_t breath;          /* "scroll" s15 */
  int16_t death_t;         /* frames into Springtrap's lunge (0 none) */
  uint8_t pad[2];
  uint32_t frame;          /* steps since the night began */
} Night;

typedef struct {
  int scene;
  float t;                 /* seconds in the scene */
  uint32_t held, pressed;
  bool running;
  /* the save (Ini "freddy3"): the night reached, the stars, the extras */
  uint8_t level, beatgame, beat6, goodend, keys_seen;
  uint8_t cheats;          /* bits: fast nights, radar, aggressive, no errors */
  uint8_t menu, menu_n;
  uint8_t paused, confirm, pause_item;
  uint8_t after_keys, extras_sel, ending;
  float repeat;            /* a held key repeats */
  const char *cap[4];      /* what is heard, as captions (the newest last) */
  float cap_t[4];
  Night n;
} Game;

enum { CH_FAST = 1, CH_RADAR = 2, CH_AGGRESSIVE = 4, CH_NOERRORS = 8 };

extern Game g;

/* game.c */
void game_init(void);
void game_step(uint32_t keys);
void game_draw(void);
uint32_t rnd(void);
uint32_t rnd_n(uint32_t n);    /* Random(n): 0..n-1 */
void start_night(int night);
void caption(const char *s);

/* night.c */
void night_begin(int night);
void night_step(void);
int feed_anim(int cam);        /* the camera feed's animation for a camera (fnaf3_spec 7.5) */
int screen_up(void);           /* "viewing a screen" */
int spring_cam(void);          /* "mon in": 1..15, 0 elsewhere */

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
