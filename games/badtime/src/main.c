/* Bad Time Simulator on the NumWorks calculator: the Judge's fight, a recreation of the fight in jcw87's Bad Time
 * Simulator (itself Undertale's last fight of its hardest route): 24 attacks of bones, gavels and platforms, the
 * blue heart, KR, the menu between attacks, the break and the final attack; Practice, Endless and Single attack.
 * The rules are sim.c and fight.c, the screens draw.c, the words text.c.
 *
 * Keys: the arrows move the heart (shift: slowly), OK chooses, Back goes back or pauses; Home saves and quits. */
#include <string.h>
#include "draw.h"
#include "fight.h"
#include "plat.h"
#include "save.h"

enum { V_TITLE, V_ENDLESS, V_SINGLE, V_HELP, V_FIGHT, V_PAUSE, V_CONFIRM, V_END };
enum { T_NORMAL, T_PRACTICE, T_ENDLESS, T_SINGLE, T_HELP, T_COUNT };

static bt_progress sv;
static fight_t fight;
static uint8_t view, under, sel, page, first_run, single_first, start_mode;
static int start_arg;
static uint32_t now, frame;
static bool fight_saved;

/* frames drawn, the slowest (ms) and those over 33 ms: read by tools/emu.py */
#ifndef HOST
__attribute__((used, externally_visible))
#endif
uint32_t perf_frames, perf_max, perf_slow;
#ifdef PERF
/* tools/armperf.py: an attack played with the heart unhurt, every frame's work timed */
volatile uint32_t perf_go, perf_attack, perf_done, perf_out[8];
#endif

static const char *const TITLE_ITEMS[T_COUNT] = {"Normal", "Practice", "Endless", "Single attack", "How to play"};
static const char *const ENDLESS_ITEMS[2] = {"Phase 1", "Phase 2"};
static const char *const PAUSE_ITEMS[3] = {"Resume", "How to play", "Quit game"};
static const char *const CONFIRM_ITEMS[2] = {"Keep playing", "Quit this fight"};

static char *cat(char *p, const char *s) {
  while (*s) *p++ = *s++;
  *p = 0;
  return p;
}
static char *num(char *p, unsigned v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 10);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

static void save(void) { save_write(&sv); }

/* what a fight left in the record */
static void record(void) {
  bool changed = false;
  if (fight.mode == MODE_NORMAL && fight.progress > sv.best) sv.best = fight.progress, changed = true;
  if (fight.mode == MODE_NORMAL && fight.state == FS_WON && !fight_saved) sv.wins++, changed = true;
  if (fight.mode == MODE_ENDLESS && fight.attacks_done > sv.endless[fight.phase & 1])
    sv.endless[fight.phase & 1] = (uint16_t)(fight.attacks_done > 65535 ? 65535 : fight.attacks_done), changed = true;
  if (fight.state == FS_WON || fight.state == FS_LOST) fight_saved = true;
  if (changed) save();
}

static void start(int mode, int arg) {
  start_mode = (uint8_t)mode, start_arg = arg;
  if (!sv.guide) {
    /* how to play first, the first time */
    sv.guide = 1, first_run = 1;
    save();
    view = V_HELP, under = V_TITLE, page = 0;
    return;
  }
  fight_start(&fight, mode, arg, plat_random() ^ now ^ 0x5EEDu);
  fight_saved = false;
  view = V_FIGHT;
}

static void title_screen(screen_t *s) {
  static char foot[64];
  char *p = foot;
  if (sv.best) p = cat(p, "Best: attack "), p = num(p, sv.best), p = cat(p, " of 24");
  else p = cat(p, "24 attacks. Survive them all.");
  if (sv.wins) p = cat(p, "     Wins: "), p = num(p, sv.wins);
  s->title = "Bad Time Simulator", s->title_y = 40;
  s->judge = true, s->judge_y = 112, s->judge_head = HEAD_DEFAULT;
  s->items = TITLE_ITEMS, s->n = T_COUNT, s->sel = sel, s->list_x = 116, s->list_y = 136, s->step = 17, s->rows = T_COUNT;
  s->foot = foot, s->keys = "Up Down choose   OK start   Back quit";
}

static void help_screen(screen_t *s) {
  static char t[24];
  char *p = cat(t, "How to play  ");
  p = num(p, page + 1u), p = cat(p, "/"), p = num(p, HELP_PAGES);
  s->title = t, s->title_y = 44;
  s->body = HELP[page], s->body_x = 14, s->body_y = 84;
  s->keys = first_run ? (page < 2 ? "Left Right page   OK next" : "OK start") : "Left Right page   Back done";
}

static void end_screen(screen_t *s) {
  static char body[160];
  char *p = body;
  bool won = fight.state == FS_WON;
  s->title = won ? "You won" : "You lost", s->title_y = 44;
  s->judge = true, s->judge_y = 128, s->judge_head = won ? HEAD_TIRED2 : HEAD_WINK;
  if (fight.mode == MODE_NORMAL) {
    if (won) p = cat(p, "All 24 attacks survived.");
    else p = cat(p, "Attack "), p = num(p, fight.progress), p = cat(p, " of 24.");
  } else {
    p = num(p, fight.attacks_done), p = cat(p, fight.attacks_done == 1 ? " attack survived." : " attacks survived.");
  }
  s->body = body, s->body_x = 160 - 8 * (int)strlen(body) / 3, s->body_y = 156;
  static char foot[48];
  p = foot;
  if (fight.mode == MODE_NORMAL) {
    p = cat(p, "Best: attack "), p = num(p, sv.best), p = cat(p, " of 24");
    if (sv.wins) p = cat(p, "     Wins: "), p = num(p, sv.wins);
  } else if (fight.mode == MODE_ENDLESS) p = cat(p, "Best: "), p = num(p, sv.endless[fight.phase & 1]);
  else foot[0] = 0;
  s->foot = foot, s->keys = "OK menu";
}

static void single_screen(screen_t *s) {
  int rows = 9;
  if (sel < single_first) single_first = sel;
  if (sel >= single_first + rows) single_first = (uint8_t)(sel - rows + 1);
  s->title = "Single attack", s->title_y = 40;
  s->items = atk_name, s->n = N_ATTACKS, s->sel = sel, s->first = single_first, s->rows = rows;
  s->list_x = 70, s->list_y = 74, s->step = 17;
  s->more_up = single_first > 0, s->more_down = single_first + rows < N_ATTACKS;
  s->keys = "Up Down choose   OK play   Back back";
}

static void draw(void) {
  screen_t s;
  memset(&s, 0, sizeof s);
  s.tick = frame;
  switch (view) {
    case V_FIGHT: {
      static char t[48];
      const char *title = 0;
      if (fight.mode != MODE_NORMAL && fight.state == FS_ATTACK) {
        char *p = cat(t, fight.mode == MODE_PRACTICE ? "Practice: " : fight.mode == MODE_ENDLESS ? "Endless: " : "");
        cat(p, atk_name[fight.last_attack]);
        title = t;
      }
      draw_fight(&fight, title);
      return;
    }
    case V_PAUSE:
    case V_CONFIRM:
      s.title = view == V_PAUSE ? "Paused" : "Quit?";
      s.items = view == V_PAUSE ? PAUSE_ITEMS : CONFIRM_ITEMS, s.n = view == V_PAUSE ? 3 : 2, s.sel = sel;
      s.keys = "Up Down choose   OK select   Back resume";
      draw_pause(&fight, &s);
      return;
    case V_TITLE: title_screen(&s); break;
    case V_ENDLESS:
      s.title = "Endless", s.title_y = 40, s.judge = true, s.judge_y = 120, s.judge_head = HEAD_DEFAULT;
      s.items = ENDLESS_ITEMS, s.n = 2, s.sel = sel, s.list_x = 126, s.list_y = 150, s.step = 18, s.rows = 2;
      s.foot = "Attacks back to back, no menu, until you fall.";
      s.keys = "Up Down choose   OK play   Back back";
      break;
    case V_SINGLE: single_screen(&s); break;
    case V_HELP: help_screen(&s); break;
    case V_END: end_screen(&s); break;
  }
  draw_screen(&s);
}

static void menu_move(uint32_t hit, int n) {
  if (hit & BK_UP) sel = (uint8_t)((sel + n - 1) % n);
  if (hit & BK_DOWN) sel = (uint8_t)((sel + 1) % n);
}

/* a key on a screen other than the fight; false: quit the app */
static bool screen_key(uint32_t hit) {
  switch (view) {
    case V_TITLE:
      menu_move(hit, T_COUNT);
      if (hit & BK_BACK) return false;
      if (hit & BK_OK) {
        if (sel == T_NORMAL) start(MODE_NORMAL, 0);
        else if (sel == T_PRACTICE) start(MODE_PRACTICE, 0);
        else if (sel == T_ENDLESS) view = V_ENDLESS, sel = 0;
        else if (sel == T_SINGLE) view = V_SINGLE, sel = 0, single_first = 0;
        else view = V_HELP, under = V_TITLE, page = 0, first_run = 0;
      }
      break;
    case V_ENDLESS:
      menu_move(hit, 2);
      if (hit & BK_BACK) view = V_TITLE, sel = T_ENDLESS;
      else if (hit & BK_OK) start(MODE_ENDLESS, sel);
      break;
    case V_SINGLE:
      menu_move(hit, N_ATTACKS);
      if (hit & BK_BACK) view = V_TITLE, sel = T_SINGLE;
      else if (hit & BK_OK) start(MODE_SINGLE, sel);
      break;
    case V_HELP:
      if ((hit & BK_RIGHT) && page < HELP_PAGES - 1) page++;
      else if ((hit & BK_LEFT) && page > 0) page--;
      else if ((hit & BK_OK) && first_run && page < 2) page++;
      else if (hit & (BK_BACK | BK_OK)) {
        if (first_run) {
          first_run = 0;
          start(start_mode, start_arg);
        } else if (under == V_PAUSE) view = V_PAUSE, sel = 1;
        else view = V_TITLE, sel = T_HELP;
      }
      break;
    case V_PAUSE:
      menu_move(hit, 3);
      if (hit & BK_BACK) view = V_FIGHT;
      else if (hit & BK_OK) {
        if (sel == 0) view = V_FIGHT;
        else if (sel == 1) view = V_HELP, under = V_PAUSE, page = 0, first_run = 0;
        else view = V_CONFIRM, sel = 0;
      }
      break;
    case V_CONFIRM:
      menu_move(hit, 2);
      if (hit & BK_BACK) view = V_PAUSE, sel = 2;
      else if (hit & BK_OK) {
        if (sel == 0) view = V_FIGHT;
        else record(), view = V_TITLE, sel = 0;
      }
      break;
    case V_END:
      if (hit & (BK_OK | BK_BACK)) view = V_TITLE, sel = 0;
      break;
  }
  return true;
}

int main(void) {
  plat_begin();
  save_load(&sv);
  view = V_TITLE, sel = 0;
  uint32_t stale = plat_keys(), prev = stale, pending = 0;
  now = plat_millis();
  uint32_t t0 = now, ticks = 0;
  bool dirty = true;
  for (;;) {
    now = plat_millis();
    uint32_t keys = plat_keys();
    stale &= keys;                      /* keys still down from the screen before don't count */
    uint32_t live = keys & ~stale, hit = live & ~prev;
    prev = live;
    if (keys & BK_HOME) break;          /* Home: saved below, then out */
#ifdef PERF
    if (perf_go == 1) {
      perf_go = 2;
      fight_start(&fight, MODE_SINGLE, (int)perf_attack, 1234);
      view = V_FIGHT, t0 = now, ticks = 0;
    }
#endif
    if (view == V_FIGHT) {
      pending |= hit;
      if (hit & BK_BACK && fight_can_pause(&fight) && !fight_over(&fight)) {
        view = V_PAUSE, sel = 0, pending = 0;
        stale = keys;
        dirty = true;
        continue;
      }
      if (fight_over(&fight) && (hit & (BK_OK | BK_BACK))) {
        record();
        view = V_END;
        stale = keys;
        dirty = true;
        continue;
      }
      /* the fight at 30 ticks a second, catching up when a frame took long */
      uint32_t due = (now - t0) * 3 / 100;
      if (due > ticks + 3) ticks = due - 3;
      bool ran = false;
      uint32_t w0 = plat_millis();
      while (ticks < due) {
#ifdef PERF
        if (perf_go == 2 && (ticks & 1)) pending |= BK_OK;   /* (its words read at once) */
#endif
        fight_tick(&fight, live, pending);
        pending = 0, ticks++, ran = true;
#ifdef PERF
        if (perf_go == 2) fight.w.hp = MAX_HP, fight.w.kr = 0;
#endif
      }
      if (fight.progress > sv.best && fight.mode == MODE_NORMAL) record();
      if (fight_over(&fight) && !fight_saved) record();
      if (ran) {
        frame++;
        draw();
        uint32_t spent = plat_millis() - w0;
        perf_frames++;
        if (spent > perf_max) perf_max = spent;
        if (spent > 33) perf_slow++;
#ifdef PERF
        if (perf_go == 2) {
          perf_out[0]++, perf_out[1] += spent;
          if (spent > perf_out[2]) perf_out[2] = spent;
          if (spent > 33) perf_out[3]++;
          perf_out[4] = fight.w.nbones > perf_out[4] ? fight.w.nbones : perf_out[4];
          perf_out[5] = fight.w.nblasters > perf_out[5] ? fight.w.nblasters : perf_out[5];
          if (fight.attacks_done >= 1) perf_go = 3, perf_done = 1;
        }
#endif
      } else plat_sleep(4);
      continue;
    }
    /* the other screens */
    if (hit) {
      uint8_t before = view;
      if (!screen_key(hit)) break;
      if (view == V_FIGHT && before != V_FIGHT) t0 = now, ticks = 0, stale = keys, prev = 0, pending = 0;
      dirty = true;
    }
    if (view == V_TITLE || view == V_ENDLESS || view == V_END) {
      /* (the Judge breathes) */
      if ((now - t0) / 33 != frame) frame = (now - t0) / 33, dirty = true;
    }
    if (dirty) {
      draw();
      dirty = false;
    } else plat_sleep(10);
  }
  if (view == V_FIGHT || view == V_PAUSE || view == V_CONFIRM) record();
  save();
  return plat_end();
}
