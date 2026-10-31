/* Meowdoku: put one cat in every row, every column and every colour; no two cats may touch.
 *
 * Played as on the phone: OK on a cell marks an X (or takes it off), OK twice quickly places a cat there
 * (EXE does too). A cat in the right place stays; a wrong one turns into an X and costs a fish, and with
 * the third fish gone the level starts over. OK held while moving marks X's along the way. The lamp
 * (shift) shows the next step a player can reason out and does it on OK; the winking cat (alpha) finds
 * one cat. The backspace takes back X's.
 *
 * The puzzles are the game's own (levels.h, from its level banks), dealt the way the game deals them:
 * the board size set by the level number, special picture levels at fixed numbers, a hard level every
 * tenth, and a difficulty that climbs while the player wins cleanly and drops after failures.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and saves
 * through its file functions; on its own (Meowdoku.nwa) it brings them along. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#ifdef HOST /* (the tests keep the file in memory, and have no app header) */
const uint8_t *np_file_read(const char *name, uint32_t *len);
bool np_file_write(const char *name, const void *data, uint32_t len);
#else
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Meowdoku";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

typedef struct {
  uint8_t w, h;
  const uint16_t *pal;
  const uint8_t *alpha, *px;
} sprite_t;
#include "art.h"
#include "levels.h"

#define NMAX 11
#define NPOOL (int)(sizeof lv_pool / sizeof *lv_pool)
#define NLK (int)(sizeof lv_lk / sizeof *lv_lk)
#define NSPECIAL (int)(sizeof lv_special / sizeof *lv_special)
#define NPUZ (int)(sizeof lv_at / sizeof *lv_at - 1)
#define K(k) (1ull << (k))

#define TEXT HEX(0x6B4A2E)   /* brown, as the game's words */
#define SOFT HEX(0xA08466)
#define WHITE 0xFFFF
#define ORANGE HEX(0xFFA62B) /* the game's buttons */
#define RED HEX(0xE5484D)
#define INK HEX(0x24170E)    /* the frame around the cell picked */
#define SUN HEX(0xFFD43B)    /* a hint's cells */
static const uint32_t palette[12] = {0xCDA400, 0xD36F8F, 0x8979DA, 0xF89BE5, 0xFA9D5C, 0xFBD983,
                                     0x5076A5, 0xA5C6E7, 0x38A9C0, 0x2A8C53, 0x8BD57D, 0xA86D4A};

enum { EMPTY, MARK, CAT };

/* meowdoku.sav: the level being played and how the game deals the next ones */
static struct {
  uint8_t magic, version, flags, lives;   /* flags: 1 the guide was read, 2 a level in play */
  uint16_t level, wins;
  uint8_t strat, clean, fails, pend;      /* the difficulty: its strategy, clean wins and fails in a row, a drop due */
  uint8_t retried, dirty, rlevels, rstrat; /* the level was failed once; a helper was used; retried levels in a row */
  uint32_t seed;
  uint16_t puz;                           /* the puzzle, its size, its turn (0-7), whether it has its own colours */
  uint8_t size, t, special, perfect;
  uint8_t cell[NMAX * NMAX];
  uint8_t pc[NPOOL][3];                   /* each pool: the next puzzle, its turn, puzzles since a LinkedIn one */
  uint8_t lkc[NLK];                       /* the next LinkedIn-modified puzzle of each size and rank */
} sv;
#define SAVE_NAME "meowdoku.sav"

static color_t strip[SCREEN_W * STRIP_H];
static int n, reg[NMAX * NMAX], sol[NMAX], color[NMAX]; /* the puzzle: size, regions, each row's cat, colours */
static int cr, cc;                                        /* the cell picked */
static int ov, menu, quit_ok;
static uint32_t now, err_until, err_cell;

static uint32_t rnd(void) {
  sv.seed ^= sv.seed << 13;
  sv.seed ^= sv.seed >> 17;
  return sv.seed ^= sv.seed << 5;
}

/* ------------------------------------------------------------ the puzzles */
static int size_at(int lv) {
  static const uint8_t first[10] = {4, 5, 6, 6, 8, 6, 7, 8, 9, 7}, then[10] = {8, 10, 10, 9, 10, 10, 9, 10, 10, 10};
  return lv <= 10 ? first[lv - 1] : then[(lv - 11) % 10];
}
static bool is_hard(int lv) { return lv >= 21 && lv % 10 == 0; }
static int special_at(int lv) {
  for (int i = 0; i < NSPECIAL; i++)
    if (lv_special[i][0] == lv) return i;
  return -1;
}
static int pool_first(int p) { return lv_pool[p][3] | lv_pool_hi[p] << 8; }
static int find_pool(int size, int rank, int main_path) {
  int only = main_path && size == 10 && (rank == 3 || rank == 4);
  for (int p = 0; p < NPOOL; p++)
    if (lv_pool[p][0] == size && lv_pool[p][1] == rank && lv_pool[p][2] == only) return p;
  return -1;
}

/* The next level's puzzle (NLControlStrategy): its size from the level number, a special one at fixed
   levels, rank 5 on hard levels, otherwise the rank of the difficulty reached (from rank 3, any rank from 2
   up to it); then the next puzzle of that pool, which turns and mirrors once the pool has been played. */
static void deal(void) {
  int lv = sv.level, sp = special_at(lv), main_path = lv >= 51, rank;
  sv.t = 0, sv.special = sp >= 0;
  if (sp >= 0) {
    sv.size = (uint8_t)lv_special[sp][1], sv.puz = lv_special[sp][2];
    return;
  }
  sv.size = (uint8_t)size_at(lv);
  if (is_hard(lv)) rank = 5;
  else {
    if (main_path && sv.strat < 2) sv.strat = 2;
    int s = lv <= 5 ? 1 : sv.strat, cap = main_path ? 4 : lv >= 21 ? 3 : 2;
    if (s > cap) s = cap;
    if (s >= 3) s = 2 + (int)(rnd() % (uint32_t)(s - 1));
    rank = s;
  }
  int p = find_pool(sv.size, rank, main_path);
  for (int r = rank - 1; p < 0 && r >= 1; r--) p = find_pool(sv.size, r, main_path); /* (as the game relaxes) */
  for (int r = rank + 1; p < 0 && r <= 5; r++) p = find_pool(sv.size, r, main_path);
  uint8_t *c = sv.pc[p];
  for (int k = 0; main_path && rank < 5 && k < NLK; k++)
    if (lv_lk[k][0] == sv.size && lv_lk[k][1] == rank && c[2] >= 4 && sv.lkc[k] < lv_lk[k][4]) {
      sv.puz = (uint16_t)(lv_lk[k][3] + sv.lkc[k]++), c[2] = 0;
      return;
    }
  if (c[0] >= lv_pool[p][4]) c[0] = 0, c[1] = (uint8_t)((c[1] + 1) & 7);
  sv.puz = (uint16_t)(pool_first(p) + c[0]++), sv.t = c[1];
  if (c[2] < 255) c[2]++;
}

/* The colours of the regions (LevelGenerator.compute_color_map_with_seed): regions in turn (most neighbours
   first, or shuffled by the puzzle's turn), each the unused colour farthest from its coloured neighbours. */
static void colour(int seed) {
  static const uint32_t rgb[12] = {0xE6A8C1, 0xC1658A, 0x9179D1, 0xFEA0EC, 0xFFA96C, 0xE3BA46,
                                   0x6182B5, 0xA6BED8, 0x69BCE6, 0x46B3B0, 0xACD994, 0xAB6D46};
  uint16_t adj[NMAX] = {0};
  int order[NMAX], used = 0;
  for (int i = 0; i < n * n; i++) {
    int a = reg[i];
    if (i % n + 1 < n && reg[i + 1] != a) adj[a] |= (uint16_t)(1 << reg[i + 1]), adj[reg[i + 1]] |= (uint16_t)(1 << a);
    if (i + n < n * n && reg[i + n] != a) adj[a] |= (uint16_t)(1 << reg[i + n]), adj[reg[i + n]] |= (uint16_t)(1 << a);
  }
  for (int i = 0; i < n; i++) order[i] = i, color[i] = -1;
  if (!seed)
    for (int i = 1; i < n; i++) /* (a stable sort: most neighbours, then the lower number) */
      for (int j = i; j > 0 && __builtin_popcount(adj[order[j]]) > __builtin_popcount(adj[order[j - 1]]); j--) {
        int t = order[j];
        order[j] = order[j - 1], order[j - 1] = t;
      }
  else {
    uint64_t st = ((uint64_t)seed * 1664525u + 1013904223u) & 0x7FFFFFFF;
    for (int i = n - 1; i > 0; i--) {
      st = (st * 1664525u + 1013904223u) & 0x7FFFFFFF;
      int j = (int)(st % (uint64_t)(i + 1)), t = order[i];
      order[i] = order[j], order[j] = t;
    }
  }
  for (int k = 0; k < n; k++) {
    int ri = order[k], best = 0;
    int64_t best_d = -1;
    for (int ci = 0; ci < 12; ci++) {
      if (used >> ci & 1) continue;
      int64_t d = INT64_MAX;
      for (int nb = 0; nb < n; nb++)
        if (adj[ri] >> nb & 1 && color[nb] >= 0) {
          int dr = (int)(rgb[ci] >> 16) - (int)(rgb[color[nb]] >> 16), dg = (int)(rgb[ci] >> 8 & 255) - (int)(rgb[color[nb]] >> 8 & 255),
              db = (int)(rgb[ci] & 255) - (int)(rgb[color[nb]] & 255);
          if (dr * dr + dg * dg + db * db < d) d = dr * dr + dg * dg + db * db;
        }
      if (d > best_d) best_d = d, best = ci;
    }
    color[ri] = best, used |= 1 << best;
  }
}

/* Counts the solutions, up to two; the first in sol. */
static int nsol, tmp[NMAX];
static void search(int r, int cols, int regs) {
  if (r == n) {
    if (!nsol++)
      for (int i = 0; i < n; i++) sol[i] = tmp[i];
    return;
  }
  for (int c = 0; c < n && nsol < 2; c++) {
    int g = reg[r * n + c];
    if (cols >> c & 1 || regs >> g & 1 || (r && (tmp[r - 1] - c) * (tmp[r - 1] - c) <= 1)) continue;
    tmp[r] = c;
    search(r + 1, cols | 1 << c, regs | 1 << g);
  }
}
static int solve(void) {
  nsol = 0;
  search(0, 0, 0);
  return nsol;
}

/* The puzzle of sv.puz, turned by sv.t (LevelData.apply_transform), solved and coloured. */
static const uint8_t *bp;
static int bb;
static int bits(int k) {
  int v = 0;
  while (k--) {
    v = v << 1 | (*bp >> (7 - bb) & 1);
    if (++bb == 8) bb = 0, bp++;
  }
  return v;
}
static void load_puzzle(void) {
  int t[NMAX * NMAX];
  bp = lv_data + lv_at[sv.puz], bb = 0, n = sv.size;
  for (int i = 0; i < n * n; i++)
    reg[i] = i % n && bits(1) ? reg[i - 1] : i >= n && bits(1) ? reg[i - n] : bits(4);
  if (sv.special && lv_special[special_at(sv.level)][3])
    for (int i = 0; i < n; i++) color[i] = bits(4) % 12;
  if (sv.t >= 4)
    for (int i = 0; i < n * n; i++) t[i] = reg[i - i % n + n - 1 - i % n];
  else
    for (int i = 0; i < n * n; i++) t[i] = reg[i];
  for (int k = 0; k < sv.t % 4; k++) { /* a quarter turn: new (r, c) is old (n - 1 - c, r) */
    for (int i = 0; i < n * n; i++) reg[i] = t[(n - 1 - i % n) * n + i / n];
    for (int i = 0; i < n * n; i++) t[i] = reg[i];
  }
  for (int i = 0; i < n * n; i++) reg[i] = t[i] % NMAX;
  solve();
  if (!(sv.special && lv_special[special_at(sv.level)][3])) colour(sv.t);
}

/* ------------------------------------------------------------ playing */
static uint16_t undo[256]; /* each X put or taken: the cell, the state before, and whether a move starts there */
static uint8_t utop, ucount;
static int stroke = -1; /* while OK is held: what moving onto a cell does (MARK or EMPTY) */

static int cats(void) {
  int k = 0;
  for (int i = 0; i < n * n; i++) k += sv.cell[i] == CAT;
  return k;
}
static void set_mark(int i, int v, int first) {
  if (sv.cell[i] == CAT || sv.cell[i] == v) return;
  undo[utop++] = (uint16_t)(i | sv.cell[i] << 7 | first << 9);
  if (ucount < 255) ucount++;
  sv.cell[i] = (uint8_t)v;
}
static void undo_move(void) {
  while (ucount) {
    uint16_t e = undo[--utop];
    ucount--;
    if (sv.cell[e & 127] != CAT) sv.cell[e & 127] = (uint8_t)(e >> 7 & 3);
    if (e >> 9 & 1) break;
  }
}

enum { OV_NONE, OV_MENU, OV_HELP, OV_WON, OV_FAILED };
static void won(void);
static void failed(void);

/* a cat on cell i (the double tap): it stays if the solution has it there; otherwise the cell gets an X
   and a fish is lost */
static void place(int i) {
  if (sv.cell[i] == CAT || ov) return;
  if (sol[i / n] == i % n) {
    sv.cell[i] = CAT;
    if (cats() == n) won();
    return;
  }
  sv.cell[i] = MARK, err_cell = (uint32_t)i, err_until = now + 600;
  if (sv.lives) sv.lives--;
  sv.perfect = 0;
  if (!sv.lives) failed();
}

/* the level from its start (a retry, or a new level): three fish, the board empty, and on levels 1 to 10 a
   cat already in (LevelData.compute_prefill) */
static void start_level(void) {
  load_puzzle();
  for (int i = 0; i < NMAX * NMAX; i++) sv.cell[i] = EMPTY;
  sv.lives = 3, sv.perfect = 1, sv.flags |= 2, ucount = 0, cr = cc = n / 2, err_until = 0;
  if (sv.level <= 10) { /* (levels 7 to 10: a cat alone in its colour) */
    int area[NMAX] = {0}, at = -1, one = sv.level >= 7;
    for (int i = 0; i < n * n; i++) area[reg[i]]++;
    for (int r = 0; r < n && at < 0; r++)
      if ((area[reg[r * n + sol[r]]] == 1) == one) at = r * n + sol[r];
    sv.cell[at < 0 ? sol[0] : at] = CAT;
  }
}

/* GameState._apply_level_won: the difficulty climbs after clean wins (two in a row before level 51, then
   one) and drops after failures, then the next level is dealt */
static void won(void) {
  int lv = sv.level;
  if (lv >= 6) {
    int mx = lv >= 201 ? 6 : lv >= 101 ? 5 : lv >= 51 ? 4 : lv >= 21 ? 3 : 2, mn = mx >= 5 ? 2 : 1, demoted = 0;
    if (!sv.dirty) {
      if (++sv.clean >= (lv >= 51 ? 1 : 2) && sv.strat < mx) sv.strat++, sv.clean = 0;
    } else sv.clean = 0;
    if (sv.fails >= (lv >= 21 ? 2 : 1) && sv.strat > mn) sv.strat--, demoted = 1;
    sv.fails = 0;
    if (lv >= 21) {
      if (!sv.retried) sv.rlevels = sv.rstrat = 0;
      else if (sv.strat != sv.rstrat) sv.rlevels = 1, sv.rstrat = sv.strat;
      else if (++sv.rlevels >= 2 && sv.strat > mn && !demoted) sv.strat--, sv.rlevels = sv.rstrat = 0;
    }
    if (sv.pend && !demoted) sv.strat = (uint8_t)(sv.strat > mn ? sv.strat - 1 : mn), sv.pend = 0, demoted = 1;
    if (sv.retried && !demoted) {
      if (is_hard(lv + 1) || special_at(lv + 1) >= 0) sv.pend = 1;
      else sv.strat = (uint8_t)(sv.strat > mn ? sv.strat - 1 : mn);
    }
  }
  sv.retried = sv.dirty = 0, sv.flags &= (uint8_t)~2, sv.wins++, sv.level++;
  deal();
  ov = OV_WON, menu = 0;
}
static void failed(void) {
  sv.retried = sv.dirty = 1;
  if (sv.level >= 6) sv.clean = 0, sv.fails++;
  sv.flags &= (uint8_t)~2;
  ov = OV_FAILED, menu = 0;
}

/* ------------------------------------------------------------ the helpers */
/* Locate (LocateStrategy): of the cats still missing, in colours without one, the one whose colour has the
   fewest cells left without an X */
static void locate(void) {
  int left[NMAX] = {0}, done = 0, best = -1, bl = 999;
  for (int i = 0; i < n * n; i++) {
    if (sv.cell[i] == CAT) done |= 1 << reg[i];
    if (sv.cell[i] != MARK) left[reg[i]]++;
  }
  for (int r = 0; r < n; r++) {
    int i = r * n + sol[r];
    if (sv.cell[i] != CAT && !(done >> reg[i] & 1) && left[reg[i]] < bl) bl = left[reg[i]], best = i;
  }
  if (best < 0) return;
  sv.dirty = 1;
  place(best);
}

/* Hints (HintEngine, in the game's order): the X's a cat already decides, an X on a cat's cell, a line or
   colour with one place left, a colour held in one line (or a line held by one colour), a few colours held
   in as many lines, and a cell where a cat would leave a line or colour with no room. */
enum { H_NONE, H_MARKS, H_UNMARK, H_ONE_ROW, H_ONE_COL, H_ONE_COLOR, H_COLOR_LINE, H_LINE_COLOR, H_SETS, H_CHAIN };
static struct {
  int kind, cell;
  uint8_t hl[NMAX * NMAX]; /* 1 a cell the hint is about, 2 one it changes */
} hint;
static const char *const hint_text[] = {
  0,
  T("This cat rules out its row, its column and the cells around it."),
  T("A cat goes in this cell: take the X off."),
  T("Only one cell in this row can take a cat."),
  T("Only one cell in this column can take a cat."),
  T("Only one cell of this color can take a cat."),
  T("This color's cat must be in this line, so the rest of the line can't have one."),
  T("This line's cat must be in this color, so the rest of the color can't have one."),
  T("These colors fill these lines between them, so nothing else in the lines can have a cat."),
  T("A cat here would leave a row, column or color with no room."),
};

static uint8_t cand[NMAX * NMAX]; /* the cells a cat could still go in */
static void candidates(const uint8_t *cell, uint8_t *out) {
  int rows = 0, cols = 0, regs = 0;
  for (int i = 0; i < n * n; i++)
    if (cell[i] == CAT) rows |= 1 << i / n, cols |= 1 << i % n, regs |= 1 << reg[i];
  for (int i = 0; i < n * n; i++) {
    int r = i / n, c = i % n, ok = cell[i] == EMPTY && !(rows >> r & 1) && !(cols >> c & 1) && !(regs >> reg[i] & 1);
    for (int dr = -1; dr <= 1 && ok; dr++)
      for (int dc = -1; dc <= 1; dc++)
        if (r + dr >= 0 && r + dr < n && c + dc >= 0 && c + dc < n && cell[(r + dr) * n + c + dc] == CAT) ok = 0;
    out[i] = (uint8_t)ok;
  }
}
/* unit u: rows 0..n-1, columns n..2n-1, colours 2n..3n-1 */
static int unit_of(int type, int i) { return type == 0 ? i / n : type == 1 ? n + i % n : 2 * n + reg[i]; }
static uint16_t touch[3 * NMAX][3]; /* the rows, columns and colours a unit's candidates are in */
static uint16_t has_cat[3]; /* the rows, columns and colours with a cat */

/* k colours whose cells left lie in k lines (or k lines in k colours): the other cells of those lines
   (colours) get X's. Units of type a picked, their candidates spanning units of type b. */
static int set_k, set_a, set_b;
static bool sets(int from, int k, uint32_t pick, uint32_t span) {
  if (__builtin_popcount(span) > set_k) return false;
  if (k == set_k) {
    if (__builtin_popcount(span) != k) return false;
    bool any = false;
    for (int i = 0; i < n * n; i++) {
      int in_b = span >> (unit_of(set_b, i) - set_b * n) & 1, in_a = pick >> (unit_of(set_a, i) - set_a * n) & 1;
      hint.hl[i] = in_b && !in_a && sv.cell[i] == EMPTY ? 2 : in_a && cand[i] ? 1 : 0;
      any |= hint.hl[i] == 2;
    }
    return any;
  }
  for (int u = from; u < n; u++)
    if (!(has_cat[set_a] >> u & 1) && touch[set_a * n + u][set_b] && sets(u + 1, k + 1, pick | 1u << u, span | touch[set_a * n + u][set_b]))
      return true;
  return false;
}

/* a cat on cell i, then the cells forced in turn (a row, column or colour with one place left): the cats
   placed before some unit is left with no room, or -1 */
static int contradiction(int i) {
  uint8_t cell[NMAX * NMAX], c2[NMAX * NMAX];
  for (int k = 0; k < n * n; k++) cell[k] = sv.cell[k] == CAT ? CAT : cand[k] ? EMPTY : MARK;
  cell[i] = CAT;
  for (int depth = 0;; depth++) {
    int cnt[3 * NMAX] = {0}, at[3 * NMAX], has[3] = {0};
    candidates(cell, c2);
    for (int k = 0; k < n * n; k++)
      for (int t = 0; t < 3; t++) {
        int u = unit_of(t, k);
        if (cell[k] == CAT) has[t] |= 1 << (u - t * n);
        if (c2[k]) cnt[u]++, at[u] = k;
      }
    int forced = -1;
    for (int u = 0; u < 3 * n; u++) {
      if (has[u / n] >> (u % n) & 1) continue;
      if (!cnt[u]) return depth;
      if (cnt[u] == 1 && forced < 0) forced = at[u];
    }
    if (forced < 0) return -1;
    cell[forced] = CAT;
  }
}

static void find_hint(void) {
  for (int i = 0; i < n * n; i++) hint.hl[i] = 0;
  hint.kind = H_NONE;
  for (int i = 0; i < n * n; i++) /* a cat's row, column and neighbours */
    if (sv.cell[i] == CAT) {
      for (int k = 0; k < n * n; k++) {
        int dr = k / n - i / n, dc = k % n - i % n;
        if (sv.cell[k] == EMPTY && (!dr || !dc || (dr * dr <= 1 && dc * dc <= 1))) hint.hl[k] = 2, hint.kind = H_MARKS;
      }
      if (hint.kind) {
        hint.cell = i, hint.hl[i] = 1;
        return;
      }
    }
  for (int r = 0; r < n; r++) /* an X where the cat is */
    if (sv.cell[r * n + sol[r]] == MARK) {
      hint.kind = H_UNMARK, hint.cell = r * n + sol[r], hint.hl[hint.cell] = 2;
      return;
    }
  candidates(sv.cell, cand);
  int cnt[3 * NMAX] = {0}, at[3 * NMAX];
  has_cat[0] = has_cat[1] = has_cat[2] = 0;
  for (int u = 0; u < 3 * n; u++) touch[u][0] = touch[u][1] = touch[u][2] = 0;
  for (int i = 0; i < n * n; i++)
    for (int t = 0; t < 3; t++) {
      int u = unit_of(t, i);
      if (sv.cell[i] == CAT) has_cat[t] |= (uint16_t)(1 << (u - t * n));
      if (cand[i]) {
        cnt[u]++, at[u] = i;
        for (int t2 = 0; t2 < 3; t2++) touch[u][t2] |= (uint16_t)(1 << (unit_of(t2, i) - t2 * n));
      }
    }
  for (int u = 0; u < 3 * n; u++) /* one place left */
    if (cnt[u] == 1 && !(has_cat[u / n] >> (u % n) & 1)) {
      for (int i = 0; i < n * n; i++) hint.hl[i] = unit_of(u / n, i) == u;
      hint.kind = H_ONE_ROW + u / n, hint.cell = at[u], hint.hl[at[u]] = 2;
      return;
    }
  /* a colour in one line, then a line in one colour; then 2, 3, ... of them */
  static const uint8_t pass[4][2] = {{2, 0}, {2, 1}, {0, 2}, {1, 2}};
  for (set_k = 1; 2 * set_k <= n; set_k++)
    for (int p = 0; p < 4; p++) {
      set_a = pass[p][0], set_b = pass[p][1];
      if (sets(0, 0, 0, 0)) {
        hint.kind = set_k > 1 ? H_SETS : p < 2 ? H_COLOR_LINE : H_LINE_COLOR;
        return;
      }
    }
  for (int i = 0; i < n * n; i++) hint.hl[i] = 0;
  int best = -1, bd = 99;
  for (int i = 0; i < n * n; i++) /* a cat that leaves no room, the soonest */
    if (cand[i]) {
      int d = contradiction(i);
      if (d >= 0 && d < bd) bd = d, best = i;
    }
  if (best >= 0) hint.kind = H_CHAIN, hint.cell = best, hint.hl[best] = 2;
}

static void apply_hint(void) {
  int first = 1;
  sv.dirty = 1;
  if (hint.kind == H_ONE_ROW || hint.kind == H_ONE_COL || hint.kind == H_ONE_COLOR) place(hint.cell);
  else if (hint.kind == H_UNMARK) set_mark(hint.cell, EMPTY, 1);
  else
    for (int i = 0; i < n * n; i++)
      if (hint.hl[i] == 2) set_mark(i, MARK, first), first = 0;
  hint.kind = H_NONE;
}

/* ------------------------------------------------------------ drawing */
static int pitch, bx, by; /* the board: a cell's pitch, its top left */
static color_t catc[48 * 48];
static uint8_t cat_a[48 * 48], x_a[48 * 48]; /* the cat and the X at the cell's size, their opacity 0-32 */
static int cat_w, cat_h, cell_w;

static inline void blend(int x, int y, color_t c, int a) {
  if (a > 0 && x >= 0 && x < SCREEN_W && y >= gfx_y0 && y < gfx_y1) {
    color_t *p = gfx_buf + (y - gfx_y0) * SCREEN_W + x;
    *p = a >= 32 ? c : gfx_mix(*p, c, a);
  }
}
static int nib(const sprite_t *s, int x, int y) { return s->px[y * ((s->w + 1) / 2) + x / 2] >> (x & 1 ? 0 : 4) & 15; }
static void sprite(const sprite_t *s, int x, int y) {
  for (int j = NP_MAX(0, gfx_y0 - y); j < s->h && y + j < gfx_y1; j++)
    for (int i = 0; i < s->w; i++) {
      int k = nib(s, i, j);
      blend(x + i, y + j, s->pal[k], (s->alpha[k] + 4) >> 3);
    }
}
/* the cat shrunk to w x h: each pixel the mean of 4 x 4 samples */
static void scale_cat(int w, int h) {
  const sprite_t *s = &spr_cat;
  cat_w = w, cat_h = h;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int sr = 0, sg = 0, sb = 0, sa = 0;
      for (int v = 0; v < 4; v++)
        for (int u = 0; u < 4; u++) {
          int k = nib(s, (x * 4 + u) * s->w / (w * 4), (y * 4 + v) * s->h / (h * 4)), a = s->alpha[k];
          uint16_t c = s->pal[k];
          sr += (c >> 11) * a, sg += (c >> 5 & 63) * a, sb += (c & 31) * a, sa += a;
        }
      cat_a[y * w + x] = (uint8_t)((sa / 16 + 4) >> 3);
      catc[y * w + x] = sa ? (color_t)((sr / sa) << 11 | (sg / sa) << 5 | sb / sa) : 0;
    }
}
/* the game's white X, two strokes with round ends, at the cell's size */
static void make_x(int w) {
  float half = w * 0.25f, th = w * 0.085f + 0.6f, c = (w - 1) * 0.5f;
  cell_w = w;
  for (int y = 0; y < w; y++)
    for (int x = 0; x < w; x++) {
      float px = x - c, py = y - c, best = 99;
      for (int d = -1; d <= 1; d += 2) { /* the distance to each stroke, from (-half, -d half) to (half, d half) */
        float t = (px + d * py) / 2;
        t = t < -half ? -half : t > half ? half : t;
        float ex = px - t, ey = py - d * t, dist = __builtin_sqrtf(ex * ex + ey * ey);
        if (dist < best) best = dist;
      }
      float a = th - best + 0.5f;
      x_a[y * w + x] = (uint8_t)(a <= 0 ? 0 : a >= 1 ? 32 : a * 32);
    }
}
static void layout(void) {
  pitch = NP_MIN(40, 214 / n);
  int side = n * pitch + 6;
  bx = 4 + (230 - side) / 2, by = 2 + (224 - side) / 2;
  int s = pitch - 2, w = s * 92 / 100;
  scale_cat(w, w * spr_cat.h / spr_cat.w);
  make_x(s);
}
static void cell_at(int i, int *x, int *y) { *x = bx + 3 + i % n * pitch + 1, *y = by + 3 + i / n * pitch + 1; }

static void say(const np_font_t *f, int x, int y, const char *s, color_t c) { gfx_text_center(f, x, y, s, c, 256); }
static char *num(char *o, uint32_t v) {
  char t[10];
  int k = 0;
  do t[k++] = (char)('0' + v % 10); while (v /= 10);
  while (k) *o++ = t[--k];
  *o = 0;
  return o;
}

static void board(void) {
  int s = pitch - 2, rad = s / 5 + 1;
  gfx_rrect(bx, by, n * pitch + 6, n * pitch + 6, 6, WHITE, 256);
  for (int i = 0; i < n * n; i++) {
    int x, y;
    cell_at(i, &x, &y);
    if (y >= gfx_y1 || y + s <= gfx_y0) continue;
    color_t c = gfx_rgb(palette[color[reg[i]]]);
    if (hint.kind && !hint.hl[i]) c = gfx_mix(c, WHITE, 14);
    gfx_rrect(x, y, s, s, rad, c, 256);
    if (sv.cell[i] == CAT) {
      int ox = x + (s - cat_w) / 2, oy = y + (s - cat_h) / 2;
      for (int j = NP_MAX(0, gfx_y0 - oy); j < cat_h && oy + j < gfx_y1; j++)
        for (int k = 0; k < cat_w; k++) blend(ox + k, oy + j, catc[j * cat_w + k], cat_a[j * cat_w + k]);
    }
    bool err = err_until && (uint32_t)i == err_cell;
    if (err) gfx_rrect(x, y, s, s, rad, RED, 200);
    int preview = hint.kind && hint.hl[i] == 2 && sv.cell[i] == EMPTY && hint.kind != H_ONE_ROW && hint.kind != H_ONE_COL &&
                  hint.kind != H_ONE_COLOR;
    if (sv.cell[i] == MARK || preview)
      for (int j = NP_MAX(0, gfx_y0 - y); j < s && y + j < gfx_y1; j++)
        for (int k = 0; k < s; k++) blend(x + k, y + j, preview ? INK : WHITE, preview ? x_a[j * s + k] / 2 : x_a[j * s + k]);
    if (hint.kind && hint.hl[i] == 2 && !preview) gfx_rrect_outline(x - 1, y - 1, s + 2, s + 2, rad + 1, 2, SUN, 256);
  }
  if (!ov) { /* the cell picked: a black frame with a white edge inside, around it */
    int x, y;
    cell_at(cr * n + cc, &x, &y);
    gfx_rrect_outline(x - 3, y - 3, s + 6, s + 6, rad + 3, 3, INK, 256);
    gfx_rrect_outline(x, y, s, s, rad, 1, WHITE, 256);
  }
}

static void panel(void) {
  char s[24];
  if (hint.kind) {
    sprite(&spr_lamp, 265, 6);
    int y = gfx_paragraph(&np_font_small, 276, 44, 80, 13, hint_text[hint.kind], TEXT, 256);
    say(&np_font_small, 276, y + 10, T("OK  apply"), TEXT);
    say(&np_font_small, 276, y + 24, T("Back  close"), SOFT);
    return;
  }
  say(&np_font_small, 276, 14, T("Level"), SOFT);
  num(s, sv.level);
  say(&np_font_title, 276, 40, s, TEXT);
  if (is_hard(sv.level)) {
    int w = gfx_text_width(&np_font_small, T("Hard"));
    sprite(&spr_flame, 276 - (w + 15) / 2, 47);
    gfx_text(&np_font_small, 276 - (w + 15) / 2 + 15, 59, T("Hard"), RED, 256);
  }
  for (int f = 0; f < 3; f++) sprite(f < sv.lives ? &spr_fish : &spr_fish_dim, 243 + 23 * f, 68);
  char *e = num(s, (uint32_t)cats());
  *e++ = ' ', *e++ = '/', *e++ = ' ';
  num(e, (uint32_t)n);
  say(&np_font_body, 276, 110, s, TEXT);
  sprite(&spr_lamp, 244, 130);
  sprite(&spr_locate, 286, 130);
  say(&np_font_small, 255, 166, T("shift"), TEXT);
  say(&np_font_small, 297, 166, T("alpha"), TEXT);
  say(&np_font_small, 255, 179, T("hint"), SOFT);
  say(&np_font_small, 297, 179, T("find"), SOFT);
}

/* each card: its title, then its buttons; what each button leads to */
#define QUIT 6
#define NEXT 7
#define RETRY 8
static const char *const cards[5][4] = {
  {0},
  {T("Paused"), T("Resume"), T("How to play"), T("Quit game")},
  {T("How to play"), T("Play")},
  {T("Clear!"), T("Next level"), T("Quit game")},
  {T("Out of fish"), T("Try again"), T("Quit game")},
};
static const uint8_t buttons[5] = {0, 3, 1, 2, 2}, leads[5][3] = {
  {0}, {OV_NONE, OV_HELP, QUIT}, {OV_NONE}, {NEXT, QUIT}, {RETRY, QUIT}};

static void overlay(void) {
  char s[32];
  int nb = buttons[ov], extra = ov == OV_HELP ? 98 : 26, h = 50 + extra + 30 * nb, y = (SCREEN_H - h) / 2;
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, INK, 100);
  gfx_rrect(48, y - 2, 224, h + 4, 14, TEXT, 256);
  gfx_rrect(50, y, 220, h, 12, HEX(0xFFF8EC), 256);
  say(&np_font_title, 160, y + 32, cards[ov][0], ov == OV_WON ? HEX(0xE8920C) : TEXT);
  y += 44;
  if (ov == OV_HELP) {
    int b = gfx_paragraph(&np_font_small, 160, y + 6, 200, 13,
                          T("One cat in each row, each column and each color. Cats can't touch, not even at the corners."), TEXT, 256);
    gfx_paragraph(&np_font_small, 160, b + 6, 200, 13,
                  T("OK marks an X. OK twice (or EXE) places a cat. A wrong cat costs a fish; with all 3 gone, try again."), SOFT, 256);
  } else {
    char *e = s;
    for (const char *m = T("Level "); *m;) *e++ = *m++;
    num(e, ov == OV_WON ? sv.level - 1u : sv.level);
    say(&np_font_body, 160, y + 12, ov == OV_WON && sv.perfect ? T("Perfect!") : s, SOFT);
  }
  y += extra;
  for (int i = 0; i < nb; i++, y += 30) {
    bool on = menu == i;
    gfx_rrect(70, y, 180, 26, 13, on ? ORANGE : HEX(0xF3E6D3), 256);
    say(&np_font_body, 160, y + 18, ov == OV_MENU && i == 2 && quit_ok ? T("OK again to quit") : cards[ov][i + 1],
        on ? WHITE : TEXT);
  }
}

static void scene(void *ctx) {
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0xFAE4BA, 0xFAF7ED);
  board();
  panel();
  if (!hint.kind) say(&np_font_small, 160, 236, T("OK  X      OK OK  cat      DEL  undo      Back  menu"), SOFT);
  if (ov) overlay();
}

static void draw(void) {
  eadk_display_wait_for_vblank();
  gfx_render(scene, 0, 0, SCREEN_H);
}

/* ------------------------------------------------------------ the game */
static void save(void) {
  sv.magic = 'M', sv.version = 1;
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

/* a save this version could have written; anything else starts over */
static bool sane(void) {
  if (sv.magic != 'M' || sv.version != 1 || !sv.level || sv.puz >= NPUZ || sv.t > 7 || sv.lives > 3 || sv.strat > 7) return false;
  for (int p = 0; p < NPOOL; p++)
    if (sv.pc[p][0] > lv_pool[p][4] || sv.pc[p][1] > 7) return false;
  for (int k = 0; k < NLK; k++)
    if (sv.lkc[k] > lv_lk[k][4]) return false;
  int sp = special_at(sv.level);
  if (sv.size != (sp >= 0 ? lv_special[sp][1] : size_at(sv.level)) || !!sv.special != (sp >= 0)) return false;
  return true;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  now = (uint32_t)eadk_timing_millis();
  uint32_t len = 0, repeat_at = 0, last_ok = 0, ok_at = ~0u;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = d && len == sizeof sv ? d[i] : 0;
  if (!sane()) {
    for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = 0;
    sv.magic = 'M', sv.version = 1, sv.level = 1, sv.strat = 1;
    deal();
  }
  if (!sv.seed) sv.seed = eadk_random() | 1;
  load_puzzle();
  bool in_play = sv.flags & 2;
  for (int i = 0; i < n * n && in_play; i++) /* a cat only where the solution has one */
    if (sv.cell[i] > CAT || (sv.cell[i] == CAT && sol[i / n] != i % n)) in_play = false;
  if (!in_play) start_level();
  if (!sv.lives) sv.lives = 3;
  cr = cc = n / 2;
  layout();
  if (!(sv.flags & 1)) ov = OV_HELP, sv.flags |= 1;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  bool dirty_screen = true;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t k = eadk_keyboard_scan(), hit = k & ~held, arrows = k & 15 & ~(stale &= k);
    if (hit & 15) repeat_at = now + 250; /* an arrow held repeats */
    else if (arrows && (int32_t)(now - repeat_at) >= 0) hit |= arrows, repeat_at = now + 80;
    held = k;
    if (k & (K(eadk_key_home) | K(eadk_key_on_off))) break;
    int dx = !!(hit & K(eadk_key_right)) - !!(hit & K(eadk_key_left));
    int dy = !!(hit & K(eadk_key_down)) - !!(hit & K(eadk_key_up));
    bool ok = hit & K(eadk_key_ok), exe = hit & K(eadk_key_exe), back = hit & K(eadk_key_back);
    if (ov) {
      int i = menu = (menu + dy + buttons[ov]) % buttons[ov], to = (ok || exe) ? leads[ov][i] : ov;
      if (dy) quit_ok = 0;
      if (back && (ov == OV_MENU || ov == OV_HELP)) to = OV_NONE;
      if (to == QUIT && (ov != OV_MENU || quit_ok)) {
        if (ov == OV_MENU) sv.dirty = 1; /* (leaving a level part way, as the game counts it) */
        break;
      }
      if (to == QUIT) to = OV_MENU, quit_ok = 1; /* Quit game asks first */
      if (to != ov) menu = 0;
      if (to == NEXT || to == RETRY) {
        start_level();
        layout();
        to = OV_NONE;
      }
      ov = to;
    } else if (hint.kind) {
      if (ok || exe) apply_hint();
      else if (back || hit & K(eadk_key_shift)) hint.kind = H_NONE;
    } else if (back) {
      ov = OV_MENU, menu = quit_ok = 0;
    } else if (hit) {
      int i = (cr = NP_MAX(0, NP_MIN(n - 1, cr + dy))) * n + (cc = NP_MAX(0, NP_MIN(n - 1, cc + dx)));
      if ((dx || dy) && stroke >= 0 && k & K(eadk_key_ok)) set_mark(i, stroke, 0); /* OK held: a swipe */
      if (exe) place(i);
      else if (ok && now - last_ok < 400 && ok_at == (uint32_t)i) place(i), last_ok = 0, stroke = -1; /* the double tap */
      else if (ok) {
        stroke = sv.cell[i] == EMPTY ? MARK : sv.cell[i] == MARK ? EMPTY : -1;
        if (stroke >= 0) set_mark(i, stroke, 1);
        last_ok = now, ok_at = (uint32_t)i;
      }
      if (hit & K(eadk_key_backspace)) undo_move();
      if (hit & K(eadk_key_alpha)) locate();
      if (hit & K(eadk_key_shift)) {
        find_hint();
        if (!hint.kind) locate();
      }
    }
    if (!(k & K(eadk_key_ok))) stroke = -1;
    if (err_until && (int32_t)(now - err_until) >= 0) err_until = 0, dirty_screen = true;
    if (hit) save(), dirty_screen = true;
    if (dirty_screen || err_until) draw(), dirty_screen = false;
    eadk_timing_msleep(20);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
