/* Level Devil on a computer, for tests and pictures (make host, make test).
 *
 *   play solve            every room played through with its keys (tests/solutions.txt): each must
 *                         reach the door without dying; a room with a walk instead ("L-R~ keys", where
 *                         no keys were found) must set off its traps without dying
 *   play search L R [D|-] [CHUNK] [X,Y] [KEYS]
 *                         finds keys through a room, for tests/solutions.txt (or, failing that, a walk:
 *                         the way that sets off the most of its traps): D to try the Down key too, the
 *                         keys held CHUNK steps at a time, to the place X,Y (the runner's middle) instead
 *                         of the door, after KEYS. PLAY_TRAPS=1 makes for the triggers first, then the
 *                         door; PLAY_DEPTH the most chunks (900); PLAY_DEBUG=1 shows the search
 *   play trace L R [KEYS] where the runner goes (every PLAY_EVERY steps; PLAY_EXACT=1 stops with the
 *                         keys; PLAY_OBJS=1 lists the objects at the end)
 *   play rooms DIR        a picture of every room as it starts (DIR/room_LL_R.ppm)
 *   play film L R DIR [KEYS]  the room played with its keys (or these), a picture every 2 steps
 *                         (DIR/f_NNNN.ppm)
 *   play screens DIR      the title, the map, the pause and the end
 */
#define main ld_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t fb[SCREEN_H][SCREEN_W];
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *p) {
  for (int j = 0; j < r.height; j++)
    for (int i = 0; i < r.width; i++) fb[r.y + j][r.x + i] = p[j * r.width + i];
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (int j = 0; j < r.height; j++)
    for (int i = 0; i < r.width; i++) fb[r.y + j][r.x + i] = c;
}
bool eadk_display_wait_for_vblank(void) { return true; }
uint64_t eadk_keyboard_scan(void) { return 0; }
uint64_t eadk_timing_millis(void) { return 0; }
void eadk_timing_msleep(uint32_t ms) { (void)ms; }
uint32_t eadk_random(void) { return 0x9E3779B9u; }
void *np_alloc(uint32_t size) { return malloc(size); }

/* tests/solutions.txt: a line a room, "LEVEL-ROOM: keys"; "R40 RJ12 N30": the keys held (L R J D, or
   N for none) for so many steps; "LEVEL-ROOM~ keys" for a walk */
static char *solutions[ROOM_COUNT], walk[ROOM_COUNT];
static void read_solutions(void) {
  static char text[1 << 17];
  FILE *f = fopen("tests/solutions.txt", "r");
  size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
  if (f) fclose(f);
  text[n] = 0;
  for (char *line = strtok(text, "\n"); line; line = strtok(0, "\n")) {
    int l, r, at = 0;
    char c = 0;
    if (line[0] == '#' || sscanf(line, "%d-%d%c%n", &l, &r, &c, &at) < 3 || (c != ':' && c != '~')) continue;
    int i = (l - 1) * ROOMS + r - 1;
    if (i >= 0 && i < ROOM_COUNT) solutions[i] = line + at, walk[i] = c == '~';
  }
  for (int i = 0; i < ROOM_COUNT; i++)
    if (!solutions[i]) solutions[i] = "";
}

static void shot(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) exit(2);
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t c = fb[y][x];
      fputc((c >> 11) * 255 / 31, f), fputc((c >> 5 & 63) * 255 / 63, f), fputc((c & 31) * 255 / 31, f);
    }
  fclose(f);
}
static void draw(void) { gfx_render(play_scene, 0, 0, SCREEN_H); }

static void setup(int i) {
  gfx_buf = strip, seed = 1;
  scr = SC_PLAY, ov = 0, level = i / ROOMS, rnum = i % ROOMS;
  enter();
  jaw = J_OPEN;
}
static int keys_of(char c) {
  return c == 'L' ? I_LEFT : c == 'R' ? I_RIGHT : c == 'J' ? I_UP : c == 'D' ? I_DOWN : 0;
}

/* plays keys ("R30 RJ12 N8"); 1 the door reached, 0 dead, -1 neither */
static int exact; /* (no waiting after the last keys) */
static int play(const char *s, const char *film, int trace) {
  int t = 0;
  char path[256];
  for (;;) {
    while (*s == ' ') s++;
    int in = 0, n = 300; /* (after the last keys, nothing for a while) */
    if (!*s && exact) return -1;
    for (; *s >= 'A' && *s <= 'Z'; s++) in |= keys_of(*s);
    if (*s) {
      char *e;
      n = (int)strtol(s, &e, 10);
      if (e == s) return -1;
      s = e;
    }
    while (n--) {
      step(in), t++;
      if (trace && (!(t % trace) || dead || won))
        printf("%4d x %6.1f y %6.1f vx %5.1f vy %5.1f size %dx%d%s%s%s\n", t, hx / 256.0, hy / 256.0, vx * 60 / 256.0,
               vy * 60 / 256.0, hw, hh, ground ? " ground" : "", dead ? " DEAD" : "", won ? " DOOR" : "");
      if (film && !(t & 1)) {
        draw();
        snprintf(path, sizeof path, "%s/f_%04d.ppm", film, t / 2);
        shot(path);
      }
      if ((won || dead) && film) /* the runner in pieces, or the door shutting, a little longer */
        for (int k = 0; k < 40; k++) {
          step(0), t++;
          if (!(t & 1)) {
            draw();
            snprintf(path, sizeof path, "%s/f_%04d.ppm", film, t / 2);
            shot(path);
          }
        }
      if (won || dead) return won ? 1 : 0;
    }
    if (!*s) return -1;
  }
}

/* ---- finding keys: a beam search over the keys held 4 steps at a time, the states nearest the door
   (or the room's next waypoint) kept at each depth */
static struct {
  void *p;
  size_t n;
} vars[] = {
#define V(x) {&(x), sizeof(x)}
  V(ob), V(st), V(rule), V(no), V(ns), V(nr), V(gcol), V(cnt), V(at_), V(hx), V(hy), V(vx), V(vy), V(hw), V(hh),
  V(hsx), V(hsy), V(tsx), V(tsy), V(hgl), V(shape), V(run), V(jumpv), V(maxup), V(maxdown), V(back), V(downjump), V(dir),
  V(slide), V(pushx), V(gx), V(gy), V(keyev), V(face), V(ground), V(coyote), V(jumping), V(anim), V(dead), V(won),
  V(frozen), V(born), V(started), V(flapped), V(held_in), V(frame), V(shake), V(shake_n), V(modes), V(waiting),
};
#define NV (int)(sizeof vars / sizeof vars[0])
static size_t snap_size(void) {
  size_t n = 0;
  for (int i = 0; i < NV; i++) n += vars[i].n;
  return n;
}
static void take(uint8_t *s) {
  for (int i = 0; i < NV; i++) memcpy(s, vars[i].p, vars[i].n), s += vars[i].n;
}
static void give(const uint8_t *s) {
  for (int i = 0; i < NV; i++) memcpy(vars[i].p, s, vars[i].n), s += vars[i].n;
}

static int goal_x, goal_y, have_goal;
static int to_traps; /* (searching for a walk: towards the triggers not yet touched) */
static int score(void) { /* lower is better: the distance to the door (or the waypoint), in pixels */
  int gxp = goal_x, gyp = goal_y, best = 1 << 30;
  if (to_traps) {
    int n = 0;
    for (int e = 1; e < EV_MOVE; e++) n += cnt[e] > 0;
    for (int k = 0; k < no; k++)
      if ((ob[k].kind & 15) == K_TRIG && !(ob[k].f & (F_USED | F_GHOST))) {
        int d = iabs((ob[k].x >> 8) - (hx >> 8) - hw / 2) + iabs((ob[k].y >> 8) - (hy >> 8) - hh / 2);
        if (d < best) best = d;
      }
    if (best < 1 << 30) return best - n * 1000 + 8000;
  }
  if (!have_goal)
    for (int k = 0; k < no; k++)
      if ((ob[k].kind & 15) == K_DOOR && !(ob[k].f & (F_OFF | F_GHOST))) {
        int dx = (ob[k].x >> 8) - (hx >> 8) - hw / 2, dy = (ob[k].y >> 8) - (hy >> 8) - hh / 2;
        int d = iabs(dx) + iabs(dy);
        if (d < best) best = d, gxp = ob[k].x >> 8, gyp = ob[k].y >> 8;
      }
  int dx = iabs(gxp - (hx >> 8) - hw / 2), dy = iabs(gyp - (hy >> 8) - hh / 2);
  if (modes & R_WRAP) dx = imin(dx, 424 - dx), dy = imin(dy, 242 - dy); /* (the short way, round the screen) */
  return dx + dy;
}

static const int *sort_sc;
static int by_score(const void *a, const void *b) {
  int x = sort_sc[*(const int *)a], y = sort_sc[*(const int *)b];
  return x != y ? (x > y) - (x < y) : *(const int *)a - *(const int *)b;
}

#define BEAM 1000
#define DEPTH 900
#define CELL_MAX 4
static const char *prefix; /* keys played before the search */
static void write_keys(char *out, const uint8_t *p, int d, const uint8_t *moves, int chunk) {
  char *o = out + sprintf(out, "%s%s", prefix ? prefix : "", prefix ? " " : "");
  for (int a = 0; a <= d;) {
    int b = a;
    while (b <= d && p[b] == p[a]) b++;
    int in = moves[p[a]];
    o += sprintf(o, "%s%s%s%s%s%d ", in & I_LEFT ? "L" : "", in & I_RIGHT ? "R" : "", in & I_UP ? "J" : "",
                 in & I_DOWN ? "D" : "", in ? "" : "N", chunk * (b - a));
    a = b;
  }
  o[-1] = 0;
}
static int events_set_off(void) { /* (the triggers, and the jump key once armed) */
  int n = 0;
  for (int e = 1; e < EV_MOVE; e++) n += cnt[e] > 0;
  return n;
}
static int search(int i, int nmoves, const uint8_t *moves, int chunk, char *out, int depth_max) {
  size_t sz = snap_size();
  uint8_t *cur = malloc(sz * BEAM), *nxt = malloc(sz * BEAM * 9), *path = malloc((size_t)BEAM * DEPTH),
          *npath = malloc((size_t)BEAM * DEPTH * 9);
  int *sc = malloc(sizeof(int) * BEAM * 9), *ord = malloc(sizeof(int) * BEAM * 9);
  uint64_t *keyv = malloc(8 * BEAM * 9);
  uint16_t *cellv = malloc(2 * BEAM * 9);
  int ncur = 1, found = 0, best_q = -(1 << 30), best_d = -1;
  static uint8_t best[DEPTH]; /* (the walk: the way that sets off the most events, then nearest the door) */
  setup(i);
  if (prefix) exact = 1, play(prefix, 0, 0), exact = 0;
  take(cur);
  for (int d = 0; d < depth_max && d < DEPTH && !found; d++) {
    int nn = 0;
    for (int c = 0; c < ncur && !found; c++)
      for (int m = 0; m < nmoves; m++) {
        give(cur + sz * c);
        for (int k = 0; k < chunk && !dead && !won; k++) step(moves[m]);
        if (dead) continue;
        memcpy(npath + (size_t)nn * DEPTH, path + (size_t)c * DEPTH, (size_t)d);
        npath[(size_t)nn * DEPTH + d] = (uint8_t)m;
        if (won || (have_goal && score() < 6 && (ground || frozen))) { /* the keys (to the door, or the waypoint) */
          write_keys(out, npath + (size_t)nn * DEPTH, d, moves, chunk);
          found = 1;
          break;
        }
        int q = events_set_off() * 100000 - score();
        if (q > best_q) best_q = q, best_d = d, memcpy(best, npath + (size_t)nn * DEPTH, (size_t)d + 1);
        take(nxt + sz * nn);
        uint64_t key = (uint64_t)(hx >> 9) * 1000003u ^ (uint64_t)(hy >> 9) * 9176u ^ (uint64_t)((vx >> 6) + 512) * 131u ^
                       (uint64_t)((vy >> 6) + 512) * 7u ^ (uint64_t)ground << 40 ^ (uint64_t)(gy < 0) << 41;
        for (int e = 0; e < EVENTS; e++) /* (and how long ago each event went off) */
          key = key * 31 + cnt[e] + (cnt[e] ? (uint64_t)imin((uint16_t)(frame - at_[e]), 600) / 8 * 7 : 0);
        key = key * 31 + (uint64_t)frozen + (uint64_t)born * 64;
        keyv[nn] = key;
        uint32_t ev = 0;
        for (int e = 1; e < EVENTS; e++) ev = ev * 3 + cnt[e];
        uint32_t cell = ((uint32_t)(((hx >> 12) & 63) | ((hy >> 12) & 31) << 6 | (vx > 0 ? 1 : vx < 0 ? 2 : 0) << 11 |
                                    (vy > 0 ? 1 : vy < 0 ? 2 : 0) << 13) ^ ev * 40503u ^ (uint32_t)(frozen >> 2) << 15) * 2654435761u;
        cellv[nn] = (uint16_t)(cell >> 16);
        sc[nn] = score() * 4;
        ord[nn] = nn;
        nn++;
      }
    if (found || !nn) {
      if (!found) fprintf(stderr, "(all dead after %d steps)\n", d * chunk);
      break;
    }
    /* the best, one of each state */
    sort_sc = sc;
    qsort(ord, (size_t)nn, sizeof *ord, by_score);
    ncur = 0;
    static uint64_t seen[1 << 15];
    static uint8_t per_cell[1 << 16];
    memset(seen, 0, sizeof seen), memset(per_cell, 0, sizeof per_cell);
    for (int a = 0; a < nn && ncur < BEAM; a++) {
      int q = ord[a];
      uint64_t k = keyv[q] | 1;
      uint32_t h = (uint32_t)(k * 0x9E3779B97F4A7C15u >> 49);
      while (seen[h] && seen[h] != k) h = (h + 1) & ((1 << 15) - 1);
      if (seen[h] || per_cell[cellv[q]] >= CELL_MAX) continue; /* (a few states a place, for variety) */
      seen[h] = k, per_cell[cellv[q]]++;
      memcpy(cur + sz * ncur, nxt + sz * q, sz);
      memcpy(path + (size_t)ncur * DEPTH, npath + (size_t)q * DEPTH, (size_t)d + 1);
      ncur++;
    }
    if (getenv("PLAY_DEBUG")) {
      fprintf(stderr, "depth %d: %d states, %d kept:", d, nn, ncur);
      for (int c = 0; c < ncur; c++) {
        give(cur + sz * c);
        fprintf(stderr, " %d,%d%s%d", (int)(hx >> 8), (int)(hy >> 8), frozen ? "f" : "", frozen);
      }
      fprintf(stderr, "\n");
    }
  }
  free(cur), free(nxt), free(path), free(npath), free(sc), free(ord), free(keyv), free(cellv);
  if (!found && best_d >= 0) write_keys(out, best, best_d, moves, chunk);
  return found ? 1 : best_d >= 0 ? -1 : 0;
}

int main(int argc, char **argv) {
  char path[256];
  static char out[1 << 16];
  read_solutions();
  const char *cmd = argc > 1 ? argv[1] : "";
  int i = argc > 3 ? (atoi(argv[2]) - 1) * ROOMS + atoi(argv[3]) - 1 : 0;
  if (i < 0 || i >= ROOM_COUNT) return 2;
  if (!strcmp(cmd, "search") && argc > 3) {
    static const uint8_t moves[] = {I_RIGHT, I_RIGHT | I_UP, 0, I_LEFT, I_LEFT | I_UP, I_UP, I_DOWN, I_RIGHT | I_DOWN,
                                    I_LEFT | I_DOWN};
    int n = argc > 4 && strchr(argv[4], 'D') ? 9 : 6, chunk = argc > 5 ? atoi(argv[5]) : 4;
    if (argc > 6) have_goal = sscanf(argv[6], "%d,%d", &goal_x, &goal_y) == 2;
    if (argc > 7) prefix = argv[7];
    to_traps = getenv("PLAY_TRAPS") != 0;
    int got = search(i, n, moves, chunk, out, getenv("PLAY_DEPTH") ? atoi(getenv("PLAY_DEPTH")) : 900);
    if (got) printf("%d-%d%c %s\n", i / ROOMS + 1, i % ROOMS + 1, got > 0 ? ':' : '~', out);
    else printf("no way found\n");
    return got > 0 ? 0 : 1;
  }
  if (!strcmp(cmd, "trace") && argc > 3) {
    setup(i);
    exact = getenv("PLAY_EXACT") || (argc <= 4 && walk[i]);
    int got = play(argc > 4 ? argv[4] : solutions[i], 0, getenv("PLAY_EVERY") ? atoi(getenv("PLAY_EVERY")) : 4);
    fprintf(stderr, "%d events set off\n", events_set_off());
    if (getenv("PLAY_OBJS"))
      for (int k = 0; k < no; k++)
        fprintf(stderr, "%d: kind %d flags %d at %d,%d size %d,%d\n", k, ob[k].kind & 15, ob[k].f, (int)(ob[k].x >> 8),
                (int)(ob[k].y >> 8), ob[k].w / 16, ob[k].h / 16);
    printf("%d\n", got);
    return 0;
  }
  if (!strcmp(cmd, "film") && argc > 4) {
    setup(i);
    return play(argc > 5 ? argv[5] : solutions[i], argv[4], 0) == 1 ? 0 : 1;
  }
  if (!strcmp(cmd, "rooms") && argc > 2) {
    for (int r = 0; r < ROOM_COUNT; r++) {
      setup(r);
      for (int k = 0; k < 40; k++) step(0);
      draw();
      snprintf(path, sizeof path, "%s/room_%02d_%d.ppm", argv[2], r / ROOMS + 1, r % ROOMS + 1);
      shot(path);
    }
    return 0;
  }
  if (!strcmp(cmd, "screens") && argc > 2) { /* the title, the map, the pause and the end */
    static const struct {
      int scr, ov, sel, jaw;
      const char *name;
    } screens[] = {{SC_TITLE, OV_NONE, 0, J_OPEN, "title"}, {SC_TITLE, OV_NONE, 0, J_SHUT, "jaws"},
                   {SC_MAP, OV_NONE, 6, J_OPEN, "map"},     {SC_MAP, OV_NONE, 13, J_OPEN, "map_right"},
                   {SC_PLAY, OV_PAUSE, 1, J_SHUT, "pause"}, {SC_PLAY, OV_QUIT, 1, J_SHUT, "quit"},
                   {SC_END, OV_NONE, 0, J_OPEN, "end"}};
    sv.open = 15, sv.done = 0x3FFF, sv.deaths = 173;
    for (int l = 0; l < LEVELS; l++) sv.best[l] = (uint16_t)(l * 3 % 7);
    for (unsigned k = 0; k < sizeof screens / sizeof screens[0]; k++) {
      setup(0);
      scr = screens[k].scr, ov = screens[k].ov, sel = screens[k].sel, jaw = screens[k].jaw;
      if (scr == SC_MAP) sv.sel = (uint16_t)sel, pan = pan_for(sel);
      credits_y = 100, menu_anim = 0;
      for (int s = 0; s < 60; s++) step(0);
      gfx_render(scr == SC_PLAY ? play_scene : scr == SC_MAP ? map_scene : scr == SC_END ? end_scene : title_scene, 0, 0,
                 SCREEN_H);
      snprintf(path, sizeof path, "%s/%s.ppm", argv[2], screens[k].name);
      shot(path);
    }
    return 0;
  }
  if (!strcmp(cmd, "solve")) {
    int bad = 0, walks = 0;
    for (int r = 0; r < ROOM_COUNT; r++) {
      setup(r);
      exact = walk[r];
      int got = play(solutions[r], 0, 0);
      exact = 0;
      if (walk[r] && got == -1 && events_set_off()) { /* (walked: its traps set off, not dead) */
        printf("level %d room %d: walked, %d of its events set off (no keys to the door yet)\n", r / ROOMS + 1,
               r % ROOMS + 1, events_set_off());
        walks++;
      } else if (got != 1) {
        printf("level %d room %d: %s at x %d y %d\n", r / ROOMS + 1, r % ROOMS + 1, got ? "stuck" : "dead",
               (int)(hx >> 8), (int)(hy >> 8));
        bad++;
      }
    }
    if (bad) printf("%d rooms fail\n", bad);
    else if (walks) printf("%d rooms cleared, %d walked\n", ROOM_COUNT - walks, walks);
    else printf("all %d rooms cleared\n", ROOM_COUNT);
    return !!bad;
  }
  return 2;
}
