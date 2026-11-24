/* Host tests: level data, physics, saves and storage safety, and the app
 * driven through its real input path. Extra modes:
 *   tests --replay <level> <file>   complete a level with a recorded input
 *   tests --shots <dir>             render every screen to PPM files
 *   tests --frames <level> <file> <dir> <x,...>
 *                                   play a replay, saving a frame at each x */
#include "app.h"
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ platform */

static uint8_t store[42 * 1024];
static uint16_t fb[GFX_W * GFX_H];
static unsigned frame_ticks;
uint8_t *platform_storage(size_t *size) { *size = sizeof(store); return store; }
void platform_storage_commit(void) {}
bool platform_init(void) { return true; }
void platform_close(void) {}
uint32_t platform_keys(void) { return 0; }
uint32_t platform_millis(void) { return 0; }
void platform_sleep(unsigned ms) { (void)ms; }
uint32_t platform_random(void) { return 12345; }
void platform_frame_begin(bool vsync) { (void)vsync; }
void platform_strip(int y, int h, const uint16_t *px) { memcpy(fb + y * GFX_W, px, (size_t)h * GFX_W * 2); }
void platform_frame_end(void) {}

static void render(void) {
  for (int s = 0; s < STRIPS; s++) {
    gfx_begin_strip(s);
    app_draw();
    platform_strip(s * STRIP_H, STRIP_H, gfx_strip);
  }
}
static void save_ppm(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
  for (int i = 0; i < GFX_W * GFX_H; i++) {
    uint8_t c[3] = {(uint8_t)c_r(fb[i]), (uint8_t)c_g(fb[i]), (uint8_t)c_b(fb[i])};
    fwrite(c, 1, 3, f);
  }
  fclose(f);
}
/* Runs the app for n physics ticks with keys held, 40 frames per second,
 * drawing every frame like the device does. */
static bool draw_frames = true;
static void run(uint32_t keys, int n) {
  for (int i = 0; i < n; i++) {
    app_tick(keys);
    if (++frame_ticks % 6 == 0) {
      app_frame(6.0f / ND_HZ);
      if (draw_frames) render();
    }
  }
}
static void tap(uint32_t k) { run(k, 6); run(0, 6); }
static void settle(void) { run(0, 240); }
static void fresh_app(void) {
  memset(store, 0, sizeof(store));
  app_init();
  assert(app.screen == SCR_LOADING);
  run(0, 240 * 2 + 60);   /* loading screen, then the fade into the menu */
  assert(app.screen == SCR_MENU);
}

/* ------------------------------------------------------------ unit tests */

static void test_levels(void) {
  static Level L;
  static const uint16_t counts[LEVEL_COUNT] = {2272, 1530, 1509, 1402, 2126, 1691, 2767, 3563, 3852, 7506, 19006, 16771,
#if ND_EXTRA_LEVELS
                                               9092, 10094, 14581, 16540
#endif
  };
  for (unsigned i = 0; i < LEVEL_COUNT; i++) {
    assert(level_load_builtin(&L, i));
    assert(L.count == counts[i]);
    assert(level_valid(&L));
    unsigned coins = 0;
    assert(level_check_all(&L, &coins));
    assert(coins <= 3 && coins == L.coin_count);
  }
  assert(!level_load_builtin(&L, LEVEL_COUNT));
  uint8_t out[8];
  static const uint8_t bad[] = {0xff, 0xff, 0xff};
  assert(inflate_raw(out, sizeof(out), bad, sizeof(bad)) < 0);
}

static void test_physics(void) {
  static Level L;
  static Game a, b;
  static LObj objs[1];
  memset(&L, 0, sizeof(L));
  level_load_flat(&L, objs, 0);
  L.end_x = L.wall_x = 60000;
  game_start(&a, &L, false);
  float peak = 0;
  for (int i = 0; i < 240; i++) {
    game_step(&a, i < 4);
    if (a.p.y > peak) peak = a.p.y;
  }
  /* A GD cube jump peaks a little over two blocks above the ground. */
  assert(peak > 15 + 60 && peak < 15 + 90);
  assert(a.p.on_ground && !a.dead && a.jumps == 1);
  game_start(&a, &L, false);
  game_start(&b, &L, false);
  for (int i = 0; i < 2000; i++) {
    bool h = (i / 37) % 3 == 0;
    game_step(&a, h);
    game_step(&b, h);
  }
  assert(!memcmp(&a, &b, sizeof(a)));
  Checkpoint c;
  game_save_checkpoint(&a, &c);
  for (int i = 0; i < 100; i++) game_step(&a, i % 20 < 5);
  game_load_checkpoint(&a, &c);
  game_load_checkpoint(&b, &c);
  for (int i = 0; i < 300; i++) { game_step(&a, i % 30 < 8); game_step(&b, i % 30 < 8); }
  assert(!memcmp(&a.p, &b.p, sizeof(a.p)));
  /* a spike on the ground kills */
  objs[0] = (LObj){300, 15, OT_SPIKE, 0};
  level_load_flat(&L, objs, 1);
  game_start(&a, &L, false);
  for (int i = 0; i < 400 && !a.dead; i++) game_step(&a, false);
  assert(a.dead && a.death_obj == 0);
  /* landing on a block is safe */
  objs[0] = (LObj){300, 15, OT_BLOCK, 0};
  level_load_flat(&L, objs, 1);
  game_start(&a, &L, false);
  for (int i = 0; i < 400 && !a.dead; i++) game_step(&a, i >= 150 && i < 154);
  assert(!a.dead && a.p.x > 400);
}

static void test_storage(void) {
  static uint8_t arena[4096];
  memset(arena, 0, sizeof(arena));
  const uint8_t small[3] = {7, 8, 9};
  uint8_t prog[PROGRESS_BYTES];
  Progress p;
  progress_defaults(&p);
  p.lv[2].normal = 57;
  p.lv[2].attempts = 123456;
  p.jumps = 99;
  p.options = OPT_FPS | OPT_BAR;
  progress_encode(&p, prog);
  assert(storage_put(arena, sizeof(arena), "a.py", small, 3, 1));
  assert(storage_put(arena, sizeof(arena), "numdash.nds", prog, sizeof(prog), 1));
  assert(storage_put(arena, sizeof(arena), "b.py", small, 3, 1));
  size_t len;
  const uint8_t *bpy = storage_read(arena, sizeof(arena), "b.py", &len);
  assert(bpy && len == 3 && !memcmp(bpy, small, 3));
  /* rewriting with the same size leaves every other record in place */
  p.lv[2].normal = 58;
  progress_encode(&p, prog);
  assert(storage_put(arena, sizeof(arena), "numdash.nds", prog, sizeof(prog), 1));
  assert(storage_read(arena, sizeof(arena), "b.py", &len) == bpy);
  Progress q;
  const uint8_t *d = storage_read(arena, sizeof(arena), "numdash.nds", &len);
  assert(d && progress_decode(&q, d, len) && q.lv[2].normal == 58 && q.lv[2].attempts == 123456 && q.options == (OPT_FPS | OPT_BAR));
  /* a record that is not the last one never grows */
  uint32_t crc = nd_crc32(arena, sizeof(arena));
  uint8_t big[400] = {1};
  assert(!storage_put(arena, sizeof(arena), "numdash.nds", big, sizeof(big), 1));
  assert(crc == nd_crc32(arena, sizeof(arena)));
  /* but it can shrink in place (padded) */
  assert(storage_put(arena, sizeof(arena), "numdash.nds", small, 3, 1));
  assert(storage_read(arena, sizeof(arena), "b.py", &len) == bpy);
  d = storage_read(arena, sizeof(arena), "numdash.nds", &len);
  assert(d && len == PROGRESS_BYTES && d[0] == 7 && d[3] == 0);
  assert(!storage_remove_last(arena, sizeof(arena), "numdash.nds"));
  /* the last record is resized freely */
  assert(storage_put(arena, sizeof(arena), "c.ndl", big, sizeof(big), 512));
  assert(storage_put(arena, sizeof(arena), "c.ndl", big, 1000 > sizeof(big) ? sizeof(big) : 0, 512));
  static uint8_t huge[1500];
  assert(storage_put(arena, sizeof(arena), "c.ndl", huge, sizeof(huge), 512));
  assert(storage_read(arena, sizeof(arena), "b.py", &len) == bpy);
  assert(storage_read(arena, sizeof(arena), "c.ndl", &len) && len >= sizeof(huge));
  assert(storage_remove_last(arena, sizeof(arena), "c.ndl"));
  assert(!storage_read(arena, sizeof(arena), "c.ndl", &len));
  assert(storage_read(arena, sizeof(arena), "b.py", &len) == bpy);
  /* full storage */
  static uint8_t giant[4000];
  assert(!storage_put(arena, sizeof(arena), "d.ndl", giant, sizeof(giant), 1));
  /* malformed storage is rejected, never written */
  memset(arena, 0xff, 64);
  assert(!storage_put(arena, sizeof(arena), "numdash.nds", prog, sizeof(prog), 1));
  for (int j = 0; j < 3000; j++) {
    size_t size = (unsigned)(j * 7) % sizeof(arena);
    for (size_t i = 0; i < size; i++) arena[i] = (uint8_t)(i * 31 + j * 17 + (i >> 3));
    storage_read(arena, size, "numdash.nds", &len);
    storage_put(arena, size, "numdash.nds", prog, sizeof(prog), 1);
  }
  /* corrupted progress is rejected */
  progress_encode(&p, prog);
  for (int i = 0; i < PROGRESS_BYTES; i += 7) {
    prog[i] ^= 0x10;
    assert(!progress_decode(&q, prog, sizeof(prog)));
    prog[i] ^= 0x10;
  }
  assert(progress_decode(&q, prog, sizeof(prog)));
  /* a save from the 7-level release: the custom slots' stats move up */
  memset(prog, 0, sizeof(prog));
  memcpy(prog, "NDS1", 4);
  prog[8] = 1; prog[9] = 7; prog[10] = CUSTOM_SLOTS;
  prog[12 + 12 * 2] = 77;             /* level 3 */
  prog[12 + 12 * 8] = 42;             /* custom slot 2 */
  prog[12 + 12 * 10 + 11] = 8;        /* last played: custom slot 2 */
  uint32_t sum = nd_crc32(prog + 8, PROGRESS_BYTES - 8);
  for (int k = 0; k < 4; k++) prog[4 + k] = (uint8_t)(sum >> (8 * k));
  assert(progress_decode(&q, prog, sizeof(prog)));
  assert(q.lv[2].normal == 77 && q.lv[7].normal == 0 && q.lv[8].normal == 0);
  assert(q.lv[LEVEL_COUNT + 1].normal == 42 && q.last_level == LEVEL_COUNT + 1);
}

static void test_custom(void) {
  static LObj a[CUSTOM_MAX], b[CUSTOM_MAX];
  static uint8_t buf[16 + CUSTOM_MAX * 8];
  unsigned n = 0, x = 15;
  for (unsigned i = 0; i < 600; i++) {
    x += (i * 7) % 3 == 0 ? 30 : 0;
    a[n++] = (LObj){(uint16_t)x, (int16_t)(15 + (i % 9) * 30 - (i % 4 == 0 ? 13 : 0)), (uint8_t)(1 + i % (OT_LOBJ - 1)), (uint8_t)(i & 3)};
  }
  CustomMeta m = {(uint16_t)n, 9000, 3, 1, true}, m2;
  size_t len = custom_encode(a, n, &m, buf, sizeof(buf));
  assert(len > 16 && len < n * 5 + 16);
  assert(custom_decode(buf, len, b, CUSTOM_MAX, &m2));
  assert(m2.count == n && m2.theme == 3 && m2.flags == 1 && m2.end_x == 9000);
  assert(!memcmp(a, b, n * sizeof(LObj)));
  for (size_t i = 0; i < len; i += 5) {
    buf[i] ^= 4;
    assert(!custom_decode(buf, len, b, CUSTOM_MAX, &m2));
    buf[i] ^= 4;
  }
  assert(!custom_encode(a, n, &m, buf, 64));
}

/* Builds a save of the previous NumDash release (format NDASH003). */
static size_t legacy_save(uint8_t *out) {
  static const struct { int16_t x, y; uint16_t id; uint8_t rot; } objs[3] = {{15, 15, 1, 0}, {45, 15, 8, 0}, {75, 2, 35, 0}};
  size_t n = 16;
  memcpy(out, "NDASH003", 8);
  for (int i = 0; i < 10; i++) {
    out[n++] = (uint8_t)(i == 1 ? 64 : 0);
    out[n++] = (uint8_t)(i == 1 ? 100 : 0);
    out[n++] = (uint8_t)(i == 1 ? 2 : 0);
    uint32_t att = i == 1 ? 321 : 0;
    memcpy(out + n, &att, 4);
    n += 4;
  }
  out[n++] = 1; out[n++] = 1; out[n++] = 0; out[n++] = 0;
  for (int s = 0; s < 3; s++) {
    unsigned c = s == 0 ? 3 : 0;
    out[n] = (uint8_t)c; out[n + 1] = 0; out[n + 2] = 0x08; out[n + 3] = 0x07; out[n + 4] = 1; out[n + 5] = 128;
    n += 6;
    for (unsigned j = 0; j < c; j++) {
      memcpy(out + n, &objs[j].x, 2); memcpy(out + n + 2, &objs[j].y, 2); memcpy(out + n + 4, &objs[j].id, 2);
      out[n + 6] = objs[j].rot; out[n + 7] = 0;
      n += 8;
    }
  }
  uint32_t size = (uint32_t)n, crc = nd_crc32(out + 16, n - 16);
  memcpy(out + 8, &size, 4);
  memcpy(out + 12, &crc, 4);
  return n;
}

static void test_legacy(void) {
  static uint8_t old[512];
  size_t n = legacy_save(old);
  memset(store, 0, sizeof(store));
  assert(storage_put(store, sizeof(store), "numdash.ndd", old, n, 1));
  Progress p;
  CustomMeta metas[CUSTOM_SLOTS];
  assert(save_load_all(&p, metas));
  assert(p.lv[1].normal == 64 && p.lv[1].practice == 100 && p.lv[1].coins == 3 && p.lv[1].attempts == 321);
  assert(metas[0].exists && metas[0].count == 3 && !metas[1].exists);
  size_t len;
  assert(storage_read(store, sizeof(store), "numdash.nds", &len));
  assert(!storage_read(store, sizeof(store), "numdash.ndd", &len));   /* it was last: removed */
  static LObj objs[8];
  CustomMeta m;
  assert(save_read_custom(0, objs, 8, &m) && m.count == 3 && objs[1].type == OT_SPIKE && objs[2].type == OT_PAD_Y);
  /* the second load uses the new records */
  assert(save_load_all(&p, metas) && p.lv[1].attempts == 321);
}

/* ------------------------------------------------------------ app flow */

#if ND_EXTRA_LEVELS
/* NumDash.nwa and NumPlay's NumDash share numdash.nds: a save of the twelve levels comes back
   whole, the other levels' stats go to numdash.ndx, and numdash.nds stays as the twelve-level
   build writes it. */
static void test_extra_saves(void) {
  memset(store, 0, sizeof(store));
  /* a save of NumPlay's NumDash (layout 2, twelve levels, three custom slots) */
  uint8_t nds[PROGRESS_BYTES];
  memset(nds, 0, sizeof(nds));
  memcpy(nds, "NDS1", 4);
  nds[8] = 2; nds[9] = 12; nds[10] = CUSTOM_SLOTS;
  uint8_t *q = nds + 12;
  for (int i = 0; i < 12 + CUSTOM_SLOTS; i++, q += 11) {
    q[0] = (uint8_t)(i * 7 % 101); q[1] = (uint8_t)(i * 13 % 101); q[2] = (uint8_t)(i % 8);
    q[3] = (uint8_t)(i + 1); q[7] = (uint8_t)(2 * i + 1);
  }
  q[0] = 77; q[4] = 88; q[8] = 5; q[9] = 9; q[10] = OPT_BAR | OPT_FPS; q[11] = 13;   /* last played: custom slot 2 */
  uint32_t sum = nd_crc32(nds + 8, PROGRESS_BYTES - 8);
  for (int k = 0; k < 4; k++) nds[4 + k] = (uint8_t)(sum >> (8 * k));
  assert(storage_put(store, sizeof(store), "numdash.nds", nds, sizeof(nds), 1));
  static LObj objs[3] = {{30, 0, OT_BLOCK, 0}, {60, 0, OT_SPIKE, 0}, {90, 30, OT_PAD_Y, 0}};
  CustomMeta m = {3, 400, 2, 0, true};
  assert(save_write_custom(1, objs, 3, &m));
  Progress p;
  CustomMeta metas[CUSTOM_SLOTS];
  assert(save_load_all(&p, metas));
  for (int i = 0; i < 12 + CUSTOM_SLOTS; i++) {
    const LevelStat *s = &p.lv[i < 12 ? i : i - 12 + LEVEL_COUNT];
    assert(s->normal == i * 7 % 101 && s->practice == i * 13 % 101 && s->coins == i % 8 && s->attempts == (uint32_t)i + 1 &&
           s->jumps == (uint32_t)(2 * i + 1));
  }
  for (int i = 12; i < LEVEL_COUNT; i++) assert(!p.lv[i].normal && !p.lv[i].attempts);
  assert(p.attempts == 77 && p.jumps == 88 && p.color1 == 5 && p.color2 == 9 && p.last_level == LEVEL_COUNT + 1);
  assert(metas[1].exists && metas[1].count == 3 && !metas[0].exists);
  /* written back: numdash.nds byte for byte as it was, the new levels in numdash.ndx */
  p.lv[LEVEL_BASE + 2].normal = 64;
  p.lv[LEVEL_BASE + 2].coins = 5;
  p.lv[LEVEL_COUNT - 1].attempts = 4242;
  assert(save_write_progress(&p));
  size_t len;
  const uint8_t *d = storage_read(store, sizeof(store), "numdash.nds", &len);
  assert(d && len == PROGRESS_BYTES && !memcmp(d, nds, PROGRESS_BYTES));
  assert(storage_read(store, sizeof(store), "numdash.ndx", &len) && len == EXTRA_BYTES);
  assert(save_load_all(&p, metas) && p.lv[LEVEL_BASE + 2].normal == 64 && p.lv[LEVEL_BASE + 2].coins == 5 &&
         p.lv[LEVEL_COUNT - 1].attempts == 4242 && p.last_level == LEVEL_COUNT + 1 && metas[1].exists);
  /* the last level played is one of the new ones: numdash.nds says level 1 */
  p.last_level = LEVEL_COUNT - 1;
  assert(save_write_progress(&p));
  d = storage_read(store, sizeof(store), "numdash.nds", &len);
  assert(d[12 + (12 + CUSTOM_SLOTS) * 11 + 11] == 0);
  Progress r;
  assert(save_load_all(&r, metas) && r.last_level == LEVEL_COUNT - 1);
  /* NumPlay's NumDash played another level since: it wins */
  Progress b;
  assert(progress_decode(&b, d, len));
  b.last_level = 3;
  uint8_t nds2[PROGRESS_BYTES];
  progress_encode(&b, nds2);
  assert(storage_put(store, sizeof(store), "numdash.nds", nds2, sizeof(nds2), 1));
  assert(save_load_all(&r, metas) && r.last_level == 3 && r.lv[LEVEL_BASE + 2].normal == 64);
  /* a broken numdash.ndx is left out, the rest loads */
  uint8_t *x = (uint8_t *)(uintptr_t)storage_read(store, sizeof(store), "numdash.ndx", &len);
  x[20] ^= 1;
  assert(save_load_all(&r, metas) && r.lv[LEVEL_BASE + 2].normal == 0 && r.lv[1].attempts == 2);
}
#endif

static void test_app(void) {
  fresh_app();
  assert(app.screen == SCR_MENU && app.sel == 1);
  tap(K_OK);
  settle();
  assert(app.screen == SCR_SELECT);
  tap(K_RIGHT);
  tap(K_LEFT);
  tap(K_EXE);
  settle();
  assert(app.screen == SCR_PLAY && app.level == 0 && app.attempt == 1);
  run(0, 240 * 8);   /* no input: the first spike kills */
  assert(app.attempt >= 2 && progress.lv[0].attempts >= 2 && progress.lv[0].normal > 0);
  tap(K_BACK);
  assert(app.paused);
  run(0, 100);
  tap(K_BACK);
  assert(!app.paused);
  /* practice: checkpoint then restart from it */
  tap(K_BACK);
  app.sel = 0;
  tap(K_OK);
  assert(app.practice && !app.paused);
  run(0, 200);
  tap(K_CHECK);
  assert(app.checkpoint_count == 1);
  tap(K_ERASE);
  assert(app.checkpoint_count == 0);
  tap(K_BACK);
  app.sel = 2;   /* menu */
  tap(K_OK);
  settle();
  assert(app.screen == SCR_SELECT);
  size_t len;
  const uint8_t *d = storage_read(store, sizeof(store), "numdash.nds", &len);
  Progress p;
  assert(d && progress_decode(&p, d, len) && p.lv[0].attempts == progress.lv[0].attempts);
  tap(K_BACK);
  settle();
  assert(app.screen == SCR_MENU);
  /* quit dialog */
  tap(K_BACK);
  assert(app.dialog == DLG_QUIT);
  tap(K_BACK);
  assert(!app.dialog && app.running);
  /* settings popup toggles and saves */
  app.sel = 4;
  tap(K_OK);
  run(0, 60);
  uint8_t before = progress.options;
  tap(K_OK);
  assert(progress.options == (before ^ OPT_PERCENT));
  tap(K_BACK);
  d = storage_read(store, sizeof(store), "numdash.nds", &len);
  assert(d && progress_decode(&p, d, len) && p.options == progress.options);
}

static void test_editor(void) {
  fresh_app();
  app.sel = 2;
  tap(K_OK);
  settle();
  assert(app.screen == SCR_CREATOR);
  tap(K_OK);
  settle();
  assert(app.screen == SCR_EDITOR);
  /* the key guide shows the first time only */
  assert(!(progress.options & OPT_KEYS_SEEN));
  tap(K_RIGHT);
  assert(editor_object_count() == 0);
  tap(K_OK);
  assert(progress.options & OPT_KEYS_SEEN);
  for (int i = 0; i < 5; i++) { tap(K_RIGHT); tap(K_OK); }
  tap(K_PLUS);
  for (int i = 0; i < 6; i++) tap(K_PLUS);
  tap(K_RIGHT); tap(K_RIGHT); tap(K_OK);
  assert(editor_object_count() == 6);
  tap(K_UNDO);
  assert(editor_object_count() == 5);
  tap(K_ERASE);   /* nothing under the cursor now */
  tap(K_LEFT); tap(K_LEFT);
  tap(K_ERASE);
  assert(editor_object_count() == 4);
  tap(K_SAVE);
  CustomMeta m;
  static LObj objs[16];
  assert(save_read_custom(0, objs, 16, &m) && m.count == 4);
  tap(K_EXE);
  settle();
  assert(app.screen == SCR_PLAY && app.testing);
  run(K_OK, 240);
  tap(K_BACK);
  app.sel = 2;
  tap(K_OK);
  settle();
  assert(app.screen == SCR_EDITOR && editor_object_count() == 4);
  tap(K_BACK);
  settle();
  assert(app.screen == SCR_CREATOR && custom_meta[0].count == 4);
}

/* ------------------------------------------------------------ replay */

static int replay(int index, const char *path) {
  static Level L;
  static Game g;
  if (!level_load_builtin(&L, (unsigned)index)) return 2;
  FILE *f = fopen(path, "r");
  if (!f) return 2;
  static int ticks[8192], vals[8192];
  int n = 0, t, v;
  while (n < 8192 && fscanf(f, "input=%d,%d\n", &t, &v) == 2) { ticks[n] = t; vals[n++] = v; }
  fclose(f);
  game_start(&g, &L, false);
  bool held = false;
  for (int i = 1, pos = 0; i < 200 * ND_HZ && !g.dead && !g.complete; i++) {
    while (pos < n && ticks[pos] <= i) held = vals[pos++] != 0;
    game_step(&g, held);
    g.fx_count = 0;
  }
  printf("%s: %s at %.0f%% (x=%.0f)\n", L.name, g.complete ? "COMPLETE" : "FAILED", game_progress(&g), g.p.x);
  return g.complete ? 0 : 1;
}

/* ------------------------------------------------------------ screenshots */

static int shots(const char *dir) {
  fresh_app();
  progress.lv[0].normal = 64;
  progress.lv[0].practice = 100;
  progress.lv[0].coins = 5;
  progress.lv[1].normal = 12;
  run(0, 240 * 3);
  render(); save_ppm(dir, "01_menu");
  app.sel = 4; tap(K_OK); run(0, 120);
  render(); save_ppm(dir, "02_settings");
  tap(K_BACK); app.sel = 5; tap(K_OK); run(0, 120);
  render(); save_ppm(dir, "03_stats");
  tap(K_BACK); app.sel = 3; tap(K_OK); run(0, 120);
  render(); save_ppm(dir, "04_help");
  tap(K_BACK); app.sel = 1; tap(K_OK); settle();
  render(); save_ppm(dir, "05_select");
  tap(K_RIGHT); run(0, 30);
  render(); save_ppm(dir, "06_select_scroll");
  settle();
  for (int i = 9; i < LEVEL_COUNT; i++) {   /* the demon levels */
    char name[32];
    snprintf(name, sizeof(name), "06_select_%d", i + 1);
    app.select_page = app.select_scroll = i;
    settle();
    render(); save_ppm(dir, name);
  }
  app.select_page = app.select_scroll = 1;
  settle();
  tap(K_LEFT); settle();
  tap(K_OK); settle();
  render(); save_ppm(dir, "07_play_start");
  run(0, 240 * 2 + 150);
  render(); save_ppm(dir, "08_play_death");
  run(0, 240);
  render(); save_ppm(dir, "09_play_attempt2");
  tap(K_BACK); run(0, 60);
  render(); save_ppm(dir, "10_pause");
  tap(K_BACK);
  /* level complete: replay level 1 */
  FILE *f = fopen("tests/replays/level1.txt", "r");
  static int ticks[8192], vals[8192];
  int n = 0, t, v;
  while (f && n < 8192 && fscanf(f, "input=%d,%d\n", &t, &v) == 2) { ticks[n] = t; vals[n++] = v; }
  if (f) fclose(f);
  app.checkpoint_count = 0;
  app.practice = false;
  app.attempt = 0;
  app.g.dead = true;
  app.death_t = 10;
  run(0, 6);
  bool held = false;
  int pos = 0;
  draw_frames = false;
  for (int i = 1; i < 200 * ND_HZ && !app.g.complete; i++) {
    while (pos < n && ticks[pos] <= i) held = vals[pos++] != 0;
    run(held ? K_OK : 0, 1);
    if (i == 3000) { render(); save_ppm(dir, "11_play_mid"); }
    if (i == 12000) { render(); save_ppm(dir, "12_play_ship"); }
  }
  draw_frames = true;
  run(0, 120);
  render(); save_ppm(dir, "13_complete_rays");
  run(0, 240 * 2 - 120 + 60);
  render(); save_ppm(dir, "14_complete_title");
  run(0, 240 * 3);
  render(); save_ppm(dir, "15_complete_window");
  tap(K_RIGHT); tap(K_OK); settle(); settle();
  tap(K_BACK); settle();
  app.sel = 0; tap(K_OK); settle();
  render(); save_ppm(dir, "16_garage");
  tap(K_BACK); settle();
  app.sel = 2; tap(K_OK); settle();
  render(); save_ppm(dir, "17_creator");
  tap(K_OK); settle();
  for (int i = 0; i < 6; i++) { tap(K_RIGHT); tap(K_OK); }
  tap(K_UP); tap(K_PLUS); tap(K_PLUS); tap(K_PLUS); tap(K_PLUS); tap(K_PLUS); tap(K_PLUS); tap(K_PLUS);
  tap(K_RIGHT); tap(K_OK); tap(K_RIGHT);
  render(); save_ppm(dir, "18_editor");
  tap(K_PROPS); run(0, 30);
  render(); save_ppm(dir, "19_editor_settings");
  puts("shots written");
  return 0;
}

/* Plays a level through the app with a replay and saves a frame whenever
   the player passes one of the given x positions. */
static int frames(int index, const char *path, const char *dir, const char *list) {
  static int ticks[8192], vals[8192];
  static float xs[1024];
  static bool at_tick[1024];
  int nx = 0, n = 0, t, v;
  for (const char *c = list; *c && nx < 1024;) {
    /* "t1234": at that tick instead of an x position */
    at_tick[nx] = *c == 't';
    if (*c == 't') c++;
    xs[nx++] = strtof(c, NULL);
    while (*c && *c != ',') c++;
    if (*c) c++;
  }
  FILE *f = fopen(path, "r");
  if (!f) return 2;
  while (n < 8192 && fscanf(f, "input=%d,%d\n", &t, &v) == 2) { ticks[n] = t; vals[n++] = v; }
  fclose(f);
#ifdef ND_PROBE
  { extern int nd_noclip; if (getenv("NOCLIP")) nd_noclip = 1; }   /* scratch builds only */
#endif
  fresh_app();
  app.level = index;
  app.practice = false;
  app_go(SCR_PLAY);
  settle();
  if (app.screen != SCR_PLAY) return 2;
  /* restart the way a death does, so the replay's ticks line up */
  app.attempt = 0;
  app.g.dead = true;
  app.death_t = 10;
  run(0, 6);
  draw_frames = false;
  bool held = false;
  int pos = 0, next = 0;
  for (int i = 1; i < 200 * ND_HZ && !app.g.complete && !app.g.dead && next < nx; i++) {
    while (pos < n && ticks[pos] <= i) held = vals[pos++] != 0;
    run(held ? K_OK : 0, 1);
    if (at_tick[next] ? i >= (int)xs[next] : app.g.p.x >= xs[next] && frame_ticks % 6 == 0) {
      char name[32];
      snprintf(name, sizeof(name), at_tick[next] ? "t%05d" : "x%05d", (int)xs[next]);
      next++;
      render();
      save_ppm(dir, name);
      printf("%s camera %.0f %.0f\n", name, app.g.cam_x, app.g.cam_y);
    }
  }
  printf("%d frames, %s at x=%.0f\n", next, app.g.dead ? "dead" : "alive", app.g.p.x);
  return 0;
}

/* ------------------------------------------------------------ monkey */

/* Random input across every screen, drawing every frame, with storage that
 * is either empty or nearly full of other records. */
static int monkey(unsigned seed, long ticks) {
  static const uint32_t keys[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_EXE, K_BACK, K_TOOL, K_SHIFT, K_ERASE,
                                  K_SAVE, K_UNDO, K_CHECK, K_PLUS, K_MINUS, K_COPY, K_PROPS};
  uint32_t r = seed * 2654435761u + 1;
  memset(store, 0, sizeof(store));
  if (seed & 1) {
    /* fill all but a few hundred bytes with other apps' records */
    static uint8_t junk[2000];
    char name[16];
    for (int i = 0; ; i++) {
      snprintf(name, sizeof(name), "junk%d.py", i);
      if (!storage_put(store, sizeof(store) - 600, name, junk, sizeof(junk), 1)) break;
    }
  }
  app_init();
  uint32_t held = 0;
  unsigned restarts = 0;
  static long visits[8];
  memset(visits, 0, sizeof(visits));
  for (long t = 0; t < ticks; t++) {
    visits[app.screen & 7]++;
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    if (r % 40 == 0) held = 0;
    if (r % 23 == 0) held ^= keys[(r >> 8) % (sizeof(keys) / sizeof(keys[0]))];
    /* mostly hold jump while playing so levels progress */
    uint32_t k = held;
    if (app.screen == SCR_PLAY && (r >> 20) % 3 == 0) k |= K_OK;
    /* leave long play sessions through Home so every screen gets visited */
    static long in_play;
    in_play = app.screen == SCR_PLAY ? in_play + 1 : 0;
    if (in_play > 240 * 15) { k = K_HOME; in_play = 0; }
    run(k, 1);
    if (!app.running) { app_init(); restarts++; }
  }
  printf("monkey %u: %ld ticks, %u quits; ticks per screen: menu %ld select %ld play %ld garage %ld creator %ld editor %ld loading %ld\n",
         seed, ticks, restarts, visits[SCR_MENU], visits[SCR_SELECT], visits[SCR_PLAY], visits[SCR_GARAGE], visits[SCR_CREATOR],
         visits[SCR_EDITOR], visits[SCR_LOADING]);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 4 && !strcmp(argv[1], "--replay")) return replay(atoi(argv[2]), argv[3]);
  if (argc == 4 && !strcmp(argv[1], "--monkey")) return monkey((unsigned)atoi(argv[2]), atol(argv[3]));
  if (argc == 3 && !strcmp(argv[1], "--shots")) return shots(argv[2]);
  if (argc == 6 && !strcmp(argv[1], "--frames")) return frames(atoi(argv[2]), argv[3], argv[4], argv[5]);
  test_levels();
  test_physics();
  test_storage();
  test_custom();
  test_legacy();
#if ND_EXTRA_LEVELS
  test_extra_saves();
#endif
  test_app();
  test_editor();
  puts("PASS: levels, physics, checkpoints, storage safety, saves, legacy migration, menus, gameplay, editor");
  return 0;
}
