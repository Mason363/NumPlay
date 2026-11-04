/* Chrome Dino for NumWorks and NumPlay: Chrome's dinosaur game, the T-Rex Runner of
 * Chromium's components/neterror/resources/dino_game (BSD license, see ../LICENSE), as
 * Chrome plays it on a screen 320 pixels wide. Its rules, numbers and pictures are
 * Chrome's, run at Chrome's 60 frames a second (with catch-up where drawing takes
 * longer); the names below are the ones of Chrome's classes (Runner, Trex, Horizon,
 * Obstacle, Cloud, NightMode, DistanceMeter, GameOverPanel).
 *
 * The screen is drawn in bands of 16 rows, in grey levels, and only the 32 x 16
 * tiles that changed since the last frame are sent to the display.
 *
 * First brought to NumPlay by LilyTheCatgirl. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_lz.h"
#include "../../common/np_text.h"
#include "sprites.h"

#ifdef __ELF__
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Chrome Dino";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

/* ---------------------------------------------------------------- the screen */
#define W 320
#define H 240
#define BAND 16
#define TILE_W 32
#define TILES_X (W / TILE_W)
#define BANDS (H / BAND)
#define CANVAS_Y 40  /* Chrome's canvas, 320 x 150, then the keys under it */
#define CANVAS_H 150
#define HINT_Y 197
#define CREDIT_Y 213

/* ---------------------------------------------------------------- Chrome's numbers */
#define FPS 60
#define TICK_MS (1000.0 / FPS)
#define MAX_CATCHUP 8               /* ticks a frame at most, after a slow one */
#define WIDTH 320                   /* Runner.dimensions */
#define DEFAULT_WIDTH 600           /* DEFAULT_DIMENSIONS.width */
/* Runner's defaultBaseConfig and normalModeConfig */
#define ACCELERATION 0.001
#define BOTTOM_PAD 10
#define CLEAR_TIME 3000
#define CLOUD_FREQUENCY 0.5
#define GAMEOVER_CLEAR_TIME 1200
#define GAP_COEFFICIENT 0.6
#define INVERT_DISTANCE 700
#define INVERT_FADE_DURATION 12000
#define MAX_BLINK_COUNT 3
#define MAX_CLOUDS 6
#define MAX_OBSTACLE_LENGTH 3
#define MAX_OBSTACLE_DUPLICATION 2
#define MAX_SPEED 13
#define MOBILE_SPEED_COEFFICIENT 1.2
#define SPEED 6
#define MAX_GAP_COEFFICIENT 1.5
#define INTRO_MS 400                /* the canvas's CSS intro: .4s ease-out */
#define INVERT_MS 1500              /* the page's CSS filter transition */
/* Trex: defaultTrexConfig and normalJumpConfig */
#define TREX_W 44
#define TREX_H 47
#define TREX_W_DUCK 59
#define DROP_VELOCITY -5
#define GRAVITY 0.6
#define MAX_JUMP_HEIGHT 30
#define MIN_JUMP_HEIGHT 30
#define INITIAL_JUMP_VELOCITY -10
#define SPEED_DROP_COEFFICIENT 3
#define START_X_POS 50
#define INTRO_DURATION 1500
#define BLINK_TIMING 7000
#define GROUND_Y (CANVAS_H - TREX_H - BOTTOM_PAD)
/* the keyboard's repeat, as on a computer: a held key presses again after 500 ms, 30 times a second */
#define REPEAT_DELAY 500
#define REPEAT_MS 33

#define KEY(k) (1ull << (k))
#define JUMP_KEYS (KEY(eadk_key_ok) | KEY(eadk_key_up) | KEY(eadk_key_eight))
#define DUCK_KEYS (KEY(eadk_key_down) | KEY(eadk_key_two))
#define RESTART_KEYS KEY(eadk_key_exe)
#define BACK_KEYS KEY(eadk_key_back)
#define QUIT_KEYS (KEY(eadk_key_home) | KEY(eadk_key_on_off))
#define SAVE_NAME "dinorun.sav"

typedef struct { int16_t x, y, w, h; } box;   /* CollisionBox */

/* ---------------------------------------------------------------- random numbers */
static uint32_t rng = 0xA319F4B5u;
static double rnd(void) {  /* Math.random() */
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (rng >> 8) * (1.0 / 16777216.0);
}
static int random_num(int lo, int hi) { return (int)(rnd() * (hi - lo + 1)) + lo; }  /* getRandomNum */
static int ifloor(double v) {
  int i = (int)v;
  return v < i ? i - 1 : i;
}
static int iceil(double v) {
  int i = (int)v;
  return v > i ? i + 1 : i;
}
static int iround(double v) { return ifloor(v + 0.5); }  /* Math.round */
/* a CSS cubic-bezier(x1, y1, x2, y2) at t (0..1) */
static float bezier(float t, float x1, float y1, float x2, float y2) {
  float lo = 0, hi = 1;
  for (int k = 0; k < 20; k++) {
    float u = (lo + hi) / 2, x = 3 * u * (1 - u) * (1 - u) * x1 + 3 * u * u * (1 - u) * x2 + u * u * u;
    if (x < t) lo = u;
    else hi = u;
  }
  float u = (lo + hi) / 2;
  return 3 * u * (1 - u) * (1 - u) * y1 + 3 * u * u * (1 - u) * y2 + u * u * u;
}

/* ---------------------------------------------------------------- the obstacles' types */
typedef struct {
  uint8_t region;
  int16_t width, height;
  int16_t ypos[3];
  uint8_t nypos;
  float multiple_speed, min_speed, speed_offset;
  int16_t min_gap;
  uint8_t frames;            /* numFrames, at frame_ms */
  float frame_ms;
  uint8_t nboxes;
  box boxes[5];
} obstacle_type;
enum { CACTUS_SMALL, CACTUS_LARGE, PTERODACTYL, TYPES };
static const obstacle_type TYPE[TYPES] = {
  {R_CACTUS_SMALL, 17, 35, {105}, 1, 4, 0, 0, 120, 0, 0, 3, {{0, 7, 5, 27}, {4, 0, 6, 34}, {10, 4, 7, 14}}},
  {R_CACTUS_LARGE, 25, 50, {90}, 1, 7, 0, 0, 120, 0, 0, 3, {{0, 12, 7, 38}, {8, 0, 7, 49}, {13, 10, 10, 38}}},
  {R_PTERODACTYL, 46, 40, {100, 75, 50}, 3, 999, 8.5f, 0.8f, 150, 2, 1000.f / 6, 5,
   {{15, 15, 16, 5}, {18, 21, 24, 6}, {2, 14, 4, 3}, {6, 10, 4, 7}, {10, 8, 6, 9}}},
};
static const box TREX_BOXES_DUCKING[1] = {{1, 18, 55, 25}};
static const box TREX_BOXES_RUNNING[6] = {{22, 0, 17, 16}, {1, 18, 30, 9}, {10, 35, 14, 8},
                                          {1, 24, 29, 5}, {5, 30, 21, 4}, {9, 34, 15, 4}};

/* ---------------------------------------------------------------- the game's state */
typedef struct {
  const obstacle_type *type;
  int size, x, y, width, gap, frame;
  float speed_offset, timer;
  bool following_created, remove;
  box boxes[5];
} obstacle;
#define MAX_OBSTACLES 8

enum { WAITING, RUNNING, CRASHED, JUMPING, DUCKING };  /* Trex's Status */
static struct {
  int x, y, x_initial, status, frame, jump_count, blink_count, shown;
  float timer, ms_per_frame, blink_delay, anim_time;
  double jump_velocity;
  bool jumping, ducking, speed_drop, reached_min_height, playing_intro;
} trex;

static struct {
  bool playing, activated, crashed, playing_intro, inverted, invert_trigger, achievement;
  double distance_ran, current_speed;
  float running_time, invert_timer, intro_time, crash_time;
  uint32_t highest_score;
  /* Horizon */
  obstacle obstacles[MAX_OBSTACLES];
  int obstacle_count;
  int history[MAX_OBSTACLE_DUPLICATION], history_count;
  struct { int x, y, gap; } clouds[MAX_CLOUDS];
  int cloud_count;
  int line_x[2], line_src[2];  /* HorizonLine: the two 600 wide stretches */
  /* NightMode */
  float moon_x, opacity;
  struct { float x; int y, sy; } stars[2];
  int phase;
  bool draw_stars;
  /* DistanceMeter */
  int max_units, digit_count;
  uint32_t max_score;
  char digits[8];
  float flash_timer;
  int flash_iterations;
  bool painted;
  /* GameOverPanel */
  float panel_timer;
  int panel_frame;
  /* the page's colours inverted (night): from, to, ms into the change */
  float inv_from, inv_to, inv_t;
} g;

/* the calculator's own: the pause menu, and leaving */
enum { MENU_NONE, MENU_PAUSE, MENU_QUIT };
static int menu, menu_sel;
static bool leave;

/* ---------------------------------------------------------------- saves */
/* "DINO", version 2, 3 bytes of nothing, the highest score Chrome keeps (the distance run, rounded up) */
static void save_best(void) {
  uint8_t d[12] = {'D', 'I', 'N', 'O', 2};
  uint32_t v = g.highest_score;
  for (int i = 0; i < 4; i++) d[8 + i] = (uint8_t)(v >> (8 * i));
  ef_write(SAVE_NAME, d, sizeof d);
}
static void load_best(void) {
  uint32_t len = 0;
  const uint8_t *s = ef_read(SAVE_NAME, &len);
  if (!s || len < 8 || s[0] != 'D' || s[1] != 'I' || s[2] != 'N' || s[3] != 'O') return;
  if (len == 12 && s[4] == 2) {
    uint32_t v = (uint32_t)s[8] | (uint32_t)s[9] << 8 | (uint32_t)s[10] << 16 | (uint32_t)s[11] << 24;
    if (v < 40000000u) g.highest_score = v;
  } else if (len == 8) {
    /* the first version kept the score as shown: the distance is 40 times it */
    uint32_t v = (uint32_t)s[4] | (uint32_t)s[5] << 8 | (uint32_t)s[6] << 16 | (uint32_t)s[7] << 24;
    if (v < 1000000u) g.highest_score = v * 40;
  }
}

/* ---------------------------------------------------------------- Trex */
static const int16_t FRAMES_WAITING[2] = {44, 0}, FRAMES_RUNNING[2] = {88, 132}, FRAMES_DUCKING[2] = {264, 323};
static const int16_t *trex_frames(int *n) {
  switch (trex.status) {
    case WAITING: *n = 2; return FRAMES_WAITING;
    case RUNNING: *n = 2; return FRAMES_RUNNING;
    case DUCKING: *n = 2; return FRAMES_DUCKING;
    default: *n = 1; return 0;  /* CRASHED 220, JUMPING 0 */
  }
}
static int trex_frame_x(void) {
  int n;
  const int16_t *f = trex_frames(&n);
  return f ? f[trex.frame] : trex.status == CRASHED ? 220 : 0;
}
static void trex_set_status(int status) {
  trex.status = status;
  trex.frame = 0;
  trex.ms_per_frame = status == WAITING ? 1000.f / 3 : status == RUNNING ? 1000.f / 12 : status == DUCKING ? 1000.f / 8
                                                                                                        : 1000.f / 60;
  if (status == WAITING) trex.anim_time = 0, trex.blink_delay = (float)iceil(rnd() * BLINK_TIMING);
}
static void trex_set_duck(bool on);
/* Trex.update(deltaTime, status) */
static void trex_update(float dt, int status) {
  trex.timer += dt;
  if (status >= 0) trex_set_status(status);
  if (trex.playing_intro && trex.x < START_X_POS) {
    trex.x += iround(((double)START_X_POS / INTRO_DURATION) * dt);
    trex.x_initial = trex.x;
  }
  if (trex.status == WAITING) {
    /* blink(): drawn again once the delay is over, and the delay starts again after the blink */
    trex.anim_time += dt;
    if (trex.anim_time >= trex.blink_delay) {
      trex.shown = trex_frame_x();
      if (trex.frame == 1) {
        trex.blink_delay = (float)iceil(rnd() * BLINK_TIMING);
        trex.anim_time = 0;
        trex.blink_count++;
      }
    }
  } else {
    trex.shown = trex_frame_x();
  }
  int n;
  trex_frames(&n);
  if (trex.timer >= trex.ms_per_frame) {
    trex.frame = trex.frame == n - 1 ? 0 : trex.frame + 1;
    trex.timer = 0;
  }
  if (trex.speed_drop && trex.y == GROUND_Y) {
    trex.speed_drop = false;
    trex_set_duck(true);
  }
}
static void trex_reset(void) {
  trex.x = trex.x_initial;
  trex.y = GROUND_Y;
  trex.jump_velocity = 0;
  trex.jumping = false;
  trex.ducking = false;
  trex_update(0, RUNNING);
  trex.speed_drop = false;
  trex.jump_count = 0;
}
static void trex_start_jump(double speed) {
  if (!trex.jumping) {
    trex_update(0, JUMPING);
    trex.jump_velocity = INITIAL_JUMP_VELOCITY - speed / 10;
    trex.jumping = true;
    trex.reached_min_height = false;
    trex.speed_drop = false;
  }
}
static void trex_end_jump(void) {
  if (trex.reached_min_height && trex.jump_velocity < DROP_VELOCITY) trex.jump_velocity = DROP_VELOCITY;
}
static void trex_update_jump(void) {
  /* (one frame of Chrome's each tick: framesElapsed is 1) */
  if (trex.speed_drop) trex.y += iround(trex.jump_velocity * SPEED_DROP_COEFFICIENT);
  else trex.y += iround(trex.jump_velocity);
  trex.jump_velocity += GRAVITY;
  if (trex.y < GROUND_Y - MIN_JUMP_HEIGHT || trex.speed_drop) trex.reached_min_height = true;
  if (trex.y < MAX_JUMP_HEIGHT || trex.speed_drop) trex_end_jump();
  if (trex.y > GROUND_Y) {
    trex_reset();
    trex.jump_count++;
  }
}
static void trex_set_duck(bool on) {
  if (on && trex.status != DUCKING) {
    trex_update(0, DUCKING);
    trex.ducking = true;
  } else if (trex.status == DUCKING) {
    trex_update(0, RUNNING);
    trex.ducking = false;
  }
}

/* ---------------------------------------------------------------- Horizon: obstacles */
static void obstacle_init(obstacle *o, const obstacle_type *t, double speed) {
  o->type = t;
  o->size = random_num(1, MAX_OBSTACLE_LENGTH);
  o->x = WIDTH + t->width;
  o->frame = 0;
  o->timer = 0;
  o->following_created = false;
  o->remove = false;
  for (int i = 0; i < t->nboxes; i++) o->boxes[i] = t->boxes[i];
  if (o->size > 1 && t->multiple_speed > speed) o->size = 1;
  o->width = t->width * o->size;
  o->y = t->nypos > 1 ? t->ypos[random_num(0, t->nypos - 1)] : t->ypos[0];
  if (o->size > 1) {
    o->boxes[1].w = (int16_t)(o->width - o->boxes[0].w - o->boxes[2].w);
    o->boxes[2].x = (int16_t)(o->width - o->boxes[2].w);
  }
  o->speed_offset = t->speed_offset ? (rnd() > 0.5 ? t->speed_offset : -t->speed_offset) : 0;
  /* getGap */
  int min_gap = iround(o->width * speed + t->min_gap * GAP_COEFFICIENT);
  int max_gap = iround(min_gap * MAX_GAP_COEFFICIENT);
  o->gap = random_num(min_gap, max_gap);
}
static bool duplicate_obstacle(int type) {
  int dup = 0;
  for (int i = 0; i < g.history_count; i++) dup = g.history[i] == type ? dup + 1 : 0;
  return dup >= MAX_OBSTACLE_DUPLICATION;
}
static void add_new_obstacle(double speed) {
  for (;;) {  /* (Chrome calls itself again until a type fits) */
    int type = random_num(0, TYPES - 1);
    if (duplicate_obstacle(type) || speed < TYPE[type].min_speed) continue;
    if (g.obstacle_count < MAX_OBSTACLES) obstacle_init(&g.obstacles[g.obstacle_count++], &TYPE[type], speed);
    for (int i = MAX_OBSTACLE_DUPLICATION - 1; i > 0; i--) g.history[i] = g.history[i - 1];
    g.history[0] = type;
    if (g.history_count < MAX_OBSTACLE_DUPLICATION) g.history_count++;
    return;
  }
}
static void update_obstacles(float dt, double speed) {
  int removed = 0;
  for (int i = 0; i < g.obstacle_count; i++) {
    obstacle *o = &g.obstacles[i];
    if (!o->remove) {
      double s = speed + o->speed_offset;
      o->x -= ifloor(s * FPS / 1000 * dt);
      if (o->type->frames) {
        o->timer += dt;
        if (o->timer >= o->type->frame_ms) {
          o->frame = o->frame == o->type->frames - 1 ? 0 : o->frame + 1;
          o->timer = 0;
        }
      }
      if (o->x + o->width <= 0) o->remove = true;
    }
    if (o->remove) removed++;  /* (Chrome takes as many off the front, the first being the oldest) */
  }
  for (int i = removed; i < g.obstacle_count; i++) g.obstacles[i - removed] = g.obstacles[i];
  g.obstacle_count -= removed;
  if (g.obstacle_count > 0) {
    obstacle *last = &g.obstacles[g.obstacle_count - 1];
    if (!last->following_created && last->x + last->width > 0 && last->x + last->width + last->gap < WIDTH) {
      add_new_obstacle(speed);
      last->following_created = true;
    }
  } else {
    add_new_obstacle(speed);
  }
}

/* ---------------------------------------------------------------- Horizon: clouds, ground, night */
static void add_cloud(void) {
  if (g.cloud_count >= MAX_CLOUDS) return;
  g.clouds[g.cloud_count].x = WIDTH;
  g.clouds[g.cloud_count].gap = random_num(100, 400);
  g.clouds[g.cloud_count].y = random_num(30, 71);
  g.cloud_count++;
}
static void update_clouds(float dt, double speed) {
  double el_speed = 0.2 / 1000 * dt * speed;
  int n = g.cloud_count;
  if (!n) {
    add_cloud();
    return;
  }
  for (int i = n - 1; i >= 0; i--) g.clouds[i].x -= iceil(el_speed);
  if (n < MAX_CLOUDS && WIDTH - g.clouds[n - 1].x > g.clouds[n - 1].gap && CLOUD_FREQUENCY > rnd()) add_cloud();
  int kept = 0;  /* (then the ones gone off the left) */
  for (int i = 0; i < g.cloud_count; i++)
    if (g.clouds[i].x + 46 > 0) g.clouds[kept++] = g.clouds[i];
  g.cloud_count = kept;
}
static void update_lines(float dt, double speed) {
  int inc = ifloor(speed * FPS / 1000 * dt);
  int a = g.line_x[0] <= 0 ? 0 : 1, b = !a;
  g.line_x[a] -= inc;
  g.line_x[b] = g.line_x[a] + 600;
  if (g.line_x[a] <= -600) {
    g.line_x[a] += 1200;
    g.line_x[b] = g.line_x[a] - 600;
    g.line_src[a] = rnd() > 0.5 ? 600 : 0;
  }
}
/* NightMode: its container is the canvas's height, 150, as Chrome gives it */
#define NIGHT_W 150
static void place_stars(void) {
  int seg = NIGHT_W / 2;
  for (int i = 0; i < 2; i++) {
    g.stars[i].x = (float)random_num(seg * i, seg * (i + 1));
    g.stars[i].y = random_num(0, 70);
    g.stars[i].sy = 9 * i;
  }
}
static float night_x(float x, float speed) { return x < -20 ? NIGHT_W : x - speed; }
static void update_night(bool activated) {
  if (activated && g.opacity == 0) {
    g.phase++;
    if (g.phase >= 7) g.phase = 0;
  }
  if (activated && (g.opacity < 1 || g.opacity == 0)) g.opacity += 0.035f;
  else if (g.opacity > 0) g.opacity -= 0.035f;
  if (g.opacity > 0) {
    g.moon_x = night_x(g.moon_x, 0.25f);
    if (g.draw_stars)
      for (int i = 0; i < 2; i++) g.stars[i].x = night_x(g.stars[i].x, 0.3f);
  } else {
    g.opacity = 0;
    place_stars();
  }
  g.draw_stars = true;
}
static void reset_night(void) {
  g.phase = 0;
  g.opacity = 0;
  update_night(false);
}
static void horizon_update(float dt, double speed, bool update_obstacles_too, bool night) {
  update_lines(dt, speed);
  update_night(night);
  update_clouds(dt, speed);
  if (update_obstacles_too) update_obstacles(dt, speed);
}
static void horizon_reset(void) {
  g.obstacle_count = 0;
  g.line_x[0] = 0, g.line_x[1] = 600;
  reset_night();
}

/* ---------------------------------------------------------------- DistanceMeter */
#define ACHIEVEMENT_DISTANCE 100
#define FLASH_DURATION 250
#define FLASH_ITERATIONS 3
static uint32_t actual_distance(uint32_t d) { return d ? (uint32_t)iround(d * 0.025) : 0; }
static void meter_update(float dt, uint32_t distance) {
  g.painted = true;
  if (!g.achievement) {
    distance = actual_distance(distance);
    if (distance > g.max_score && g.max_units == 5) g.max_units = 6, g.max_score = 999999;
    if (distance > 0 && distance % ACHIEVEMENT_DISTANCE == 0) g.achievement = true, g.flash_timer = 0;
    for (int i = g.max_units - 1; i >= 0; i--, distance /= 10) g.digits[i] = (char)('0' + distance % 10);
    g.digit_count = g.max_units;
  } else if (g.flash_iterations <= FLASH_ITERATIONS) {
    g.flash_timer += dt;
    if (g.flash_timer < FLASH_DURATION) g.painted = false;
    else if (g.flash_timer > FLASH_DURATION * 2) g.flash_timer = 0, g.flash_iterations++;
  } else {
    g.achievement = false, g.flash_iterations = 0, g.flash_timer = 0;
  }
}
static void meter_reset(void) {
  g.achievement = false;
  meter_update(0, 0);
}

/* ---------------------------------------------------------------- Runner */
static void set_speed(double s) {  /* setSpeed(): slower on a screen narrower than Chrome's */
  double mobile = s * WIDTH / DEFAULT_WIDTH * MOBILE_SPEED_COEFFICIENT;
  g.current_speed = mobile > s ? s : mobile;
}
/* how far the page is inverted (0 day, 1 night): its CSS filter's transition, 1.5s cubic-bezier(0.65, 0.05, 0.36, 1) */
static float night_e(void) {
  float t = g.inv_t >= INVERT_MS ? 1 : g.inv_t / INVERT_MS;
  return g.inv_from + (g.inv_to - g.inv_from) * bezier(t, 0.65f, 0.05f, 0.36f, 1);
}
static void invert(bool reset) {
  bool to = reset ? false : g.invert_trigger;
  if (reset) g.invert_timer = 0;
  if (to != g.inverted) {
    /* the CSS transition goes on from where the colours are */
    g.inv_from = night_e();
    g.inv_to = to ? 1 : 0;
    g.inv_t = 0;
  }
  g.inverted = to;
}
static void runner_init(void) {
  g.current_speed = SPEED;
  set_speed(g.current_speed);
  g.max_units = 5;
  g.max_score = 99999;
  meter_reset();
  g.line_x[0] = 0, g.line_x[1] = 600;
  g.line_src[0] = 0, g.line_src[1] = 600;
  g.cloud_count = 0;
  add_cloud();
  reset_night();
  g.inv_t = INVERT_MS;
  trex.y = GROUND_Y;
  trex.shown = 0;
  trex_update(0, WAITING);
}
static bool check_collision(const obstacle *o) {
  box t = {(int16_t)(trex.x + 1), (int16_t)(trex.y + 1), TREX_W - 2, TREX_H - 2};
  box b = {(int16_t)(o->x + 1), (int16_t)(o->y + 1), (int16_t)(o->type->width * o->size - 2), (int16_t)(o->type->height - 2)};
  if (!(t.x < b.x + b.w && t.x + t.w > b.x && t.y < b.y + b.h && t.h + t.y > b.y)) return false;
  const box *tb = trex.ducking ? TREX_BOXES_DUCKING : TREX_BOXES_RUNNING;
  int tn = trex.ducking ? 1 : 6;
  for (int i = 0; i < tn; i++)
    for (int j = 0; j < o->type->nboxes; j++) {
      box p = {(int16_t)(tb[i].x + t.x), (int16_t)(tb[i].y + t.y), tb[i].w, tb[i].h};
      box q = {(int16_t)(o->boxes[j].x + b.x), (int16_t)(o->boxes[j].y + b.y), o->boxes[j].w, o->boxes[j].h};
      if (p.x < q.x + q.w && p.x + p.w > q.x && p.y < q.y + q.h && p.h + p.y > q.y) return true;
    }
  return false;
}
static void game_over(void) {
  g.playing = false;
  g.crashed = true;
  g.achievement = false;
  trex_update(100, CRASHED);
  if (trex.ducking) trex.x++;  /* (Trex.draw moves a crashed ducking T-rex on by a pixel) */
  g.panel_timer = 0, g.panel_frame = 0;
  if (g.distance_ran > g.highest_score) {
    g.highest_score = (uint32_t)iceil(g.distance_ran);
    save_best();
  }
  g.crash_time = 0;
}
static void restart(void) {
  g.running_time = 0;
  g.playing = true;
  g.crashed = false;
  g.distance_ran = 0;
  set_speed(SPEED);
  meter_reset();
  horizon_reset();
  trex_reset();
  invert(true);
  g.panel_frame = 0, g.panel_timer = 0;
}
static void play_intro(void) {
  if (!g.activated && !g.crashed) {
    g.playing_intro = true;
    trex.playing_intro = true;
    g.intro_time = 0;
    g.playing = true;
    g.activated = true;
  } else if (g.crashed) {
    restart();
  }
}
/* Runner.update(), one frame of Chrome's */
static void tick(void) {
  const float dt = (float)TICK_MS;
  if (g.playing) {
    if (trex.jumping) trex_update_jump();
    g.running_time += dt;
    bool has_obstacles = g.running_time > CLEAR_TIME;
    if (trex.jump_count == 1 && !g.playing_intro) play_intro();
    if (g.playing_intro) horizon_update(0, g.current_speed, has_obstacles, false);
    else if (!g.crashed) horizon_update(g.activated ? dt : 0, g.current_speed, has_obstacles, g.inverted);
    bool collision = has_obstacles && g.obstacle_count && check_collision(&g.obstacles[0]);
    if (!collision) {
      g.distance_ran += g.current_speed * dt / TICK_MS;
      if (g.current_speed < MAX_SPEED) g.current_speed += ACCELERATION;
    } else {
      game_over();
    }
    meter_update(dt, (uint32_t)iceil(g.distance_ran));
    /* night: every 700, for 12 seconds */
    if (g.invert_timer > INVERT_FADE_DURATION) {
      g.invert_timer = 0;
      g.invert_trigger = false;
      invert(false);
    } else if (g.invert_timer) {
      g.invert_timer += dt;
    } else {
      uint32_t actual = actual_distance((uint32_t)iceil(g.distance_ran));
      if (actual > 0) {
        g.invert_trigger = !(actual % INVERT_DISTANCE);
        if (g.invert_trigger && g.invert_timer == 0) {
          g.invert_timer += dt;
          invert(false);
        }
      }
    }
    /* the canvas widens once (CSS intro), then the game starts */
    if (g.playing_intro) {
      g.intro_time += dt;
      if (g.intro_time >= INTRO_MS) {
        g.running_time = 0;
        g.playing_intro = false;
        trex.playing_intro = false;
      }
    }
  }
  if (g.playing || (!g.activated && trex.blink_count < MAX_BLINK_COUNT)) trex_update(dt, -1);
  if (g.crashed) {
    g.crash_time += dt;
    /* GameOverPanel: the logo for 875 ms, then the restart button's 7 other frames over 875 */
    g.panel_timer += dt;
    if (g.panel_frame == 0 && g.panel_timer > 875) g.panel_timer = 0, g.panel_frame = 1;
    else if (g.panel_frame > 0 && g.panel_frame < 8 && g.panel_timer >= 875.f / 8) g.panel_frame++, g.panel_timer = 0;
  }
  if (g.inv_t < INVERT_MS) g.inv_t += dt;
}

/* ---------------------------------------------------------------- keys (Runner.onKeyDown and onKeyUp) */
static void key_down_jump(void) {
  if (g.crashed || menu) return;
  if (!g.playing && !g.activated) g.playing = true;  /* the first press starts the game */
  if (!g.playing) return;
  if (!trex.jumping && !trex.ducking) trex_start_jump(g.current_speed);
}
static void key_down_duck(void) {
  if (!g.playing || g.crashed || menu) return;
  if (trex.jumping) trex.speed_drop = true, trex.jump_velocity = 1;  /* setSpeedDrop */
  else if (!trex.ducking) trex_set_duck(true);
}
static void key_up_jump(void) {
  if (g.playing) trex_end_jump();
  else if (g.crashed && g.crash_time >= GAMEOVER_CLEAR_TIME) restart();
}
static void key_up_duck(void) {
  trex.speed_drop = false;
  trex_set_duck(false);
}

/* ---------------------------------------------------------------- drawing: grey levels in a band */
static uint8_t band[W * BAND] __attribute__((aligned(4)));
static int band_y;
static uint8_t sheet[SHEET_BYTES];
static uint8_t tone[4];  /* the sheet's tones on screen this frame (0 unused) */
static uint8_t bg, ink, soft;
static int clip_x0, clip_x1, clip_y0, clip_y1;  /* what may be drawn on (the canvas, or the screen) */

static uint8_t inv(float e, int v) { return (uint8_t)(v + (255 - 2 * v) * e + 0.5f); }
static void clip_screen(void) { clip_x0 = 0, clip_x1 = W, clip_y0 = 0, clip_y1 = H; }
static void fill(int x, int y, int w, int h, uint8_t c) {
  int x0 = x < clip_x0 ? clip_x0 : x, x1 = x + w > clip_x1 ? clip_x1 : x + w;
  int y0 = y < clip_y0 ? clip_y0 : y, y1 = y + h > clip_y1 ? clip_y1 : y + h;
  if (y0 < band_y) y0 = band_y;
  if (y1 > band_y + BAND) y1 = band_y + BAND;
  for (int r = y0; r < y1; r++)
    for (int c2 = x0; c2 < x1; c2++) band[(r - band_y) * W + c2] = c;
}
/* a w x h part of a region at (sx, sy) of it, drawn at (dx, dy), at alpha a (0..256) */
static void blit(int region, int sx, int sy, int w, int h, int dx, int dy, int a) {
  if (a <= 0) return;
  const dino_region *rg = &REGIONS[region];
  int x0 = dx < clip_x0 ? clip_x0 : dx, x1 = dx + w > clip_x1 ? clip_x1 : dx + w;
  int y0 = dy < clip_y0 ? clip_y0 : dy, y1 = dy + h > clip_y1 ? clip_y1 : dy + h;
  if (y0 < band_y) y0 = band_y;
  if (y1 > band_y + BAND) y1 = band_y + BAND;
  for (int y = y0; y < y1; y++) {
    const uint8_t *src = sheet + rg->off + (sy + y - dy) * rg->stride;
    uint8_t *d = band + (y - band_y) * W;
    for (int x = x0; x < x1; x++) {
      int u = sx + x - dx, t = src[u >> 2] >> (2 * (u & 3)) & 3;
      if (!t) continue;
      if (a >= 256) d[x] = tone[t];
      else d[x] = (uint8_t)(d[x] + ((tone[t] - d[x]) * a >> 8));
    }
  }
}
static bool band_meets(int y, int h) { return y < band_y + BAND && y + h > band_y; }

/* ---- text: capitals 5 x 7 (lowercase drawn as capitals), columns of 7 bits, top bit first */
static const uint8_t FONT[59][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};
static int glyph_of(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  return ch < 32 || ch > 'Z' ? '?' - 32 : ch - 32;
}
static uint8_t pen;
static void glyph(char ch, int x, int y, int k) {
  const uint8_t *gl = FONT[glyph_of(ch)];
  for (int i = 0; i < 5; i++)
    for (int j = 0, b = gl[i]; b; j++, b >>= 1)
      if (b & 1) fill(x + i * k, y + j * k, k, k, pen);
}
#if NP_TEXT_EXTRA
/* Other languages: an accented letter is the font's capital with np_accent() over it (a cedilla
   under it); a Chinese one comes from the 12-pixel font, 12 pixels tall up to size 2, then 24. */
static void pen_px(int x, int y, void *ctx) { fill(x, y, 1, 1, pen); }
static int xk(int k) { return (7 * k + 6) / 12; }
static int letter(const char **s, int x, int y, int k, bool draw) {
  uint32_t cp = np_utf8(s);
  char b = (char)cp, b2 = 0;
  int acc = 0;
  if (cp >= 0x80 && (acc = np_latin(cp, &b, &b2), !b)) {
    if (draw) np_xdraw(cp, x, y + (7 * k - 12 * xk(k)) / 2, xk(k), pen_px, 0);
    return np_xadvance(cp, xk(k));
  }
  if (draw) {
    glyph(b, x, y, k);
    np_accent(acc, x, acc == NP_ACC_CEDIL ? y + 7 * k : y - 2 * k - 1, k, pen_px, 0);
    if (b2) glyph(b2, x + 6 * k, y, k);
  }
  return (b2 ? 12 : 6) * k;
}
static int tw(const char *s, int k) {
  int w = -k;
  while (*s) w += letter(&s, 0, 0, k, false);
  return w;
}
static void text(const char *s, int x, int y, int k, uint8_t c) {
  if (!band_meets(y - 3 * k - 3, 11 * k + 6)) return;
  pen = c;
  while (*s) x += letter(&s, x, y, k, true);
}
#else
static int tw(const char *s, int k) {
  int n = 0;
  while (s[n]) n++;
  return n * 6 * k - k;
}
static void text(const char *s, int x, int y, int k, uint8_t c) {
  if (!band_meets(y, 7 * k)) return;
  pen = c;
  for (; *s; s++, x += 6 * k) glyph(*s, x, y, k);
}
#endif
static void ctext(const char *s, int cx, int y, int k, uint8_t c) { text(s, cx - tw(s, k) / 2, y, k, c); }

/* ---------------------------------------------------------------- a frame */
static int container_w(void) {  /* the canvas's container, 44 wide until the intro widens it (ease-out) */
  if (!g.activated) return TREX_W;
  if (!g.playing_intro) return WIDTH;
  float t = g.intro_time / INTRO_MS;
  return TREX_W + (int)((WIDTH - TREX_W) * bezier(t > 1 ? 1 : t, 0, 0, 0.58f, 1));  /* ease-out */
}
static void draw_canvas(int cw) {
  clip_x0 = 0, clip_x1 = cw, clip_y0 = CANVAS_Y, clip_y1 = CANVAS_Y + CANVAS_H;
  if (!band_meets(CANVAS_Y, CANVAS_H)) return;
  const int Y = CANVAS_Y;
  /* HorizonLine */
  for (int k = 0; k < 2; k++) blit(R_HORIZON, g.line_src[k], 0, 600, 12, g.line_x[k], Y + 127, 256);
  /* NightMode */
  if (g.opacity > 0) {
    int a = g.opacity >= 1 ? 256 : (int)(g.opacity * 256);
    static const uint8_t PHASES[7] = {140, 120, 100, 60, 40, 20, 0};
    if (g.draw_stars)
      for (int i = 0; i < 2; i++) blit(R_STAR, 0, g.stars[i].sy, 9, 9, iround(g.stars[i].x), Y + g.stars[i].y, a);
    blit(R_MOON, PHASES[g.phase], 0, g.phase == 3 ? 40 : 20, 40, iround(g.moon_x), Y + 30, a);
  }
  /* Cloud */
  for (int i = 0; i < g.cloud_count; i++) blit(R_CLOUD, 0, 0, 46, 14, g.clouds[i].x, Y + g.clouds[i].y, 256);
  /* Obstacle */
  for (int i = 0; i < g.obstacle_count; i++) {
    const obstacle *o = &g.obstacles[i];
    int w = o->type->width * o->size, sx = (int)(o->type->width * o->size * (0.5 * (o->size - 1)));
    sx += o->type->width * o->frame;
    blit(o->type->region, sx, 0, w, o->type->height, o->x, Y + o->y, 256);
  }
  /* DistanceMeter (from the first frame on, as Chrome draws it when it starts) */
  int mx = WIDTH - 11 * (5 + 1);
  if (g.painted)
    for (int i = 0; i < g.digit_count; i++) blit(R_TEXT, 10 * (g.digits[i] - '0'), 0, 10, 13, mx + i * 11, Y + 5, 256);
  if (g.highest_score) {
    uint32_t v = actual_distance(g.highest_score);
    int hx = mx - g.max_units * 2 * 10;
    blit(R_TEXT, 100, 0, 10, 13, hx, Y + 5, 204);   /* H */
    blit(R_TEXT, 110, 0, 10, 13, hx + 11, Y + 5, 204);  /* I */
    for (int i = g.max_units - 1; i >= 0; i--, v /= 10)
      blit(R_TEXT, 10 * (int)(v % 10), 0, 10, 13, hx + (3 + i) * 11, Y + 5, 204);
  }
  /* Trex */
  if (trex.status == CRASHED) blit(R_TREX_CRASH, 0, 0, TREX_W, TREX_H, trex.x, Y + trex.y, 256);
  else if (trex.ducking) blit(R_TREX_DUCK, trex.shown - 264, 0, TREX_W_DUCK, TREX_H, trex.x, Y + trex.y, 256);
  else blit(R_TREX, trex.shown, 0, TREX_W, TREX_H, trex.x, Y + trex.y, 256);
  /* GameOverPanel: its frames drawn over one another, as Chrome does */
  if (g.crashed) {
    blit(R_TEXT, 0, 13, 191, 11, iround(WIDTH / 2.0 - 191 / 2.0), Y + iround((CANVAS_H - 25) / 3.0), 256);
    for (int f = 0; f <= g.panel_frame && f < 8; f++) blit(R_RESTART, 36 * f, 0, 36, 32, WIDTH / 2 - 16, Y + CANVAS_H / 2, 256);
  }
}
static void button(int x, int y, int w, const char *s, bool sel) {
  fill(x, y, w, 15, sel ? ink : bg);
  if (!sel) fill(x, y, w, 1, soft), fill(x, y + 14, w, 1, soft), fill(x, y, 1, 15, soft), fill(x + w - 1, y, 1, 15, soft);
  ctext(s, x + w / 2, y + 4, 1, sel ? bg : ink);
}
static void draw_menu(void) {
  clip_screen();
  const int x = 70, y = CANVAS_Y + 20, w = 180, h = 108;
  if (!band_meets(y, h)) return;
  fill(x, y, w, h, bg);
  fill(x, y, w, 2, ink), fill(x, y + h - 2, w, 2, ink), fill(x, y, 2, h, ink), fill(x + w - 2, y, 2, h, ink);
  if (menu == MENU_PAUSE) {
    ctext(T("Paused"), 160, y + 12, 2, ink);
    static const char *const ITEMS[3] = {T("Resume"), T("Restart"), T("Quit game")};
    for (int i = 0; i < 3; i++) button(x + 30, y + 40 + i * 21, w - 60, ITEMS[i], menu_sel == i);
  } else {
    ctext(T("Quit game?"), 160, y + 26, 2, ink);
    button(x + 24, y + 62, 56, T("Yes"), menu_sel == 1);
    button(x + w - 80, y + 62, 56, T("No"), menu_sel == 0);
  }
}
static void draw_hints(void) {
  clip_screen();
  const char *h = menu == MENU_PAUSE ? T("Up/Down: choose   OK: confirm   Back: resume")
                  : menu ? T("Left/Right: choose   OK: confirm")
                  : g.crashed ? T("OK: play again   Back: quit")
                  : !g.activated && !g.playing ? T("OK: start   Back: quit")
                  : T("OK or Up: jump   Down: duck   Back: pause");
  ctext(h, 160, HINT_Y, 1, soft);
  if (!g.activated && !g.playing) {
    ctext(T("Chrome's dinosaur game, not affiliated with Google"), 160, CREDIT_Y, 1, soft);
    ctext(T("Made by LilyTheCatgirl and Mason Chen for NumPlay"), 160, CREDIT_Y + 12, 1, soft);
  }
}

static uint32_t hashes[BANDS][TILES_X];
static bool hashes_valid;
static uint16_t upload[W * BAND];
static uint16_t rgb(uint8_t v) { return (uint16_t)((v >> 3) << 11 | (v >> 2) << 5 | (v >> 3)); }
static uint32_t tile_hash(int x) {
  uint32_t h = 2166136261u;
  for (int y = 0; y < BAND; y++) {
    const uint32_t *p = (const uint32_t *)(const void *)(band + y * W + x);
    for (int i = 0; i < TILE_W / 4; i++) h = (h ^ p[i]) * 16777619u;
  }
  return h;
}
static void send_band(int b) {
  int run = -1;
  for (int c = 0; c <= TILES_X; c++) {
    bool changed = false;
    if (c < TILES_X) {
      uint32_t h = tile_hash(c * TILE_W);
      changed = !hashes_valid || hashes[b][c] != h;
      hashes[b][c] = h;
    }
    if (changed && run < 0) run = c;
    if (!changed && run >= 0) {
      int x = run * TILE_W, w = (c - run) * TILE_W;
      for (int y = 0; y < BAND; y++)
        for (int i = 0; i < w; i++) upload[y * w + i] = rgb(band[y * W + x + i]);
      eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)(b * BAND), (uint16_t)w, BAND}, upload);
      run = -1;
    }
  }
}
static void frame(void) {
  float e = night_e();
  bg = inv(e, 255);
  tone[1] = inv(e, 83), tone[2] = inv(e, 218), tone[3] = inv(e, 255);
  ink = tone[1];
  soft = inv(e, 140);
  int cw = container_w();
  for (int b = 0; b < BANDS; b++) {
    band_y = b * BAND;
    for (int i = 0; i < W * BAND; i++) band[i] = bg;
    draw_canvas(cw);
    if (menu) draw_menu();
    draw_hints();
    send_band(b);
  }
  hashes_valid = true;
}

/* ---------------------------------------------------------------- keys and the loop */
typedef struct { bool held; float held_ms, repeat_at; } key_state;
static key_state jump_key, duck_key;
/* a key's presses this frame, the keyboard's repeat included */
static int presses(key_state *k, bool down, float dt) {
  int n = 0;
  if (down && !k->held) n = 1, k->held_ms = 0, k->repeat_at = REPEAT_DELAY;
  else if (down) {
    k->held_ms += dt;
    while (k->held_ms >= k->repeat_at) n++, k->repeat_at += REPEAT_MS;
  }
  return n;
}
static void menu_input(uint64_t hit) {
  if (menu == MENU_PAUSE) {
    if (hit & BACK_KEYS) menu = MENU_NONE;
    else if (hit & KEY(eadk_key_up)) menu_sel = (menu_sel + 2) % 3;
    else if (hit & KEY(eadk_key_down)) menu_sel = (menu_sel + 1) % 3;
    else if (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) {
      if (menu_sel == 0) menu = MENU_NONE;
      else if (menu_sel == 1) menu = MENU_NONE, restart();
      else menu = MENU_QUIT, menu_sel = 0;
    }
  } else {
    if (hit & BACK_KEYS) menu = MENU_PAUSE, menu_sel = 2;
    else if (hit & (KEY(eadk_key_left) | KEY(eadk_key_right))) menu_sel = !menu_sel;
    else if (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) {
      if (menu_sel) leave = true;
      else menu = MENU_PAUSE, menu_sel = 2;
    }
  }
}
/* the keys of a frame: dt ms since the last */
static void input(uint64_t keys, uint64_t hit, uint64_t released, float dt) {
  if (keys & QUIT_KEYS) {
    leave = true;
    return;
  }
  if (menu) {
    menu_input(hit);
    jump_key.held = (keys & JUMP_KEYS) != 0, duck_key.held = (keys & DUCK_KEYS) != 0;
    return;
  }
  if (hit & BACK_KEYS) {
    if (g.playing && !g.crashed) menu = MENU_PAUSE, menu_sel = 0;
    else leave = true;
    return;
  }
  bool j = (keys & JUMP_KEYS) != 0, d = (keys & DUCK_KEYS) != 0;
  for (int n = presses(&jump_key, j, dt); n > 0; n--) key_down_jump();
  for (int n = presses(&duck_key, d, dt); n > 0; n--) key_down_duck();
  if (!j && jump_key.held) key_up_jump();
  if (!d && duck_key.held) key_up_duck();
  jump_key.held = j, duck_key.held = d;
  if (g.crashed && (released & RESTART_KEYS)) restart();
}

int main(void) {
  np_app_begin();
  np_lz_unpack(SHEET_LZ, sizeof SHEET_LZ, sheet, sizeof sheet);
  rng ^= (uint32_t)eadk_timing_millis() * 2654435761u;
  if (!rng) rng = 1;
  load_best();
  runner_init();
  uint64_t held = eadk_keyboard_scan();
  uint32_t last = (uint32_t)eadk_timing_millis();
  double owed = 0;
  while (!leave) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    float dt = (float)(now - last);
    last = now;
    uint64_t keys = eadk_keyboard_scan(), hit = keys & ~held, released = held & ~keys;
    held = keys;
    input(keys, hit, released, dt);
    if (leave) break;
    if (!menu) {
      owed += dt;
      int n = 0;
      while (owed >= TICK_MS && n < MAX_CATCHUP) tick(), owed -= TICK_MS, n++;
      if (owed >= TICK_MS) owed = 0;  /* (a long stall: the game waits instead of rushing) */
    } else {
      owed = 0;
    }
    frame();
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  return np_app_end();
}
