/* Among Us: the game's state and rules. Everything here is plain data (no pointers), so a game is saved by copying
 * it. Times are in seconds; the logic runs at the game's fixed 30 ticks a second. */
#ifndef AU_GAME_H
#define AU_GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "ship.h"

#define DT (1.0f / 30)
#define MAXP 10
#define MAX_TASKS 10
#define NONE 0xFF
#define SKIP 10   /* a vote to skip */

/* the game's TaskTypes */
enum {
  T_SUBMIT_SCAN, T_PRIME_SHIELDS, T_FUEL_ENGINES, T_CHART_COURSE, T_START_REACTOR, T_SWIPE_CARD, T_CLEAR_ASTEROIDS,
  T_UPLOAD_DATA, T_INSPECT_SAMPLE, T_EMPTY_CHUTE, T_EMPTY_GARBAGE, T_ALIGN_ENGINE, T_FIX_WIRING, T_CALIBRATE,
  T_DIVERT_POWER, T_UNLOCK_MANIFOLDS, T_RESET_REACTOR, T_FIX_LIGHTS, T_CLEAN_O2, T_FIX_COMMS, T_RESTORE_O2,
  T_STABILIZE, N_TASK_TYPES
};
/* the sabotages */
enum { SAB_NONE, SAB_REACTOR, SAB_O2, SAB_LIGHTS, SAB_COMMS };
/* the screens */
enum { S_TITLE, S_LOBBY, S_INTRO, S_PLAY, S_REPORT, S_MEETING, S_EJECT, S_END, S_HOWTO, S_PAUSE, S_KILLED };
/* what the player has open over the ship */
enum { O_NONE, O_TASK, O_MAP, O_SABMAP, O_ADMIN, O_CAMS, O_EMERGENCY };

typedef struct {
  uint8_t def;     /* the task in TASK_DEFS */
  uint8_t step;
  uint8_t data[3]; /* wiring: the consoles' ids in order */
  uint8_t pad;
  float timer;     /* Inspect Sample's wait */
} Task;

typedef struct {
  uint8_t state, task, cons, target;  /* what the bot is doing, and with what */
  uint8_t plen, pi, pad0, pad1;
  uint16_t path[40];                  /* nav cells to walk through */
  V2 goal;
  float t, think, stuck_t;
  V2 last;
  /* what the bot knows: when and where it last saw each player, how much it suspects them */
  float seen_t[MAXP];
  uint8_t seen_room[MAXP];
  int8_t sus[MAXP];
  uint8_t saw_kill, saw_vent, saw_vent_room, saw_kill_room;  /* who (NONE) */
  uint8_t near_body, body_room, found_body, said;              /* who it saw near the body it found */
  uint8_t claim_room, with, scanned, pad2;                     /* its alibi */
  float with_t[MAXP];                                          /* time spent with each player lately */
} Bot;

typedef struct {
  V2 pos;          /* the feet (the collider's middle) */
  V2 vel;
  float anim_t, kill_cd, busy_t;
  uint8_t color, alive, imp, flip;
  uint8_t vent, vent_anim, room, walking;  /* vent: the vent it's in, or NONE */
  uint8_t ntasks, emergencies, busy, pad;   /* busy: doing a task (bots) */
  Task tasks[MAX_TASKS];
  Bot bot;
} Player;

typedef struct {
  V2 pos;
  uint8_t who, on, pad[2];
} Body;

typedef struct {
  uint8_t who, kind, a, b;   /* a chat line: who says what (LINE_*), about whom and where */
} Line;

/* the game's settings (its lobby options, with the original's defaults) */
typedef struct {
  uint8_t bots, impostors, role, color;   /* role: 0 random, 1 crewmate, 2 impostor */
  uint8_t kill_cd;      /* seconds, 10-60 by 2.5 halves: stored as tenths? no: seconds x2 */
  uint8_t kill_dist;    /* 0 short, 1 medium, 2 long */
  uint8_t crew_vision, imp_vision;   /* x0.25 steps: 4 = 1.0x */
  uint8_t speed;        /* x0.25: 4 = 1.0x */
  uint8_t common, longt, shortt;
  uint8_t meetings, meet_cd, discuss, vote;   /* emergency meetings, cooldown s, discussion s, voting s/5 */
  uint8_t confirm, anon, visual, taskbar;      /* taskbar: 0 always, 1 meetings, 2 never */
} Options;

typedef struct {
  uint8_t screen, overlay, back_screen, sel;
  uint8_t nplayers, me, winner, end_reason;   /* winner: 0 crew, 1 impostors */
  float t, screen_t;
  uint32_t rng;
  Options opt;
  Player p[MAXP];
  Body bodies[MAXP];
  /* doors: time each stays closed, its animation, and each room's cooldown */
  float door_t[16], door_anim[16];
  float door_cd[N_ROOMS];
  /* sabotage */
  uint8_t sab, switches, o2_done, sab_fixing;
  float sab_t, sab_cd, light, sab_age;   /* sab_age: how long the sabotage has been on */
  uint32_t o2_code;
  uint8_t reactor_hold[2], comms_ok, pad0;
  float reactor_held;
  float emergency_cd;
  /* meeting */
  uint8_t caller, reported, meet_stage, ejected;
  uint8_t votes[MAXP];
  uint8_t voted_me, tie, nlines, line_shown;
  Line lines[32];
  float meet_t, line_t;
  /* vents: when each opened, for its lid */
  float vent_t[16];
  /* the player's view */
  V2 cam;
  uint8_t killer, kill_anim, task_open, mg_cons;
  float kill_t;
  uint8_t room_shown, pad1[3];
  float room_t;
  float task_bar;   /* shown (it updates only in meetings when set so) */
  uint8_t mg[512];  /* the open minigame's own state */
} Game;

extern Game g;
extern bool autopilot;
/* is player i played by the bots' brain (the others, and in tests the player too) */
static inline bool is_bot(int i) { return i != g.me || autopilot; }

/* the tasks a ship gives (ShipStatus' lists) */
typedef struct {
  uint8_t type, room, steps, kind, target;   /* kind 0 common, 1 long, 2 short; target: divert power's room */
  const char *name;
} TaskDef;
extern const TaskDef TASK_DEFS[];
extern const int N_TASK_DEFS;

/* colours (Palette.PlayerColors and ShadowColors) */
#define N_COLORS 12
extern const uint32_t BODY_COLORS[N_COLORS], SHADOW_COLORS[N_COLORS];
extern const char *const COLOR_NAMES[N_COLORS];
extern uint16_t color_pal[N_COLORS][256];

uint32_t rnd(void);
static inline int rnd_n(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }
float rnd_f(void);

void game_init(void);
void game_tick(uint32_t keys);
void game_draw(void);
void game_new(void);           /* a new game from the lobby's settings */
void spawn_at_table(int i);
bool game_save(void);
bool game_load(void);
void settings_save(void);

/* rules (game.c) */
float player_speed(int i);
float vision_radius(int i);
float kill_distance(void);
float kill_cooldown(void);
bool task_done(const Task *t);
int task_steps(const Task *t);
int task_console(const Player *P, const Task *t);   /* the console of its current step (-1: any of a kind) */
bool console_serves(int c, const Task *t);
const char *task_name(const Task *t);
int task_room(const Task *t);
void task_step_done(int pl, int ti);
float tasks_progress(void);
int living(bool imp);
void kill_player(int killer, int victim);
void start_meeting(int caller, int body);
void sabotage(int kind);
void close_doors(int room);
bool sabotage_ready(void);
void fix_sabotage(void);
int nearest_vent(V2 p, float maxd);
bool can_see(int a, int b);
void check_win(void);
void enter_vent(int i, int v);
void exit_vent(int i);
void move_vent(int i, int v);

/* bots (bot.c) */
void bot_init(int i);
void bot_tick(int i);
void bot_meeting_start(void);
void bot_meeting_tick(void);
void bot_votes(void);
void bot_see(int i);
void bot_saw_kill(int i, int killer);
void bots_dispatch(void);

/* the minigames (mg.c) */
void mg_open(int cons, int task);
void mg_tick(uint32_t keys, uint32_t pressed);
void mg_draw(void);
bool mg_blocks_move(void);

/* drawing (view.c, hud.c, screens.c) */
void view_draw(void);
void hud_draw(void);
void screens_tick(uint32_t keys, uint32_t pressed);
void screens_draw(void);
void map_draw(int mode);
void map_open(int mode);
void map_tick(uint32_t pressed, int mode);
void meeting_tick(uint32_t keys, uint32_t pressed);
void meeting_draw(void);
const char *line_text(const Line *l, char *buf, int n);
#endif
