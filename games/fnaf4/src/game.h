/* Five Nights at Freddy's 4: the game's state and its parts.
 *
 * game.c: the scenes (the warning, the title, extras, the night's card, 6 AM, Fun with Plushtrap, game over, the
 * ending and the box), keys and saves. night.c: a night as the game's own events run it (the bedroom, its two doors,
 * the closet and the bed; Bonnie, Chica, Foxy, the Freddles, Fredbear and Nightmare), 60 steps a second, in the
 * events' order. view.c: drawing it. The game's names for its objects and counters are kept in the comments. */
#ifndef FNAF4_GAME_H
#define FNAF4_GAME_H
#include <stdbool.h>
#include <stdint.h>

/* where an animatronic can be (the markers it overlaps); P_AWAY: off the map (its AI is 0) */
enum { P_AWAY, P_LRC, P_LRL, P_LRR, P_KITCHEN, P_LHF, P_RHF, P_LHN, P_RHN, P_CLOSET, P_BED };

/* the night's objects that come and go (each its animation, AN_* in anims.h); O_ROOM is the bedroom ("Active") */
enum {
  O_ROOM, O_DARKL, O_DARKR, O_DARKC, O_OFF4, O_ON4, O_CREAK, O_CARPET, O_LSTART, O_RSTART, O_PEEKL, O_PEEKR,
  O_CLOSET, O_BED, O_BHIDE, O_CHIDE, O_LEAVE3, O_LEAVE2, O_LEAVE1, O_BITE, O_JUMP, O_FREDDYATK, O_CUPCAKE,
  O_BONNIEATK, O_FREDBEAR, O_COUNT
};

typedef struct {
  uint8_t alive, vis;
  uint8_t an;              /* AN_* */
  uint8_t img;             /* its picture */
  uint8_t cnt;             /* the speed counter: a picture each 100 */
  uint8_t plays;           /* plays left (0: for ever) */
  uint8_t done;            /* its animation is over */
  uint16_t z;              /* made after the ones lower (drawn over them) */
} Ob;

typedef struct {
  uint8_t loc;             /* P_* */
  uint8_t s0, s1, s5, s6, s7;   /* its alterable values A, B, F, G, H */
} Anim;

/* A night, all of it: kept as it is when Home quits (save.c), so the next start goes on from there. */
typedef struct {
  uint8_t night;           /* "Night": 1 to 8 (7 is the Nightmare, 8 the 20/20/20/20) */
  uint8_t shadow;          /* "shadow": 0, 1 (the Nightmare), 2 (20/20/20/20) */
  uint8_t hour;            /* "hour": 12, then 1 to 6 */
  uint8_t ai_bonnie, ai_chica, ai_foxy, ai_freddy, ai_fredbear;   /* "Bonnie AI" ... "Fredbear AI" */
  uint8_t force_bonnie, force_chica;   /* the hour each comes to the door once (nights 2 to 4) */
  uint8_t s0;              /* "follow" A: where you are and what you do (the game's numbers, 0 to 45, 99) */
  int16_t x;               /* "follow" X: where the bedroom view looks, 400 to 600 */
  uint8_t s3;              /* "follow" D */
  uint8_t what_side;       /* "what side": 1 at the left door, 2 going left, 3 the bed's side, 4 going right, 5 right */
  uint8_t flash, shut;     /* "flashlight", "close door" */
  uint8_t listening;       /* "listening mode": 1 the left door, 2 the right */
  uint8_t left_shut, right_shut;
  uint8_t view_left, view_right, view_closet, view_bed;   /* "viewing left hall" ... */
  uint8_t door_lock;       /* "in closet" F: the doors held on them, nobody moves (1) or they went (2) */
  uint8_t kitchen2, kitchen3, kitchen4;   /* "kitchen" C, D, E: the lights on Fredbear */
  uint8_t gameover, got_you, foxy_got_you, fredcheck, force_turn;
  uint8_t bed_foxy;        /* (385: Foxy gets you for looking at the bed 15 seconds) */
  uint8_t random;          /* "random": the empty bed's picture */
  int16_t freddy;          /* "Freddy counter": the Freddles */
  Anim bonnie, chica;
  uint8_t foxy, foxy_s0, foxy_s1, foxy_s2, foxy_s3, foxy_s5;   /* "foxy": where, A, B, C (the closet), D, F */
  uint8_t fred, fred_s0, fred_s1;      /* "Fredbear": where, A (his side), B (to move) */
  uint8_t fred_s4, fred_s5, fred_s6, fred_s7, fred_s12, fred_s13, fred_s14, fred_s18, fred_s19;
  uint8_t fred_s8, fred_s9;  /* frames the light is on him in a hall */
  uint8_t flash_vis;       /* "black flash": shown, A to F, S (frames) */
  int16_t bf0, bf1, bf2, bf3, bf6, bf18;
  int16_t bf_alpha;        /* its transparency: 0 black, 255 none */
  int16_t fade;            /* "black flash 2": the night's start, from black */
  uint8_t warning;         /* "door warning" (the first night until 2 AM) */
  uint8_t total;           /* "total danger" */
  uint16_t zc;
  uint8_t jump_kind;       /* (the scare playing, for the game over) */
  uint8_t dead;            /* the scare is over: game over */
  Ob ob[O_COUNT];
  uint16_t every[44];      /* frames left of each "every" timer */
  uint32_t last[52];       /* the step each "only once while true" was last true */
  uint8_t once[4];         /* "run this event once" done */
  uint32_t frame;          /* steps since the night began */
} Night;

/* Fun with Plushtrap */
typedef struct {
  uint8_t loc;             /* "plushtrap" overlapping: 0 chair, 1 hall 1, 2 far left, 3 far right, 4 hall 2,
                              5 close left, 6 close right, 7 hall 3 (the X), 8 got you */
  uint8_t light;           /* "light on" */
  uint8_t s0, s1;          /* "Active" A, B */
  int16_t active;          /* "becoming active" */
  uint8_t move, aggressive, random;
  int16_t counter;         /* "Counter": seconds left */
  uint16_t s2;             /* "Active" C: frames since GREAT! */
  uint8_t an, img, cnt, plays, done;   /* "Active"'s animation */
  uint8_t from_extras;     /* "minigame play" */
  uint32_t frame;
  uint16_t every[3];
} Plush;

/* scenes */
enum { SC_WARN, SC_TITLE, SC_EXTRAS, SC_CARD, SC_NIGHT, SC_OVER, SC_SIX, SC_PLUSHCARD, SC_PLUSH, SC_TOOBAD,
       SC_END, SC_BOX, SC_NIGHTMARE };

typedef struct {
  int scene;
  float t;                 /* seconds in the scene */
  uint32_t held, pressed;
  bool running;
  /* the save (Ini "fn4"): the night reached and the stars */
  uint8_t level, beat5, beat6, beat7, beat8;
  uint8_t reward;          /* "reward": Plushtrap caught, the next night starts at 2 AM */
  uint8_t twenty;          /* "shadow freddy" B: the extras' Nightmare is 20/20/20/20 */
  uint8_t menu, paused, confirm, pause_item;
  uint8_t ex_sel, ex_lr;   /* extras: "selection", "left/right" */
  uint8_t text, gone;      /* the ending's "text", the toys gone */
  float talk_t;            /* its "text counter" */
  uint8_t wait;            /* frames before OK does something again */
  const char *cap[4];      /* what is heard, as captions (the newest last) */
  float cap_t[4];
  const char *hold;        /* heard while it lasts (breathing, the kitchen) */
  Night n;
  Plush p;
} Game;

extern Game g;

/* game.c */
void game_init(void);
void game_step(uint32_t keys);
void game_draw(void);
uint32_t rnd(void);
uint32_t rnd_n(uint32_t n);    /* Random(n): 0..n-1 */
void start_night(int night, int shadow);
void caption(const char *s);

/* night.c */
void night_begin(int night, int shadow);
void night_step(void);
int night_top(void);           /* the object on top (O_*), -1 none */
int ob_pic(int o);             /* an object's picture now */
bool ob_wide(int o);           /* drawn in the bedroom (it pans) */
int anim_pic(int an, int k);   /* an animation's picture k */
int anim_len(int an);          /* its pictures */
int anim_count(void);
int anim_frames(int an);       /* frames one play lasts */
const char *night_keys(void);
void plush_begin(bool from_extras);
void plush_step(void);
int plush_done(void);       /* 1 caught on the X, 2 lost */
int plush_pic(void);
int scare_anim(int k);       /* extras' jumpscares: the animatronics' in their order (-1 Nightmare's picture) */

/* view.c */
void view_night(void);
void view_plush(void);

/* save.c */
void save_restore_copy(void);
void save_progress(void);
void save_load(void);
void save_resume(void);     /* the night as it is (Home) */
bool save_resume_load(void);
#endif
