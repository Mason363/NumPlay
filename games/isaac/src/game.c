/* The Binding of Isaac: the title, the menus, a run from the Basement down,
 * pausing, dying, the endings, and going from room to room. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "res.h"

int g_mode;
bool g_running = true;
uint32_t g_frame, g_keys, g_pressed;
int g_room_frames, g_shake, g_killer, g_transition, g_boss_intro;
bool g_room_was_cleared;
uint32_t g_run_seed, g_rng = 0x2545F491u;
uint8_t g_wins, g_best_stage;
uint16_t g_deaths, g_runs;
static int menu_sel, pause_sel, confirm_sel, mode_t, ending, char_sel;
static int trans_dir, trans_room;
static bool boss_hit;   /* he was hurt in the boss's room (the Devil's deal) */
void pills_shuffle(void);
int floor_neighbour(int d);
bool door_try_unlock(int d);

/* ---------------------------------------------------------------- numbers */
uint32_t rng_next(uint32_t *s) {
  uint32_t x = *s;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return *s = x ? x : 0x9E3779B9u;
}
int rnd(int n) { return n > 0 ? (int)(rng_next(&g_rng) % (uint32_t)n) : 0; }
float rndf(void) { return (rng_next(&g_rng) >> 8) * (1.0f / 16777216.0f); }
bool chance(float p) { return rndf() < p; }
float fsqrt(float v) { return v > 0 ? __builtin_sqrtf(v) : 0; }
float fatan2(float y, float x) { return atan2f(y, x); }
float fsin(float a) { return sinf(a); }
float fcos(float a) { return cosf(a); }
float dist2(float ax, float ay, float bx, float by) { return (ax - bx) * (ax - bx) + (ay - by) * (ay - by); }

/* ---------------------------------------------------------------- a run */
uint8_t g_last_char;
void game_new_run(int character) {
  g_run_seed = g_rng ^ (plat_millis() * 2654435761u);
  if (!g_run_seed) g_run_seed = 1;
  g_rng = g_run_seed;
  g_frame = 0;
  player_reset(character);
  items_new_run();
  player_starting_items();
  pills_shuffle();
  floor_new(1);
  floor_enter_room(fl.start, DIR_NONE);
  g_runs++;
  save_progress();
  g_mode = MODE_STAGE, mode_t = 0;
}

static uint8_t next_alt;
void game_next_floor(void) {
  int stage = fl.stage + 1;
  if (stage > 9) return;
  next_alt = fl.alt;
  if (stage >= 9) {
    floor_new(stage);
    fl.alt = next_alt;
    fl.boss = (uint8_t)(fl.alt ? MON_ISAAC : MON_SATAN);
    fl.rooms[fl.boss_room].boss = fl.boss;
  } else
    floor_new(stage);
  pl.damage_taken_floor = 0;
  boss_hit = false;
  if (pl.has[76]) floor_reveal(4);
  if (pl.has[54]) floor_reveal(1);
  if (pl.has[21]) floor_reveal(2);
  floor_enter_room(fl.start, DIR_NONE);
  if (stage > g_best_stage) g_best_stage = (uint8_t)stage;
  g_mode = MODE_STAGE, mode_t = 0;
  save_run();
  save_progress();
}

void game_won(int how) {
  ending = how;
  g_mode = MODE_WIN, mode_t = 0;
  if (how >= 2) {
    g_wins |= (uint8_t)(1 << how);
    save_remove_run();
  } else
    g_wins |= (uint8_t)(1 << how);
  save_progress();
}

void game_over(void) {
  g_deaths++;
  g_mode = MODE_DEAD, mode_t = 0;
  save_remove_run();
  save_progress();
}

void game_init(void) {
  save_restore_copy();
  load_progress();
  g_mode = MODE_TITLE;
  menu_sel = run_saved() ? 1 : 0;
}

/* ---------------------------------------------------------------- the boss's reward */
static void boss_reward(void) {
  Room *r = room_cur();
  if (r->kind != ROOM_BOSS) return;
  fl.boss_beaten = true;
  if (fl.stage == 9) {   /* Satan or Isaac: the end */
    game_won(fl.alt ? 3 : 2);
    return;
  }
  if (fl.stage != 8 || true) pedestal_spawn(item_from_pool(POOL_BOSS), 160, 85, 0, false);
  pickup_spawn(rnd(3) ? PK_HEART : PK_SOUL, 0, 130, 140, true);
  if (fl.stage == 8) {   /* Mom's Heart: the ending; then down to Sheol, or up to the Cathedral */
    Ent *t = ent_new(EK_TRAPDOOR, 120, 140);
    if (t) t->spr = SPR_TRAPDOOR, t->type = 0;
    t = ent_new(EK_TRAPDOOR, 200, 140);
    if (t) t->type = 1;
    game_won(1);
  } else {
    Ent *t = ent_new(EK_TRAPDOOR, 160, 130);
    if (t) t->spr = SPR_TRAPDOOR;
    if (fl.stage == 6) game_won(0);
  }
  /* the Devil's room, or an angel's, behind a door in the boss's room */
  if (fl.stage >= 2 && fl.stage <= 8) {
    float p = 0.15f + (boss_hit ? 0 : 0.35f) + (pl.damage_taken_floor ? 0 : 0.35f);
    if (pl.has[51]) p += 0.1f;
    if (pl.has[122]) p += 0.1f;
    int side = -1;
    for (int k = 0; k < 4 && side < 0; k++) {
      static const int8_t order[4] = {DIR_UP, DIR_LEFT, DIR_RIGHT, DIR_DOWN};
      int mx = r->mx + DIR_X[order[k]], my = r->my + DIR_Y[order[k]];
      if (mx < 0 || my < 0 || mx >= MAP_W || my >= MAP_H || fl.map[my][mx] < 0) side = order[k];
    }
    if (side >= 0 && chance(p)) {
      fl.devil_open = true, fl.devil_side = (int8_t)side;
      fl.devil_angel = pl.devil_deals == 0 && rnd(2) == 0;
    }
  }
}

/* ---------------------------------------------------------------- playing */
static void room_cleared(void) {
  Room *r = room_cur();
  r->state |= RS_CLEARED;
  if (r->kind == ROOM_CHALLENGE && r->variant == 1) {   /* the second wave */
    r->state &= (uint8_t)~RS_CLEARED;
    r->variant = 2;
    int cb = 1 << (chapter_of(fl.stage) - 1);
    (void)cb;
    for (int i = 0; i < 3; i++) mon_spawn(fl.stage > 4 ? MON_KNIGHT : MON_GAPER, 80 + i * 80, 90);
    mon_spawn(fl.stage > 4 ? MON_BONY : MON_HOPPER, 160, 160);
    return;
  }
  if (r->kind == ROOM_CHALLENGE) pickup_spawn(PK_CHEST, 0, 160, 120, true), pedestal_spawn(item_from_pool(POOL_TREASURE), 160, 100, 0, false);
  if (r->kind == ROOM_MINIBOSS && r->boss == MON_GREED) for (int i = 0; i < 4; i++) pickup_spawn(PK_PENNY, 0, 160, 120, true);
  else if (r->kind == ROOM_MINIBOSS) pickup_drop_random(160, 120);
  room_clear_reward();
  item_room_cleared();
  boss_reward();
}

static void enter_door(int d) {
  int j = floor_neighbour(d);
  if (j < 0) return;
  trans_dir = d, trans_room = j;
  g_transition = 10;
}

static void play_update(void) {
  uint32_t k = g_keys;
  g_frame++;
  g_room_frames++;
  if (g_shake) g_shake--;
  if (banner_t > 0) banner_t--;
  if (g_transition > 0) {   /* the dip to black between rooms */
    g_transition--;
    if (g_transition == 5) {
      static const int8_t opposite[4] = {DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT};
      floor_enter_room(trans_room, opposite[trans_dir]);
    }
    return;
  }
  if (g_boss_intro > 0) {
    g_boss_intro--;
    if (g_boss_intro > 12 && (g_pressed & (K_OK | K_BACK))) g_boss_intro = 12;
    return;
  }
  if (pl.dead) {
    player_update(0);
    ents_update();
    if (pl.hurt_anim <= 0) game_over();
    return;
  }
  /* the keys of a room */
  if (g_pressed & K_ITEM || (g_pressed & K_OK)) item_use_active();
  if ((g_pressed & K_BOMB) && (pl.bombs > 0 || fl.golden_bomb) && !pl.has[52]) {
    if (!fl.golden_bomb) pl.bombs--;
    bomb_place(pl.x, pl.y + 1, 0);
  }
  if ((g_pressed & K_CARD) && pl.pocket_kind) {
    int kind = pl.pocket_kind, what = pl.pocket;
    pl.pocket_kind = 0, pl.pocket = 0;
    if (kind == 1) pill_use(what);
    else card_use(what);
  }
  int hp_before = pl.red + pl.soul;
  player_update(k);
  grid_update();
  ents_update();
  tears_update();
  lasers_update();
  if (room_cur()->kind == ROOM_BOSS && pl.red + pl.soul < hp_before) boss_hit = true;
  if (pl.dead) return;
  /* the room is cleared when its last monster dies */
  if (!(room_cur()->state & RS_CLEARED) && !ents_alive_monsters() && g_room_frames > 3) room_cleared();
  /* the doors: walking into one (open) goes through; a locked one takes a key */
  static const uint32_t way[4] = {K_DOWN, K_LEFT, K_UP, K_RIGHT};
  for (int d = 0; d < 4; d++) {
    if (!(k & way[d]) || !room_door_exists(d, NULL)) continue;
    bool at = d == DIR_UP ? pl.y <= ROOM_Y + 6.5f && absf(pl.x - 160) < 9
              : d == DIR_DOWN ? pl.y >= ROOM_B - 6.5f && absf(pl.x - 160) < 9
              : d == DIR_LEFT ? pl.x <= ROOM_X + 6.5f && absf(pl.y - 120) < 9
              : pl.x >= ROOM_R - 6.5f && absf(pl.y - 120) < 9;
    if (!at) continue;
    if (!room_door_open(d) && !ents_alive_monsters()) door_try_unlock(d);
    if (room_door_open(d)) enter_door(d);
  }
  if (g_pressed & K_BACK) g_mode = MODE_PAUSE, pause_sel = 0;
}

/* ---------------------------------------------------------------- drawing */
static void draw_room(void) {
  room_draw_floor();
  doors_draw();
  ents_draw();
  tears_draw();
  lasers_draw();
  if (fl.curse & CURSE_DARKNESS) gfx_dark((int)pl.x, (int)pl.y - 8, 46, 26);
  hud_draw();
  if (g_keys & K_MAP) map_draw_full();
  if (g_boss_intro > 0 && g_transition == 0) {
    /* Rebirth's: a band across the room, him on the left, the boss on the right, VS between them */
    int k = 64 - g_boss_intro, slide = k < 10 ? (10 - k) * 32 : g_boss_intro < 10 ? (10 - g_boss_intro) * 32 : 0;
    int a = g_boss_intro < 8 ? g_boss_intro * 4 : 32;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, a / 2);
    gfx_rect(0, 70, SCREEN_W, 100, rgb565(0x1A0E0A), a);
    gfx_rect(0, 70, SCREEN_W, 2, rgb565(0x6A3020), a);
    gfx_rect(0, 168, SCREEN_W, 2, rgb565(0x6A3020), a);
    int head = character_head(pl.character);
    gfx_sprite_scaled(head + 1, 0, 70 - slide, 156, 512, 512, 0, 0, 0, a);
    gfx_sprite_scaled(head, 0, 70 - slide, 142, 512, 512, 0, 0, 0, a);
    Room *r = room_cur();
    int boss = r->kind == ROOM_MINIBOSS ? r->boss : fl.boss;
    const char *name = boss_name(boss);
    int spr = boss_sprite(boss);
    if (spr >= 0) gfx_sprite_scaled(spr, 0, 238 + slide, 160, 300, 300, GFX_FLIPX, 0, 0, a);
    gfx_text_c(FONT_UPHEAVAL, 160, 98, "VS", rgb565(0xE0D0B0), 3, true);
    gfx_text_c(FONT_UPHEAVAL, 160, 178, name, C_WHITE, 2, true);
    gfx_text_c(FONT_UPHEAVAL, 160, 50, character_name(pl.character), rgb565(0xE0D0B0), 2, true);
  }
  if (g_transition > 0) {
    int a = g_transition > 5 ? (10 - g_transition) * 7 : g_transition * 7;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, a > 32 ? 32 : a);
  }
}

static void paper(int x, int y, int w, int h) {
  gfx_rect(x + 3, y + 3, w, h, C_BLACK, 14);
  gfx_rect(x, y, w, h, rgb565(0xE6D6B4), 32);
  gfx_rect(x, y, w, 2, rgb565(0xC8B48C), 32);
  gfx_rect(x, y + h - 2, w, 2, rgb565(0xB49C74), 32);
}
#define INK rgb565(0x3A2A20)
#define INK2 rgb565(0x7A6048)

static void menu_list(const char *const *items, int n, int sel, int x, int y) {
  for (int i = 0; i < n; i++) {
    uint16_t c = i == sel ? INK : INK2;
    gfx_text_c(FONT_UPHEAVAL, x, y + i * 24, items[i], c, 2, false);
    if (i == sel) {
      int w = gfx_text_width(FONT_UPHEAVAL, items[i], 2);
      gfx_sprite_scaled(SPR_TEAR, 0, x - w / 2 - 10, y + i * 24 + 8, 150, 150, 0, 0, 0, 32);
    }
  }
}

static void draw_title(void) {
  gfx_backdrop(BD_ISAACS_ROOM);
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 10);
  gfx_text_c(FONT_UPHEAVAL, 162, 30, "THE BINDING OF", C_BLACK, 2, false);
  gfx_text_c(FONT_UPHEAVAL, 160, 28, "THE BINDING OF", rgb565(0xE8DCC8), 2, false);
  gfx_text_c(FONT_UPHEAVAL, 163, 55, "ISAAC", C_BLACK, 5, false);
  gfx_text_c(FONT_UPHEAVAL, 160, 52, "ISAAC", rgb565(0xB01010), 5, false);
  /* Isaac, crying on the floor of his room */
  int x = 160, y = 172;
  gfx_ellipse(x, y, 9, 3, C_BLACK, 12);
  gfx_sprite(SPR_ISAAC_BODY, 0, x, y, 0, 32);
  gfx_sprite(SPR_ISAAC_HEAD, (g_frame / 12) % 2, x, y - 7, 0, 32);
  if (g_frame % 50 < 34) gfx_text_c(FONT_UPHEAVAL, 160, 206, "PRESS OK", C_WHITE, 1, true);
}

bool g_swap;
enum { M_NEW, M_CONTINUE, M_CONTROLS, M_HELP };
/* the menu's lines: Continue only with a run kept */
static int menu_items(int *ids, const char **names) {
  static const char *const all[4] = {"NEW RUN", "CONTINUE", "CONTROLS", "HOW TO PLAY"};
  int n = 0;
  for (int i = 0; i < 4; i++)
    if (i != M_CONTINUE || run_saved()) ids[n] = i, names[n] = all[i], n++;
  return n;
}

static void draw_menu(void) {
  gfx_backdrop(BD_ISAACS_ROOM);
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 12);
  paper(70, 30, 180, 170);
  int ids[4];
  const char *names[4];
  int n_items = menu_items(ids, names);
  menu_list(names, n_items, menu_sel, 160, 48);
  for (int i = 0; i < n_items; i++)
    if (ids[i] == M_CONTROLS) gfx_text_c(FONT_UPHEAVAL, 160, 48 + i * 24 + 17, g_swap ? "NUMBERS WALK, ARROWS CRY" : "ARROWS WALK, NUMBERS CRY", INK2, 1, false);
  char s[48];
  int n = 0;
  const char *p = "RUNS ";
  while (*p) s[n++] = *p++;
  int v = g_runs;
  char digits[8];
  int nd = 0;
  do digits[nd++] = (char)('0' + v % 10), v /= 10; while (v && nd < 6);
  while (nd) s[n++] = digits[--nd];
  s[n] = 0;
  gfx_text_c(FONT_UPHEAVAL, 160, 172, s, INK2, 1, false);
  /* the bosses beaten: Mom, Mom's Heart, Satan, Isaac */
  static const int16_t marks[4] = {SPR_MOM_EYE, SPR_MOMS_HEART, SPR_SATAN, SPR_ISAAC_BOSS};
  for (int i = 0; i < 4; i++)
    if (g_wins & (1 << i)) gfx_sprite_scaled(marks[i], 0, 112 + i * 32, 226, 90, 90, 0, 0, 0, 32);
}

/* who plays: Rebirth's characters, their hearts and what they start with */
static void draw_charsel(void) {
  gfx_backdrop(BD_ISAACS_ROOM);
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 12);
  paper(50, 20, 220, 196);
  int c = char_sel;
  bool open = character_unlocked(c);
  gfx_text_c(FONT_UPHEAVAL, 160, 30, open ? character_name(c) : "???", INK, 2, false);
  int head = character_head(c);
  int bob = (int)(g_frame / 10 % 2);
  gfx_ellipse(160, 118, 16, 5, C_BLACK, 10);
  gfx_sprite_scaled(head + 1, 0, 160, 118, 512, 512, 0, C_BLACK, open ? 0 : 32, 32);
  gfx_sprite_scaled(head, 0, 160, 104 + bob, 512, 512, 0, C_BLACK, open ? 0 : 32, 32);
  /* the neighbours, smaller, either side */
  for (int s = -1; s <= 1; s += 2) {
    int n = (c + CH_COUNT + s) % CH_COUNT, nh = character_head(n);
    bool o = character_unlocked(n);
    gfx_sprite_scaled(nh + 1, 0, 160 + s * 76, 112, 256, 256, 0, C_BLACK, o ? 12 : 32, 24);
    gfx_sprite_scaled(nh, 0, 160 + s * 76, 105, 256, 256, 0, C_BLACK, o ? 12 : 32, 24);
  }
  gfx_text_c(FONT_UPHEAVAL, 84, 100, "<", INK2, 2, false);
  gfx_text_c(FONT_UPHEAVAL, 236, 100, ">", INK2, 2, false);
  if (!open) {
    gfx_text_c(FONT_UPHEAVAL, 160, 140, "LOCKED", rgb565(0x901010), 2, false);
    gfx_text_c(FONT_UPHEAVAL, 160, 162, character_unlock_hint(c), INK2, 1, false);
    return;
  }
  /* his hearts and his start, as the run will give them */
  Player keep = pl;
  player_reset(c);
  int x0 = 160 - (pl.containers + pl.soul / 2) * 11 / 2;
  for (int i = 0; i < pl.containers; i++) gfx_sprite(SPR_HUD_HEART, 0, x0 + i * 11, 136, 0, 32);
  for (int i = 0; i < pl.soul / 2; i++) gfx_sprite(SPR_HUD_HEART, (pl.black >> i) & 1 ? 5 : 3, x0 + (pl.containers + i) * 11, 136, 0, 32);
  pl = keep;
  static const int16_t start[CH_COUNT][3] = {{0}, {45}, {46}, {34}, {36}, {122, 117, 126}, {157}, {118}};
  int n = 0;
  while (n < 3 && start[c][n]) n++;
  for (int i = 0; i < n; i++) gfx_sprite(item_sprite(start[c][i]), 0, 160 - n * 13 + 13 + i * 26, 178, 0, 32);
  static const char *const about[CH_COUNT] = {"THE ONE WHO STARTED IT ALL", "MORE HEARTS, SLOWER", "LUCKY, QUICK, A KEY",
                                              "ONE HEART, HARD TEARS", "SOUL HEARTS ONLY", "CURSED, AND STRONGER FOR IT",
                                              "ANGRIER WITH EVERY HIT", "SHORT BRIMSTONE, FLIES"};
  gfx_text_c(FONT_UPHEAVAL, 160, 190, about[c], INK2, 1, false);
}

static void draw_help(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, rgb565(0x1A1210), 32);
  paper(14, 10, 292, 220);
  gfx_text_c(FONT_UPHEAVAL, 160, 18, "HOW TO PLAY", INK, 2, false);
  const char *rows[][2] = {
    {g_swap ? "8 4 6 5 (2)" : "ARROWS", "WALK"},
    {g_swap ? "ARROWS" : "8 4 6 2 (5)", "CRY TEARS UP, LEFT, RIGHT, DOWN"}, {"9 OR SHIFT", "DROP A BOMB"},
    {"7 OR OK", "USE THE ACTIVE ITEM"}, {"1 OR 3", "TAKE THE PILL OR CARD"}, {"TOOLBOX", "HOLD FOR THE MAP"},
    {"BACK", "PAUSE"}, {"HOME", "KEEP THE RUN AND QUIT"},
  };
  for (int i = 0; i < 8; i++) {
    gfx_text(FONT_UPHEAVAL, 28, 48 + i * 17, rows[i][0], INK, 1, false);
    gfx_text(FONT_UPHEAVAL, 122, 48 + i * 17, rows[i][1], INK2, 1, false);
  }
  gfx_text_c(FONT_UPHEAVAL, 160, 190, "CLEAR EACH ROOM. FIND ITEMS. GO DOWN.", INK, 1, false);
  gfx_text_c(FONT_UPHEAVAL, 160, 204, "BEAT MOM, AND WHAT LIES BELOW HER.", INK, 1, false);
}

static void stat_line(int x, int y, const char *name, float v) {
  char s[24];
  int n = 0;
  while (*name) s[n++] = *name++;
  s[n++] = ' ';
  int whole = (int)v, frac = (int)((v - whole) * 100 + 0.5f);
  if (frac >= 100) whole++, frac -= 100;
  if (v < 0) whole = -whole, frac = 0, s[n++] = '-';
  char d[8];
  int nd = 0;
  do d[nd++] = (char)('0' + whole % 10), whole /= 10; while (whole && nd < 5);
  while (nd) s[n++] = d[--nd];
  s[n++] = '.', s[n++] = (char)('0' + frac / 10), s[n++] = (char)('0' + frac % 10), s[n] = 0;
  gfx_text(FONT_UPHEAVAL, x, y, s, INK, 1, false);
}

static void draw_pause(void) {
  draw_room();
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 16);
  if (g_mode == MODE_CONFIRM) {
    paper(100, 78, 120, 84);
    gfx_text_c(FONT_UPHEAVAL, 160, 90, "QUIT?", INK, 2, false);
    static const char *const yn[] = {"NO", "YES"};
    menu_list(yn, 2, confirm_sel, 160, 116);
    return;
  }
  paper(20, 20, 280, 200);
  gfx_text_c(FONT_UPHEAVAL, 160, 28, "PAUSED", INK, 2, false);
  static const char *const opts[] = {"RESUME", "QUIT GAME"};
  menu_list(opts, 2, pause_sel, 100, 58);
  /* his stats, as the game lists them */
  stat_line(190, 54, "DMG", pl.damage);
  stat_line(190, 66, "TEARS", 30.0f / (pl.tear_delay + 1));
  stat_line(190, 78, "RANGE", pl.range / 3.65f);
  stat_line(190, 90, "SHOTSPD", pl.shot_speed);
  stat_line(190, 102, "SPEED", pl.speed);
  stat_line(190, 114, "LUCK", pl.luck);
  /* the items he found */
  for (int i = 0; i < pl.norder && i < 30; i++) gfx_sprite(item_sprite(pl.order[i]), 0, 40 + (i % 10) * 25, 158 + (i / 10) * 24, 0, 32);
  gfx_text_c(FONT_UPHEAVAL, 160, 128, stage_name(fl.stage, fl.alt), INK2, 1, false);
  char t[40];
  uint32_t secs = g_frame / FPS, k = 0;
  static const char seed_chars[] = "ABCDEFGHJKLMNPQRSTWXYZ01234V6789";
  const char *lbl = "TIME ";
  while (*lbl) t[k++] = *lbl++;
  if (secs >= 3600) t[k++] = (char)('0' + secs / 36000 % 10), t[k++] = (char)('0' + secs / 3600 % 10), t[k++] = ':';
  t[k++] = (char)('0' + secs / 600 % 6), t[k++] = (char)('0' + secs / 60 % 10), t[k++] = ':';
  t[k++] = (char)('0' + secs / 10 % 6), t[k++] = (char)('0' + secs % 10);
  lbl = "   SEED ";
  while (*lbl) t[k++] = *lbl++;
  for (int i = 0; i < 8; i++) {
    t[k++] = seed_chars[(g_run_seed >> (i * 4 % 28)) & 31];
    if (i == 3) t[k++] = ' ';
  }
  t[k] = 0;
  gfx_text_c(FONT_UPHEAVAL, 160, 140, t, INK2, 1, false);
}

static void draw_stage(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 32);
  int k = mode_t;
  /* Isaac falls through the dark to the next floor */
  int y = k * 4 - 30;
  if (y > 150) y = 150;
  gfx_sprite(character_head(pl.character) + 1, 0, 160, y + 7, 0, 32);
  gfx_sprite(character_head(pl.character), 1, 160, y, 0, 32);
  gfx_text_c(FONT_UPHEAVAL, 160, 90, stage_name(fl.stage, fl.alt), C_WHITE, 3, false);
  if (fl.curse) {
    static const char *const names[6] = {"CURSE OF DARKNESS", "CURSE OF THE LOST", "CURSE OF THE UNKNOWN", "CURSE OF THE MAZE",
                                         "CURSE OF THE BLIND", "CURSE OF THE LABYRINTH"};
    int i = 0;
    while (i < 5 && !(fl.curse & (1 << i))) i++;
    gfx_text_c(FONT_UPHEAVAL, 160, 124, names[i], rgb565(0xB040C0), 1, false);
  }
}

static void draw_dead(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, rgb565(0x140808), 32);
  paper(40, 16, 240, 208);
  gfx_text_c(FONT_UPHEAVAL, 160, 26, "YOU DIED", rgb565(0x901010), 3, false);
  gfx_sprite(character_head(pl.character) + 1, 0, 90, 110, 0, 32);
  gfx_sprite(character_head(pl.character), 1, 90, 103, 0, 32);
  gfx_text(FONT_UPHEAVAL, 120, 66, "KILLED BY", INK2, 1, false);
  const char *who = mon_name(g_killer);
  if (g_killer >= MON_MONSTRO) {
    switch (g_killer) {
      case MON_MOM: case MON_MOM_HAND: case MON_MOM_FOOT: who = "MOM"; break;
      default: who = NULL; break;
    }
  }
  static const char *const causes[] = {"", "A BOMB", "SPIKES", "A CURSED DOOR", "FIRE", "GREED", "A SPIKED CHEST",
                                       "THE DEVIL", "HIS OWN BLOOD", "A BAD PILL", "A SHOT", "A LASER", "CREEP"};
  if (!who) who = g_killer < 0 && -g_killer < (int)(sizeof causes / sizeof causes[0]) ? causes[-g_killer] : "SOMETHING";
  gfx_text(FONT_UPHEAVAL, 120, 80, who, INK, 1, false);
  gfx_text(FONT_UPHEAVAL, 120, 100, "ON", INK2, 1, false);
  gfx_text(FONT_UPHEAVAL, 120, 114, stage_name(fl.stage, fl.alt), INK, 1, false);
  for (int i = 0; i < pl.norder && i < 16; i++) gfx_sprite(item_sprite(pl.order[i]), 0, 64 + (i % 8) * 25, 160 + (i / 8) * 24, 0, 32);
  if (mode_t > 30) gfx_text_c(FONT_UPHEAVAL, 160, 206, "OK: NEW RUN   BACK: MENU", INK, 1, false);
}

static void draw_win(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 32);
  static const char *const text[4][4] = {
    {"MOM IS DEAD.", "BUT ISAAC'S STORY ISN'T OVER:", "THE WOMB IS BELOW HER ROOM.", ""},
    {"MOM'S HEART STOPS.", "ISAAC HIDES IN HIS CHEST", "AND WAITS... DOWN TO SHEOL,", "OR UP TO THE CATHEDRAL?"},
    {"SATAN FALLS.", "ISAAC HAS FACED HIS DEMONS", "AND THE DARK IS QUIET NOW.", "THE END."},
    {"ISAAC HAS FACED HIMSELF.", "IN THE LIGHT, NOTHING IS LEFT", "TO BE AFRAID OF.", "THE END."},
  };
  int e = ending & 3;
  gfx_sprite(character_head(pl.character) + 1, 0, 160, 200, 0, 32);
  gfx_sprite(character_head(pl.character), e >= 2 ? 0 : 1, 160, 193, 0, 32);
  for (int i = 0; i < 4; i++)
    if (mode_t > i * 25) gfx_text_c(FONT_UPHEAVAL, 160, 50 + i * 26, text[e][i], C_WHITE, i == 0 || (e >= 2 && i == 3) ? 2 : 1, false);
  if (mode_t > 110) gfx_text_c(FONT_UPHEAVAL, 160, 222, e >= 2 ? "OK: TO THE MENU" : "OK: GO ON", rgb565(0xB0B0B0), 1, false);
}

void game_draw(void) {
  gfx_begin();
  switch (g_mode) {
    case MODE_TITLE: draw_title(); break;
    case MODE_MENU: draw_menu(); break;
    case MODE_HELP: draw_help(); break;
    case MODE_CHARSEL: draw_charsel(); break;
    case MODE_PLAY: case MODE_MAP: draw_room(); break;
    case MODE_PAUSE: case MODE_CONFIRM: draw_pause(); break;
    case MODE_STAGE: draw_stage(); break;
    case MODE_DEAD: draw_dead(); break;
    case MODE_WIN: draw_win(); break;
    default: break;
  }
  gfx_end();
}

/* ---------------------------------------------------------------- the keys */
static int up_down(int sel, int n) {
  if (g_pressed & (K_UP | K_SUP)) sel = (sel + n - 1) % n;
  if (g_pressed & (K_DOWN | K_SDOWN)) sel = (sel + 1) % n;
  return sel;
}

void game_frame(uint32_t keys) {
  if (g_swap) keys = (keys & ~(uint32_t)(K_MOVE | K_SHOOT)) | (keys & K_MOVE) << 4 | (keys & K_SHOOT) >> 4;
  g_pressed = keys & ~g_keys;
  g_keys = keys;
  if (keys & K_HOME) {   /* Home and On/Off: the run kept, then out */
    save_run();
    save_progress();
    g_running = false;
    return;
  }
  bool ok = g_pressed & K_OK;
  mode_t++;
  switch (g_mode) {
    case MODE_TITLE:
      g_frame++;
      if (ok || (g_pressed & (K_SDOWN | K_ITEM))) g_mode = MODE_MENU, mode_t = 0, menu_sel = run_saved() ? 1 : 0;
      else if (g_pressed & K_BACK) g_running = false;
      break;
    case MODE_MENU: {
      g_frame++;
      int ids[4];
      const char *names[4];
      int n = menu_items(ids, names);
      menu_sel = up_down(menu_sel, n);
      if (g_pressed & K_BACK) g_mode = MODE_TITLE;
      bool side = g_pressed & (K_LEFT | K_RIGHT | K_SLEFT | K_SRIGHT);
      if (ok || (side && ids[menu_sel] == M_CONTROLS)) {
        int pick = ids[menu_sel];
        if (pick == M_NEW) g_mode = MODE_CHARSEL, mode_t = 0, char_sel = g_last_char;
        else if (pick == M_CONTROLS) g_swap = !g_swap, save_progress();
        else if (pick == M_CONTINUE) {
          if (load_run()) {
            floor_enter_room(fl.cur, DIR_NONE);
            player_stats();
            g_mode = MODE_PLAY;
          } else
            game_new_run(g_last_char);
        } else
          g_mode = MODE_HELP;
      }
      break;
    }
    case MODE_HELP:
      if (ok || (g_pressed & K_BACK)) g_mode = MODE_MENU;
      break;
    case MODE_CHARSEL:
      g_frame++;
      if (g_pressed & (K_LEFT | K_SLEFT)) char_sel = (char_sel + CH_COUNT - 1) % CH_COUNT;
      if (g_pressed & (K_RIGHT | K_SRIGHT)) char_sel = (char_sel + 1) % CH_COUNT;
      if (g_pressed & K_BACK) g_mode = MODE_MENU;
      if (ok && character_unlocked(char_sel)) {
        g_last_char = (uint8_t)char_sel;
        save_progress();
        game_new_run(char_sel);
      }
      break;
    case MODE_STAGE:
      g_frame++;
      if (mode_t > 75 || (ok && mode_t > 10)) {
        g_mode = MODE_PLAY;
        hud_banner(stage_name(fl.stage, fl.alt), "");
        banner_t = 40;
      }
      break;
    case MODE_PLAY:
      play_update();
      break;
    case MODE_PAUSE:
      pause_sel = up_down(pause_sel, 2);
      if (g_pressed & K_BACK) g_mode = MODE_PLAY;
      if (ok) {
        if (pause_sel == 0) g_mode = MODE_PLAY;
        else g_mode = MODE_CONFIRM, confirm_sel = 0;
      }
      break;
    case MODE_CONFIRM:
      confirm_sel = up_down(confirm_sel, 2);
      if (g_pressed & K_BACK) g_mode = MODE_PAUSE;
      if (ok) {
        if (confirm_sel == 1) {
          g_mode = MODE_PLAY;
          save_run();
          save_progress();
          g_mode = MODE_MENU, menu_sel = 1;
        } else
          g_mode = MODE_PAUSE;
      }
      break;
    case MODE_DEAD:
      if (mode_t > 30 && ok) game_new_run(pl.character);
      else if (mode_t > 30 && (g_pressed & K_BACK)) g_mode = MODE_MENU, menu_sel = 0;
      break;
    case MODE_WIN:
      if (mode_t > 110 && ok) {
        if (ending >= 2) g_mode = MODE_MENU, menu_sel = 0;
        else g_mode = MODE_PLAY;
      }
      break;
    default: break;
  }
}
