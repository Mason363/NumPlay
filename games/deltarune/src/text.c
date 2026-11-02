/* Text typed a letter at a time, and the dialogue box (see text.h): the original's obj_writer,
 * obj_dialoguer, obj_face and obj_choicer_neo. */
#include <stdarg.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "res.h"
#include "text.h"
#include "world.h"

/* ---------------------------------------------------------------- faces (obj_face's draw) */
/* each face's expressions (global.fe): a sprite each, or one sprite and fe its frame (n 0) */
typedef struct {
  const uint16_t *sprs;
  uint8_t n;
  int8_t dx, dy;      /* (the original's offsets, in its units: halved in the Dark World) */
} FaceDef;

static const uint16_t f_susie[] = {spr_face_s0, spr_face_s1, spr_face_s2, spr_face_s3, spr_face_s4, spr_face_s5,
                                   spr_face_s6, spr_face_s7, spr_face_s8, spr_face_s9, spr_face_sA, spr_face_sB,
                                   spr_face_sC, spr_face_sD};
static const uint16_t f_noelle[] = {spr_face_n0, spr_face_n1, spr_face_n2, spr_face_n3, spr_face_n4, spr_face_n5,
                                    spr_face_n6, spr_face_n7, spr_face_n8, spr_face_n9};
static const uint16_t f_toriel[] = {spr_face_t0, spr_face_t1, spr_face_t2, spr_face_t3, spr_face_t4, spr_face_t5,
                                    spr_face_t6, spr_face_t7, spr_face_t8, spr_face_t9};
static const uint16_t f_sans[] = {spr_face_sans0, spr_face_sans1, spr_face_sans2, spr_face_sans3, spr_face_sans4,
                                  spr_face_sans5};
static const uint16_t f_asgore[] = {spr_face_asgore0, spr_face_asgore1, spr_face_asgore2, spr_face_asgore3,
                                    spr_face_asgore4, spr_face_asgore5, spr_face_asgore6};
static const uint16_t f_berdly[] = {spr_face_b0, spr_face_b1, spr_face_b2, spr_face_b3, spr_face_b4, spr_face_b5};
static const uint16_t f_c[] = {spr_face_c0, spr_face_c1, spr_face_c2};
static const uint16_t f_jock[] = {spr_face_jock0, spr_face_jock1, spr_face_jock2, spr_face_jock3, spr_face_jock4,
                                  spr_face_jock5, spr_face_jock6, spr_face_jock7, spr_face_jock8, spr_face_jock9};
static const uint16_t f_one[] = {spr_face_susie_alt, spr_face_r_dark, spr_face_r_hood, spr_face_r_nohat, spr_face_l0,
                                 spr_face_undyne, spr_alphysface, spr_face_rudy, spr_face_catty, spr_face_bratty,
                                 spr_face_rurus, spr_face_burgerpants, spr_face_king};

static const FaceDef *face_def(int fc) {
  static const FaceDef susie = {f_susie, 14, -5, 0}, susie_alt = {f_one + 0, 0, -5, 0};
  static const FaceDef ralsei = {f_one + 1, 0, -15, -10}, ralsei_hood = {f_one + 2, 0, -15, -10};
  static const FaceDef ralsei_nohat = {f_one + 3, 0, -15, -10};
  static const FaceDef noelle = {f_noelle, 10, -12, -10}, toriel = {f_toriel, 10, 0, 0};
  static const FaceDef lancer = {f_one + 4, 0, -15, -10}, sans = {f_sans, 6, 0, 0};
  static const FaceDef undyne = {f_one + 5, 0, -10, 0}, asgore = {f_asgore, 7, -10, 0};
  static const FaceDef alphys = {f_one + 6, 0, -10, 0}, berdly = {f_berdly, 6, -10, 0};
  static const FaceDef cface = {f_c, 3, -10, 0}, jock = {f_jock, 10, -10, 0};
  static const FaceDef rudy = {f_one + 7, 0, -12, -10}, catty = {f_one + 8, 0, -10, 0};
  static const FaceDef bratty = {f_one + 9, 0, -5, 2}, rurus = {f_one + 10, 0, -10, 0};
  static const FaceDef burgerpants = {f_one + 11, 0, -5, -5}, king = {f_one + 12, 0, -5, -5};
  switch (fc) {
    case FC_SUSIE: return flag(29) ? &susie_alt : &susie;
    case FC_RALSEI: return flag(30) == 1 ? &ralsei_hood : flag(30) == 2 ? &ralsei_nohat : &ralsei;
    case FC_NOELLE: return &noelle;
    case FC_TORIEL: return &toriel;
    case FC_LANCER: return &lancer;
    case FC_SANS: return &sans;
    case FC_UNDYNE: return &undyne;
    case FC_ASGORE: return &asgore;
    case FC_ALPHYS: return &alphys;
    case FC_BERDLY: return &berdly;
    case FC_C: return &cface;
    case FC_JOCK: return &jock;
    case FC_RUDY: return &rudy;
    case FC_CATTY: return &catty;
    case FC_BRATTY: return &bratty;
    case FC_RURUS: return &rurus;
    case FC_BURGERPANTS: return &burgerpants;
    case FC_KING: return &king;
    default: return NULL;
  }
}

static int fc_of_letter(char c) {
  switch (c) {
    case 'S': return FC_SUSIE;
    case 'R': return FC_RALSEI;
    case 'N': return FC_NOELLE;
    case 'T': return FC_TORIEL;
    case 'L': return FC_LANCER;
    case 'A': return FC_ASGORE;
    case 'a': return FC_ALPHYS;
    case 'B': return FC_BERDLY;
    case 'r': return FC_RUDY;
    case 'u': return FC_RURUS;
    case 'K': return FC_KING;
    default: return FC_NONE;
  }
}

static int fe_of(char c) { return c >= 'A' && c <= 'Z' ? 10 + c - 'A' : c >= '0' && c <= '9' ? c - '0' : 0; }

/* draws the face fc, expression fe, its top-left near (x, y); the mouth moves while talking */
static void face_draw(int fc, int fe, int x, int y, bool talking, int t) {
  const FaceDef *d = face_def(fc);
  if (!d) return;
  int s = world_scale();
  if (fc == FC_TORIEL) gfx_sprite(spr_face_tbody, 0, x - 7 / s, y + 29 / s, 0, 255);
  if (d->n) {
    uint16_t spr = d->sprs[fe < d->n ? fe : 0];
    int fi = talking && spr_frames(spr) > 1 && (t / 3) % 2;
    gfx_sprite(spr, fi, x + d->dx / s, y + d->dy / s, 0, 255);
  } else {
    gfx_sprite(d->sprs[0], fe, x + d->dx / s, y + d->dy / s, 0, 255);
  }
}

/* ---------------------------------------------------------------- the writer */
static const int pauses[10] = {0, 5, 10, 15, 20, 30, 40, 60, 90, 150};

/* the message laid out: the original's wrapping (at the last space past charline letters; the line after
 * indented by two letters, "||", when the message has an asterisk), into w->line */
static void layout(Writer *w) {
  const char *s = w->msg[w->cur];
  char *o = w->line;
  int n = 0, cap = (int)sizeof w->line - 4;
  int charpos = 0, remspace = -1;
  bool aster = false;
  for (int i = 0; s[i] && n < cap; i++) {
    char c = s[i];
    if (c == '\\' && s[i + 1] && s[i + 2]) {
      o[n++] = s[i], o[n++] = s[i + 1], o[n++] = s[i + 2];
      i += 2;
      continue;
    }
    if (c == '^' && s[i + 1]) {
      o[n++] = c, o[n++] = s[i + 1];
      i++;
      continue;
    }
    if (c == '/' || c == '%') {
      o[n++] = c;
      continue;
    }
    if (c == '&') {
      o[n++] = '&';
      charpos = 0, remspace = -1;
      if (s[i + 1] != '*' && aster) o[n++] = '|', o[n++] = '|', charpos = 2;
      continue;
    }
    if (c == '*') aster = true;
    if (charpos >= w->charline) {
      if (remspace > 2) {
        /* back to the last space: it becomes the break */
        int back = n - remspace;
        o[remspace] = '&';
        n = remspace + 1;
        i -= back;
        charpos = 0;
        if (aster) o[n++] = '|', o[n++] = '|', charpos = 2;
        remspace = -1;
        continue;
      }
      o[n++] = '&';
      charpos = 0;
      if (aster) o[n++] = '|', o[n++] = '|', charpos = 2;
      remspace = -1;
    }
    if (c == ' ') remspace = n;
    o[n++] = c;
    charpos++;
  }
  o[n] = 0;
  w->len = n;
  w->aster = aster;
}

static void msg_begin(Writer *w) {
  w->pos = 0, w->wait = 0, w->halt = false, w->closing = false;
  layout(w);
}

void writer_start(Writer *w, int x, int y, int charline) {
  memset(w, 0, sizeof *w);
  w->x = x, w->y = y, w->charline = charline;
  w->hspace = 8, w->vspace = 18;
  w->font = FONT_MAIN;
  w->color = C_WHITE;
  w->skippable = true;
}

void writer_add(Writer *w, const char *s) {
  if (w->nmsg < MAX_MSG) w->msg[w->nmsg++] = s;
  if (w->nmsg == 1) msg_begin(w);
}

static int g_choice_n;           /* a \C choice showing: its options (0: none) */
static int g_choice, g_choice_sel = -1;
static const char *g_choices[4];
static int g_cue;

/* the marks the writer passes as it types (their effects) */
static void passed(Writer *w, const char *p) {
  if (p[0] != '\\') return;
  char a = p[1], b = p[2];
  if (a == 'E') w->fe = (uint8_t)fe_of(b);
  else if (a == 'F') w->fc = (uint8_t)(b == '0' ? FC_NONE : fc_of_letter(b));
  else if (a == 'M') g_cue = b - '0';
  else if (a == 's') w->skippable = b == '1';
}

static void next_msg(Writer *w) {
  if (w->cur + 1 < w->nmsg) {
    w->cur++;
    msg_begin(w);
  } else {
    w->done = true;
  }
}

void writer_show_all(Writer *w) {
  while (!w->halt && !w->done && w->pos < w->len) {
    const char *p = w->line + w->pos;
    if (*p == '\\') {
      passed(w, p);
      if (p[1] == 'C') {
        g_choice_n = p[2] - '0';
        if (g_choice_n < 2) g_choice_n = 2;
        g_choice_sel = -1;
        w->pos += 3;
        w->halt = true;
        return;
      }
      w->pos += 3;
    } else if (*p == '^') {
      w->pos += 2;
    } else if (*p == '/') {
      w->halt = true;
      w->closing = p[1] == '%';
      w->pos++;
    } else if (*p == '%') {
      w->pos++;
      if (p[1] == '%') {
        w->done = true;
        return;
      }
      next_msg(w);
      return;
    } else {
      w->pos++;
    }
  }
  if (w->pos >= w->len && !w->halt) w->halt = true, w->closing = w->cur + 1 >= w->nmsg;
}

bool writer_update(Writer *w, uint32_t pressed, uint32_t held) {
  if (w->done || !w->nmsg) return false;
  if (g_choice_n) {
    /* obj_choicer_neo: a direction picks, Z takes it */
    if (held & K_LEFT) g_choice_sel = 0;
    if (held & K_RIGHT) g_choice_sel = 1;
    if (g_choice_n >= 3 && (held & K_UP)) g_choice_sel = 2;
    if (g_choice_n >= 4 && (held & K_DOWN)) g_choice_sel = 3;
    if ((pressed & K_Z) && g_choice_sel >= 0) {
      g_choice = g_choice_sel;
      g_choice_n = 0;
      w->done = true;
    }
    return !w->done;
  }
  if (w->halt) {
    if (pressed & K_Z) {
      if (w->closing) w->done = true;
      else next_msg(w);
    }
    return !w->done;
  }
  if ((held & K_X) && w->skippable) {
    writer_show_all(w);
    return !w->done;
  }
  if (w->talk_t > 0) w->talk_t--;
  if (w->wait > 0) {
    w->wait--;
    return true;
  }
  /* one letter, past the marks before it */
  for (;;) {
    if (w->pos >= w->len) {
      w->halt = true, w->closing = w->cur + 1 >= w->nmsg;
      break;
    }
    const char *p = w->line + w->pos;
    if (*p == '\\') {
      passed(w, p);
      if (p[1] == 'C') {
        g_choice_n = p[2] - '0';
        if (g_choice_n < 2) g_choice_n = 2;
        g_choice_sel = -1;
        w->pos += 3;
        w->halt = true;
        break;
      }
      w->pos += 3;
      continue;
    }
    if (*p == '^') {
      int d = p[1] >= '0' && p[1] <= '9' ? pauses[p[1] - '0'] : 0;
      w->pos += 2;
      if (d) {
        w->wait = d;
        break;
      }
      continue;
    }
    if (*p == '&' || *p == '|') {
      w->pos++;
      continue;
    }
    if (*p == '/') {
      w->halt = true;
      w->closing = p[1] == '%';
      w->pos++;
      break;
    }
    if (*p == '%') {
      w->pos++;
      if (p[1] == '%') w->done = true;
      else next_msg(w);
      break;
    }
    w->pos++;
    if (*p != ' ') w->talk_t = 6;
    break;
  }
  return !w->done;
}

bool writer_talking(const Writer *w) { return !w->done && !w->halt && w->talk_t > 0; }

static uint16_t color_of(char c, uint16_t own) {
  switch (c) {
    case 'R': return rgb565(0xFF0000);
    case 'B': return rgb565(0x0000FF);
    case 'Y': return rgb565(0xFFFF00);
    case 'G': return rgb565(0x00FF00);
    case 'W': return C_WHITE;
    case 'X': return C_BLACK;
    default: return own;
  }
}

void writer_draw(const Writer *w) {
  if (w->done || !w->nmsg) return;
  int x0 = w->x + (w->fc ? 58 : 0);
  int wx = x0, wy = w->y;
  uint16_t col = w->color;
  char run[64];
  int nrun = 0, run_x = wx;
  for (int i = 0; i < w->pos && i < w->len; i++) {
    const char *p = w->line + i;
    bool flush = false;
    if (*p == '\\') {
      if (p[1] == 'c') {
        if (nrun) gfx_text_ex(w->font, run_x, wy, run, nrun, col, 0, 0, 0), nrun = 0;
        col = color_of(p[2], w->color);
      }
      i += 2;
      continue;
    }
    if (*p == '^') {
      i++;
      continue;
    }
    if (*p == '/' || *p == '%') continue;
    if (*p == '&') {
      flush = true;
    } else if (*p == '|') {
      if (nrun) gfx_text_ex(w->font, run_x, wy, run, nrun, col, 0, 0, 0), nrun = 0;
      wx += w->hspace;
      run_x = wx;
      continue;
    }
    if (flush) {
      if (nrun) gfx_text_ex(w->font, run_x, wy, run, nrun, col, 0, 0, 0), nrun = 0;
      wx = x0, wy += w->vspace, run_x = wx;
      continue;
    }
    /* every letter is hspace wide: drawn one by one where the font's own width differs */
    char ch = *p;
    if (nrun == (int)sizeof run) gfx_text_ex(w->font, run_x, wy, run, nrun, col, 0, 0, 0), nrun = 0, run_x = wx;
    if (!nrun) run_x = wx;
    run[nrun++] = ch;
    wx += w->hspace;
  }
  if (nrun) gfx_text_ex(w->font, run_x, wy, run, nrun, col, 0, 0, 0);
}

/* ---------------------------------------------------------------- the dialogue box */
static Writer box;
static bool box_open;
static int box_side = -1, side_now;
static int face_t;
static const char *pending[MAX_MSG];
static int npending;
static char fbuf[4][256];
static int fbuf_i;
static uint8_t start_fc, start_fe;

void text_clear(void) { npending = 0, start_fc = 0, start_fe = 0; }
void text_add(const char *s) {
  if (npending < MAX_MSG) pending[npending++] = s;
}

/* "~1" and "~2" in a text filled with the strings given (scr_84_get_subst_string) */
void text_addf(const char *fmt, ...) {
  va_list ap;
  const char *args[4] = {"", "", "", ""};
  va_start(ap, fmt);
  for (int k = 0; k < 4; k++) {
    const char *a = va_arg(ap, const char *);
    if (!a) break;
    args[k] = a;
  }
  va_end(ap);
  char *o = fbuf[fbuf_i++ & 3];
  int n = 0;
  for (int i = 0; fmt[i] && n < 250; i++) {
    if (fmt[i] == '~' && fmt[i + 1] >= '1' && fmt[i + 1] <= '4') {
      for (const char *a = args[fmt[i + 1] - '1']; *a && n < 250; a++) o[n++] = *a;
      i++;
    } else {
      o[n++] = fmt[i];
    }
  }
  o[n] = 0;
  text_add(o);
}

void text_face(int fc, int fe) { start_fc = (uint8_t)fc, start_fe = (uint8_t)fe; }
void text_side(int side) { box_side = side; }
void text_set_choices(const char *a, const char *b, const char *c, const char *d) {
  g_choices[0] = a, g_choices[1] = b, g_choices[2] = c, g_choices[3] = d;
}

void text_open(void) {
  if (!npending) return;
  side_now = box_side >= 0 ? box_side : !world_kris_low();
  writer_start(&box, 29, side_now ? 170 : 15, 33);
  box.fc = start_fc, box.fe = start_fe;
  for (int i = 0; i < npending; i++) writer_add(&box, pending[i]);
  if (box.fc) box.charline = 26, msg_begin(&box);
  box_open = true;
  npending = 0;
  box_side = -1;
  face_t = 0;
}

bool text_active(void) { return box_open; }
int text_choice(void) { return g_choice; }
int text_cue(void) { return g_cue; }
void text_set_cue(int v) { g_cue = v; }
int text_msgno(void) { return box.cur; }
void text_close(void) { box_open = false, g_choice_n = 0; }

void text_update(uint32_t pressed, uint32_t held) {
  if (!box_open) return;
  int fc = box.fc;
  writer_update(&box, pressed, held);
  if (box.fc != fc) box.charline = box.fc ? 26 : 33;
  face_t++;
  if (box.done) box_open = false;
}

static void draw_choices(int top) {
  if (!g_choice_n) return;
  int d = top ? 0 : 155;
  int n = g_choice_n;
  int hx[4], hy[4], tx[4], ty[4];
  hx[0] = 30, hy[0] = 34 + d, tx[0] = 46, ty[0] = 13 + d;
  int w1 = gfx_text_width(FONT_MAIN, g_choices[1] ? g_choices[1] : "", 64);
  hx[1] = 320 - 30 - w1 - 14, hy[1] = 34 + d, tx[1] = hx[1] + 16, ty[1] = 13 + d;
  if (n >= 3) {
    int r0 = hx[0] + 16 + gfx_text_width(FONT_MAIN, g_choices[0] ? g_choices[0] : "", 64);
    int w2 = gfx_text_width(FONT_MAIN, g_choices[2] ? g_choices[2] : "", 64) + 16;
    if (n == 4) {
      int w3 = gfx_text_width(FONT_MAIN, g_choices[3] ? g_choices[3] : "", 64) + 16;
      if (w3 > w2) w2 = w3;
    }
    hx[2] = r0 + (hx[1] - r0) / 2 - w2 / 2, hy[2] = 16 + d, tx[2] = hx[2] + 16, ty[2] = 13 + d;
    hx[3] = hx[2], hy[3] = 60 + d, tx[3] = hx[3] + 16, ty[3] = 56 + d;
  }
  for (int i = 0; i < n; i++)
    if (g_choices[i]) gfx_text(FONT_MAIN, tx[i], ty[i], g_choices[i], g_choice_sel == i ? C_YELLOW : C_WHITE, 255);
  int sx = g_choice_sel >= 0 ? hx[g_choice_sel] : 140, sy = g_choice_sel >= 0 ? hy[g_choice_sel] : 36 + d;
  gfx_sprite(spr_heartsmall, 0, sx, sy, 0, 255);
}

/* scr_darkbox: the Dark World's box, its corners' jewels turning */
void draw_darkbox(int x0, int y0, int x1, int y1) {
  gfx_rect(x0 + 7, y0 + 7, x1 - x0 - 14, y1 - y0 - 14, C_BLACK, 255);
  int jewel = (face_t / 10) % 4;
  gfx_sprite_ex(spr_textbox_top, 0, x0 + 16, y0, 256 * (x1 - x0 - 31), 256, 0, 0, 255);
  gfx_sprite_ex(spr_textbox_top, 0, x0 + 16, y1 + 1, 256 * (x1 - x0 - 31), -256, 0, 0, 255);
  gfx_sprite_ex(spr_textbox_left, 0, x0, y0 + 16, 256, 256 * (y1 - y0 - 31), 0, 0, 255);
  gfx_sprite_ex(spr_textbox_left, 0, x1 + 1, y0 + 16, -256, 256 * (y1 - y0 - 31), 0, 0, 255);
  gfx_sprite(spr_textbox_topleft, jewel, x0, y0, 0, 255);
  gfx_sprite(spr_textbox_topleft, jewel, x1 + 1, y0, GFX_FLIPX, 255);
  gfx_sprite(spr_textbox_topleft, jewel, x0, y1 + 1, GFX_FLIPY, 255);
  gfx_sprite(spr_textbox_topleft, jewel, x1 + 1, y1 + 1, GFX_FLIPX | GFX_FLIPY, 255);
}

void text_draw(void) {
  if (!box_open) return;
  int top = !side_now;
  if (!world_dark()) {
    /* scr_dbox in the Light World: a white frame, three pixels */
    int y = top ? 5 : 160;
    gfx_rect(16, y, 289, 76, C_WHITE, 255);
    gfx_rect(19, y + 3, 283, 70, C_BLACK, 255);
  } else {
    int y0 = top ? 5 : 160;
    gfx_rect(19, y0 + 3, 282, 69, C_BLACK, 255);
    draw_darkbox(12, y0 - 4, 308, y0 + 79);
  }
  if (box.fc) face_draw(box.fc, box.fe, box.x + 8, box.y + 5, writer_talking(&box), face_t);
  writer_draw(&box);
  draw_choices(top);
}
