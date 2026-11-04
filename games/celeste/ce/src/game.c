/* The game: the frame loop's steps, the menus, the HUD, the save. */
#include "ce.h"

bool g_running = true;
int g_berries_now;
int g_anchor_x, g_anchor_y, g_anchor_lerp;
uint8_t g_anchor_ignore;

enum { SC_TITLE, SC_CHAPTERS, SC_PLAY, SC_PAUSE, SC_COMPLETE };
static int screen = SC_TITLE, sel, chapter = 1;
static int screen_t;
static bool full_redraw = true;
#ifdef CEL_BENCH
unsigned long g_bench[16], g_bench_last;
static int selftest;
#endif

/* ---------------------------------------------------------------- the save (appvar CELSAVE) */
#define NCHAPTERS 10
typedef struct {
  char magic[4];
  uint8_t version, size_lo, size_hi, pad;
  struct {
    uint8_t berries[8];        /* a bit per strawberry (its index in the chapter) */
    uint32_t deaths, best_time;
    uint8_t flags;             /* 1 completed, 2 golden, 4 cassette, 8 heart */
    uint8_t checkpoints;
  } ch[NCHAPTERS];
  uint32_t total_deaths;
} Save;
static Save save;
static void save_write(void) { plat_save("CELSAVE", &save, sizeof save); }
static void save_read(void) {
  if (!plat_load("CELSAVE", &save, sizeof save) || memcmp(save.magic, "CELS", 4) || save.version != 1) {
    memset(&save, 0, sizeof save);
    memcpy(save.magic, "CELS", 4);
    save.version = 1;
  }
}
bool save_has_berry(int ch, int index) { return index >= 0 && index < 64 && (save.ch[ch].berries[index >> 3] >> (index & 7) & 1); }
void save_add_berry(int ch, int index, bool golden) {
  if (golden) save.ch[ch].flags |= 2;
  else if (index >= 0 && index < 64) save.ch[ch].berries[index >> 3] |= (uint8_t)(1 << (index & 7));
}
static int berries_of(int ch) {
  int n = 0;
  for (int i = 0; i < 64; i++) n += save_has_berry(ch, i);
  return n;
}
void save_add_death(void) {
  save.ch[g_level.chapter].deaths++;
  save.total_deaths++;
}

/* ---------------------------------------------------------------- text helpers */
static void text_center(const char *s, int y, uint8_t c) { draw_text(s, 160 - text_width(s) / 2, y, c); }
static void num(char *out, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  while (n) *out++ = t[--n];
  *out = 0;
}

/* ---------------------------------------------------------------- playing */
static void play_chapter(int ch) {
  if (!chapter_open(ch)) return;
  chapter = ch;
  palette_apply(256);
  g_berries_now = 0;
  level_load(ch, -1, INTRO_JUMP);
  screen = SC_PLAY;
  full_redraw = true;
}

void game_chapter_end(void) {
  if (g_level.completed) return;
  extern void ents_collect_berries(void);
  ents_collect_berries();
  g_level.completed = true;
  save.ch[chapter].flags |= 1;
  if (!save.ch[chapter].best_time || g_level.time < save.ch[chapter].best_time) save.ch[chapter].best_time = g_level.time;
  save_write();
  screen_t = 0;
}

static void hud_draw(void) {
  /* the strawberry count, top left, while some are being collected (Celeste's TotalStrawberriesDisplay) */
  static int shown, timer;
  if (g_berries_now != shown) shown = g_berries_now, timer = 180;
  if (timer > 0) {
    timer--;
    char buf[16];
    buf[0] = 'x';
    num(buf + 1, (uint32_t)(berries_of(chapter)));
    draw_sprite(sprite(S_collectables_strawberry_normal00), g_camx + 2, g_camy + 2, 0);
    draw_text(buf, 22, VIEW_Y + 8, WHITE);
    draw_gone(0, 0, 22 + text_width(buf), 20);   /* (drawn over the view: both buffers draw it again) */
  }
}

/* ---------------------------------------------------------------- the frame */
void game_frame(void) {
  input_update(plat_keys());
  if (g_in.keys & K_QUIT) {
    save_write();
    g_running = false;
    return;
  }
  screen_t++;
  switch (screen) {
    case SC_TITLE:
      if (btn_pressed(&g_in.ok) || btn_pressed(&g_in.jump)) screen = SC_CHAPTERS, sel = 0, full_redraw = true;
      break;
    case SC_CHAPTERS:
      if (btn_pressed(&g_in.back)) screen = SC_TITLE, full_redraw = true;
      else if (btn_pressed(&g_in.ok) || btn_pressed(&g_in.jump)) play_chapter(1);
      break;
    case SC_PLAY:
      if (g_level.completed) {
        if (++screen_t > 90 && (btn_pressed(&g_in.ok) || btn_pressed(&g_in.jump))) screen = SC_CHAPTERS, full_redraw = true;
        break;
      }
      if (btn_pressed(&g_in.pause) && !g_player.dead && !g_level.transitioning) {
        btn_consume(&g_in.pause);
        screen = SC_PAUSE, sel = 0;
        break;
      }
      level_update();
      break;
    case SC_PAUSE:
      if (btn_pressed(&g_in.back) || btn_pressed(&g_in.pause)) screen = SC_PLAY;
      else if (g_in.move_y && !(g_in.prev & (K_UP | K_DOWN))) sel = (sel + g_in.move_y + 3) % 3;
      else if (btn_pressed(&g_in.ok) || btn_pressed(&g_in.jump)) {
        if (sel == 0) screen = SC_PLAY;
        else if (sel == 1) {
          screen = SC_PLAY;
          if (!g_player.dead) player_die(0, 0);
        } else {
          save_write();
          screen = SC_CHAPTERS, full_redraw = true;
        }
      }
      break;
  }
}

static void wipe_draw(void) {   /* AngledWipe: black bands sliding across, 24 frames either way */
  int w = g_level.wipe;
  if (!w) return;
  int t = w > 0 ? w : 24 + w;   /* 0..24: how closed */
  if (w < 0) t = -w;            /* opening: from closed to open */
  for (int y = 0; y < VIEW_H; y += 4) {
    int x = (t * 380 / 24) - 30 - (y * 30 / VIEW_H) + ((y / 4) & 1) * 6;
    if (w > 0) draw_rect_screen(320 - x, y, x + 1, 4, 0);
    else draw_rect_screen(0, y, x, 4, 0);
  }
}

void game_draw(void) {
  if (full_redraw) draw_clear_screen(0), full_redraw = false, level_render_forget();
  switch (screen) {
    case SC_TITLE:
      draw_clear_screen(0);
      text_center("CELESTE", 90, WHITE);
      text_center("2nd: start", 150, WHITE);
      break;
    case SC_CHAPTERS: {
      draw_clear_screen(0);
      text_center("Chapter 1", 80, WHITE);
      text_center("Forsaken City", 95, WHITE);
      char buf[24];
      num(buf, (uint32_t)berries_of(1));
      strcat(buf, " / 20 strawberries");
      text_center(buf, 120, WHITE);
      text_center("2nd: climb   clear: back", 200, WHITE);
      break;
    }
    case SC_PLAY:
    case SC_PAUSE:
      level_render(screen == SC_PAUSE || g_level.wipe || g_level.completed);
      hud_draw();
      wipe_draw();
      BENCH(7);
#ifdef CEL_BENCH
      {   /* thousands of cycles a frame (over the last 32 frames): update, wait, cells, measure, back, entities, fg,
           * hud, swap, rest */
        static unsigned frames;
        static unsigned shown[16];
        if (++frames % 32 == 0) {
          for (int k = 0; k < 16; k++) shown[k] = (unsigned)(g_bench[k] / 32000);
          memset(g_bench, 0, sizeof g_bench);
        }
        draw_rect_screen(0, -VIEW_Y, 320, VIEW_Y, 0);
        char buf[96], *q = buf;
        for (int k = 0; k < 16; k++) {
          if (k == 10) *q++ = '/', *q++ = ' ';
          unsigned v = shown[k];
          char tmp[8];
          int n = 0;
          do tmp[n++] = (char)('0' + v % 10), v /= 10; while (v);
          while (n) *q++ = tmp[--n];
          *q++ = ' ';
        }
        *q = 0;
        draw_text(buf, 2, 4, WHITE);
        if (q - buf > 52) draw_text(buf + 52, 2, 14, WHITE);
        draw_text(selftest ? "BAD" : "ok", 290, 14, WHITE);
      }
#endif
      if (screen == SC_PAUSE) {
        static const char *items[] = {"Resume", "Retry", "Save & Quit"};
        draw_rect_screen(100, 50, 120, 80, 0);
        for (int i = 0; i < 3; i++) draw_text(items[i], 120, VIEW_Y + 62 + i * 20, i == sel ? WHITE : 60);
      }
      if (g_level.completed) {
        draw_rect_screen(60, 50, 200, 80, 0);
        text_center("Chapter complete", VIEW_Y + 62, WHITE);
        char buf[32];
        num(buf, (uint32_t)berries_of(chapter));
        strcat(buf, " strawberries");
        text_center(buf, VIEW_Y + 82, WHITE);
      }
      break;
  }
}

/* straight into a room, Madeline at (x, y) in it (tests) */
void game_start_room(int ch, const char *room, int x, int y) {
  if (!chapter_open(ch)) return;
  chapter = ch;
  palette_apply(256);
  level_load(ch, level_room_find(room), INTRO_NONE);
  g_level.wipe = 0;
  if (x || y) {
    g_player.e->x = g_level.room->x + x, g_player.e->y = g_level.room->y + y;
    g_level.camx = player_cam_targetx() * 256, g_level.camy = player_cam_targety() * 256;
  }
  screen = SC_PLAY;
  full_redraw = true;
}

void game_init(void) {
  save_read();
  palette_apply(256);
#ifdef CEL_BENCH   /* a test build (tests/bench.sh): straight into that room, the camera going about it */
#define STR_(x) #x
#define STR(x) STR_(x)
  extern int draw_selftest(void);
  selftest = draw_selftest();
  game_start_room(1, STR(CEL_BENCH), 0, 0);
#endif
#ifndef __TICE__
  {
    extern char *getenv(const char *);
    extern int atoi(const char *);
    const char *s = getenv("CEL_START");   /* chapter,room[,x,y] */
    if (s) {
      char room[32] = "";
      int ch = atoi(s), x = 0, y = 0;
      const char *p = s;
      while (*p && *p != ',') p++;
      if (*p) p++;
      int k = 0;
      while (*p && *p != ',' && k < 31) room[k++] = *p++;
      room[k] = 0;
      if (*p) x = atoi(++p);
      while (*p && *p != ',') p++;
      if (*p) y = atoi(++p);
      game_start_room(ch, room, x, y);
    }
  }
#endif
}
