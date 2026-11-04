#include "chess.h"
#include <eadk.h>
#include <string.h>

#ifdef __TICE__ /* the TI-84 CE: a 4 KB stack and 60 KB for static data */
#define LIST static /* the move lists of the search's callers (none calls itself) */
#define PLYS 20     /* how deep the search goes at most: its frames are on the stack */
#define TTN 1024
#else
#define LIST
#define PLYS MAXPLY
#define TTN 2048
#endif

Pos P;
Undo H[MAXGAME + MAXPLY];
int hp;
uint32_t nodes;

static uint64_t Z[16 * 64 + 25];
#define ZP(pc, s) Z[(pc) * 64 + (s)]
#define Zc (Z + 1024)
#define Zep (Z + 1040)
#define Zside Z[1048]

/* PeSTO piece-square tables (Ronald Friederich), mirrored across the
 * d/e files and halved to fit in int8. Rows run rank 8 -> rank 1. */
static const int8_t PST[12][32] = {
  {0,0,0,0,22,42,47,41,-6,8,21,24,-9,8,5,11,-13,2,0,7,-9,7,0,-2,-14,9,1,-9,0,0,0,0},
  {0,0,0,0,91,85,73,70,45,46,35,31,12,10,4,1,3,3,-3,-3,-1,2,-3,0,2,3,2,6,0,0,0,0},
  {-68,-26,-33,3,-22,-8,34,15,-1,33,42,37,3,9,22,23,-5,6,9,10,-10,4,7,7,-12,-17,2,-1,-32,-10,-21,-12},
  {-39,-25,-10,-15,-19,-8,-12,-3,-16,-10,0,2,-9,3,8,11,-9,0,8,10,-11,-6,-1,6,-21,-11,-7,-2,-23,-25,-10,-9},
  {-9,3,-31,-15,-18,9,10,4,-4,19,23,19,-1,3,14,22,0,6,6,15,3,8,11,7,1,12,9,2,-13,-10,-6,-8},
  {-9,-9,-5,-4,-5,-2,-1,-4,2,-2,2,-1,0,3,6,6,-4,0,6,7,-7,-2,3,6,-10,-8,-4,1,-10,-3,-10,-3},
  {19,18,10,29,18,15,31,36,3,20,18,13,-11,-5,11,13,-15,-5,-5,2,-19,-7,-4,-3,-29,-5,-2,-2,-11,-12,2,8},
  {5,5,8,7,4,5,4,2,1,1,1,2,2,1,4,1,-2,-1,1,0,-5,-2,-4,-2,-2,-4,-2,-2,-7,2,-2,-1},
  {4,11,18,18,8,-3,13,-4,11,8,16,9,-6,-7,0,-4,-3,-6,-3,-3,-2,4,-2,-2,-8,-3,7,3,-13,-12,-8,-1},
  {3,8,10,14,-4,13,14,25,-3,6,11,24,10,20,16,26,1,17,13,20,-3,-4,8,4,-13,-15,-13,-8,-18,-12,-13,-12},
  {-13,6,-4,-18,0,-10,-6,-4,-8,12,2,-9,-13,-8,-9,-14,-25,-8,-18,-21,-10,-7,-13,-22,2,4,-6,-27,0,15,-4,-11},
  {-23,-8,-1,-7,0,10,13,9,6,15,17,9,-1,12,14,13,-7,1,11,13,-7,1,7,11,-11,-4,2,7,-24,-14,-9,-10},
};
static const int16_t VMG[7] = {0, 82, 337, 365, 477, 1025, 0};
static const int16_t VEG[7] = {0, 94, 281, 297, 512, 936, 0};
static const uint8_t PHASE[7] = {0, 0, 1, 1, 2, 4, 0};

static const int8_t KN[8] = {33, 31, 18, 14, -33, -31, -18, -14};
static const int8_t KG[8] = {1, -1, 16, -16, 17, 15, -17, -15};

#define SQ64(s) (((s) + ((s) & 7)) >> 1)
#define OFF(s) ((s) & 0x88)
#define LAST(s) ((unsigned)((s) - 16) >= 96)

static void upd(int sq, int pc, int sign) {
  int t = TYPE(pc), f = sq & 7, r = sq >> 4;
  if (f > 3) f = 7 - f;
  if (!COLOR(pc)) r = 7 - r;
  int i = r * 4 + f;
  int mg = VMG[t] + 2 * PST[t * 2 - 2][i], eg = VEG[t] + 2 * PST[t * 2 - 1][i];
  if (COLOR(pc) ^ (sign < 0)) mg = -mg, eg = -eg;
  P.mg += mg;
  P.eg += eg;
  P.phase += sign * PHASE[t];
  P.cnt[pc] += sign;
  P.hash ^= ZP(pc, SQ64(sq));
}

static void put(int sq, int pc) {
  P.b[sq] = pc;
  upd(sq, pc, 1);
  if (TYPE(pc) == KING) P.king[COLOR(pc)] = sq;
}

static void del(int sq) {
  upd(sq, P.b[sq], -1);
  P.b[sq] = 0;
}

static uint64_t rng64(void) {
  static uint64_t s = 0x9E3779B97F4A7C15ull;
  s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
  return s * 0x2545F4914F6CDD1Dull;
}

void ch_init(void) {
  for (int i = 0; i < 16 * 64 + 25; i++) Z[i] = rng64();
}

void ch_reset(void) {
  static const uint8_t back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
  memset(&P, 0, sizeof P);
  hp = 0;
  for (int f = 0; f < 8; f++) {
    put(f, back[f]);
    put(16 + f, PAWN);
    put(0x60 + f, BLACK | PAWN);
    put(0x70 + f, BLACK | back[f]);
  }
  P.castle = 15;
  P.ep = NOSQ;
  P.hash ^= Zc[15];
}

static int attacked(int sq, int by) {
  int c = by << 3;
  for (int i = 15; i <= 17; i += 2) {
    int s = by ? sq + i : sq - i;
    if (!OFF(s) && P.b[s] == (c | PAWN)) return 1;
  }
  for (int i = 0; i < 8; i++) {
    int s = sq + KN[i];
    if (!OFF(s) && P.b[s] == (c | KNIGHT)) return 1;
    s = sq + KG[i];
    if (!OFF(s) && P.b[s] == (c | KING)) return 1;
    for (; !OFF(s); s += KG[i]) {
      int p = P.b[s];
      if (p) {
        if (COLOR(p) == by && (TYPE(p) == QUEEN || TYPE(p) == (i < 4 ? ROOK : BISHOP))) return 1;
        break;
      }
    }
  }
  return 0;
}

int in_check(void) { return attacked(P.king[P.side], !P.side); }

static int left_in_check(void) { return attacked(P.king[P.side ^ 1], P.side); }

static Move *addp(Move *ml, int f, int t, int caps) {
  if (!LAST(t)) *ml++ = MOVE(f, t, 0);
  else for (int p = 3; p >= (caps ? 3 : 0); p--) *ml++ = MOVE(f, t, p);
  return ml;
}

/* Pseudo-legal moves (castling is fully checked). caps: captures and queen promotions only. */
static int gen(Move *ml0, int caps) {
  Move *ml = ml0;
  int us = P.side;
  for (int sq = 0; sq < 120; sq++) {
    int p = P.b[sq];
    if (OFF(sq) || !p || COLOR(p) != us) continue;
    int t = TYPE(p);
    if (t == PAWN) {
      int fwd = us ? -16 : 16, s = sq + fwd;
      if (!P.b[s]) {
        if (LAST(s) || !caps) ml = addp(ml, sq, s, caps);
        if (!caps && ((sq >> 4) == (us ? 6 : 1)) && !P.b[s + fwd]) *ml++ = MOVE(sq, s + fwd, 0);
      }
      for (int d = -1; d <= 1; d += 2) {
        int c = s + d;
        if (!OFF(c) && ((P.b[c] && COLOR(P.b[c]) != us) || c == P.ep)) ml = addp(ml, sq, c, caps);
      }
      continue;
    }
    const int8_t *dir = t == KNIGHT ? KN : KG + (t == BISHOP) * 4;
    int nd = (t == BISHOP || t == ROOK) ? 4 : 8, slide = t != KNIGHT && t != KING;
    for (int i = 0; i < nd; i++) {
      for (int s = sq + dir[i]; !OFF(s); s += dir[i]) {
        int q = P.b[s];
        if (q) {
          if (COLOR(q) != us) *ml++ = MOVE(sq, s, 0);
          break;
        }
        if (!caps) *ml++ = MOVE(sq, s, 0);
        if (!slide) break;
      }
    }
  }
  if (!caps) {
    int k = us ? 0x74 : 0x04;
    if (P.king[us] == k && (P.castle & (us ? 12 : 3)) && !attacked(k, !us)) {
      if ((P.castle & (us ? 4 : 1)) && !P.b[k + 1] && !P.b[k + 2] && !attacked(k + 1, !us))
        *ml++ = MOVE(k, k + 2, 0);
      if ((P.castle & (us ? 8 : 2)) && !P.b[k - 1] && !P.b[k - 2] && !P.b[k - 3] && !attacked(k - 1, !us))
        *ml++ = MOVE(k, k - 2, 0);
    }
  }
  return ml - ml0;
}

static int crmask(int s) {
  static const uint8_t sq[6] = {0x00, 0x04, 0x07, 0x70, 0x74, 0x77}, mk[6] = {2, 3, 1, 8, 12, 4};
  for (int i = 0; i < 6; i++)
    if (s == sq[i]) return mk[i];
  return 0;
}

void make(Move m) {
  int f = MFROM(m), t = MTO(m), p = P.b[f], us = P.side;
  Undo *u = &H[hp++];
  u->hash = P.hash, u->m = m, u->pc = p, u->castle = P.castle, u->ep = P.ep, u->fifty = P.fifty;
  u->cap = P.b[t];
  P.fifty++;
  if (TYPE(p) == PAWN) {
    P.fifty = 0;
    if (t == P.ep) {
      int cs = t + (us ? 16 : -16);
      u->cap = P.b[cs];
      del(cs);
    }
  }
  if (P.b[t]) del(t), P.fifty = 0;
  del(f);
  if (TYPE(p) == PAWN && LAST(t)) p = (us << 3) | (MPROMO(m) + KNIGHT);
  put(t, p);
  if (TYPE(p) == KING && (t - f == 2 || f - t == 2)) {
    int rf = t > f ? f + 3 : f - 4;
    del(rf);
    put((f + t) >> 1, (us << 3) | ROOK);
  }
  P.hash ^= Zc[P.castle];
  P.castle &= ~(crmask(f) | crmask(t));
  P.hash ^= Zc[P.castle];
  if (P.ep != NOSQ) P.hash ^= Zep[P.ep & 7];
  P.ep = NOSQ;
  if (TYPE(p) == PAWN && (t - f == 32 || f - t == 32)) P.ep = (f + t) >> 1, P.hash ^= Zep[P.ep & 7];
  P.side ^= 1;
  P.hash ^= Zside;
}

void unmake(void) {
  Undo *u = &H[--hp];
  int m = u->m, f = MFROM(m), t = MTO(m), us = P.side ^= 1;
  if (m) {
    del(t);
    put(f, u->pc);
    if (u->cap) put(TYPE(u->pc) == PAWN && t == u->ep ? t + (us ? 16 : -16) : t, u->cap);
    if (TYPE(u->pc) == KING && (t - f == 2 || f - t == 2)) {
      del((f + t) >> 1);
      put(t > f ? f + 3 : f - 4, (us << 3) | ROOK);
    }
  }
  P.castle = u->castle, P.ep = u->ep, P.fifty = u->fifty, P.hash = u->hash;
}

static void make_null(void) {
  Undo *u = &H[hp++];
  u->hash = P.hash, u->m = 0, u->castle = P.castle, u->ep = P.ep, u->fifty = P.fifty;
  if (P.ep != NOSQ) P.hash ^= Zep[P.ep & 7];
  P.ep = NOSQ;
  P.side ^= 1;
  P.hash ^= Zside;
}

int legal(Move *out) {
  int n = gen(out, 0), k = 0;
  for (int i = 0; i < n; i++) {
    make(out[i]);
    if (!left_in_check()) out[k++] = out[i];
    unmake();
  }
  return k;
}

int is_legal(Move m) {
  Move ml[256];
  int n = legal(ml);
  while (n--)
    if (ml[n] == m) return 1;
  return 0;
}

int gives_check(Move m) {
  make(m);
  int c = in_check();
  unmake();
  return c;
}

static int reps(void) {
  int r = 0;
  for (int i = hp - 2; i >= 0 && i >= hp - P.fifty; i -= 2) r += H[i].hash == P.hash;
  return r;
}

static int insufficient(void) {
  uint8_t *c = P.cnt;
  if (c[PAWN] | c[ROOK] | c[QUEEN] | c[8 | PAWN] | c[8 | ROOK] | c[8 | QUEEN]) return 0;
  return c[KNIGHT] + c[BISHOP] + c[8 | KNIGHT] + c[8 | BISHOP] <= 1;
}

int status(void) {
  Move ml[256];
  if (!legal(ml)) return in_check() ? CHECKMATE : STALEMATE;
  if (P.fifty >= 100 || hp >= MAXGAME) return FIFTY;
  if (reps() >= 2) return REPETITION;
  if (insufficient()) return MATERIAL;
  return ONGOING;
}

void san(Move m, char *o) {
  int f = MFROM(m), t = MTO(m), p = P.b[f], ty = TYPE(p);
  Move ml[256];
  if (ty == KING && (t - f == 2 || f - t == 2)) {
    const char *c = t > f ? "O-O" : "O-O-O";
    while (*c) *o++ = *c++;
  } else {
    int cap = P.b[t] || (ty == PAWN && t == P.ep);
    if (ty != PAWN) {
      *o++ = " PNBRQK"[ty];
      int n = legal(ml), amb = 0, sf = 0, sr = 0;
      while (n--) {
        int mf = MFROM(ml[n]);
        if (MTO(ml[n]) == t && mf != f && P.b[mf] == p) {
          amb = 1;
          sf |= (mf & 7) == (f & 7);
          sr |= (mf >> 4) == (f >> 4);
        }
      }
      if (amb && (!sf || sr)) *o++ = 'a' + (f & 7);
      if (amb && sf) *o++ = '1' + (f >> 4);
    } else if (cap) {
      *o++ = 'a' + (f & 7);
    }
    if (cap) *o++ = 'x';
    *o++ = 'a' + (t & 7);
    *o++ = '1' + (t >> 4);
    if (ty == PAWN && LAST(t)) *o++ = '=', *o++ = "NBRQ"[MPROMO(m)];
  }
  make(m);
  if (in_check()) *o++ = legal(ml) ? '+' : '#';
  unmake();
  *o = 0;
}

/* ---------------------------------------------------------------- Eval */

int eval(void) {
  int ph = P.phase > 24 ? 24 : P.phase;
  int s = (P.mg * ph + P.eg * (24 - ph)) / 24;
  uint8_t *c = P.cnt;
  s += 30 * ((c[BISHOP] >= 2) - (c[8 | BISHOP] >= 2));
  for (int w = 0; w < 2; w++) {
    int sg = w ? -1 : 1, o = w << 3, x = 8 - o;
    if (s * sg <= 0) continue;
    if (!c[o | PAWN] && !c[o | ROOK] && !c[o | QUEEN] && c[o | KNIGHT] + c[o | BISHOP] <= 1) {
      s /= 8;
    } else if (!(c[x | PAWN] | c[x | KNIGHT] | c[x | BISHOP] | c[x | ROOK] | c[x | QUEEN])) {
      /* Mop-up: drive the lone king to the edge and bring ours closer. */
      int k = P.king[!w], K = P.king[w], kf = k & 7, kr = k >> 4;
      int cd = (kf < 4 ? 3 - kf : kf - 4) + (kr < 4 ? 3 - kr : kr - 4);
      int df = kf - (K & 7), dr = kr - (K >> 4);
      s += sg * (300 + cd * 20 + (14 - (df < 0 ? -df : df) - (dr < 0 ? -dr : dr)) * 8);
    }
  }
  return (P.side ? -s : s) + 8;
}

/* -------------------------------------------------------------- Search */

typedef struct {
  uint16_t key;
  Move m;
  int16_t s;
  uint8_t d, f;
} TTE;
enum { T_EXACT = 1, T_LOWER, T_UPPER };
static TTE tt[TTN];
static Move killers[MAXPLY][2];
static int16_t hist[16 * 64];
#define MSN 3000
static Move mstack[MSN];
static int16_t mscore[MSN];
static int msp;
static uint64_t deadline;
static int stopped;
int think_depth, think_score;

void ui_tick(void);

static int tick(void) {
  if ((++nodes & 1023) == 0) {
    ui_tick();
    if (eadk_timing_millis() > deadline) stopped = 1;
  }
  return stopped;
}

static void score_moves(Move *ml, int16_t *sc, int n, Move ttm, int ply) {
  for (int i = 0; i < n; i++) {
    Move m = ml[i];
    int f = MFROM(m), t = MTO(m), p = P.b[f], v = P.b[t], s;
    if (m == ttm) s = 30000;
    else if (v || (TYPE(p) == PAWN && t == P.ep)) s = 20000 + TYPE(v ? v : PAWN) * 16 - TYPE(p);
    else if (TYPE(p) == PAWN && LAST(t)) s = MPROMO(m) == 3 ? 19000 : -1000;
    else if (m == killers[ply][0]) s = 18000;
    else if (m == killers[ply][1]) s = 17990;
    else s = hist[p * 64 + SQ64(t)];
    sc[i] = s;
  }
}

static Move pick(Move *ml, int16_t *sc, int n, int i) {
  int b = i;
  for (int j = i + 1; j < n; j++)
    if (sc[j] > sc[b]) b = j;
  Move m = ml[b];
  int16_t s = sc[b];
  ml[b] = ml[i], sc[b] = sc[i], ml[i] = m, sc[i] = s;
  return m;
}

static int qs(int alpha, int beta, int ply) {
  if (tick()) return 0;
  int best = eval();
  if (best >= beta || ply >= PLYS - 1 || msp > MSN - 256) return best;
  if (best > alpha) alpha = best;
  Move *ml = mstack + msp;
  int16_t *sc = mscore + msp;
  int n = gen(ml, 1);
  msp += n;
  score_moves(ml, sc, n, 0, ply);
  for (int i = 0; i < n; i++) {
    Move m = pick(ml, sc, n, i);
    make(m);
    if (left_in_check()) {
      unmake();
      continue;
    }
    int s = -qs(-beta, -alpha, ply + 1);
    unmake();
    if (s > best) {
      best = s;
      if (s > alpha) {
        alpha = s;
        if (s >= beta) break;
      }
    }
  }
  msp -= n;
  return best;
}

int search(int depth, int alpha, int beta, int ply) {
  if (ply && (P.fifty >= 100 || reps())) return 0;
  int check = in_check();
  if (check) depth++;
  if (depth <= 0) return qs(alpha, beta, ply);
  if (tick()) return 0;
  if (ply >= PLYS - 1 || msp > MSN - 256) return eval();
  if (alpha < -MATE + ply) alpha = -MATE + ply;
  if (beta > MATE - ply - 1) beta = MATE - ply - 1;
  if (alpha >= beta) return alpha;

  TTE *e = &tt[P.hash & (TTN - 1)];
  uint16_t key = P.hash >> 48;
  Move ttm = 0;
  if (e->key == key) {
    ttm = e->m;
    if (ply && e->d >= depth) {
      int s = e->s;
      if (s > MATE - 200) s -= ply;
      else if (s < -MATE + 200) s += ply;
      if (e->f == T_EXACT || (e->f == T_LOWER && s >= beta) || (e->f == T_UPPER && s <= alpha)) return s;
    }
  }
  int pv = beta - alpha > 1, us = P.side << 3;
  if (!pv && !check && depth >= 3 && ply &&
      (P.cnt[us | KNIGHT] | P.cnt[us | BISHOP] | P.cnt[us | ROOK] | P.cnt[us | QUEEN]) && eval() >= beta) {
    make_null();
    int s = -search(depth - 3, -beta, -beta + 1, ply + 1);
    unmake();
    if (stopped) return 0;
    if (s >= beta) return s > MATE - 200 ? beta : s;
  }

  Move *ml = mstack + msp;
  int16_t *sc = mscore + msp;
  int n = gen(ml, 0);
  msp += n;
  score_moves(ml, sc, n, ttm, ply);
  int best = -INF, legalc = 0, a0 = alpha;
  Move bm = 0;
  for (int i = 0; i < n; i++) {
    Move m = pick(ml, sc, n, i);
    int p = P.b[MFROM(m)], t = MTO(m);
    int quiet = !P.b[t] && !(TYPE(p) == PAWN && (t == P.ep || LAST(t)));
    make(m);
    if (left_in_check()) {
      unmake();
      continue;
    }
    legalc++;
    int s, nd = depth - 1;
    if (legalc == 1) {
      s = -search(nd, -beta, -alpha, ply + 1);
    } else {
      int r = depth >= 3 && legalc > 3 && quiet && sc[i] < 17000 && !check && !in_check() ? 1 + (legalc > 8) : 0;
      s = -search(nd - r, -alpha - 1, -alpha, ply + 1);
      if (r && s > alpha) s = -search(nd, -alpha - 1, -alpha, ply + 1);
      if (s > alpha && s < beta) s = -search(nd, -beta, -alpha, ply + 1);
    }
    unmake();
    if (stopped) break;
    if (s > best) {
      best = s, bm = m;
      if (s > alpha) {
        alpha = s;
        if (s >= beta) {
          if (quiet) {
            if (killers[ply][0] != m) killers[ply][1] = killers[ply][0], killers[ply][0] = m;
            int16_t *h = &hist[p * 64 + SQ64(t)];
            *h = *h + depth * depth > 16000 ? 16000 : *h + depth * depth;
          }
          break;
        }
      }
    }
  }
  msp -= n;
  if (stopped) return 0;
  if (!legalc) return check ? -MATE + ply : 0;
  int s = best;
  if (s > MATE - 200) s += ply;
  else if (s < -MATE + 200) s -= ply;
  e->key = key, e->m = bm, e->s = s, e->d = depth;
  e->f = best >= beta ? T_LOWER : best > a0 ? T_EXACT : T_UPPER;
  return best;
}

static void limit(int ms) {
  deadline = eadk_timing_millis() + ms;
  stopped = 0;
  msp = 0;
}

Move think(int maxd, int ms, int *score) {
  LIST Move root[256];
  int n = legal(root), bs = 0;
  if (!n) return 0;
  limit(ms);
  nodes = 0;
  uint64_t t0 = eadk_timing_millis();
  memset(killers, 0, sizeof killers);
  for (int i = 0; i < 16 * 64; i++) hist[i] >>= 2;
  think_depth = 0;
  for (int d = 1; d <= maxd; d++) {
    int alpha = -INF, cur = -INF;
    for (int i = 0; i < n; i++) {
      make(root[i]);
      int s;
      if (!i) {
        s = -search(d - 1, -INF, INF, 1);
      } else {
        s = -search(d - 1, -alpha - 1, -alpha, 1);
        if (s > alpha && !stopped) s = -search(d - 1, -INF, -alpha, 1);
      }
      unmake();
      if (stopped) break;
      if (s > alpha) {
        Move m = root[i];
        memmove(root + 1, root, i * sizeof(Move));
        root[0] = m;
        alpha = cur = s;
      }
    }
    if (cur > -INF) bs = cur;
    if (stopped) break;
    think_depth = d;
    if (MATE - (bs < 0 ? -bs : bs) <= d) break;
    if ((eadk_timing_millis() - t0) * 2 > (uint64_t)ms) break;
  }
  if (score) *score = bs;
  think_score = bs;
  return root[0];
}

/* Score of one root move at a given depth, using a (alpha, beta) window. */
int score_move(Move m, int depth, int alpha, int beta) {
  make(m);
  int s = -search(depth - 1, -beta, -alpha, 1);
  unmake();
  return s;
}

static int noise(int n) { return n ? (int)(eadk_random() % (n + 1) + eadk_random() % (n + 1)) - n : 0; }

Move bot_move(int depth, int nz, int blunder, int ms) {
  LIST Move ml[256];
  int n = legal(ml), bs;
  if (n <= 1) return n ? ml[0] : 0;
  if ((int)(eadk_random() % 100) < blunder) return ml[eadk_random() % n];
  if (!nz && hp < 8) nz = 12, depth = depth < 4 ? depth : 4; /* opening variety */
  Move best = think(depth, nz ? 60000 : ms, &bs);
  if (!nz) return best;
  int d = think_depth ? think_depth : 1, bv = -INF;
  limit(60000);
  for (int i = 0; i < n; i++) {
    int s = ml[i] == best ? bs : score_move(ml[i], d, bs - 2 * nz, INF);
    s += noise(nz);
    if (s > bv) bv = s, best = ml[i];
  }
  return best;
}

/* ---------------------------------------------------------- Mate search */

static int all_mated(int n, int co);
static Move mate_first;

/* Can the side to move force mate in at most n moves? co: attacker only checks. */
int mate_in(int n, int co) {
  Move *ml = mstack + msp;
  int k = gen(ml, 0), found = 0;
  msp += k;
  for (int pass = 0; pass < 2 && !found; pass++) {
    if (pass && (co || n == 1)) break;
    for (int i = 0; i < k && !found; i++) {
      make(ml[i]);
      if (!left_in_check() && in_check() == !pass) found = all_mated(n - 1, co);
      unmake();
      if (found) mate_first = ml[i];
    }
  }
  msp -= k;
  return found;
}

/* Defender to move: does every reply lose to mate within n moves? */
static int all_mated(int n, int co) {
  Move *ml = mstack + msp;
  int k = gen(ml, 0), any = 0, ok = 1;
  msp += k;
  for (int i = 0; i < k && ok; i++) {
    make(ml[i]);
    if (!left_in_check()) {
      any = 1;
      if (!n || !mate_in(n, co)) ok = 0;
    }
    unmake();
  }
  msp -= k;
  return any ? ok : in_check();
}

int mated_within(int n) {
  msp = 0;
  return all_mated(n, 0);
}

int mate_len(int max, int co) {
  msp = 0;
  for (int n = 1; n <= max; n++)
    if (mate_in(n, co)) return n;
  return 0;
}

/* First move of a mate in at most n, or 0. */
Move mate_move(int n) {
  return mate_len(n, 1) || mate_len(n, 0) ? mate_first : 0;
}

/* Defender to move: the reply that postpones mate the longest. */
Move defend(int n) {
  LIST Move ml[256];
  int k = legal(ml), best = -1;
  Move bm = ml[0];
  for (int i = 0; i < k; i++) {
    make(ml[i]);
    int l = mate_len(n, 0);
    unmake();
    if (!l) l = 99;
    if (l > best || (l == best && eadk_random() % 3 == 0)) best = l, bm = ml[i];
  }
  return bm;
}

/* ------------------------------------------------------------- Puzzles */

Puzzle PZ;
#define PMARGIN 180

static int is_quiet(Move m) { return !P.b[MTO(m)] && !gives_check(m); }

/* Is m the only good move here (every other move scores at least PMARGIN less)? */
static int unique(Move b, int s) {
  LIST Move ml[256];
  int n = legal(ml), d = think_depth;
  for (int i = 0; i < n; i++)
    if (ml[i] != b && score_move(ml[i], d, s - PMARGIN - 1, s - PMARGIN) > s - PMARGIN) return 0;
  return 1;
}

static int tactic(void) {
  int s, r, k = 0, made = 0;
  LIST Move ml[256];
  Move b = think(5, 60000, &s);
  if (s < 200 || s > MATE - 200 || !unique(b, s)) return 0;
  int df = 1;
  while (df < 5 && think(df, 60000, &r) != b) df++;
  PZ.sol[k++] = b;
  make(b), made++;
  while (k < 5 && legal(ml)) {
    Move rep = think(4, 60000, &r);
    make(rep), made++;
    Move b2 = think(5, 60000, &r);
    if (r < 200 || r > MATE - 200 || !unique(b2, r)) break;
    PZ.sol[k++] = rep, PZ.sol[k++] = b2;
    make(b2), made++;
  }
  while (made--) unmake();
  PZ.nsol = k;
  PZ.mate = 0;
  PZ.rating = 450 + 150 * df + 120 * (k >> 1) + 150 * is_quiet(b) - 200 * (MTO(b) == MTO(H[hp - 1].m)) +
              eadk_random() % 80;
  return 1;
}

void puzzle_gen(int kind, int target) {
  for (int tries = 0;;) {
    ch_reset();
    int solver = eadk_random() & 1, prev = 0, s;
    for (int ply = 0; ply < 100; ply++) {
      LIST Move ml[256];
      if (!legal(ml) || P.fifty > 30) break;
      if (P.side == solver) {
        if (ply >= 8) {
          int n = mate_len(3, 1), ok = 0;
          if (n && (kind == 0 || kind == n) && !(n == 3 && mate_len(2, 0))) {
            PZ.sol[0] = mate_move(n);
            PZ.nsol = 1, PZ.mate = n;
            PZ.rating = 250 + n * 400 + 150 * is_quiet(PZ.sol[0]) + eadk_random() % 100;
            ok = 1;
          } else if (!n && (kind == 0 || kind == 4)) {
            think(2, 60000, &s);
            if (s >= 250 && s < MATE - 200 && s - prev >= 250) ok = tactic();
          }
          int dr = PZ.rating - target;
          if (ok && !(kind & 3) && (dr < 0 ? -dr : dr) > 200 + 60 * tries++) ok = 0;
          if (ok) {
            PZ.pre = H[hp - 1].m;
            unmake();
            PZ.start = P;
            return;
          }
        }
        ui_tick();
        make(bot_move(1, 40, 0, 60000));
      } else {
        Move m = eadk_random() % 8 ? bot_move(1, 110, 0, 60000) : (think(1, 60000, 0), ml[eadk_random() % legal(ml)]);
        prev = -think_score;
        make(m);
      }
    }
  }
}
