/* NumPico: PICO-8 carts on the calculator.
 *
 * The carts come in a table in flash, np_carts (tools/p8cart.py, or NumPlay Studio), read in place.
 * One cart starts at once; with several, a list shows them with their labels. A cart plays with the
 * keys written at the left of its picture (pico.c runs it); its errors, and a cart too big for the
 * calculator's memory, say so in plain words. Home quits from anywhere.
 *
 * The screens are drawn in strips (the launcher's gfx.c), the strip's buffer at the start of the RAM
 * Lua gets while a cart plays. Everything is written with PICO-8's own font. */
#include <string.h>
#include "gfx.h"
#include "heap.h"
#include "pico.h"
#include "plat.h"

#if PLATFORM_DEVICE
/* the carts: np_carts (weak: a NumPlay with no carts has none), up to the end of NumPico's block of
   flash (np_numpico_end: NumPlay's linker script, or carts.ld on its own) */
extern const uint8_t np_carts[] __attribute__((weak));
extern const uint8_t np_numpico_end[];
#define TABLE_LIMIT ((uintptr_t)np_numpico_end > (uintptr_t)np_carts ? (uint32_t)((uintptr_t)np_numpico_end - (uintptr_t)np_carts) : 0)
#else
extern const uint8_t *np_carts;
extern uint32_t np_carts_size;
#define TABLE_LIMIT np_carts_size
#endif

#define WHITE pico_rgb565[7]
#define GREY pico_rgb565[6]
#define DARK pico_rgb565[5]
#define BLACK 0x0000

/* ------------------------------------------------------------ settings (numpico.cfg) */
#define CFG_NAME "numpico.cfg"
static struct {
  uint8_t magic, version, sel, pad;
} cfg;

static void cfg_load(void) {
  uint32_t len = 0;
  const uint8_t *d = plat_load(CFG_NAME, &len);
  memset(&cfg, 0, sizeof cfg);
  if (d && len == sizeof cfg) memcpy(&cfg, d, sizeof cfg);
  if (cfg.magic != 'P' || cfg.version != 1) memset(&cfg, 0, sizeof cfg);
  cfg.magic = 'P', cfg.version = 1;
}
static void cfg_save(void) { plat_save(CFG_NAME, &cfg, sizeof cfg); }

/* ------------------------------------------------------------ drawing */
static void strips_init(void) {
  uint8_t *a, *b;
  plat_heap(&a, &b);
  gfx_buf = (color_t *)(((uintptr_t)a + 3) & ~(uintptr_t)3);
}

/* PICO-8's letters (lowercase shows as its capitals), `s` pixels each */
static int ptext(int x, int y, const char *t, color_t c, int s) {
  for (; *t; t++) {
    uint8_t ch = (uint8_t)*t;
    if (ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch - 'A' + 'a');
    int w = ch >= 0x80 ? 8 : 4;
    if (y + 6 * s > gfx_y0 && y < gfx_y1)
      for (int gy = 0; gy < 6; gy++)
        for (int gx = 0; gx < w; gx++)
          if (pico_glyph_px(ch, gx, gy)) gfx_fill(x + gx * s, y + gy * s, s, s, c);
    x += w * s;
  }
  return x;
}

static int ptext_w(const char *t, int s) { return pico_text_w(t) * s; }

/* a cart's label, 128 x 128 at (x, y) */
static void label(const uint8_t *l, int x, int y) {
  uint16_t pal[16];
  for (int i = 0; i < 16; i++) pal[i] = pico_rgb565[(l[i] & 15) | (l[i] & 0x80 ? 16 : 0)];
  const uint8_t *px = l + 16;
  for (int row = NP_MAX(y, gfx_y0); row < NP_MIN(y + 128, gfx_y1); row++) {
    color_t *d = gfx_buf + (row - gfx_y0) * SCREEN_W + x;
    const uint8_t *s = px + (row - y) * 64;
    for (int i = 0; i < 64; i++) d[2 * i] = pal[s[i] & 15], d[2 * i + 1] = pal[s[i] >> 4];
  }
}

/* the words of `t` on lines of at most `w` pixels, from y down; the next line's y */
static int paragraph(int x, int y, int w, const char *t, color_t c, int s) {
  char line[80];
  int max = NP_MIN(w / (4 * s), (int)sizeof line - 1);
  while (*t) {
    int n = 0, cut = 0;
    while (t[n] && t[n] != '\n' && n < max) {
      if (t[n] == ' ') cut = n;
      n++;
    }
    if (t[n] && t[n] != '\n' && t[n] != ' ' && cut) n = cut;
    memcpy(line, t, n);
    line[n] = 0;
    ptext(x, y, line, c, s);
    y += 8 * s;
    t += n;
    while (*t == ' ' || *t == '\n') t++;
  }
  return y;
}

/* a cart's name to show: its title, else its file's; and its author */
static void cart_name(int i, char *name, int n, char *author, int an) {
  carts_title(&pico_carts[i], name, n, author, an);
  if (!name[0]) carts_name(&pico_carts[i], name, n);
}

/* the key line at the bottom */
static void keys_line(const char *a, const char *b) {
  int x = ptext(14, 222, a, GREY, 2);
  if (b) ptext(x + 20, 222, b, GREY, 2);
}

/* ------------------------------------------------------------ the list of carts */
static int sel, top;
#define ROWS 8
#define ROW_H 20

static void list_scene(void *ctx) {
  (void)ctx;
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, BLACK);
  ptext(14, 12, "numpico", WHITE, 2);
  char n[16];
  int k = pico_ncarts, d = 0;
  char tmp[8];
  do tmp[d++] = (char)('0' + k % 10), k /= 10; while (k);
  for (int i = 0; i < d; i++) n[i] = tmp[d - 1 - i];
  strcpy(n + d, " carts");
  ptext(SCREEN_W - 14 - ptext_w(n, 2), 12, n, DARK, 2);
  for (int r = 0; r < ROWS && top + r < pico_ncarts; r++) {
    int i = top + r, y = 40 + r * ROW_H;
    if (i == sel) {
      gfx_rrect(8, y, 160, ROW_H - 2, 3, pico_rgb565[1], 256);
      gfx_rrect_outline(8, y, 160, ROW_H - 2, 3, 2, pico_rgb565[12], 256);
    }
    char name[19], author[2];
    cart_name(i, name, sizeof name, author, sizeof author);
    ptext(16, y + 4, name, i == sel ? WHITE : GREY, 2);
  }
  /* (more above or below) */
  if (top > 0) ptext(84, 30, "\x94", DARK, 1);
  if (top + ROWS < pico_ncarts) ptext(84, 40 + ROWS * ROW_H - 2, "\x83", DARK, 1);
  /* the cart: its label, title and author */
  const p8_cart_t *c = &pico_carts[sel];
  gfx_fill(178, 36, 136, 136, WHITE);
  if (c->label) label(c->label, 182, 40);
  else gfx_fill(182, 40, 128, 128, pico_rgb565[1]);
  char t[33], a[33];
  cart_name(sel, t, sizeof t, a, sizeof a);
  ptext(178, 180, t, WHITE, 1);
  ptext(178, 190, a, GREY, 1);
  keys_line("ok play", "home quit");
}

static void list_draw(void) {
  if (sel < top) top = sel;
  if (sel >= top + ROWS) top = sel - ROWS + 1;
  gfx_render(list_scene, NULL, 0, 240);
}

/* ------------------------------------------------------------ other screens */
static void blank_scene(void *ctx) {
  (void)ctx;
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, BLACK);
}

static void none_scene(void *ctx) {
  blank_scene(ctx);
  ptext(14, 12, "numpico", WHITE, 2);
  ptext(14, 70, "no carts yet", WHITE, 2);
  paragraph(14, 100, 292, "add your .p8 and .p8.png carts in numplay studio.", GREY, 2);
  keys_line("home quit", NULL);
}

/* the keys, at the left of the picture */
static void panel_scene(void *ctx) {
  (void)ctx;
  gfx_fill(0, gfx_y0, PIC_X, gfx_y1 - gfx_y0, BLACK);
  static const char *const hints[][2] = {
    {"arrows", "\x8b\x91\x94\x83"}, {"ok, exe", "\x8e"}, {"back, ans", "\x97"}, {"backspace", "menu"}, {"home", "quit"}};
  int y = 40;
  for (int i = 0; i < 5; i++) {
    ptext(4, y, hints[i][0], DARK, 2);
    ptext(4, y + 14, hints[i][1], WHITE, 2);
    y += 34;
  }
}

static int ended; /* how the cart ended, for end_scene */
static bool alone;

static void end_scene(void *ctx) {
  blank_scene(ctx);
  int y = 30;
  if (ended == PICO_MEMORY) {
    ptext(14, y, "out of memory", pico_rgb565[8], 2);
    char t[96] = "this cart needs more memory than the calculator has (";
    int kb = (int)(heap_size() / 1024), n = (int)strlen(t);
    char d[8];
    int k = 0;
    do d[k++] = (char)('0' + kb % 10), kb /= 10; while (kb);
    while (k) t[n++] = d[--k];
    strcpy(t + n, " kb).");
    paragraph(14, y + 34, 292, t, GREY, 2);
  } else {
    ptext(14, y, pico_error.what, pico_rgb565[8], 2);
    if (pico_error.line > 0) {
      char t[16] = "line ";
      int n = 5, v = pico_error.line;
      char d[8];
      int k = 0;
      do d[k++] = (char)('0' + v % 10), v /= 10; while (v);
      while (k) t[n++] = d[--k];
      t[n] = 0;
      ptext(14, y + 20, t, GREY, 2);
    }
    paragraph(14, y + 44, 292, pico_error.msg, WHITE, 2);
  }
  keys_line("ok run again", alone ? "home quit" : "back carts");
}

/* ------------------------------------------------------------ keys */
static uint32_t held;
static uint32_t hits(void) {
  uint32_t k = plat_keys(), hit = k & ~held;
  held = k;
  return hit;
}

/* ------------------------------------------------------------ playing */
/* a cart until it's left: 0 back to the list, 1 Home */
static int play(int i) {
  for (;;) {
    strips_init();
    gfx_render(panel_scene, NULL, 0, 240);
    int how = pico_run(i);
    while (how == PICO_LOAD) {
      i = pico_next;
      strips_init();
      gfx_render(panel_scene, NULL, 0, 240);
      how = pico_run(i);
    }
#ifdef HOST
    void host_ended(int how);
    host_ended(how);
#endif
    if (how == PICO_HOME) return 1;
    if (how == PICO_QUIT) return 0;
    /* an error, or too little memory: say so */
    strips_init();
    ended = how;
    gfx_render(end_scene, NULL, 0, 240);
    held = plat_keys();
    for (;;) {
      uint32_t k = hits();
      if (k & PK_HOME) return 1;
      if (k & PK_OK) break;
      if ((k & PK_BACK) && !alone) return 0;
      plat_sleep(20);
    }
  }
}

int numpico_main(void) {
  plat_begin();
  strips_init();
  cfg_load();
  int n = carts_read(np_carts, np_carts ? TABLE_LIMIT : 0, pico_carts, P8_MAX_CARTS);
  pico_ncarts = n > 0 ? n : 0;
  held = plat_keys();
  if (!pico_ncarts) {
    gfx_render(none_scene, NULL, 0, 240);
    while (!(hits() & PK_HOME)) plat_sleep(20);
    return plat_end();
  }
  alone = pico_ncarts == 1;
  if (alone) {
    play(0);
    return plat_end();
  }
  sel = cfg.sel < pico_ncarts ? cfg.sel : 0;
  list_draw();
  for (;;) {
    uint32_t k = hits();
    if (k & PK_HOME) break;
    int was = sel;
    if (k & PK_UP) sel = (sel + pico_ncarts - 1) % pico_ncarts;
    if (k & PK_DOWN) sel = (sel + 1) % pico_ncarts;
    if (k & PK_LEFT) sel = NP_MAX(sel - ROWS, 0);
    if (k & PK_RIGHT) sel = NP_MIN(sel + ROWS, pico_ncarts - 1);
    if (sel != was) list_draw();
    if (k & PK_OK) {
      cfg.sel = (uint8_t)sel;
      cfg_save();
      if (play(sel)) break;
      strips_init();
      list_draw();
      held = plat_keys();
    }
    plat_sleep(16);
  }
  cfg.sel = (uint8_t)sel;
  cfg_save();
  return plat_end();
}

#if PLATFORM_DEVICE
int main(void) { return numpico_main(); }
#endif
