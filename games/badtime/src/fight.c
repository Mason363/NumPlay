/* Bad Time Simulator: the fight's turns (fight.h), as jcw87's Battle sheet runs them: the attacks in their
 * order, the menu between them, FIGHT and the dodge, the items, the break, the final attack and the end; and the
 * Practice, Endless and Single attack modes. */
#include <string.h>
#include "fight.h"

#define MENU_L 33.0f
#define MENU_T 251.0f
#define MENU_R 608.0f
#define MENU_B 391.0f

static const uint8_t FIRST_HALF[14] = {A_INTRO, A_BONEGAP1, A_BLUEBONE, A_BONEGAP2, A_PLATFORMS1, A_PLATFORMS2,
                                       A_PLATFORMS3, A_PLATFORMS4, A_PLATFORMBLASTER, A_PLATFORMS4HARD,
                                       A_BONEGAP1FAST, A_BONESLIDEH, A_BONEGAP2, A_PLATFORMBLASTERFAST};
static const uint8_t FIRST_RANDOM[4] = {A_BONEGAP1FAST, A_BONEGAP2, A_BONESLIDEH, A_PLATFORMBLASTERFAST};
static const uint8_t SECOND_HALF[9] = {A_MULTI1, A_RANDOMBLASTER1, A_MULTI2, A_BONESTAB1, A_BONESTAB2,
                                       A_RANDOMBLASTER2, A_BONESLIDEV, A_MULTI3, A_BONESTAB3};
static const uint8_t SECOND_RANDOM[3] = {A_BONESTAB3, A_MULTI3, A_RANDOMBLASTER2};
/* the buttons, in the original's pixels */
static const float BUTTON_X[4] = {32, 184, 344, 496};

int fight_progress_of(int hit, int next) {
  if (hit < 13) return (next < 13 ? next : 13) + 1;
  if (hit == 13) return 14;
  if (hit <= 22) return 15 + (next < 8 ? next : 8);
  return 24;
}

static void say(fight_t *f, const char *s, int kind, int after) {
  size_t n = s ? strlen(s) : 0;
  if (n >= TEXT_MAX) n = TEXT_MAX - 1;
  memcpy(f->text, s ? s : "", n);
  f->text[n] = 0;
  f->text_len = (uint16_t)n, f->text_shown = 0;
  f->text_kind = (uint8_t)kind, f->text_after = (uint8_t)after;
}
static void quiet(fight_t *f) { f->text_kind = TXT_NONE, f->text_len = f->text_shown = 0, f->text[0] = 0; }

/* the heart as the menu's cursor */
static void heart_at(fight_t *f, float x, float y) {
  f->w.hx = x, f->w.hy = y, f->w.heart_on = 1, f->w.heart_free = 0;
  f->w.heart_mode = HEART_RED, f->w.heart_dir = DIR_DOWN;
}
static void heart_on_button(fight_t *f) { heart_at(f, BUTTON_X[f->sel] + 18, 453); }
void fight_item_pos(int i, float *x, float *y) {
  *x = 64 + (float)(i % 2) * 256 + 8;
  *y = 272 + (float)((i % 4) / 2) * 32 + 12;
}

static int pick_attack(fight_t *f) {
  if (f->mode == MODE_SINGLE) return f->single;
  if (f->mode == MODE_PRACTICE) f->next_attack = f->practice;
  int h = f->hit_attempts, n = f->next_attack, a;
  if (h < 13) {
    if (n < 14) a = FIRST_HALF[n], f->next_attack++;
    else a = FIRST_RANDOM[bt_below(&f->w.rng, 4)];
  } else if (h == 13) {
    a = A_SPARE;
    f->w.sweat = 2;
    f->next_attack = 0;
  } else if (h <= 22) {
    if (n < 9) a = SECOND_HALF[n], f->next_attack++;
    else a = SECOND_RANDOM[bt_below(&f->w.rng, 3)];
  } else a = A_FINAL;
  return a;
}

static void start_turn(fight_t *f) {
  world_t *w = &f->w;
  if (f->mode == MODE_NORMAL) {
    int p = fight_progress_of(f->hit_attempts, f->next_attack);
    if (p > f->progress) f->progress = (uint8_t)p;
  }
  int a = pick_attack(f);
  f->last_attack = (uint8_t)a;
  sim_start_attack(w, a);
  w->heart_on = 0;   /* (until the attack puts it in the box) */
  if (f->mode == MODE_PRACTICE) w->hp = MAX_HP, w->kr = 0, f->failed = 0;
  if (f->mode == MODE_SINGLE) w->anim = ANIM_IDLE, w->head = HEAD_DEFAULT;
  quiet(f);
  f->state = FS_ATTACK;
}

static const char *info_line(const fight_t *f) {
  int h = f->hit_attempts, kr = f->w.kr;
  const char *s = f->info ? f->info : INFO_KR[kr >= 20 ? 2 : kr >= 10 ? 1 : 0];
  if (h < 13 && f->next_attack == 1) s = INFO_FIRST;
  if (h == 13) s = INFO_BREAK;
  if (h == 15) s = INFO_15;
  if (h == 19) s = INFO_19;
  if (h == 20) s = INFO_20;
  if (h == 21) s = INFO_21;
  if (h == 22) s = INFO_22;
  return s;
}

static void open_menu(fight_t *f) {
  f->state = FS_MENU;
  heart_on_button(f);
  f->info = info_line(f);
  say(f, f->info, TXT_BOX, 0);
}

static void end_attack(fight_t *f) {
  world_t *w = &f->w;
  bool failed = f->failed;
  sim_stop_attack(w);
  f->attacks_done++;
  if (f->mode == MODE_PRACTICE) {
    f->banner = failed ? 2 : 1, f->banner_t = 30;
    if (!failed) {
      f->practice++;
      if (f->hit_attempts < 13 && f->practice == 14) f->hit_attempts = 14, f->practice = 0;
      if (f->hit_attempts > 13 && f->practice > 9) f->practice = 9;
    }
    if (f->hit_attempts > 13 && f->practice == 9) f->hit_attempts = 23;
  }
  w->anim = ANIM_IDLE, w->head = HEAD_DEFAULT, w->torso = TORSO_DEFAULT, w->body = BODY_NONE;
  if (f->mode == MODE_NORMAL && f->hit_attempts > 22) {
    /* the last words, then the win */
    f->state = FS_WIN, f->win_line = 0;
    sim_zone_to(w, w->zl, w->zt, w->zr, w->zb, false);
    w->anim = ANIM_TIRED, w->head = HEAD_TIRED2;
    w->heart_on = 0;
    say(f, TALK_WIN[0], TXT_BUBBLE, 0);
    return;
  }
  sim_zone_to(w, MENU_L, MENU_T, MENU_R, MENU_B, false);
  int h = f->hit_attempts;
  if (f->mode == MODE_NORMAL) sim_menu_bones(w, h > 13 && h != 16 && h != 17 && h <= 22, h > 15 && h <= 22);
  f->state = FS_RETURN;
}

static void die(fight_t *f) {
  world_t *w = &f->w;
  sim_stop_attack(w);
  sim_menu_bones(w, false, false);
  w->nmbones = 0;
  w->black = 1, w->heart_on = 1, w->heart_mode = HEART_RED, w->heart_dir = DIR_DOWN;
  quiet(f);
  f->state = FS_DEATH, f->death_t = 0;
}

void fight_start(fight_t *f, int mode, int arg, uint32_t seed) {
  memset(f, 0, sizeof *f);
  f->mode = (uint8_t)mode, f->seed = seed;
  sim_reset(&f->w, seed);
  static const uint8_t START_ITEMS[8] = {0, 1, 2, 3, 3, 3, 3, 3};
  memcpy(f->items, START_ITEMS, 8);
  f->nitems = 8;
  world_t *w = &f->w;
  if (mode == MODE_SINGLE) f->single = (uint8_t)(arg >= 0 && arg < N_ATTACKS ? arg : 0);
  if (mode == MODE_ENDLESS) {
    f->phase = (uint8_t)(arg == 1);
    if (f->phase) f->hit_attempts = 15, w->head = HEAD_DEFAULT, w->anim = ANIM_IDLE;
  }
  if (mode == MODE_PRACTICE) f->practice = 0;
  /* the fight opens on its first attack, the box as the menu's */
  sim_zone_to(w, MENU_L, MENU_T, MENU_R, MENU_B, false);
  w->zl = MENU_L, w->zt = MENU_T, w->zr = MENU_R, w->zb = MENU_B;
  start_turn(f);
}

bool fight_over(const fight_t *f) { return f->state == FS_LOST || f->state == FS_WON; }
bool fight_can_pause(const fight_t *f) { return f->state != FS_SUB; }

/* the words come a letter a tick; OK shows them all, then goes on */
static bool text_tick(fight_t *f, uint32_t pressed) {
  if (f->text_kind == TXT_NONE) return false;
  if (f->text_shown < f->text_len) {
    f->text_shown++;
    if (pressed & BK_OK) f->text_shown = f->text_len;
    return false;
  }
  return (pressed & BK_OK) != 0;
}

static int item_count_on_page(const fight_t *f, int page) {
  int n = f->nitems - page * 4;
  return n < 0 ? 0 : n > 4 ? 4 : n;
}

static void sub_open(fight_t *f, int sub) {
  f->state = FS_SUB, f->sub = (uint8_t)sub, f->sub_sel = 0;
  quiet(f);
  float x, y;
  fight_item_pos(0, &x, &y);
  heart_at(f, x, y);
}

static void sub_cursor(fight_t *f) {
  float x, y;
  fight_item_pos(f->sub_sel, &x, &y);
  heart_at(f, x, y);
}

static void use_item(fight_t *f) {
  int slot = f->sub_sel;
  if (slot >= f->nitems) return;
  const item_t *it = &ITEMS[f->items[slot]];
  world_t *w = &f->w;
  w->hp = (int16_t)(w->hp + it->heal);
  if (w->hp > MAX_HP) w->hp = MAX_HP;
  if (w->kr >= w->hp) w->kr = (int16_t)(w->hp - 1);
  memmove(f->items + slot, f->items + slot + 1, (size_t)(f->nitems - slot - 1));
  f->nitems--;
  char buf[TEXT_MAX], *p = buf;
  const char *a = "* You eat the ", *b = ".\n* You recovered ";
  while (*a) *p++ = *a++;
  for (const char *s = it->full; *s; s++) *p++ = *s;
  while (*b) *p++ = *b++;
  int v = it->heal;   /* (the item's HP, even past the most: as the simulator says it) */
  if (v >= 10) *p++ = (char)('0' + v / 10);
  *p++ = (char)('0' + v % 10);
  for (const char *s = " HP!"; *s; s++) *p++ = *s;
  *p = 0;
  f->state = FS_INFO, f->w.heart_on = 0;
  say(f, buf, TXT_BOX, 0);
}

static void sub_ok(fight_t *f) {
  switch (f->sub) {
    case SUB_ENEMY:
      if (f->sel == 0) {
        /* FIGHT: the bar comes in from a side */
        f->state = FS_TARGET, f->w.heart_on = 0;
        f->bar_dir = bt_below(&f->w.rng, 2) == 0 ? 1 : -1;
        f->bar_x = f->bar_dir > 0 ? f->w.zl : f->w.zr;
        f->bar_hit = 0, f->bar_fade = 0;
      } else {
        f->sub = SUB_ACT, f->sub_sel = 0;
        sub_cursor(f);
      }
      break;
    case SUB_ACT:
      f->state = FS_INFO, f->w.heart_on = 0;
      say(f, CHECK_TEXT, TXT_BOX, f->hit_attempts > 0 ? 1 : 0);
      break;
    case SUB_ITEM: use_item(f); break;
    case SUB_MERCY:
      f->w.heart_on = 0;
      start_turn(f);
      break;
  }
}

static void sub_keys(fight_t *f, uint32_t pressed) {
  if (pressed & BK_BACK) {
    if (f->sub == SUB_ACT) {
      f->sub = SUB_ENEMY, f->sub_sel = 0;
      sub_cursor(f);
    } else open_menu(f);
    return;
  }
  if (pressed & BK_OK) {
    sub_ok(f);
    return;
  }
  if (f->sub != SUB_ITEM) return;
  /* the items: two columns, four a page; Left and Right go on to the next page */
  int s = f->sub_sel, page = s / 4, i = s % 4, on = item_count_on_page(f, page);
  if ((pressed & BK_RIGHT) && (i % 2 == 0) && i + 1 < on) s++;
  else if ((pressed & BK_RIGHT) && (i % 2 == 1 || i + 1 >= on) && item_count_on_page(f, page + 1))
    s = (page + 1) * 4 + (i / 2) * 2, s = s < f->nitems ? s : f->nitems - 1;
  else if ((pressed & BK_LEFT) && (i % 2 == 1)) s--;
  else if ((pressed & BK_LEFT) && (i % 2 == 0) && page > 0) s = (page - 1) * 4 + (i / 2) * 2 + 1;
  else if ((pressed & BK_DOWN) && i + 2 < on) s += 2;
  else if ((pressed & BK_UP) && i >= 2) s -= 2;
  if (s != f->sub_sel) {
    f->sub_sel = (uint8_t)s;
    sub_cursor(f);
  }
}

static void dodge_tick(fight_t *f) {
  world_t *w = &f->w;
  float t = (float)f->timer / 30.0f;
  f->timer++;
  if (w->dodge == 1) {
    w->ox = 320.0f - bt_sinf_deg(t * 225.0f) * 100.0f;
    if (f->timer >= 12) w->ox = 220, w->dodge = 2, f->timer = 0;
  } else if (w->dodge == 2) {
    if (f->timer == 18) f->miss_t = 45;
    if (f->timer >= 33) w->dodge = 3, f->timer = 0;
  } else if (w->dodge == 3) {
    w->ox = 320.0f - bt_sinf_deg(t * 225.0f + 90.0f) * 100.0f;
    if (f->timer >= 12) {
      w->ox = 320, w->dodge = 0, f->timer = 0;
      f->hit_attempts++;
      f->strikes++;
      start_turn(f);
    }
  }
}

void fight_tick(fight_t *f, uint32_t keys, uint32_t pressed) {
  world_t *w = &f->w;
  if (f->banner_t > 0) f->banner_t--;
  if (f->miss_t > 0) f->miss_t--;
  if (f->slash_t > 0) f->slash_t--;
  if (f->bar_fade > 0) f->bar_fade--;
  switch (f->state) {
    case FS_ATTACK: {
      /* the scripts' own words, then ours */
      if (w->sc.talk && f->text_kind != TXT_BUBBLE && w->sc.talk_line >= 0)
        say(f, TALK_SCRIPT[w->sc.talk_line & 1], TXT_BUBBLE, 0);
      if (f->text_kind == TXT_BUBBLE && text_tick(f, pressed)) {
        quiet(f);
        sim_resume(w);
        pressed &= ~(uint32_t)BK_OK;
      }
      sim_tick(w, keys, pressed);
      if (f->mode == MODE_PRACTICE && !w->slam_damage && w->sc.running && !w->sc.ended) {
        int target = f->hit_attempts > 22 ? 2 : 60;
        if (w->hp - w->kr < target) {
          if (f->hit_attempts > 22) f->hit_attempts = 22;
          f->failed = 1, w->sc.ended = 1;
        }
      }
      if (w->hp <= 0) {
        die(f);
        break;
      }
      if (w->sc.ended) end_attack(f);
      break;
    }
    case FS_RETURN:
      sim_tick(w, 0, 0);
      if (w->hp <= 0) w->hp = 1;
      if (sim_zone_there(w)) {
        if (f->mode == MODE_NORMAL) open_menu(f);
        else start_turn(f);
      }
      break;
    case FS_MENU:
      sim_tick(w, 0, 0);
      if (w->hp <= 0) w->hp = 1;
      text_tick(f, 0);
      if (pressed & BK_LEFT) f->sel = (uint8_t)((f->sel + 3) & 3);
      if (pressed & BK_RIGHT) f->sel = (uint8_t)((f->sel + 1) & 3);
      heart_on_button(f);
      if (pressed & BK_OK) {
        if (f->sel == 0 || f->sel == 1) sub_open(f, SUB_ENEMY);
        else if (f->sel == 2 && f->nitems) sub_open(f, SUB_ITEM);
        else if (f->sel == 3) sub_open(f, SUB_MERCY);
      }
      break;
    case FS_SUB:
      sim_tick(w, 0, 0);
      if (w->hp <= 0) w->hp = 1;
      sub_keys(f, pressed);
      break;
    case FS_TARGET:
      sim_tick(w, 0, 0);
      if (!f->bar_hit) {
        f->bar_x += (float)f->bar_dir * 12.0f;
        bool past = f->bar_dir > 0 ? f->bar_x >= w->zr : f->bar_x <= w->zl;
        if (past) {
          f->miss_t = 30, f->bar_fade = 12;
          start_turn(f);
        } else if (pressed & BK_OK) {
          f->bar_hit = 1, f->slash_t = 18, f->bar_fade = 40;
          f->state = FS_DODGE, w->dodge = 1, f->timer = 0;
        }
      }
      break;
    case FS_DODGE:
      sim_tick(w, 0, 0);
      dodge_tick(f);
      break;
    case FS_INFO:
      sim_tick(w, 0, 0);
      if (text_tick(f, pressed)) {
        if (f->text_after == 1) say(f, CHECK_TEXT2, TXT_BOX, 0);
        else start_turn(f);
      }
      break;
    case FS_DEATH:
      f->death_t++;
      if (f->death_t == 60) {
        for (int i = 0; i < MAX_SHARDS; i++) {
          float a = (float)bt_below(&w->fx_rng, 360);
          shard_t *s = &f->shards[i];
          s->x = w->hx, s->y = w->hy;
          s->vx = bt_sinf_deg(a + 90.0f) * 6.0f, s->vy = bt_sinf_deg(a) * 6.0f;
        }
      }
      if (f->death_t > 60)
        for (int i = 0; i < MAX_SHARDS; i++) {
          shard_t *s = &f->shards[i];
          s->x += s->vx, s->y += s->vy, s->vy += 300.0f / 900.0f;
        }
      if (f->death_t >= 120) f->state = FS_LOST;
      break;
    case FS_WIN:
      sim_tick(w, 0, 0);
      if (text_tick(f, pressed)) {
        if (++f->win_line < 2) say(f, TALK_WIN[f->win_line], TXT_BUBBLE, 0);
        else quiet(f), f->state = FS_WON, f->progress = 24;
      }
      break;
    default: break;
  }
}

const char *fight_keys(const fight_t *f) {
  const world_t *w = &f->w;
  switch (f->state) {
    case FS_ATTACK:
      if (f->text_kind == TXT_BUBBLE) return "OK next";
      if (w->heart_mode == HEART_RED) return "Arrows move   shift slow   Back pause";
      {
        static const char *const BLUE[4] = {
          "Left jump   Up Down move   Back pause", "Up jump, hold for higher   Left Right move   Back pause",
          "Right jump   Up Down move   Back pause", "Down jump   Left Right move   Back pause"};
        return BLUE[w->heart_dir & 3];
      }
    case FS_MENU: return "Left Right choose   OK select   Back pause";
    case FS_SUB: return "Arrows choose   OK select   Back back";
    case FS_TARGET: return "OK strike";
    case FS_INFO:
    case FS_WIN: return "OK next";
    case FS_LOST:
    case FS_WON: return "OK menu";
    default: return "";
  }
}
