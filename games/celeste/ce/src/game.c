/* The game: the frame loop's steps, the menus, the HUD, the save. */
#include "ce.h"

bool g_running = true;
int g_berries_now;
int g_anchor_x, g_anchor_y, g_anchor_lerp;
uint8_t g_anchor_ignore;

enum { SC_KEYS, SC_TITLE, SC_CHAPTERS, SC_PLAY, SC_PAUSE, SC_COMPLETE };
static int screen = SC_TITLE, sel, chapter = 1;
static int screen_t;
static int full_redraw = 2;   /* (frames to clear the screen in: both buffers) */
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
  uint8_t keys_seen;          /* the controls were shown */
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
  static const uint8_t ONES[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
  int n = 0;
  for (int i = 0; i < 8; i++) n += ONES[save.ch[ch].berries[i] & 15] + ONES[save.ch[ch].berries[i] >> 4];
  return n;
}
void save_add_death(void) {
  save.ch[g_level.chapter].deaths++;
  save.total_deaths++;
}

/* ---------------------------------------------------------------- words and pictures */
#define C_WHITE 0xFFFFFFul
#define C_GRAY 0x808080ul
static void num(char *out, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  while (n) *out++ = t[--n];
  *out = 0;
}
static uint32_t highlight(void) { return screen_t % 12 < 6 ? 0x84FF54ul : 0xFCFF59ul; }   /* TextMenu's, in turn */
static void pic(int id, int cx, int cy) {   /* a menu picture centred at (cx, cy), screen */
  const uint8_t *sp = sprite(id);
  int camx = g_camx, camy = g_camy;
  g_camx = g_camy = 0;
  draw_sprite(sp, cx - sp[0] / 2, cy - sp[1] / 2 - VIEW_Y, 0);
  g_camx = camx, g_camy = camy;
}
static void prompt(const char *key, const char *what, int y) {   /* "2nd" and what it does, at the bottom */
  int gap = text_width(FONT_BODY, "  "), kw = text_width(FONT_BODY, key);
  int w = 10 + kw + gap + text_width(FONT_BODY, what), x = 160 - w / 2;
  if (screen_t % 60 < 45) pic(S_gui_button, x + 3, y + 7);
  text_draw(FONT_BODY, key, x + 10, y, C_WHITE, 0);
  text_draw(FONT_BODY, what, x + 10 + kw + gap, y, C_GRAY, 0);
}
static void time_text(uint32_t frames, char *out) {   /* m:ss.mmm */
  uint32_t ms = frames * 50 / 3, m = ms / 60000, sec = ms / 1000 % 60;
  num(out, m);
  out += strlen(out);
  *out++ = ':', *out++ = (char)('0' + sec / 10), *out++ = (char)('0' + sec % 10), *out++ = '.';
  ms %= 1000;
  *out++ = (char)('0' + ms / 100), *out++ = (char)('0' + ms / 10 % 10), *out++ = (char)('0' + ms % 10), *out = 0;
}
/* the chapter's icon over its banner (OuiChapterSelect's: "Chapter 1" small in the accent colour, the name in white;
 * the icon at (960, 360) of 1080p, the banner's left top at (552, 620), the words right-aligned at its 816) */
static void banner(void) {
  pic(S_gui_city, 160, VIEW_Y + 60);
  const uint8_t *b = sprite(S_gui_banner1);
  int x = 92, top = VIEW_Y + 103;
  pic(S_gui_banner1, x + b[0] / 2, top + b[1] / 2);
  text_right(FONT_SMALL, "Chapter 1", x + 136, top + 3, 0x2f344bul, 0x6c7c81ul);
  text_right(FONT_BODY, "Forsaken City", x + 136, top + 11, C_WHITE, 0x6c7c81ul);
}
static void stat_line(int icon, const char *t, int y) {
  int w = text_width(FONT_BODY, t) + 16;
  pic(icon, 160 - w / 2 + 5, y + 7);
  text_draw(FONT_BODY, t, 160 - w / 2 + 14, y, C_WHITE, 0);
}

/* ---------------------------------------------------------------- playing */
static void hud_reset(void);
static void play_chapter(int ch) {
  if (!chapter_open(ch)) return;
  chapter = ch;
  palette_tint(true), palette_apply(256);
  g_berries_now = 0;
  hud_reset();
  level_load(ch, -1, INTRO_JUMP);
  screen = SC_PLAY;
  full_redraw = 2;
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

/* TotalStrawberriesDisplay: slides in from the left when strawberries are collected (Ease.CubeOut, 1.2 a second in,
 * 2 out), counts them up one at a time (0.4 s, then 0.3 s each), stays 2 s; its 1080p places at 1/6 on the view */
static struct { int lerp, update, wait, amount; bool started; } H;   /* (lerp 1/256, times in frames) */
static void hud_reset(void) { H.started = false, H.lerp = 0; }
static void hud_update(void) {
  int total = berries_of(chapter);
  if (!H.started) H.started = true, H.amount = total;
  if (total > H.amount && H.update <= 0) H.update = 24;
  if (total > H.amount || H.update > 0 || H.wait > 0) H.lerp = imin(256, H.lerp + 256 * 12 / 10 / 60 + 1);
  else H.lerp = imax(0, H.lerp - 256 * 2 / 60);
  if (H.wait > 0) H.wait--;
  if (H.update > 0 && H.lerp == 256 && --H.update <= 0) {
    if (H.amount < total) H.amount++;
    H.wait = 120;
    if (H.amount < total) H.update = 18;
  }
}
static void hud_draw(void) {
  if (H.lerp <= 0) return;
  int t = 256 - H.lerp, e = 256 - t * t / 256 * t / 256;   /* (CubeOut) */
  int x = (-288 + (32 + 288) * e / 256) / 6, y = 16;   /* (its 1080p x / 6, the view's y) */
  const uint8_t *bg = sprite(S_gui_countbg), *st = sprite(S_gui_strawberry), *xs = sprite(S_gui_x);
  int camx = g_camx, camy = g_camy;
  g_camx = g_camy = 0;
  draw_sprite(bg, x - 16, y - 1, 0);
  draw_sprite(st, x + 5 - st[0] / 2, y - st[1] / 2, 0);
  draw_sprite(xs, x + 10, y + 1 - xs[1] / 2, 0);
  g_camx = camx, g_camy = camy;
  char n[8];
  num(n, (uint32_t)H.amount);
  text_draw(FONT_BODY, n, x + 10 + xs[0], VIEW_Y + y - text_height(FONT_BODY) / 2, C_WHITE, 0x1a1a1aul);
  draw_gone(0, 0, x + 40 + text_width(FONT_BODY, n), y + 12);   /* (drawn over the view: both buffers draw it again) */
}

/* ---------------------------------------------------------------- the frame */
static bool ok_pressed(void) { return btn_pressed(&g_in.ok) || btn_pressed(&g_in.jump); }
static void go(int sc) {
  if (sc != SC_PLAY) palette_tint(false), palette_apply(256);   /* (the menus untinted) */
  screen = sc, sel = 0, screen_t = 0, full_redraw = 2;
}
void game_frame(void) {
  input_update(plat_keys());
  if (g_in.keys & K_QUIT) {
    save_write();
    g_running = false;
    return;
  }
  screen_t++;
  switch (screen) {
    case SC_KEYS:
      if (screen_t > 20 && ok_pressed()) save.keys_seen = 1, save_write(), go(SC_CHAPTERS);
      break;
    case SC_TITLE:
      if (screen_t > 20 && ok_pressed()) go(save.keys_seen ? SC_CHAPTERS : SC_KEYS);
      break;
    case SC_CHAPTERS:
      if (btn_pressed(&g_in.back)) go(SC_TITLE);
      else if (screen_t > 10 && ok_pressed()) play_chapter(1);
      break;
    case SC_PLAY:
      if (g_level.completed) {   /* the end: the wipe closes, then what was done */
        memset(&g_in, 0, sizeof g_in);
        if (!g_level.wipe) g_level.wipe = 1;
        level_update();
        if (g_level.wipe >= 24) go(SC_COMPLETE);
        break;
      }
      if (btn_pressed(&g_in.pause) && !g_player.dead && !g_level.transitioning) {
        btn_consume(&g_in.pause);
        screen = SC_PAUSE, sel = 0, screen_t = 0;
        break;
      }
      level_update();
      hud_update();
      break;
    case SC_PAUSE:
      if (btn_pressed(&g_in.back) || btn_pressed(&g_in.pause)) {
        btn_consume(&g_in.back), btn_consume(&g_in.pause);
        screen = SC_PLAY;
      } else if (g_in.move_y && !(g_in.prev & (K_UP | K_DOWN))) sel = (sel + g_in.move_y + 3) % 3;
      else if (ok_pressed()) {
        btn_consume(&g_in.ok), btn_consume(&g_in.jump);
        if (sel == 0) screen = SC_PLAY;
        else if (sel == 1) {
          screen = SC_PLAY;
          if (!g_player.dead) player_die(0, 0);
        } else {
          save_write();
          go(SC_CHAPTERS);
        }
      }
      break;
    case SC_COMPLETE:
      if (screen_t > 60 && ok_pressed()) go(SC_CHAPTERS);
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

/* the pause menu over the level, under HudRenderer's black (70 %, drawn into each buffer once), its words each frame */
static void pause_draw(void) {
  static const char *const items[3] = {"Resume", "Retry", "Save & Quit"};
  int y = 96;
  text_center(FONT_TITLE, "PAUSED", 160, y - 14, C_GRAY, 0);
  for (int i = 0; i < 3; i++) text_center(FONT_BODY, items[i], 160, y + 18 + i * 14, i == sel ? highlight() : C_WHITE, 0);
}
static void fade_view(void) {
  uint8_t *p = g_fb + VIEW_Y * 320;
  for (unsigned n = 320 * VIEW_H; n; n--, p++) *p = g_remap_fade[*p];
}
void game_draw(void) {
  static int paused;   /* (frames drawn paused: the black goes into both buffers) */
  static int shown[2] = {-1, -1}, buf;   /* the menu each buffer has (what it shows, the button's blink), drawn again
                                          * only when that changes */
  buf ^= 1;
  if (full_redraw) draw_clear_screen(0), full_redraw--, level_render_forget(), shown[0] = shown[1] = -1;
  if (screen != SC_PAUSE) paused = 0;
  if (screen != SC_PLAY && screen != SC_PAUSE) {
    int now = screen * 4 + (screen_t % 60 < 45) * 2 + (screen_t > 60);
    if (shown[buf] == now) return;
    shown[buf] = now;
  } else
    shown[0] = shown[1] = -1;
  switch (screen) {
    case SC_KEYS: {   /* NumPlay's keys on the CE (platform/ti84ce/README.md), as the NumWorks version has them */
      static const char *const keys[][2] = {{"arrows", "Move"}, {"2nd", "Jump"}, {"clear", "Dash"}, {"math", "Grab"},
                                            {"del", "Pause"}, {"mode", "Save and quit"}};
      draw_clear_screen(0);
      text_center(FONT_TITLE, "Controls", 160, 26, C_WHITE, 0);
      for (int i = 0; i < 6; i++) {
        text_right(FONT_BODY, keys[i][0], 150, 66 + i * 20, 0xFCFF59ul, 0);
        text_draw(FONT_BODY, keys[i][1], 170, 66 + i * 20, C_WHITE, 0);
      }
      prompt("2nd", "OK", 212);
      break;
    }
    case SC_TITLE: {
      draw_clear_screen(0);
      const uint8_t *l = sprite(S_gui_logo);
      pic(S_gui_logo, GUI_LOGO_X + l[0] / 2, VIEW_Y + GUI_LOGO_Y + l[1] / 2);
      prompt("2nd", "Start", 212);
      break;
    }
    case SC_CHAPTERS:
    case SC_COMPLETE: {
      bool done = screen == SC_COMPLETE;
      draw_clear_screen(0);
      if (done) text_center(FONT_TITLE, "Chapter Complete", 160, 6, C_WHITE, 0);
      banner();
      char b[24];
      int got = berries_of(1);
      if (done || got || (save.ch[1].flags & 1)) {
        num(b, (uint32_t)got);
        strcat(b, "/");
        num(b + strlen(b), (uint32_t)g_chapter_berries);
        stat_line(S_gui_strawberry_small, b, 170);
        num(b, save.ch[1].deaths);
        stat_line(S_gui_skull, b, 186);
      }
      if (done) {
        time_text(g_level.time, b);
        text_center(FONT_BODY, b, 160, 202, C_WHITE, 0);
        if (screen_t > 60) prompt("2nd", "Continue", 222);
      } else {
        prompt("2nd", "Climb", 212);
        text_center(FONT_BODY, "clear  Back", 160, 226, C_GRAY, 0);
      }
      break;
    }
    case SC_PLAY:
    case SC_PAUSE:
      if (screen == SC_PAUSE && paused >= 2) {
        pause_draw();
        break;
      }
      level_render(screen == SC_PAUSE || g_level.wipe || g_level.completed);
      if (screen == SC_PAUSE) {
        fade_view();
        paused++;
        level_render_forget();
        pause_draw();
        break;
      }
      hud_draw();
      wipe_draw();
      BENCH(7);
#if defined(CEL_BENCH) && !defined(CEL_BENCH_PLAIN)   /* (PLAIN: the frame rate alone) */
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
        {
          extern int g_sf_bad;
          draw_text(selftest || g_sf_bad ? "BAD" : "ok", 290, 14, WHITE);
          char sb[4] = {(char)('0' + g_sf_bad), 0};
          draw_text(sb, 280, 14, WHITE);
        }
        {
          extern unsigned long g_float_cost[4];
          char fb[40], *w = fb;
          for (int k = 0; k < 4; k++) num(w, (uint32_t)g_float_cost[k]), w += strlen(w), *w++ = ' ';
          *w = 0;
          draw_text(fb, 2, 22, WHITE);
        }
      }
#endif
      break;
  }
}

/* straight into a room, Madeline at (x, y) in it (tests) */
void game_start_room(int ch, const char *room, int x, int y) {
  if (!chapter_open(ch)) return;
  chapter = ch;
  palette_tint(true), palette_apply(256);
  level_load(ch, level_room_find(room), INTRO_NONE);
  g_level.wipe = 0;
  if (x || y) {
    g_player.e->x = g_level.room->x + x, g_player.e->y = g_level.room->y + y;
    g_level.camx = player_cam_targetx() * 256, g_level.camy = player_cam_targety() * 256;
  }
  screen = SC_PLAY;
  full_redraw = 2;
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
