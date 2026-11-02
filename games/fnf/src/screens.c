/* The screens (app.h): the game's title, main menu, story menu and freeplay, with its own pictures and letters, the
 * pause menu over the song, and NumPlay's key line along the bottom of the menus. */
#include <string.h>
#include "app.h"
#include "draw.h"

#define WHITE 0xFFFF
#define FOCUS HEX(0xFFD43B)
#define INK HEX(0x0A0F1A)
#define MUTED HEX(0xA8B3C7)
#define PINK HEX(0xE55777)
#define YELLOW HEX(0xF9CF51)

/* ------------------------------------------------------------------------------------------------ text */
static char *cat(char *p, const char *s) {
  while (*s) *p++ = *s++;
  *p = 0;
  return p;
}
static char *num(char *p, int32_t v, bool commas) {
  char t[16];
  int n = 0;
  bool neg = v < 0;
  uint32_t u = (uint32_t)(neg ? -v : v);
  do {
    if (commas && n && (n + 1) % 4 == 0) t[n++] = ',';
    t[n++] = (char)('0' + u % 10), u /= 10;
  } while (u && n < 15);
  if (neg) *p++ = '-';
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}
static void upper(char *d, const char *s, int n) {
  int i = 0;
  for (; s[i] && i < n - 1; i++) d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
  d[i] = 0;
}

static void text(int x, int base, const char *s, color_t c) { gfx_text(&np_font_body, x, base, s, c, 256); }
static void text_c(int cx, int base, const char *s, color_t c) { gfx_text_center(&np_font_body, cx, base, s, c, 256); }
static void small_c(int cx, int base, const char *s, color_t c) { gfx_text_center(&np_font_small, cx, base, s, c, 256); }

/* the key line: "key|what|key|what...", keys in yellow */
static void keyline(const char *s) {
  gfx_fill(0, 226, SCREEN_W, 14, INK);
  char b[24];
  int x = 6, i = 0;
  while (*s) {
    int n = 0;
    while (*s && *s != '|' && n < 23) b[n++] = *s++;
    b[n] = 0;
    if (*s == '|') s++;
    gfx_text(&np_font_small, x, 236, b, i % 2 ? WHITE : FOCUS, 256);
    x += gfx_text_width(&np_font_small, b) + (i % 2 ? 9 : 4);
    i++;
  }
}

static int bold_c(int cx, int y, const char *s, int alpha, int scale) {
  int w = bold_width(s, scale);
  bold_text(cx - w / 2, y, s, alpha, scale);
  return w;
}

/* a difficulty, between arrows */
static void diff_line(int cx, int y, const char *name, int scale) {
  int w = bold_c(cx, y, name, 32, scale);
  int mid = y + FNF_BOLD_HEIGHT * scale / 512 + 7;
  gfx_text_center(&np_font_title, cx - w / 2 - 12, mid, "<", FOCUS, 256);
  gfx_text_center(&np_font_title, cx + w / 2 + 12, mid, ">", FOCUS, 256);
}

/* a choice of a list: bright, on a dark band with arrows, or dimmed */
static void item(int cx, int y, const char *s, bool focus) {
  int scale = focus ? 256 : 220;
  int w = bold_width(s, scale);
  if (focus) {
    gfx_rrect(cx - w / 2 - 14, y - 4, w + 28, FNF_BOLD_HEIGHT + 8, 8, INK, 200);
    gfx_rrect_outline(cx - w / 2 - 14, y - 4, w + 28, FNF_BOLD_HEIGHT + 8, 8, 2, FOCUS, 256);
  }
  bold_text(cx - w / 2, y + (focus ? 0 : 2), s, focus ? 32 : 18, scale);
}

/* ------------------------------------------------------------------------------------------------ pictures */
static struct {
  int view;                 /* the screen they were unpacked for (-1: none) */
  int bg;                   /* the background unpacked */
  pic_t logo[2], gf;
  uint8_t *gf_px;
  int gf_frame;
  pic_t icon[5];
  int icon_id[5];
  uint8_t *icon_px[5];
} M = {.view = -1, .bg = -1};

static void bg_use(int id) {
  if (M.bg == id) return;
  M.bg = bg_load((unsigned)id) ? id : -1;
}

static const pic_t *icon_slot(int slot, unsigned icon) {
  if (slot < 0 || slot >= 5 || !M.icon_px[slot]) return 0;
  if (M.icon_id[slot] != (int)icon) {
    M.icon_id[slot] = -1;
    if (!pic_load(&M.icon[slot], fnf_icon[icon][0], M.icon_px[slot], FNF_ICON_MAX / 3)) return 0;
    M.icon_id[slot] = (int)icon;
  }
  return &M.icon[slot];
}

static void menus_load(int view) {
  ram_reset();
  memset(&M, 0, sizeof M);
  M.view = view, M.bg = -1, M.gf_frame = -1;
  for (int i = 0; i < 5; i++) M.icon_id[i] = -1;
  if (view == V_TITLE) {
    pic_new(&M.logo[0], FNF_SPR_LOGO0);
    pic_new(&M.logo[1], FNF_SPR_LOGO1);
    uint32_t big = 0;
    for (int f = 0; f < 4; f++) {
      const fnf_sprite_t *s = &fnf_sprite[FNF_SPR_GFTITLE0 + f];
      uint32_t n = (s->w + 1u) / 2 * s->h;
      if (n > big) big = n;
    }
    M.gf_px = ram_take(big);
  }
  for (int i = 0; i < 5; i++) M.icon_px[i] = ram_take(FNF_ICON_MAX / 3);
}

/* ------------------------------------------------------------------------------------------------ each screen */
typedef struct {
  app_t *a;
} ctx_t;

static void dim(int a256) { gfx_fill_alpha(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0, a256); }

/* the menus' beat: freakyMenu, 102 a minute */
static uint32_t menu_beat(const app_t *a, uint32_t *into) {
  uint32_t ms = a->clock;
  uint32_t b = ms * 102 / 60000;
  if (into) *into = ms - b * 60000 / 102;
  return b;
}

static void title_strip(app_t *a) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0);
  uint32_t into;
  uint32_t beat = menu_beat(a, &into);
  /* Girlfriend dances left, then right, on each beat; the logo bumps */
  int f = (beat & 1 ? 2 : 0) + (into > 294 ? 1 : 0);
  if (M.gf_px && f != M.gf_frame) {
    M.gf_frame = pic_load(&M.gf, FNF_SPR_GFTITLE0 + (unsigned)f, M.gf_px, 1u << 20) ? f : -1;
  }
  if (M.gf_frame >= 0) blit_at(&M.gf, 218, 104, 32);
  blit_at(&M.logo[into < 160 ? 0 : 1], 100, 66, 32);
  int alpha = 20 + (int)(12 * (into < 294 ? 294 - into : into - 294) / 294);
  bold_c(160, 194, "PRESS OK TO BEGIN", alpha, 230);
  keyline("OK|start|Back|quit");
}

static const char *const MAIN_ITEMS[MAIN_COUNT] = {"STORY MODE", "FREEPLAY", "OPTIONS", "HOW TO PLAY", "CREDITS"};

static void main_strip(app_t *a) {
  bg_use(FNF_SPR_MENUBG);
  draw_bg(fnf_palette[fnf_sprite[FNF_SPR_MENUBG].pal], 0, 1);
  for (int i = 0; i < MAIN_COUNT; i++) item(160, 30 + i * 38, MAIN_ITEMS[i], a->sel == i);
  keyline("Up Down|choose|OK|select|Back|title");
}

static const char *const WEEK_TITLE[FNF_LEVELS] = {"TUTORIAL", "WEEK 1", "WEEK 2", "WEEK 3", "WEEK 4",
                                                   "WEEK 5", "WEEK 6", "WEEK 7", "WEEKEND 1", "LE SSERAFIM"};

static void story_strip(app_t *a) {
  const fnf_progress_t *p = a->p;
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0);
  char b[64];
  char *q = cat(b, "WEEK SCORE: ");
  num(q, p->week_score[p->week][p->week_diff], false);
  gfx_text(&np_font_small, 6, 13, b, WHITE, 256);
  upper(b, fnf_level[p->week].name, sizeof b);
  gfx_text_right(&np_font_small, 314, 13, b, MUTED, 256);
  /* the yellow band, with the week's singers: the opponent and you */
  gfx_fill(0, 18, SCREEN_W, 84, YELLOW);
  const fnf_level_t *lv = &fnf_level[p->week];
  int c = app_story_chart(lv->song[0], p->week_diff);
  if (c >= 0) {
    const fnf_var_t *v = &fnf_var[fnf_chart[c].var];
    for (int r = 0; r < 2; r++) {
      if (v->chr[r] == 255) continue;
      const pic_t *ic = icon_slot(r, fnf_char[v->chr[r]].icon);
      if (ic) blit_scaled(ic, (r ? 84 : 236) - 38 + (ic->x + 19) * 2, 60 - 38 + (ic->y + 19) * 2, ic->w * 2, ic->h * 2, 32);
    }
  }
  /* the weeks */
  for (int k = -1; k <= 1; k++) {
    int w = p->week + k;
    if (w < 0 || w >= FNF_LEVELS) continue;
    int y = 128 + k * 30;
    if (!k) item(160, y, WEEK_TITLE[w], true);
    else bold_c(160, y + 2, WEEK_TITLE[w], 12, 200);
  }
  /* the tracks, and the difficulty */
  gfx_text(&np_font_small, 14, 184, "TRACKS", PINK, 256);
  for (int i = 0; i < lv->n; i++) {
    upper(b, fnf_song[lv->song[i]].name, sizeof b);
    gfx_text(&np_font_small, 14, 197 + i * 9, b, PINK, 256);
  }
  diff_line(236, 186, DIFF_NAME[p->week_diff], 200);
  keyline("Up Down|week|Left Right|difficulty|OK|play|Back|menu");
}

/* freeplay's colours, a week each (the game's FreeplayState) */
static const uint32_t WEEK_COLOR[FNF_LEVELS] = {0x9271FD, 0x9271FD, 0x223344, 0x941653, 0xFC96D7, 0xA0D1FF, 0xFF78BF,
                                                0xF6B604, 0x413CAE, 0xF9CF51};

static void freeplay_strip(app_t *a) {
  const fnf_progress_t *p = a->p;
  bg_use(FNF_SPR_MENUDESAT);
  unsigned song = a->list_n ? a->list[p->song] : 0;
  color_t tint = gfx_rgb(WEEK_COLOR[fnf_song[song].level]);
  uint16_t pal[16];
  const uint16_t *src = fnf_palette[fnf_sprite[FNF_SPR_MENUDESAT].pal];
  for (int i = 0; i < 16; i++) pal[i] = gfx_mix(src[i], tint, 20);
  draw_bg(pal, 0, 1);
  char b[64];
  /* the songs, the chosen one in the middle, with their opponents' icons */
  for (int k = -2; k <= 2; k++) {
    int i = p->song + k;
    if (i < 0 || i >= a->list_n) continue;
    int y = 104 + k * 34;
    upper(b, fnf_song[a->list[i]].name, sizeof b);
    int scale = k ? 200 : 240;
    int w = bold_width(b, scale);
    int x = 22 + (k < 0 ? -k : k) * 6;
    if (!k) {
      gfx_rrect(x - 8, y - 5, w + 58, FNF_BOLD_HEIGHT + 10, 8, INK, 190);
      gfx_rrect_outline(x - 8, y - 5, w + 58, FNF_BOLD_HEIGHT + 10, 8, 2, FOCUS, 256);
    }
    bold_text(x, y, b, k ? 14 : 32, scale);
    const pic_t *ic = icon_slot(k + 2, fnf_song[a->list[i]].icon);
    if (ic) blit_scaled(ic, x + w + 6 + (ic->x + 19) * 26 / 38, y - 4 + (ic->y + 19) * 26 / 38, ic->w * 26 / 38,
                        ic->h * 26 / 38, k ? 18 : 32);
  }
  /* the best score and the difficulty, top right */
  gfx_rrect(196, 4, 120, 46, 6, INK, 200);
  int chart = a->list_n ? app_chart(song, p->list, p->song_diff) : -1;
  char *q = cat(b, "BEST ");
  num(q, chart >= 0 ? p->chart[chart].score : 0, true);
  gfx_text_center(&np_font_small, 256, 16, b, WHITE, 256);
  q = cat(b, "< ");
  q = cat(q, DIFF_NAME[p->song_diff]);
  cat(q, " >");
  gfx_text_center(&np_font_body, 256, 31, b, FOCUS, 256);
  if (chart >= 0 && p->chart[chart].rank) {
    static const char *const RANK[6] = {"LOSS", "GOOD", "GREAT", "EXCELLENT", "PERFECT", "PERFECT"};
    q = cat(b, RANK[p->chart[chart].rank - 1]);
    q = cat(q, "  ");
    q = num(q, p->chart[chart].completion / 100, false);
    cat(q, "%");
    gfx_text_center(&np_font_small, 256, 45, b, p->chart[chart].rank >= 5 ? FOCUS : MUTED, 256);
  }
  gfx_text(&np_font_small, 8, 14, p->list == LIST_PICO ? "PICO" : "BOYFRIEND", WHITE, 256);
  keyline(p->list == LIST_PICO ? "Up Down|song|Left Right|difficulty|shift|Boyfriend|OK|play"
                               : "Up Down|song|Left Right|difficulty|shift|Pico|OK|play");
}

static void options_body(app_t *a) {
  const fnf_progress_t *p = a->p;
  char b[48];
  bold_c(160, 30, "OPTIONS", 32, 256);
  char *q = cat(b, "DOWNSCROLL ");
  cat(q, p->downscroll ? "ON" : "OFF");
  item(160, 82, b, a->sel == OPT_DOWNSCROLL);
  q = cat(b, "NOTE OFFSET ");
  if (p->offset > 0) *q++ = '+';
  q = num(q, p->offset, false);
  cat(q, " MS");
  item(160, 126, b, a->sel == OPT_OFFSET);
  small_c(160, 178, a->sel == OPT_DOWNSCROLL ? "The notes come down to the arrows at the bottom."
                                              : "Raise it if your hits come out late.", WHITE);
  keyline(a->sel == OPT_DOWNSCROLL ? "Up Down|choose|OK|on or off|Back|done" : "Up Down|choose|Left Right|change|Back|done");
}

static const char *const HELP[HELP_PAGES][4] = {
    {"HOW TO PLAY", "Press each note's key as it reaches", "the arrows at the top of the screen:",
     "Left Down Up Right, or 7 8 ( )."},
    {"HOLDS AND MISSES", "Long notes: keep the key down to the end.", "A key with no note there costs health,",
     "and so does a missed note."},
    {"THE HEALTH BAR", "Yours is the green side. Empty it and", "it's game over. Sick, Good, Bad, Shit:",
     "how close to the beat you hit."},
};

static void help_body(app_t *a) {
  bold_c(160, 46, HELP[a->page][0], 32, 240);
  for (int i = 1; i < 4; i++) text_c(160, 96 + i * 20, HELP[a->page][i], WHITE);
  char b[16];
  char *q = num(b, a->page + 1, false);
  q = cat(q, " / ");
  num(q, HELP_PAGES, false);
  small_c(160, 210, b, MUTED);
  keyline(a->first_run ? "OK|next|Back|skip" : "Left Right|pages|Back|close");
}

static void credits_strip(app_t *a) {
  (void)a;
  bg_use(FNF_SPR_MENUDESAT);
  draw_bg(fnf_palette[fnf_sprite[FNF_SPR_MENUDESAT].pal], 600, 1);
  bold_c(160, 16, "CREDITS", 32, 240);
  static const char *const L[] = {"Friday Night Funkin' by the Funkin' Crew:", "ninjamuffin99, PhantomArcade,",
                                  "evilsk8r and Kawai Sprite.", "Its charts and pictures, not affiliated with them.",
                                  "Calculator adaptation by Bobydeluxe.", "Made by Mason Chen as part of NumPlay."};
  for (int i = 0; i < 6; i++) text_c(160, 66 + i * 24 + (i >= 4 ? 10 : 0), L[i], i == 0 || i == 4 ? FOCUS : WHITE);
  keyline("Back|menu");
}

static void pause_body(app_t *a) {
  static const char *const PAUSE_ITEMS[PAUSE_COUNT] = {"RESUME", "RESTART SONG", "EXIT TO MENU", "OPTIONS",
                                                       "HOW TO PLAY", "QUIT GAME"};
  const fnf_game_t *g = a->g;
  char b[64];
  upper(b, fnf_song[fnf_var[g->var].song].name, sizeof b);
  gfx_text_right(&np_font_body, 314, 16, b, WHITE, 256);
  gfx_text_right(&np_font_small, 314, 30, DIFF_NAME[fnf_chart[g->chart].diff], FOCUS, 256);
  for (int i = 0; i < PAUSE_COUNT; i++) item(160, 30 + i * 32, PAUSE_ITEMS[i], a->sel == i);
  keyline("Up Down|choose|OK|select|Back|resume");
}

static void quit_body(app_t *a) {
  bold_c(160, 55, "QUIT THE GAME?", 32, 256);
  item(160, 107, "KEEP PLAYING", a->sel == 0);
  item(160, 145, "QUIT", a->sel == 1);
  keyline("Up Down|choose|OK|select|Back|pause");
}

static void results_strip(app_t *a) {
  static const char *const RANK[6] = {"LOSS", "GOOD", "GREAT", "EXCELLENT", "PERFECT", "PERFECT!"};
  bg_use(FNF_SPR_MENUBG);
  draw_bg(fnf_palette[fnf_sprite[FNF_SPR_MENUBG].pal], 450, 1);
  bold_c(160, 10, RANK[a->result_rank], 32, 280);
  char b[48];
  char *q = cat(b, "SCORE ");
  num(q, a->result_score, true);
  bold_c(160, 44, b, 32, 220);
  if (a->result_new) small_c(160, 82, "NEW BEST!", FOCUS);
  static const char *const LABEL[7] = {"TOTAL NOTES", "MAX COMBO", "SICK", "GOOD", "BAD", "SHIT", "MISSED"};
  int32_t v[7] = {a->result_total, a->result_max_combo, a->result_tally[0], a->result_tally[1], a->result_tally[2],
                  a->result_tally[3], a->result_tally[4]};
  static const uint32_t COLOR[7] = {0xFFFFFF, 0xFFFFFF, 0x89E59E, 0x89C9E5, 0xE6CF8A, 0xE68C8A, 0xC68AE6};
  gfx_rrect(56, 90, 208, 112, 8, INK, 210);
  for (int i = 0; i < 7; i++) {
    text(70, 107 + i * 14, LABEL[i], gfx_rgb(COLOR[i]));
    num(b, v[i], false);
    gfx_text_right(&np_font_body, 250, 107 + i * 14, b, gfx_rgb(COLOR[i]), 256);
  }
  q = cat(b, "CLEAR ");
  q = num(q, a->result_completion / 100, false);
  cat(q, "%");
  text_c(160, 218, b, WHITE);
  keyline("OK|continue");
}

static void error_strip(app_t *a) {
  (void)a;
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, INK);
  text_c(160, 110, "This song's data is damaged.", WHITE);
  small_c(160, 130, "Install the game again from NumPlay's page.", MUTED);
  keyline("OK|menu");
}

/* ------------------------------------------------------------------------------------------------ in a song */
static bool in_song(const app_t *a) {
  int v = a->view;
  return v == V_PLAY || v == V_PAUSE || v == V_QUIT || v == V_GAMEOVER ||
         ((v == V_HELP || v == V_OPTIONS) && a->under == V_PAUSE);
}

static void song_strip(void *ctx) {
  app_t *a = ((ctx_t *)ctx)->a;
  switch (a->view) {
    case V_PLAY: play_strip(); break;
    case V_GAMEOVER:
      play_actor_strip(a->g, ROLE_PLAYER);
      bold_c(160, 30, "GAME OVER", 32, 256);
      keyline("OK|retry|Back|menu");
      break;
    default:
      play_strip();
      dim(150);
      if (a->view == V_PAUSE) pause_body(a);
      else if (a->view == V_QUIT) quit_body(a);
      else if (a->view == V_HELP) help_body(a);
      else options_body(a);
      break;
  }
}

static void menu_strip(void *ctx) {
  app_t *a = ((ctx_t *)ctx)->a;
  switch (a->view) {
    case V_TITLE: title_strip(a); break;
    case V_MAIN: main_strip(a); break;
    case V_STORY: story_strip(a); break;
    case V_FREEPLAY: freeplay_strip(a); break;
    case V_OPTIONS:
      bg_use(FNF_SPR_MENUDESAT);
      draw_bg(fnf_palette[fnf_sprite[FNF_SPR_MENUDESAT].pal], 500, 1);
      options_body(a);
      break;
    case V_HELP:
      gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, INK);
      help_body(a);
      break;
    case V_CREDITS: credits_strip(a); break;
    case V_RESULTS: results_strip(a); break;
    default: error_strip(a); break;
  }
}

static bool was_in_song;

void screens_draw(app_t *a) {
  ctx_t c = {a};
  bool song = in_song(a);
  if (song) {
    if (!was_in_song) M.view = -1;
    was_in_song = true;
    const fnf_game_t *g = a->g;
    play_prepare(g, a->p->downscroll, (int32_t)((a->clock - a->play_t0) * 1000));
    draw_frame(song_strip, &c);
    return;
  }
  if (was_in_song || M.view != a->view) menus_load(a->view);
  was_in_song = false;
  draw_frame(menu_strip, &c);
}
