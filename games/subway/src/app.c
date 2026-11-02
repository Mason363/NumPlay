#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* screens over the game: smaller over faster */
#endif
/* The screens and the way between them (app.h). */
#include <string.h>
#include "actors.h"
#include "app.h"
#include "data.h"
#include "draw.h"
#include "game.h"
#include "icons.h"
#include "plat.h"
#include "save.h"
#include "scene.h"

enum { S_HELP, S_TITLE, S_RUN, S_PAUSE, S_SAVEME, S_RESULTS, S_SHOP, S_MISSIONS, S_QUIT };
bool app_running = true;
static int state, back_to;      /* the screen, and where Back goes from the shop, the missions or Quit */
static int sel;                 /* the button in focus */
static float clock, intro, saveme_t, state_t;
static bool new_best;
static int shop_tab, shop_sel, shop_top;
static char note[40];           /* the shop's word on the last try */
static float note_t;
static float cam_x, cam_y;      /* the run's camera, smoothed */
static bool drawn;              /* a screen that does not move is drawn once, then again when something changes */
static bool hit_train;          /* the crash was into a train's front */
static void runner_anim(float dt);
static Anim a_run, a_guard, a_dog, a_shop;
/* the game's run camera: 33 units up and 33 behind the runner, following 0.75 of its moves across */
#define CAM_Y (33 * U)
#define CAM_BACK (33 * U)
#define CAM_PITCH 0.36f
#define CAM_F 255.0f
#define CAM_CY 104.0f


static void fmt(char *out, uint32_t v, bool commas) {
  char t[16];
  int n = 0;
  do {
    if (commas && n % 4 == 3) t[n++] = ',';
    t[n++] = (char)('0' + v % 10), v /= 10;
  } while (v);
  for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
  out[n] = 0;
}
static void cat(char *out, const char *a) {
  while (*out) out++;
  while (*a) *out++ = *a++;
  *out = 0;
}

static void title_world(void) {
  world_reset(plat_random());
  world_title();
  memset(&pl, 0, sizeof pl);
  pl.lane = 1, pl.x = 0, pl.speed = 13;
}

/* the title's yard behind the screen */
static bool title_scene(void) {
  return state == S_TITLE || state == S_HELP || state == S_MISSIONS || (state == S_QUIT && back_to == S_TITLE);
}

static void go(int s) {
  drawn = false;
  state = s;
  state_t = 0;
  sel = 0;
}

void app_init(void) {
  save_load();
  scene_init();
  title_world();
  go(pg.help_seen ? S_TITLE : S_HELP);
}

static void start_run(void) {
  if (state != S_TITLE) title_world();
  run_start();
  intro = 0;
  cam_x = 0, cam_y = CAM_Y;
  go(S_RUN);
}

static void end_run(void) {
  new_best = run.score > pg.best && run.score > 0;
  run_bank();
  go(S_RESULTS);
}

/* ---------------------------------------------------------------- the shop's shelves */
enum { TAB_UPGRADES, TAB_ITEMS, TAB_CHARS, TAB_BOARDS, TABS };
static const char *const tab_names[TABS] = {"Upgrades", "Items", "Characters", "Boards"};
static const char *const upg_names[NUPG] = {"Jetpack", "Super Sneakers", "Coin Magnet", "2x Multiplier"};
static const uint8_t upg_icons[NUPG] = {ICON_JETPACK, ICON_SNEAKERS, ICON_MAGNET, ICON_DOUBLE};
static const char *const item_names[4] = {"Hoverboard", "Headstart", "Score Booster", "Mystery Box"};
static const char *const item_text[4] = {"A crash forgiven: OK in a run", "Fly ahead: 1 at the start",
                                         "+5 multiplier: 3 at the start", "Coins, keys and more inside"};
static const uint16_t item_price[4] = {300, 2000, 3000, 500};
static const uint8_t item_icons[4] = {ICON_BOARD, ICON_HEADSTART, ICON_BOOSTER, ICON_MYSTERY};

static int shop_rows(void) { return shop_tab == TAB_UPGRADES ? NUPG : shop_tab == TAB_ITEMS ? 4 : shop_tab == TAB_CHARS ? NCHARS : NBOARDS; }

static void say(const char *s) {
  strncpy(note, s, sizeof note - 1);
  note[sizeof note - 1] = 0;
  note_t = 2;
}

static bool pay(uint32_t price) {
  if (pg.coins < price) {
    say("Not enough coins");
    return false;
  }
  pg.coins -= price;
  return true;
}

static void shop_buy(void) {
  int i = shop_sel;
  switch (shop_tab) {
    case TAB_UPGRADES: {
      int c = upgrade_cost(pg.upg[i]);
      if (!c) say("Already at the top");
      else if (pay((uint32_t)c)) pg.upg[i]++, say("Upgraded!");
      break;
    }
    case TAB_ITEMS:
      if (!pay(item_price[i])) break;
      if (i == 0) pg.boards++, say("+1 hoverboard");
      else if (i == 1) pg.headstarts++, say("+1 headstart");
      else if (i == 2) pg.boosters++, say("+1 score booster");
      else {
        pg.total[CNT_MYSTERY]++;
        uint32_t r = plat_random() % 100;
        if (r < 55) {
          uint32_t c = 100 + plat_random() % 9 * 100;
          pg.coins += c;
          char s[40] = "+";
          fmt(s + 1, c, true);
          cat(s, " coins");
          say(s);
        } else if (r < 70) pg.keys++, say("A key!");
        else if (r < 85) pg.boards++, say("A hoverboard!");
        else if (r < 93) pg.headstarts++, say("A headstart!");
        else if (r < 99) pg.boosters++, say("A score booster!");
        else pg.coins += 20000, say("JACKPOT! 20,000 coins");
      }
      break;
    case TAB_CHARS:
      if (pg.chars & 1 << i) pg.character = (uint8_t)i, say("Selected");
      else if (pay(char_price[i])) pg.chars |= (uint8_t)(1 << i), pg.character = (uint8_t)i, say("Unlocked!");
      break;
    default:
      if (pg.ownboards & 1 << i) pg.board = (uint8_t)i, say("Selected");
      else if (pay(board_price[i])) pg.ownboards |= (uint8_t)(1 << i), pg.board = (uint8_t)i, say("Unlocked!");
      break;
  }
  save_write();
}

/* ---------------------------------------------------------------- a 60th of a second */
static void camera_follow(void) {
  float tx = pl.x * 0.75f, ty = CAM_Y + pl.ground * 0.85f + (pl.y - pl.ground) * 0.35f;
  if (pl.fly > 0) ty = CAM_Y + pl.y * 0.8f;
  /* under a roof: from a little before it (the game lowers its camera there) */
  float roof = 1000;
  for (float dz = -CAM_BACK; dz <= 16; dz += 2) {
    float r = world_roof(pl.z + dz);
    if (r < roof) roof = r;
  }
  if (ty > roof - 0.5f) ty = roof - 0.5f;
  /* (the game's follow speeds: 15 across, 7.5 up and down; at once down under a roof) */
  cam_x += (tx - cam_x) * (1 - 1 / (1 + 15 * DT));
  cam_y += (ty - cam_y) * (1 - 1 / (1 + 7.5f * DT));
  if (cam_y > roof - 0.5f) cam_y = roof - 0.5f;
}

void app_step(uint32_t keys, uint32_t pressed) {
  if (pressed) drawn = false;
  clock += DT;
  state_t += DT;
  if (note_t > 0) note_t -= DT;
  if (pressed & K_HOME) {
    /* save and quit, from anywhere: a run's coins and progress are kept */
    if (state == S_RUN || state == S_PAUSE || state == S_SAVEME || (state == S_QUIT && back_to == S_PAUSE)) run_bank();
    else save_write();
    app_running = false;
    return;
  }
  switch (state) {
    case S_HELP:
      if (pressed & (K_OK | K_BACK)) {
        pg.help_seen = 1;
        save_write();
        go(S_TITLE);
      }
      break;
    case S_TITLE:
      /* OK plays; Down goes to the buttons below: the shop, the missions, how to play */
      if (sel == 0) {
        if (pressed & K_OK) start_run();
        else if (pressed & K_DOWN) sel = 1;
        else if (pressed & K_BACK) back_to = S_TITLE, go(S_QUIT), sel = 0;
      } else {
        if (pressed & K_LEFT && sel > 1) sel--;
        if (pressed & K_RIGHT && sel < 3) sel++;
        if (pressed & (K_UP | K_BACK)) sel = 0;
        if (pressed & K_OK) {
          int s = sel;
          back_to = S_TITLE;
          if (s == 1) go(S_SHOP), shop_sel = shop_top = 0;
          else if (s == 2) go(S_MISSIONS);
          else go(S_HELP);
        }
      }
      break;
    case S_RUN:
      if (intro < 1) intro += DT / 0.9f;
      if (pressed & K_BACK && !pl.dead) {
        go(S_PAUSE);
        break;
      }
      run_step(keys, pressed);
      if (pl.ev & EV_CRASH && pl.dead) {
        /* into a train's front? */
        hit_train = false;
        for (int i = 0; i < nobs; i++)
          if ((obs[i].type == O_TRAIN || obs[i].type == O_MOVING) && obs[i].lane == pl.lane && obs[i].z0 > pl.z - 0.5f && obs[i].z0 < pl.z + 1.5f)
            hit_train = true;
      }
      camera_follow();
      if (run_over()) {
        if (pg.keys + run.keys >= (uint32_t)run_save_cost()) {
          /* (keys picked up this run count too) */
          pg.keys += run.keys, run.keys = 0;
          go(S_SAVEME);
        } else end_run();
      }
      break;
    case S_PAUSE:
      /* QUIT (1) on the left, RESUME (0) on the right */
      if (pressed & (K_LEFT | K_UP)) sel = 1;
      if (pressed & (K_RIGHT | K_DOWN)) sel = 0;
      if (pressed & K_BACK || (pressed & K_OK && sel == 0)) go(S_RUN);
      else if (pressed & K_OK) back_to = S_PAUSE, go(S_QUIT);
      break;
    case S_SAVEME:
      saveme_t += DT;
      if (pressed & K_OK) {
        run_revive();
        go(S_RUN);
      } else if (pressed & K_BACK || state_t > 5) end_run();
      break;
    case S_RESULTS:
      if (pressed & K_LEFT && sel > 0) sel--;
      if (pressed & K_RIGHT && sel < 2) sel++;
      if (pressed & K_BACK) title_world(), go(S_TITLE);
      else if (pressed & K_OK) {
        if (sel == 0) {
          title_world();
          start_run();
        } else if (sel == 1) back_to = S_RESULTS, go(S_SHOP), shop_sel = shop_top = 0;
        else title_world(), go(S_TITLE);
      }
      break;
    case S_SHOP:
      anim_step(&a_shop, DT);
      if (pressed & K_LEFT) shop_tab = (shop_tab + TABS - 1) % TABS, shop_sel = shop_top = 0;
      if (pressed & K_RIGHT) shop_tab = (shop_tab + 1) % TABS, shop_sel = shop_top = 0;
      if (pressed & K_UP && shop_sel > 0) shop_sel--;
      if (pressed & K_DOWN && shop_sel < shop_rows() - 1) shop_sel++;
      if (shop_sel < shop_top) shop_top = shop_sel;
      if (shop_sel > shop_top + 3) shop_top = shop_sel - 3;
      if (pressed & K_OK) shop_buy();
      if (pressed & K_BACK) {
        int b = back_to;
        go(b);
        if (b == S_RESULTS) sel = 1;
        if (b == S_TITLE) sel = 1;
      }
      break;
    case S_MISSIONS:
      if (pressed & (K_BACK | K_OK)) go(S_TITLE), sel = 2;
      break;
    case S_QUIT:
      /* QUIT (1) on the left, stay (0) on the right */
      if (pressed & (K_LEFT | K_UP)) sel = 1;
      if (pressed & (K_RIGHT | K_DOWN)) sel = 0;
      if (pressed & K_BACK || (pressed & K_OK && sel == 0)) {
        int b = back_to;
        go(b);
        if (b == S_PAUSE) sel = 1;
      } else if (pressed & K_OK) {
        if (back_to == S_TITLE) {
          save_write();
          app_running = false;
        } else end_run();
      }
      break;
  }
  if (title_scene()) pl.t += DT;
  if (state == S_RUN || title_scene()) runner_anim(DT);
}

/* ---------------------------------------------------------------- drawing: the 3D part */
static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static bool clip_in(const Anim *an, const Clip *const *list, int n) {
  for (int i = 0; i < n; i++)
    if (an->a == list[i]) return true;
  return false;
}

/* the runner's clip, from what happened this step: the game's own moves */
static void runner_anim(float dt) {
  Anim *an = &a_run;
  uint8_t ev = pl.ev;
  float rate = 1;
  if (title_scene()) {
    anim_play(an, &clip_runner_idle, 0, 0.25f);
  } else if (pl.dead) {
    anim_play(an, hit_train ? &clip_runner_death_train : &clip_runner_death, 0, 0.06f);
  } else if (pl.fly > 0) {
    if (ev & EV_LEFT) anim_play(an, &clip_runner_fly_l, 0, 0.05f), an->ta = 0;
    else if (ev & EV_RIGHT) anim_play(an, &clip_runner_fly_r, 0, 0.05f), an->ta = 0;
    else if (an->a != &clip_runner_fly && (anim_done(an) || (an->a != &clip_runner_fly_l && an->a != &clip_runner_fly_r)))
      anim_play(an, &clip_runner_fly, 0, 0.2f);
  } else if (pl.hover > 0) {
    static const Clip *const once[] = {&clip_runner_board_jump, &clip_runner_board_land, &clip_runner_board_roll, &clip_runner_board_l,
                                       &clip_runner_board_r};
    if (ev & EV_JUMP) anim_play(an, &clip_runner_board_jump, 0, 0.05f);
    else if (ev & EV_ROLL) anim_play(an, &clip_runner_board_roll, 0, 0.05f);
    else if (ev & EV_LAND) anim_play(an, &clip_runner_board_land, 0, 0.05f);
    else if (ev & EV_LEFT && pl.state != ST_AIR) anim_play(an, &clip_runner_board_l, 0, 0.06f);
    else if (ev & EV_RIGHT && pl.state != ST_AIR) anim_play(an, &clip_runner_board_r, 0, 0.06f);
    if (!clip_in(an, once, 5) || anim_done(an)) {
      if (pl.roll > 0) anim_play(an, &clip_runner_board_roll, 0, 0.1f);
      else if (pl.state == ST_AIR) anim_play(an, &clip_runner_board_hang, 0, 0.12f);
      else anim_play(an, &clip_runner_board, 0, 0.12f);
    }
  } else {
    static const Clip *const once[] = {&clip_runner_jump, &clip_runner_land, &clip_runner_roll, &clip_runner_dodge_l, &clip_runner_dodge_r,
                                       &clip_runner_stumble_l, &clip_runner_stumble_r, &clip_runner_salto, &clip_runner_intro};
    bool super = pl.power[PW_SNEAKERS] > 0;
    if (ev & EV_STUMBLE) anim_play(an, pl.x < LANE_X(pl.lane) ? &clip_runner_stumble_r : &clip_runner_stumble_l, 0, 0.05f);
    else if (ev & EV_JUMP) anim_play(an, ev & EV_SUPER ? &clip_runner_salto : &clip_runner_jump, 0, 0.05f);
    else if (ev & EV_ROLL) {
      anim_play(an, &clip_runner_roll, 0, 0.05f);
      an->ta = 0;
    } else if (ev & EV_LAND && pl.roll <= 0) anim_play(an, &clip_runner_land, 0, 0.05f);
    else if (ev & EV_LEFT && pl.state != ST_AIR && pl.roll <= 0) anim_play(an, &clip_runner_dodge_l, 0, 0.06f);
    else if (ev & EV_RIGHT && pl.state != ST_AIR && pl.roll <= 0) anim_play(an, &clip_runner_dodge_r, 0, 0.06f);
    if (!clip_in(an, once, 9) || anim_done(an)) {
      if (pl.roll > 0) anim_play(an, &clip_runner_roll, 0.3f, 0.1f);
      else if (pl.state == ST_AIR) anim_play(an, &clip_runner_hang, 0, an->a == &clip_runner_jump ? 0.1f : 0.2f);
      else anim_play(an, super ? &clip_runner_super_run : &clip_runner_run, 0, 0.12f);
    }
    if (an->a == &clip_runner_run || an->a == &clip_runner_super_run)
      rate = 0.9f + 0.35f * (pl.speed - SPEED_MIN) / (SPEED_MAX - SPEED_MIN);
  }
  anim_step(an, dt * rate);
  /* the Inspector and his dog */
  if (pl.dead && pl.crash_t > 0.25f) {
    anim_play(&a_guard, &clip_inspector_catch, 0, 0.15f);
    anim_play(&a_dog, &clip_dog_catch, 0, 0.15f);
  } else {
    anim_play(&a_guard, &clip_inspector_run, 0, 0.15f);
    anim_play(&a_dog, &clip_dog_run, 0, 0.15f);
  }
  float grate = title_scene() ? 0 : 1;
  anim_step(&a_guard, dt * grate);
  anim_step(&a_dog, dt * grate);
}

static void draw_runner(void) {
  int who = pg.character;
  float lt = scene_light(pl.z);
  bool title = title_scene();
  float yaw = title ? 3.14159f : 0;
  if (!title && run.start_t < 0.35f && !pl.dead) yaw += 3.14159f * (1 - run.start_t / 0.35f);   /* turning round to run */
  V3 at = {pl.x, pl.y, pl.z};
  bool blink = pl.safe > 0.15f && ((int)(clock * 12) & 1);
  if (!title && pl.hover > 0 && !pl.dead && pl.fly <= 0) {
    board_draw(at, pg.board, yaw, clock, lt);
    at.y += 0.18f;
  }
  if (!blink) {
    actor_draw(who, &a_run, at, yaw, 0, lt);
    if (pl.power[PW_JETPACK] > 0 || pl.headstart > 0) jetpack_draw(clock, lt);
  }
  float g = pl.fly > 0 ? world_ground(pl.x, pl.z, 0) : pl.ground;
  shadow_draw(pl.x, pl.z, g, pl.y - g);
  /* the Inspector and his dog, while they are after the runner */
  if (!title && (pl.chase > 0 || pl.dead)) {
    float back = pl.dead ? 1.3f - (pl.crash_t > 1 ? 1 : pl.crash_t) * 0.5f : 2.4f + (pl.chase < 1 ? (1 - pl.chase) * 9 : 0);
    if (run.start_t < 1) back = 2.4f + (1 - run.start_t) * 3;
    float ix = pl.x + (pl.dead ? 0.85f : 0.55f), dx = pl.x - 0.8f;
    actor_draw(WHO_INSPECTOR, &a_guard, (V3){ix, pl.dead ? pl.ground : 0, pl.z - back}, 0, 0, lt);
    actor_draw(WHO_DOG, &a_dog, (V3){dx, pl.dead ? pl.ground : 0, pl.z - back + 0.7f}, 0, 0, lt);
  }
}

/* coins picked up: a star of light that grows and fades, carried along with the runner */
static void draw_sparks(void) {
  for (int i = 0; i < NSPARKS; i++) {
    const Spark *s = &sparks[i];
    if (s->t >= 0.3f) continue;
    float k = s->t / 0.3f, r = 0.25f + 0.55f * k, x = pl.x + (s->x - pl.x) * 0.4f, y = s->y, z = pl.z + 0.4f;
    int a = (int)(230 * (1 - k));
    V3 h[4] = {{x - r, y + 0.07f, z}, {x + r, y + 0.07f, z}, {x + r, y - 0.07f, z}, {x - r, y - 0.07f, z}};
    V3 v[4] = {{x - 0.07f, y + r, z}, {x + 0.07f, y + r, z}, {x + 0.07f, y - r, z}, {x - 0.07f, y - r, z}};
    r_poly(h, 4, 0xFFF6B0, P_ALPHA | P_NOHAZE | P_TWOSIDED, a);
    r_poly(v, 4, 0xFFF6B0, P_ALPHA | P_NOHAZE | P_TWOSIDED, a);
  }
}

static void draw_world(void) {
  r_begin();
  bool title = title_scene();
  /* the title: in front of the runner, a little to the side, looking back at the yard */
  float tx = 0.45f, ty = 1.15f, tz = -3.3f, tp = 0.0f, tf = 240, tcy = 80;
  if (title) scene_camera(tx, ty, tz, tp, tf, tcy);
  else {
    /* the camera swings round from the title's view */
    float k = intro >= 1 ? 1 : intro * intro * (3 - 2 * intro), pitch = CAM_PITCH + (pl.fly > 0 ? 0.08f : 0);
    /* a shake when the runner crashes */
    float shake = 0;
    if (pl.dead && pl.crash_t < 0.35f) {
      float s, c;
      r_sincos(pl.crash_t * 70, &s, &c);
      shake = s * 0.15f * (1 - pl.crash_t / 0.35f);
    }
    scene_camera(lerp(tx, cam_x, k) + shake, lerp(ty, cam_y, k) + shake * 0.5f, lerp(tz, pl.z - CAM_BACK, k), lerp(tp, pitch, k),
                 lerp(tf, CAM_F, k), lerp(tcy, CAM_CY, k));
  }
  draw_runner();
  scene_draw(clock);
  if (!title) draw_sparks();
}

/* ---------------------------------------------------------------- drawing: over the picture */
static char s_score[16], s_coins[16], s_mult[8], s_best[24], s_keys[16], s_total[16];

/* the game's screen colours */
#define INK 0x1C263B        /* the dark blue under the pause screen and the shop */
#define BRICK 0x121F33      /* its bricks */
#define PERI 0x7A91CC       /* the score cards */
#define YELLOW 0xFFDB00     /* numbers that count, the multiplier */
#define TEXT_BLUE 0x383F61  /* writing on white cards */
#define TITLE_BLUE 0x403DAB
#define POPUP 0x4D73EB      /* a popup's frame */
#define POPUP_IN 0xDEE8FA   /* and its inside */
#define HUD_BG 0x171726     /* under the score and coins, see-through */
#define BAR_BG 0x404F80     /* a power-up's time: its bar */
#define BAR_FILL 0x8CD617

static bool band(int y, int h) { return y < r_y1 && y + h > r_y0; }

/* a key's name in a white key cap: what to press, always shown */
static int keycap(int x, int y, const char *k) {
  int w = d_text_width(&font_small, k) + 8;
  if (band(y, 13)) {
    d_rrect(x, y + 1, w, 12, 3, C(0x0A1020), 120);
    d_rrect(x, y, w, 12, 3, C(0xFFFFFF), 255);
    d_text(&font_small, x + 4, y + 10, k, C(0x26304A), 255);
  }
  return x + w;
}
/* a hint: key caps and words, centred on x */
static void hint(int x, int y, const char *k1, const char *w1, const char *k2, const char *w2) {
  int w = d_text_width(&font_small, k1) + 12 + d_text_width(&font_small, w1);
  if (k2) w += 14 + d_text_width(&font_small, k2) + 12 + d_text_width(&font_small, w2);
  int px = x - w / 2;
  px = keycap(px, y, k1) + 4;
  d_label(&font_small, px, y + 10, w1, C(0xFFFFFF), C(0x10182A), 1, -1);
  px += d_text_width(&font_small, w1) + 14;
  if (k2) {
    px = keycap(px, y, k2) + 4;
    d_label(&font_small, px, y + 10, w2, C(0xFFFFFF), C(0x10182A), 1, -1);
  }
}

/* one of the game's buttons; the one in focus is bigger, with a white ring round it */
static void button(int x, int y, int w, int h, const Slice *sl, const char *text, bool focus) {
  if (!band(y - 4, h + 8)) return;
  if (focus) {
    x -= 3, y -= 2, w += 6, h += 4;
    d_rrect(x - 3, y - 3, w + 6, h + 6, 10, C(0xFFFFFF), 255);
  }
  d_slice(sl, x, y, w, h, 255);
  uint16_t line = sl == &sl_green ? C(0x1E6A10) : sl == &sl_red ? C(0x8A1414) : sl == &sl_gold ? C(0x9A5A00) : sl == &sl_grey ? C(0x4A5260)
                  : sl == &sl_purple ? C(0x5A2490) : C(0x123C8A);
  d_label(&font_mid, x + w / 2, y + h / 2 + 5, text, C(0xFFFFFF), line, 2, 0);
}

/* the game's popup: a blue frame, its title on it, a pale inside */
static void popup(int x, int y, int w, int h, const char *title) {
  if (!band(y, h + 4)) return;
  d_rrect(x + 2, y + 4, w, h, 12, C(0x000000), 80);
  d_rrect(x, y, w, h, 12, C(POPUP), 255);
  d_rrect(x + 2, y + 2, w - 4, 6, 4, C(0x7A9AF2), 255);   /* (its bevel) */
  d_rrect(x + 6, y + 26, w - 12, h - 32, 9, C(POPUP_IN), 255);
  d_label(&font_big, x + w / 2, y + 22, title, C(0xFFFFFF), C(0x1F3A8A), 2, 0);
}

/* the dark blue of the pause screen and the shop, its bricks */
static void ink(int y0, int y1) {
  d_fill(0, y0 > r_y0 ? y0 : r_y0, SCREEN_W, (y1 < r_y1 ? y1 : r_y1) - (y0 > r_y0 ? y0 : r_y0), C(INK));
  static const int16_t bricks[6][2] = {{8, 0}, {120, 12}, {236, -4}, {60, 46}, {180, 56}, {280, 40}};
  for (int i = 0; i < 6; i++) {
    int bx = bricks[i][0], by = y0 + 10 + bricks[i][1];
    if (by < y1 - 10 && band(by, im_bricks.h)) d_image(bx, by, &im_bricks);
  }
}

/* the score and the coins, top right, the game's way: a see-through dark plate, the multiplier in yellow, the
 * star, the score */
static void score_plate(void) {
  char sc[16];
  int n = 0;
  while (s_score[n]) n++;
  int pad = n < 5 ? 5 - n : 0;
  for (int i = 0; i < pad; i++) sc[i] = '0';
  sc[pad] = 0;
  cat(sc, s_score);
  int sw = d_text_width(&font_big, sc), mw = d_text_width(&font_mid, s_mult);
  int w = sw + mw + 30, x = 314 - w;
  bool dbl = pl.power[PW_DOUBLE] > 0;
  if (band(5, 26)) {
    d_rrect(x, 5, w, 26, 8, C(HUD_BG), 130);
    d_label(&font_mid, x + 6, 24, s_mult, C(dbl ? 0x7CFFF0 : YELLOW), C(0x231A00), 1, -1);
    icon(ICON_STAR, (float)(x + mw + 14), 18, 15);
    d_label(&font_big, 310, 27, sc, C(0xFFFFFF), C(0x141420), 1, 1);
  }
  int cw = d_text_width(&font_mid, s_coins);
  if (band(34, 22)) {
    d_rrect(314 - cw - 28, 34, cw + 28, 21, 7, C(HUD_BG), 130);
    d_label(&font_mid, 294, 50, s_coins, C(0xFFFFFF), C(0x141420), 1, 1);
    icon(ICON_COIN, 305, 44, 15);
  }
}

/* a power-up's time, the game's way: its picture on a white tile, a green bar going down */
static void timer(int x, int y, int which, float f) {
  if (!band(y, 22)) return;
  d_rrect(x + 22, y + 5, 58, 12, 5, C(BAR_BG), 230);
  int fw = (int)(54 * (f < 0 ? 0 : f > 1 ? 1 : f));
  if (fw > 1) {
    d_rrect(x + 24, y + 7, fw, 8, 3, C(BAR_FILL), 255);
    d_fill(x + 26, y + 8, fw - 4 > 0 ? fw - 4 : 0, 2, C(0xC4F07A));
  }
  d_rrect(x, y, 22, 22, 6, C(0x2A2A50), 140);
  d_rrect(x, y, 21, 21, 6, C(0xFFFFFF), 255);
  icon(which, (float)x + 10.5f, (float)y + 10.5f, 15);
}

static void hud(void) {
  /* the pause button, top left (Back) */
  if (band(4, 28)) d_image(5, 5, &im_pause);
  score_plate();
  /* power-ups running, on the left, one under the other */
  int y = 66;
  for (int i = 0; i < PW_COUNT; i++)
    if (pl.power[i] > 0) {
      static const uint8_t pics[PW_COUNT] = {ICON_JETPACK, ICON_SNEAKERS, ICON_MAGNET, ICON_DOUBLE};
      timer(6, y, pics[i], pl.power[i] / power_time(i)), y += 26;
    }
  if (pl.headstart > 0) timer(6, y, ICON_HEADSTART, pl.headstart / 6), y += 26;
  if (pl.hover > 0) timer(6, y, ICON_BOARD, pl.hover / 30);
  /* boards to ride: bottom right, with its key */
  if (pl.hover <= 0 && pg.boards && !pl.dead && pl.fly <= 0 && band(212, 26)) {
    char s[12] = "x";
    fmt(s + 1, pg.boards, false);
    int w = d_text_width(&font_mid, s);
    int x = 314 - w - 52;
    d_rrect(x, 212, w + 52, 24, 7, C(HUD_BG), pl.board_wait > 0 ? 70 : 130);
    keycap(x + 4, 218, "OK");
    icon(ICON_BOARD, (float)x + 36, 224, 15);
    d_label(&font_mid, x + 46, 229, s, C(pl.board_wait > 0 ? 0x9AA0B0 : 0xFFFFFF), C(0x141420), 1, -1);
  }
  /* the first seconds: a headstart (1), a score booster (3) */
  if (run.start_t < 5 && !pl.dead && band(206, 32)) {
    int x = 6;
    if (pg.headstarts && pl.headstart <= 0) {
      d_slice(&sl_gold, x, 208, 74, 28, 255);
      icon(ICON_HEADSTART, (float)x + 14, 222, 24);
      keycap(x + 28, 211, "1");
      d_label(&font_small, x + 42, 221, "Head-", C(0xFFFFFF), C(0x7A4400), 1, -1);
      d_label(&font_small, x + 28, 232, "start", C(0xFFFFFF), C(0x7A4400), 1, -1);
      x += 80;
    }
    if (pg.boosters && !run.boosted) {
      d_slice(&sl_purple, x, 208, 74, 28, 255);
      icon(ICON_BOOSTER, (float)x + 14, 222, 24);
      keycap(x + 28, 211, "3");
      d_label(&font_small, x + 42, 221, "Score", C(0xFFFFFF), C(0x3A1468), 1, -1);
      d_label(&font_small, x + 28, 232, "booster", C(0xFFFFFF), C(0x3A1468), 1, -1);
    }
  }
  /* a word in the middle: a mission done, a key... (the game's banner: a dark band across) */
  if (run.toast_t > 0 && band(86, 24)) {
    int a = run.toast_t > 1.9f ? (int)((2.2f - run.toast_t) * 850) : run.toast_t < 0.3f ? (int)(run.toast_t * 850) : 255;
    if (a > 255) a = 255;
    d_fill_a(0, 86, SCREEN_W, 24, 0, a * 140 >> 8);
    d_label(&font_mid, 160, 103, run.toast, C(0xFFFFFF), C(0x000000), 1, 0);
  }
}

static void help_overlay(void) {
  d_dim(140);
  popup(12, 6, 296, 228, "How to play");
  static const char *const keys[7][2] = {
    {"Left Right", "change tracks"}, {"Up", "jump"}, {"Down", "roll; in the air: dive"},
    {"OK", "ride a hoverboard (a crash forgiven)"}, {"1  3", "headstart, score booster: at the start"},
    {"Back", "pause"}, {"Home", "save and quit"},
  };
  for (int i = 0; i < 7; i++) {
    int y = 40 + i * 19;
    if (!band(y, 16)) continue;
    int w = d_text_width(&font_small, keys[i][0]) + 8;
    d_rrect(96 - w, y, w, 13, 3, C(0xFFFFFF), 255);
    d_rrect(96 - w, y, w, 13, 3, C(0x9AA8C8), 60);
    d_text(&font_small, 100 - w, y + 10, keys[i][0], C(0x26304A), 255);
    d_text(&font_small, 104, y + 10, keys[i][1], C(TEXT_BLUE), 255);
  }
  d_text(&font_small, 26, 179, "Hit the side of a train or a barrier: you stumble.", C(0x8A2020), 255);
  d_text(&font_small, 26, 191, "Stumble twice and the Inspector has you!", C(0x8A2020), 255);
  button(110, 200, 100, 24, &sl_green, "Got it!", true);
}

static void title_overlay(void) {
  if (band(2, im_logo.h)) d_image(160 - im_logo.w / 2, 3, &im_logo);
  /* the best score top left; coins and keys top right, on the game's dark plates */
  if (band(4, 52)) {
    int w = d_text_width(&font_mid, s_best) + 30;
    if (w < 52) w = 52;
    d_rrect(6, 6, w, 34, 8, C(HUD_BG), 130);
    icon(ICON_TROPHY, 18, 18, 15);
    d_label(&font_small, 28, 19, "BEST", C(YELLOW), C(0x231A00), 1, -1);
    d_label(&font_mid, 12, 36, s_best, C(0xFFFFFF), C(0x141420), 1, -1);
    for (int k = 0; k < 2; k++) {
      const char *t = k ? s_keys : s_total;
      int tw = d_text_width(&font_mid, t);
      d_rrect(314 - tw - 28, 6 + k * 24, tw + 28, 21, 7, C(HUD_BG), 130);
      d_label(&font_mid, 294, 22 + k * 24, t, C(0xFFFFFF), C(0x141420), 1, 1);
      icon(k ? ICON_KEY : ICON_COIN, 305, 16 + k * 24, 15);
    }
  }
  if (sel == 0 && band(150, 30)) {
    if (((int)(clock * 2.2f) & 1) == 0) d_label(&font_big, 160, 176, "Press OK to play", C(0xFFFFFF), C(0x10182A), 2, 0);
  }
  if (band(180, 60)) {
    static const char *const names[3] = {"Shop", "Missions", "Help"};
    static const Slice *const sl[3] = {&sl_gold, &sl_green, &sl_blue};
    for (int i = 0; i < 3; i++) button(14 + i * 100, 192, 92, 26, sl[i], names[i], sel == i + 1);
    /* the multiplier on the missions button */
    d_rrect(186, 184, 30, 15, 6, C(0x1C1C40), 200);
    icon(ICON_STAR, 194, 191, 15);
    d_label(&font_small, 210, 195, s_mult, C(YELLOW), C(0x231A00), 1, 0);
    if (sel == 0) hint(160, 225, "OK", "play", "Down", "buttons");
    else hint(160, 225, "Left Right", "choose", "Up", "back");
  }
}

/* the missions of the set, on a white card: the game's pause screen and missions screen */
static void mission_card(int x, int y, int w) {
  if (band(y, 112)) {
    d_rrect(x, y, w, 112, 10, C(0xFFFFFF), 255);
    d_rrect(x, y, w, 24, 10, C(0x5A7DF0), 255);
    d_fill(x, y + 14, w, 10, C(0x5A7DF0));
    char t[32] = "Mission set ";
    fmt(t + 12, (uint32_t)pg.mset + 1, false);
    d_label(&font_mid, x + 10, y + 18, t, C(0xFFFFFF), C(0x1F3A8A), 1, -1);
    int mx = x + w - 10;
    d_label(&font_mid, mx, y + 18, s_mult, C(YELLOW), C(0x231A00), 1, 1);
    icon(ICON_STAR, (float)(mx - d_text_width(&font_mid, s_mult) - 10), (float)y + 12, 15);
  }
  for (int i = 0; i < 3; i++) {
    int ry = y + 30 + i * 27;
    if (!band(ry, 26)) continue;
    bool done = pg.mdone & 1 << i;
    d_rrect(x + 6, ry, w - 12, 24, 6, C(done ? 0xDDF5C8 : 0xEEF2FB), 255);
    Mission m;
    mission_get(pg.mset, i, &m);
    char t[48];
    mission_text(&m, t, sizeof t);
    d_text(&font_small, x + 14, ry + 11, t, C(TEXT_BLUE), 255);
    uint32_t p = done ? m.target : mission_progress(i);
    if (p > m.target) p = m.target;
    d_rrect(x + 14, ry + 15, w - 92, 5, 2, C(0xC8D0E4), 255);
    d_rrect(x + 14, ry + 15, (int)((float)(w - 92) * (float)p / (float)m.target) + 1, 5, 2, C(BAR_FILL), 255);
    if (done) icon(ICON_CHECK, (float)(x + w - 30), (float)ry + 12, 15);
    else {
      char n[24];
      fmt(n, p, true);
      cat(n, "/");
      fmt(n + strlen(n), m.target, true);
      d_rrect(x + w - 74, ry + 4, 60, 16, 6, C(0x5A7DF0), 255);
      d_label(&font_small, x + w - 44, ry + 15, n, C(0xFFFFFF), C(0x1F3A8A), 1, 0);
    }
  }
}

static void pause_overlay(void) {
  /* the game's pause screen: the run stays behind, dimmed; below it the dark blue with its bricks, the missions
   * on a white card, QUIT and RESUME */
  d_dim(110);
  ink(150, SCREEN_H);
  if (band(28, 32)) d_label(&font_big, 160, 54, "Paused", C(0xFFFFFF), C(0x10182A), 2, 0);
  mission_card(24, 62, 272);
  button(34, 186, 116, 30, &sl_red, "QUIT", sel == 1);
  button(170, 186, 116, 30, &sl_green, "RESUME", sel == 0);
  if (band(222, 16)) hint(160, 224, "Left Right", "choose", "Back", "resume");
}

static void quit_overlay(void) {
  d_dim(110);
  bool title = back_to == S_TITLE;
  popup(30, 64, 260, 114, "Quit");
  const char *q = title ? "Quit Subway Surfers?" : "Quit this run?";
  if (band(96, 24)) d_text(&font_mid, 160 - d_text_width(&font_mid, q) / 2, 112, q, C(0x243D5E), 255);
  button(48, 132, 100, 30, &sl_red, "QUIT", sel == 1);
  button(172, 132, 100, 30, &sl_green, title ? "STAY" : "PLAY ON", sel == 0);
  if (band(186, 16)) hint(160, 188, "Left Right", "choose", "OK", "confirm");
}

static void saveme_overlay(void) {
  d_dim(110);
  popup(60, 30, 200, 180, "Save me!");
  float f = 1 - state_t / 5;
  if (f < 0) f = 0;
  /* the time left, as a ring going round the key */
  d_ring(160, 98, 30, 6, C(0xC8D0E4), 255);
  for (int i = 0; i < 40 && i < (int)(f * 40); i++) {
    float s, c;
    r_sincos((float)i * 0.157f, &s, &c);
    d_disc(160 + s * 30, 98 - c * 30, 3.2f, C(BAR_FILL), 255);
  }
  icon(ICON_KEY, 160, 98, 24);
  char h[32] = "You have ";
  fmt(h + 9, pg.keys, false);
  cat(h, pg.keys == 1 ? " key" : " keys");
  if (band(136, 16)) d_text(&font_small, 160 - d_text_width(&font_small, h) / 2, 148, h, C(0x243D5E), 255);
  char s[24] = "USE ";
  fmt(s + 4, (uint32_t)run_save_cost(), false);
  button(90, 158, 140, 30, &sl_green, s, true);
  if (band(176, 12)) icon(ICON_KEY, 214, 173, 15);
  if (band(212, 16)) hint(160, 214, "OK", "save me", "Back", "no thanks");
}

static void results_overlay(void) {
  /* the game's score cards: periwinkle, a white heading, the number in yellow on a dark plate */
  d_dim(120);
  if (band(6, 102)) {
    d_rrect(30, 8, 260, 100, 10, C(0x2A3460), 120);
    d_rrect(28, 6, 260, 100, 10, C(PERI), 255);
    d_label(&font_mid, 158, 22, "Score", C(0xFFFFFF), C(0x2A3A78), 1, 0);
    d_rrect(48, 28, 220, 30, 8, C(0x3B405E), 150);
    d_label(&font_big, 158, 51, s_score, C(YELLOW), C(0x231A00), 2, 0);
    d_label(&font_mid, 158, 74, "Coins", C(0xFFFFFF), C(0x2A3A78), 1, 0);
    int cw = d_text_width(&font_mid, s_coins);
    d_rrect(108, 79, 100, 22, 7, C(0x3B405E), 150);
    d_label(&font_mid, 158 - 8 + cw / 2, 96, s_coins, C(YELLOW), C(0x231A00), 1, 1);
    icon(ICON_COIN, (float)(158 - 8 + cw / 2 + 12), 90, 15);
  }
  if (new_best && band(100, 20)) {
    d_rrect(108, 100, 104, 18, 8, C(0xE0306E), 255);
    d_label(&font_mid, 160, 114, "High score!", C(0xFFFFFF), C(0x6A0A2A), 1, 0);
  } else if (band(108, 14)) d_label(&font_small, 160, 119, s_best, C(0xFFFFFF), C(0x10182A), 1, 0);
  /* the missions: done or not */
  for (int i = 0; i < 3; i++) {
    int y = 126 + i * 19;
    if (!band(y, 18)) continue;
    bool done = pg.mdone & 1 << i;
    d_rrect(28, y, 264, 17, 6, C(done ? 0xDDF5C8 : 0xFFFFFF), 235);
    Mission m;
    mission_get(pg.mset, i, &m);
    char t[48];
    mission_text(&m, t, sizeof t);
    icon(done ? ICON_CHECK : ICON_STAR, 38, (float)y + 8, 15);
    d_text(&font_small, 50, y + 12, t, C(TEXT_BLUE), 255);
  }
  static const char *const names[3] = {"PLAY", "SHOP", "MENU"};
  static const Slice *const sl[3] = {&sl_green, &sl_gold, &sl_blue};
  for (int i = 0; i < 3; i++) button(22 + i * 96, 192, 84, 28, sl[i], names[i], sel == i);
  if (band(224, 16)) hint(160, 226, "Left Right", "choose", "OK", "go");
}

static void missions_overlay(void) {
  d_dim(110);
  ink(150, SCREEN_H);
  if (band(8, 32)) d_label(&font_big, 160, 34, "Missions", C(0xFFFFFF), C(0x10182A), 2, 0);
  mission_card(24, 46, 272);
  char nx[48] = "Finish all three: multiplier x";
  int next = 2 + pg.mset > 30 ? 30 : 2 + pg.mset;
  fmt(nx + strlen(nx), (uint32_t)next, false);
  if (band(164, 16)) d_label(&font_mid, 160, 178, nx, C(YELLOW), C(0x231A00), 1, 0);
  button(110, 190, 100, 28, &sl_blue, "OK", true);
  if (band(222, 16)) hint(160, 224, "OK", "back", NULL, NULL);
}

static void shop_overlay(void) {
  /* the top: the shop, its shelves as tabs, the coins */
  if (band(0, 54)) {
    d_label(&font_big, 10, 26, "Shop", C(0xFFFFFF), C(0x10182A), 2, -1);
    int tw = d_text_width(&font_mid, s_total);
    d_rrect(314 - tw - 28, 8, tw + 28, 21, 7, C(HUD_BG), 160);
    d_label(&font_mid, 294, 24, s_total, C(0xFFFFFF), C(0x141420), 1, 1);
    icon(ICON_COIN, 305, 18, 15);
    int x = 6;
    for (int t = 0; t < TABS; t++) {
      int w = d_text_width(&font_small, tab_names[t]) + 16;
      d_slice(t == shop_tab ? &sl_gold : &sl_light, x, 34, w, 18, 255);
      d_label(&font_small, x + w / 2, 47, tab_names[t], C(t == shop_tab ? 0xFFFFFF : 0x1F3A8A), C(t == shop_tab ? 0x7A4400 : 0xFFFFFF), 1, 0);
      x += w + 3;
    }
  }
  bool preview = shop_tab == TAB_CHARS || shop_tab == TAB_BOARDS;
  int lw = preview ? 196 : 308;
  int n = shop_rows();
  for (int r = 0; r < 4 && shop_top + r < n; r++) {
    int i = shop_top + r, y = 58 + r * 37;
    if (!band(y - 3, 40)) continue;
    bool focus = i == shop_sel;
    if (focus) d_rrect(4, y - 2, lw + 4, 37, 10, C(0xFFFFFF), 255);
    d_rrect(6, y, lw, 33, 8, C(focus ? 0xFFFFFF : 0xE4EAF8), 255);
    if (focus) d_rrect(6, y, lw, 33, 8, C(0xFFE680), 70);
    char name[24], sub[48], price[16];
    int pic = ICON_STAR;
    sub[0] = price[0] = 0;
    bool owned = false;
    switch (shop_tab) {
      case TAB_UPGRADES: {
        strcpy(name, upg_names[i]);
        pic = upg_icons[i];
        int c = upgrade_cost(pg.upg[i]);
        if (c) fmt(price, (uint32_t)c, true);
        else owned = true;
        /* the steps */
        for (int k = 0; k < MAXUPG; k++) d_rrect(38 + k * 15, y + 21, 12, 7, 2, C(k < pg.upg[i] ? BAR_FILL : 0xC8D0E4), 255);
        char s[12];
        fmt(s, (uint32_t)power_time(i), false);
        cat(s, " s");
        d_text(&font_small, 132, y + 28, s, C(TEXT_BLUE), 255);
        break;
      }
      case TAB_ITEMS: {
        strcpy(name, item_names[i]);
        strcpy(sub, item_text[i]);
        pic = item_icons[i];
        fmt(price, item_price[i], true);
        uint32_t have = i == 0 ? pg.boards : i == 1 ? pg.headstarts : i == 2 ? pg.boosters : 0;
        if (i < 3) {
          char s[12] = "x";
          fmt(s + 1, have, false);
          cat(name, " ");
          cat(name, s);
        }
        break;
      }
      case TAB_CHARS:
        strcpy(name, char_names[i]);
        owned = pg.chars & 1 << i;
        pic = owned ? (pg.character == i ? ICON_CHECK : ICON_STAR) : ICON_LOCK;
        strcpy(sub, owned ? (pg.character == i ? "Running" : "OK: choose") : "");
        if (!owned) fmt(price, char_price[i], true);
        break;
      default:
        strcpy(name, board_names[i]);
        strcpy(sub, board_power[i]);
        owned = pg.ownboards & 1 << i;
        pic = owned ? (pg.board == i ? ICON_CHECK : ICON_BOARD) : ICON_LOCK;
        if (!owned) fmt(price, board_price[i], true);
        break;
    }
    icon(pic, 21, (float)y + 16, 24);
    d_text(&font_mid, 38, y + 15, name, C(TITLE_BLUE), 255);
    if (sub[0] && shop_tab != TAB_UPGRADES) d_text(&font_small, 38, y + 28, sub, C(TEXT_BLUE), 255);
    if (price[0]) {
      int w = d_text_width(&font_small, price) + 28;
      d_slice(&sl_green, 6 + lw - w - 4, y + 6, w, 21, 255);
      icon(ICON_COIN, (float)(6 + lw - w + 7), (float)y + 16, 15);
      d_label(&font_small, 6 + lw - 10, y + 21, price, C(0xFFFFFF), C(0x1E6A10), 1, 1);
    } else if (owned && shop_tab == TAB_UPGRADES) d_label(&font_mid, 6 + lw - 10, y + 21, "MAX", C(0x2E9E3E), C(0xFFFFFF), 1, 1);
  }
  if (n > 4 && band(206, 10)) d_label(&font_small, 100, 214, shop_top > 0 ? "more above" : "more below", C(0xD6EEFF), C(INK), 1, 0);
  if (note_t > 0 && band(200, 30)) {
    int w = d_text_width(&font_mid, note) + 24;
    d_fill_a(0, 204, SCREEN_W, 22, 0, 150);
    d_label(&font_mid, 160, 220, note, C(0xFFFFFF), C(0x000000), 1, 0);
    (void)w;
  } else if (band(222, 16)) hint(160, 224, "OK", "buy, choose", "Left Right", "shelves");
}

static void overlay(void) {
  switch (state) {
    case S_HELP: help_overlay(); break;
    case S_TITLE: title_overlay(); break;
    case S_RUN:
      if (intro > 0.6f) hud();
      break;
    case S_PAUSE: pause_overlay(); break;
    case S_SAVEME: saveme_overlay(); break;
    case S_RESULTS: results_overlay(); break;
    case S_SHOP: shop_overlay(); break;
    case S_MISSIONS: missions_overlay(); break;
    case S_QUIT:
      if (back_to == S_TITLE) title_overlay();
      quit_overlay();
      break;
  }
}

/* the shop's background: the game's dark blue with its bricks */
static void shop_background(void) {
  ink(r_y0, r_y1);
}

static void draw_shop(void) {
  r_begin();
  if (shop_tab == TAB_CHARS || shop_tab == TAB_BOARDS) {
    /* a turning preview, on the right */
    r_camera(0, 0.75f, -2.6f, 0.06f, 240, 258, 136);
    float yaw = 3.14159f + clock * 0.9f;
    /* the stand */
    V3 st[8];
    for (int k = 0; k < 8; k++) {
      float s, c;
      r_sincos((float)k * 0.785f, &s, &c);
      st[k] = (V3){s * 0.7f, 0, c * 0.7f};
    }
    r_poly(st, 8, 0x1A4F9A, P_NOHAZE, 0);
    if (shop_tab == TAB_CHARS) {
      anim_play(&a_shop, &clip_runner_idle, 0, 0);
      actor_draw(shop_sel, &a_shop, (V3){0, 0, 0}, yaw, 0, 1);
    } else {
      anim_play(&a_shop, &clip_runner_board, 0, 0);
      board_draw((V3){0, 0.05f, 0}, shop_sel, yaw, clock, 1);
      actor_draw(pg.character, &a_shop, (V3){0, 0.23f, 0}, yaw, 0, 1);
    }
  }
  r_render(shop_background, overlay);
}

uint32_t perf_polys __attribute__((used));   /* (read by tools/emu.py and the tests) */

void app_draw(void) {
  /* the numbers on the screens, once a frame */
  fmt(s_score, run.score, false);
  fmt(s_coins, run.coins, false);
  fmt(s_total, pg.coins, false);
  fmt(s_keys, pg.keys, false);
  strcpy(s_mult, "x");
  fmt(s_mult + 1, (uint32_t)(state == S_RUN || state == S_PAUSE ? run.multiplier : multiplier()), false);
  if (state == S_RESULTS) {
    strcpy(s_best, "Best: ");
    fmt(s_best + 6, pg.best, false);
  } else fmt(s_best, pg.best, false);
  if (state == S_SHOP) {
    draw_shop();
    return;
  }
  /* the screens over a picture that stands still */
  bool still = state == S_RESULTS || state == S_PAUSE || state == S_QUIT || state == S_MISSIONS || state == S_HELP;
  if (still && drawn && state_t > 0.5f) return;
  drawn = true;
  draw_world();
  perf_polys = (uint32_t)r_count();
  r_render(scene_background, overlay);
}
