/* Cat Ninja on a computer, for tests and pictures (make host, make test).
 *   play test              the cat's physics against the original's numbers (spec/), the first levels played
 *                          through with keys, every level loaded and run, random keys, saves
 *   play shot DIR KEYS     pictures (DIR/<frame>.ppm) of a run: KEYS is "frame:keys,..." (l r u d j R b h o)
 *   play film DIR KEYS N   every 2nd frame of a run of N frames (DIR/<n>.ppm), for the GIF */
#define main catninja_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t screen_px[240][320];
static void (*on_frame)(void);
void np_push(int x, int y, int w, int h, const color_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) screen_px[y + j][x + i] = px[j * w + i];
  if (on_frame && y + h == 240) on_frame();
}
void *np_alloc(uint32_t n) {
  (void)n;
  return 0;
}
static uint64_t clock_ms;
static uint32_t (*script)(void);
static uint32_t frame_no, quit_at = 0xFFFFFFFFu;
uint32_t plat_keys(void) {
  frame_no++;
  if (frame_no >= quit_at) return PK_HOME;
  return script ? script() : 0;
}
uint32_t plat_millis(void) { return (uint32_t)(clock_ms += 16); }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
static uint32_t lcg_s = 1;
static uint32_t lcg(void) { return lcg_s = lcg_s * 1664525u + 1013904223u; }
uint32_t plat_random(void) { return lcg(); }
void plat_begin(void) {}
int plat_end(void) { return 0; }
static struct {
  char name[32];
  uint8_t data[4096];
  uint32_t len;
  bool on;
} files[4];
bool plat_save(const char *name, const void *data, uint32_t len) {
  int k = -1;
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) k = i;
  for (int i = 0; i < 4 && k < 0; i++)
    if (!files[i].on) k = i;
  if (k < 0 || len > sizeof files[k].data) return false;
  strcpy(files[k].name, name), memcpy(files[k].data, data, len), files[k].len = len, files[k].on = true;
  return true;
}
const uint8_t *plat_load(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}
static void files_clear(void) { memset(files, 0, sizeof files); }
static void file_drop(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) files[i].on = false;
}
static uint8_t *file_of(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}

static void ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = screen_px[y][x];
      uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                        (uint8_t)((c & 31) << 3 | ((c >> 2) & 7))};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static int fails;
#define CHECK(c, ...)                                                                                                  \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      fails++;                                                                                                         \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                      \
      printf(__VA_ARGS__);                                                                                             \
      printf("\n");                                                                                                    \
    }                                                                                                                  \
  } while (0)

/* ---------------------------------------------------------------- key scripts: "frame:keys,frame:keys" */
static struct {
  uint32_t at, keys;
} steps_[512];
static int n_steps_;
static uint32_t keys_of(const char *s) {
  uint32_t k = 0;
  for (; *s && *s != ','; s++) switch (*s) {
    case 'l': k |= PK_LEFT; break;
    case 'r': k |= PK_RIGHT; break;
    case 'u': k |= PK_UP; break;
    case 'd': k |= PK_DOWN; break;
    case 'j': k |= PK_SHIFT; break;
    case 'R': k |= PK_DEL; break;
    case 'b': k |= PK_BACK; break;
    case 'o': k |= PK_OK; break;
    case 'h': k |= PK_HOME; break;
    }
  return k;
}
static void parse_keys(const char *s) {
  n_steps_ = 0;
  while (*s && n_steps_ < 512) {
    steps_[n_steps_].at = (uint32_t)strtoul(s, (char **)&s, 10);
    if (*s == ':') s++;
    steps_[n_steps_].keys = keys_of(s);
    n_steps_++;
    while (*s && *s != ',') s++;
    if (*s == ',') s++;
  }
}
static int goto_level_at = -2;
static uint32_t scripted(void) {
  uint32_t k = 0;
  if (frame_no == 1 && goto_level_at >= -1) {
    extern void game_debug_goto(int lv);
    game_debug_goto(goto_level_at);
  }
  for (int i = 0; i < n_steps_; i++)
    if (steps_[i].at <= frame_no) k = steps_[i].keys;
  return k;
}
static const char *shot_dir;
static int film_every;
static void shot_frame(void) {
  char path[256];
  if (getenv("CN_TRACE")) printf("%u L%d x %.2f y %.2f rot %d wr %.1f rotating %d dead %d\n", frame_no, level, P.x, P.y, currentrotation, world_rot, rotating, P.dead);
  if (film_every) {
    if (frame_no % film_every) return;
    snprintf(path, sizeof path, "%s/%05u.ppm", shot_dir, frame_no / film_every);
  } else {
    for (int i = 0; i < n_steps_; i++)
      if (steps_[i].at == frame_no) goto take;
    return;
  take:
    snprintf(path, sizeof path, "%s/%05u.ppm", shot_dir, frame_no);
  }
  ppm(path);
}

/* A player for the first levels: it runs right, climbs shafts by jumping from wall to wall, and jumps the gaps
   the levels ask it to. Each frame it looks at the cat and the tiles about it, as a player looks at the screen. */
static uint32_t bot_keys, bot_held;
static int bot_dir = 1, bot_wait;
static bool bot_climb;
static bool solid_at(int x, int y) { return !walk_at(x, y); }
static void bot_frame(void) {
  uint32_t k = 0;
  int cx = P.coordx, cy = P.coordy;
  if (bot_wait > 0) bot_wait--;
  k |= bot_dir > 0 ? 1u << GK_RIGHT : 1u << GK_LEFT;
  if (!P.dead) {
    bool wall = bot_dir > 0 ? solid_at(cx + 1, cy) : solid_at(cx, cy) || solid_at(cx - 1, cy);
    if (P.onground && !wall) bot_climb = false;
    if (P.onground && wall && !bot_wait) k |= 1u << GK_UP, bot_wait = 4, bot_climb = true; /* a wall ahead: up it */
    else if (bot_climb && !P.onground && P.speedx == 0 && wall && !bot_wait) { /* against it: jump off, the other way */
      k |= 1u << GK_UP, bot_wait = 4;
      bot_dir = -bot_dir;
    } else if (!P.onground && !wall && bot_dir < 0 && P.gravity > 3 && level == 1) {
      bot_dir = 1; /* (over the top of the shaft: right again) */
    }
    /* a pit with spikes ahead: jump it */
    if (P.onground && bot_dir > 0 && walk_at(cx + 2, cy + 1)) {
      int y = cy + 1;
      while (y < gh && walk_at(cx + 2, y) && (grid[y * gw + cx + 2] & 0x7F) != 2) y++;
      if (y < gh && (grid[y * gw + cx + 2] & 0x7F) == 2) k |= 1u << GK_UP;
    }
    if (!P.onground && P.gravity > -6 && P.gravity < 0 && P.doublejump && bot_dir > 0 && level == 1 && cx > 100)
      k |= 1u << GK_UP; /* (the floating floors: double jump) */
  }
  uint32_t on = k & ~bot_held, off = bot_held & ~k;
  for (int i = 0; i < 6; i++) {
    if (off >> i & 1) key_release(i);
    if (on >> i & 1) key_press(i);
  }
  if (k >> GK_UP & 1) k &= ~(1u << GK_UP); /* (taps: let go at once) */
  bot_held = k & ~(1u << GK_UP);
  if (on >> GK_UP & 1) key_release(GK_UP);
  bot_keys = on;
  game_step();
}

/* the game's own steps, without the shell: a fresh game at a level's spawn */
static void at_level(int lv) {
  game_new();
  extern void test_goto(int lv);
  test_goto(lv);
}

int main(int argc, char **argv) {
  if (argc >= 4 && (!strcmp(argv[1], "shot") || !strcmp(argv[1], "film"))) {
    shot_dir = argv[2];
    parse_keys(argv[3]);
    film_every = !strcmp(argv[1], "film") ? 2 : 0;
    quit_at = argc >= 5 ? (uint32_t)atoi(argv[4]) : steps_[n_steps_ - 1].at + 2;
    script = scripted;
    on_frame = shot_frame;
    files_clear();
    if (argc >= 6 && !strchr(argv[5], ',')) { /* a level to start in */
      goto_level_at = atoi(argv[5]);
      save_t s;
      memset(&s, 0, sizeof s);
      s.magic = MAGIC, s.version = VERSION, s.flags = 1, s.level = -1, s.spawnlevel = -1;
      s.sum = fnv(&s, offsetof(save_t, sum));
      plat_save(SAVE_NAME, &s, sizeof s);
    } else if (argc >= 6) { /* a save to start from: level x y */
      int lv = 0, x = 0, y = 0;
      sscanf(argv[5], "%d,%d,%d", &lv, &x, &y);
      save_t s;
      memset(&s, 0, sizeof s);
      s.magic = MAGIC, s.version = VERSION, s.flags = 1, s.level = (int8_t)lv, s.x = (int16_t)x, s.y = (int16_t)y;
      s.spawnlevel = -1;
      s.sum = fnv(&s, offsetof(save_t, sum));
      plat_save(SAVE_NAME, &s, sizeof s);
    } else {
      save_t s;
      memset(&s, 0, sizeof s);
      s.magic = MAGIC, s.version = VERSION, s.flags = 1, s.level = -1, s.spawnlevel = -1;
      s.sum = fnv(&s, offsetof(save_t, sum));
      plat_save(SAVE_NAME, &s, sizeof s);
    }
    catninja_main();
    printf("frames %u, slowest %u ms\n", perf_frames, perf_max);
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "levels")) { /* every level's start, a second in */
    gfx_buf = strip;
    draw_init();
    for (int lv = -1; lv < 30; lv++) {
      game_new();
      extern void game_debug_goto(int lv);
      game_debug_goto(lv);
      for (int f = 0; f < 60; f++) game_step();
      draw_frame();
      gfx_render(scene, NULL, 0, 240);
      char path[256];
      snprintf(path, sizeof path, "%s/level%02d.ppm", argv[2], lv + 1);
      ppm(path);
    }
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "trace")) { /* trace LEVEL KEYS [frames]: the cat, frame by frame */
    gfx_buf = strip;
    draw_init();
    parse_keys(argv[3]);
    int frames = argc >= 5 ? atoi(argv[4]) : (int)steps_[n_steps_ - 1].at + 1;
    game_new();
    extern void game_debug_goto(int lv);
    game_debug_goto(atoi(argv[2]));
    uint32_t held = 0;
    for (frame_no = 1; (int)frame_no <= frames; frame_no++) {
      uint32_t g = game_keys_of(scripted()), on = g & ~held, off = held & ~g;
      for (int i = 0; i < 6; i++) {
        if (off >> i & 1) key_release(i);
        if (on >> i & 1) key_press(i);
      }
      held = g;
      game_step();
      printf("%4u L%d x %.2f y %.2f c %d,%d sx %.3f g %.3f %s%s%s rot %d\n", frame_no, level, P.x, P.y, P.coordx, P.coordy,
             P.speedx, P.gravity, P.onground ? "ground " : "", P.dead ? "dead " : "", P.hitwall ? "wall " : "",
             currentrotation);
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "bot")) { /* the climbing player, traced */
    gfx_buf = strip;
    draw_init();
    game_new();
    int frames = argc >= 3 ? atoi(argv[2]) : 2000;
    for (int f = 0; f < frames; f++) {
      bot_frame();
      printf("%4d L%d x %.2f y %.2f c %d,%d sx %.3f g %.3f %s%s keys %x\n", f, level, P.x, P.y, P.coordx, P.coordy, P.speedx,
             P.gravity, P.onground ? "ground " : "", P.dead ? "dead " : "", bot_keys);
    }
    return 0;
  }
  (void)at_level;
  (void)file_of;
  (void)file_drop;
  printf("%s\n", fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
