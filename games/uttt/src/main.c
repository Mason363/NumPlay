/* Ultimate Tic-Tac-Toe: nine small boards in a big one. The cell you take sends the other player
 * to the small board in the same place; win three small boards in a row.
 *
 * Two players on one calculator, or one against the computer, easy, normal or hard (a search
 * deeper and deeper for a second: src/rules.h). The number pad is a small board, 7 at the top
 * left; the arrows and OK work too. The game in progress and the games won are kept.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and
 * saves through its file functions, like Block Blast; on its own (UltimateTicTacToe.nwa) it
 * brings them along. The screen is drawn again only when something changes. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Ultimate TTT";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

/* the keys used, in 32 bits: 9, 4 to 6, 1 to 3 and EXE come down from the high word */
static uint32_t keys(void) {
  uint64_t s = eadk_keyboard_scan();
  return ((uint32_t)s & 0xC000017Fu) | ((uint32_t)(s >> 32) & 0x00101C71u) << 9;
}
#define KEY(k) ((uint32_t)1 << ((k) < 32 ? (k) : (k) - 23))
/* the number pad as a small board, 7 at the top left: where each key's bit is in keys() */
static const uint8_t DIGIT[9] = {30, 31, 9, 13, 14, 15, 19, 20, 21};
#define QUIT (KEY(eadk_key_home) | KEY(eadk_key_on_off))
static uint32_t now, deadline, seed;
static bool quit;
static bool search_stop(void) {
  if (keys() & QUIT) quit = true;
  return quit || (int32_t)((uint32_t)eadk_timing_millis() - deadline) >= 0;
}
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  return seed ^= seed << 5;
}
#include "rules.h"

#define WHITE 0xFFFF
#define OX 12 /* the big board's first cell */
#define OY 13
#define BP 74 /* from one small board to the next; cells are 22 apart */
#define PX 275 /* the middle of the panel on the right */
#define PLATE HEX(0x262C4D)
#define SQUARE HEX(0x353D66)
#define PALE HEX(0xB9C2E8)

/* uttt.sav: the opponent, the game in progress as its moves, and the games won */
static struct {
  uint8_t magic, version, opp, human; /* opp: 0 a friend, 1 to 3 the computer, easy to hard; human: the
                                         player's side against it */
  uint8_t flags, n, cur, pad;         /* flags: 1 how to play was read; n: moves played; cur: the cursor */
  uint16_t score[4][3];               /* [opp][X or the player, O or the computer, draws] */
  uint8_t moves[81];
} sv;
#define SAVE_NAME "uttt.sav"

static color_t strip[SCREEN_W * STRIP_H];
static pos_t pos;
static uint32_t turn_t;
static int pick = -1, ov, sel, sure, res, opp; /* pick: the small board chosen with a number on a free move;
                                                opp: the next game's opponent, chosen in the menu */
static color_t team[2];                    /* X red, O blue */
enum { OV_NONE, OV_MENU, OV_HELP };

/* v's digits at o; where they end */
static char *num(char *o, unsigned v) {
  if (v >= 10) o = num(o, v / 10);
  *o = (char)('0' + v % 10);
  return o + 1;
}
static bool cpu_turn(void) { return sv.opp && pos.turn != sv.human && !res; }
static void save(void) {
  sv.magic = 'U', sv.version = 1;
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

/* ------------------------------------------------------------ the game */
/* the cursor on a cell that can be taken, in the small board to play: its middle if free */
static void aim(void) {
  uint16_t mv[9];
  int b = pick >= 0 ? pick : sv.cur / 9;
  legal(&pos, mv);
  while (!mv[b]) b = (b + 1) % 9;
  if (!(mv[b] >> sv.cur % 9 & 1) || b != sv.cur / 9) sv.cur = (uint8_t)(b * 9 + (mv[b] & 020 ? 4 : __builtin_ctz(mv[b])));
}

static void move(int m) {
  play(&pos, m);
  sv.moves[sv.n++] = (uint8_t)m, pick = -1, turn_t = now;
  if ((res = result(&pos)) != 0) {
    /* the score's column: X and O, or the player and the computer, then draws */
    int w = res - 1;
    if (sv.opp && w < 2) w = w != sv.human;
    sv.score[sv.opp][w]++;
  } else {
    aim();
  }
  save();
}

static void new_game(void) {
  sv.opp = (uint8_t)opp;
  pos = (pos_t){.next = -1};
  sv.n = 0, pick = -1, res = 0, turn_t = now;
  sv.human ^= sv.opp != 0; /* against the computer, X and O swap every game */
  aim();
  save();
}

/* ------------------------------------------------------------ drawing */
static void box(int x, int y, int w, int h, color_t c) { gfx_rrect(x, y, w, h, 7, c, 256); }
static void text(int x, int y, const char *s, color_t c) { gfx_text_center(&np_font_small, x, y, s, c, 256); }
/* a line of discs r across from (x0, y0) to (x1, y1), n steps */
static void stroke(int x0, int y0, int x1, int y1, int n, int r, color_t c) {
  for (int i = 0; i <= n; i++) gfx_circle(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r, c, 256);
}
/* X (who 0) or O (who 1), s pixels across, around (x, y) */
static void mark(int who, int x, int y, int s, color_t c) {
  int t = s / 10 + 1, h = s / 2 - t;
  if (who) gfx_rrect_outline(x - s / 2, y - s / 2, s, s, s / 2, 2 * t, c, 256);
  else stroke(x - h, y - h, x + h, y + h, 2 * h, t, c), stroke(x - h, y + h, x + h, y - h, 2 * h, t, c);
}
/* a choice in the menus, as a move on a board: the one picked is marked with X, in X's colour */
static void button(int y, const char *s, bool on) {
  gfx_text_center(&np_font_body, 160, y + 18, s, on ? WHITE : PALE, 256);
  if (on) mark(0, 160 - gfx_text_width(&np_font_body, s) / 2 - 16, y + 13, 12, HEX(0xFF4D6D));
}

static const char *const OPP[4] = {T("vs a friend"), T("vs easy"), T("vs normal"), T("vs hard")};

static void overlay(void) {
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 130);
  if (ov == OV_HELP) {
    box(16, 18, 288, 204, PLATE);
    gfx_text_center(&np_font_title, 160, 46, T("How to play"), WHITE, 256);
    gfx_paragraph(&np_font_body, 160, 70, 264, 16,
                  T("Win three small boards in a row. Your cell sends the other player to the small board in the same place; if it is won or full, they play anywhere. 1 to 9 take a cell, 7 at the top left (on a free move, pick the board first). The arrows and OK work too."),
                  WHITE, 256);
    button(188, T("Play"), true);
    return;
  }
  box(60, 44, 200, 152, PLATE); /* a small board: its # in the middle, under the choices */
  for (int i = 1; i < 3; i++) gfx_fill(60 + 200 * i / 3 - 1, 50, 3, 140, SQUARE), gfx_fill(66, 44 + 152 * i / 3 - 1, 188, 3, SQUARE);
  gfx_text_center(&np_font_title, 160, 72, T("Paused"), WHITE, 256);
  const char *items[3] = {T("Resume"), T("New game"), sure ? T("Quit game?") : T("Quit game")};
  for (int i = 0; i < 3; i++) button(84 + 30 * i, items[i], sel == i);
  text(160, 188, OPP[opp], sel == 1 ? WHITE : PALE); /* the new game's opponent: Left, Right */
  if (sel == 1) text(100, 188, "<", WHITE), text(220, 188, ">", WHITE);
}

static void scene(void *ctx) {
  char s[6];
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, HEX(0x161A33));
  uint16_t mv[9];
  legal(&pos, mv);
  int who = pos.turn, last = sv.n ? sv.moves[sv.n - 1] : -1, w = res ? res - 1 : who;
  int cur = !res && !cpu_turn() ? sv.cur : -1;
  color_t tint = gfx_mix(team[!who], SQUARE, 10); /* the last move's cell */
  for (int b = 0; b < 9; b++) {
    int x0 = OX + b % 3 * BP, y0 = OY + b / 3 * BP, won = pos.won[1] >> b & 1;
    bool open = !(pos.over >> b & 1), taken = (pos.won[0] | pos.won[1]) >> b & 1;
    box(x0 - 3, y0 - 3, 70, 70, taken ? gfx_mix(team[won], PLATE, 9) : open ? PLATE : HEX(0x2B2E3D));
    if (mv[b] && !res) /* the small board(s) to play in, in the colour of who plays */
      gfx_rrect(x0 - 3, y0 - 3, 70, 70, 7, team[who], pick == b ? 120 : 50);
    for (int c = 0; c < 9; c++) {
      int x = x0 + c % 3 * 22, y = y0 + c / 3 * 22, m = b * 9 + c, t = pos.mark[1][b] >> c & 1;
      if (open) box(x, y, 20, 20, m == cur ? WHITE : m == last ? tint : SQUARE);
      if ((pos.mark[0][b] | pos.mark[1][b]) >> c & 1) mark(t, x + 10, y + 10, 12, open ? team[t] : HEX(0x5A6080));
    }
    if (taken) mark(won, x0 + 32, y0 + 32, 52, team[won]);
  }
  for (int i = 0; res && res < 3 && i < 8; i++) /* the winning line */
    if ((pos.won[w] & LINES[i]) == LINES[i]) {
      int a = __builtin_ctz(LINES[i]), z = 31 - __builtin_clz(LINES[i]);
      stroke(OX + 32 + a % 3 * BP, OY + 32 + a / 3 * BP, OX + 32 + z % 3 * BP, OY + 32 + z / 3 * BP, 74, 3, WHITE);
    }

  /* the panel: whose turn (or who won), the games won, the keys */
  box(236, 10, 78, 182, PLATE);
  if (res != 3) mark(w, PX, 44, 40, team[w]);
  text(PX, 95, res == 3 ? T("Draw")
               : sv.opp ? (res ? (w == sv.human ? T("You win!") : T("You lose")) : cpu_turn() ? T("Thinking...") : T("Your turn"))
               : res ? T("wins!") : T("to play"), WHITE);
  static const char *const SIDE[2][3] = {{"X", "O", T("Draws")}, {T("You"), T("Computer"), T("Draws")}};
  for (int i = 0; i < 3; i++) {
    int y = 136 + 22 * i;
    gfx_text(&np_font_small, 244, y, SIDE[sv.opp != 0][i], i < 2 ? team[i ^ (sv.opp && sv.human)] : PALE, 256);
    *num(s, sv.score[sv.opp][i]) = 0;
    gfx_text_right(&np_font_small, 306, y, s, WHITE, 256);
  }
  text(PX, 214, res ? T("OK  new game") : T("1-9, OK  play"), PALE);
  text(PX, 230, T("Back  menu"), PALE);
  if (ov) overlay();
}

/* ------------------------------------------------------------ the loop */
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == 'U' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  if (!ok) sv.human = 1; /* (the first game against the computer: the player is X) */
  sv.opp &= 3, sv.human &= 1;
  /* the game in progress, played again move by move: a move that can't be played ends it there */
  int n = sv.n;
  pos = (pos_t){.next = -1};
  for (sv.n = 0; sv.n < n && sv.n < 81 && !result(&pos); sv.n++) {
    uint16_t mv[9];
    int m = sv.moves[sv.n];
    legal(&pos, mv);
    if (m >= 81 || !(mv[m / 9] >> m % 9 & 1)) break;
    play(&pos, m);
  }
  res = result(&pos);
  if (sv.cur >= 81) sv.cur = 40;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  team[0] = HEX(0xFF4D6D), team[1] = HEX(0x3EC5FF);
  now = (uint32_t)eadk_timing_millis();
  seed = eadk_random() | 1;
  load();
  if (!res) aim();
  if (!(sv.flags & 1)) ov = OV_HELP, sv.flags |= 1;
  uint32_t held = keys(); /* (keys still down from the launcher are not pressed again) */
  bool dirty = true;
  while (!quit) {
    now = (uint32_t)eadk_timing_millis();
    uint32_t k = keys(), hit = k & ~held;
    held = k;
    if (k & QUIT) break;
    dirty |= hit != 0;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
    int dx = !!(hit & KEY(eadk_key_right)) - !!(hit & KEY(eadk_key_left));
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    if (ov == OV_HELP) {
      if (ok || back) ov = OV_NONE;
    } else if (ov) { /* Resume, New game (against whom: Left, Right), Quit game (OK twice) */
      if (dy) sel = (sel + dy + 3) % 3, sure = 0;
      if (sel == 1) opp = (opp + dx + 4) % 4;
      if (back) ov = OV_NONE;
      if (ok) {
        if (sel == 2 && sure) break;
        sure = sel == 2, ov = sure;
        if (sel == 1) new_game();
      }
    } else if (back) {
      if (pick >= 0) pick = -1;
      else ov = OV_MENU, sel = sure = 0, opp = sv.opp;
    } else if (res) {
      if (ok) opp = sv.opp, new_game();
    } else if (cpu_turn()) {
      if (!dirty && now - turn_t > 450) { /* the board shown first, then the computer thinks */
        deadline = now + 1000;
        static const uint8_t DEPTH[4] = {0, 1, 3, 30}, NOISE[4] = {0, 90, 10, 0};
        int m = think(&pos, DEPTH[sv.opp], NOISE[sv.opp]);
        if (quit) break;
        move(m), dirty = true;
      }
    } else {
      uint16_t mv[9];
      legal(&pos, mv);
      int digit = -1, b = pos.next >= 0 ? pos.next : pick;
      for (int c = 0; c < 9; c++)
        if (hit >> DIGIT[c] & 1) digit = c;
      if (digit >= 0 && b < 0) { /* a free move: the number picks the small board first */
        if (mv[digit]) pick = digit, aim();
      } else if (digit >= 0) {
        if (mv[b] >> digit & 1) move(b * 9 + digit);
      } else if (dx || dy) { /* the cursor over the 9 x 9 cells */
        int gx = (sv.cur / 9 % 3 * 3 + sv.cur % 3 + dx + 9) % 9, gy = (sv.cur / 27 * 3 + sv.cur % 9 / 3 + dy + 9) % 9;
        sv.cur = (uint8_t)(gy / 3 * 27 + gx / 3 * 9 + gy % 3 * 3 + gx % 3), pick = -1;
      } else if (ok && mv[sv.cur / 9] >> sv.cur % 9 & 1) {
        move(sv.cur);
      }
    }
    if (dirty) {
      eadk_display_wait_for_vblank();
      gfx_render(scene, 0, 0, SCREEN_H);
      dirty = false;
    } else {
      eadk_timing_msleep(20);
    }
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
