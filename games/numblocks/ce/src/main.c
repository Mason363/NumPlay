/* NumBlocks on the TI-84 Plus CE: the title, the worlds, and the game's loop (Minecraft's 20 ticks a second, the
 * picture as often as the CE can draw it). */
#include "nb.h"
#include "rs.h"
#include "draw.h"

extern int new_mode;
extern int xp_level, xp_points;
int xp_need(int l);
void gui_close(void);
bool gui_needs_world(void);
void gui_redraw(void);

static uint32_t rng = 0x12345678;
int rnd(int n) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return n > 0 ? (int)((rng >> 8) % (uint32_t)n) : 0;
}

void xp_add(int n) {
  xp_points += n;
  while (xp_points >= xp_need(xp_level)) xp_points -= xp_need(xp_level), xp_level++;
}

static bool in_game;
static uint32_t last_ms, tick_acc, ticks_since_save;
static int look_held;

#define TITLE_SEED 20151008u   /* the title's world */

static void loading(const char *what) {
  render_stale = 1;
  canvas_screen();
  dirt_background(0, SCREEN_H);
  text_center(SCREEN_W / 2, SCREEN_H / 2 - 4, what, C_WHITE);
  plat_frame();
}

static bool backdrop_ok;
static void title_backdrop(void) {
  /* the title's world: a view over the land, the same each time (kept as a picture, NBCETTL, once made) */
  backdrop_ok = true;
  world_time = 3000;
  unsigned len;
  const uint8_t *pic = plat_load("NBCETTL", &len);
  if (pic && len == RW * RH + RH) {
    render_sky_palette();
    render_picture(pic);
    compose(false, false);
    return;
  }
  loading("");
  world_new(TITLE_SEED);
  int x, z;
  gen_spawn(&x, &z);
  memset(&pl, 0, sizeof pl);
  pl.hit_face = -1;
  world_center(x, 40, z, true);
  pl.x = (fx)x * FX1 + FX1 / 2, pl.z = (fx)z * FX1 + FX1 / 2;
  pl.y = (fx)world_top(x, z) * FX1 + FX1 * 2;
  render_sky_palette();
  render_title(0x3000);
  plat_save("NBCETTL", rbuf, RW * RH, render_sky(), RH);
  compose(false, false);
}

static void to_title(void) {
  in_game = false;
  gui = GUI_NONE;
  title_backdrop();
  gui_open(GUI_TITLE);
}

/* the options (NBCEOPT): the controls seen once, the render distance */
static bool controls_seen;
static void options_load(void) {
  unsigned len;
  const uint8_t *p = plat_load("NBCEOPT", &len);
  controls_seen = p && len >= 1 && p[0] == 1;
  if (p && len >= 2 && (p[1] == 8 || p[1] == 10 || p[1] == 13)) view_dist = p[1];
}
void options_save(void) {
  uint8_t o[2] = {controls_seen, (uint8_t)view_dist};
  plat_save("NBCEOPT", o, 2, 0, 0);
}

static void start_game(void) {
  in_game = true;
  gui = GUI_NONE;
  render_sky_palette();
  last_ms = plat_ms(), tick_acc = 0, ticks_since_save = 0;
  if (!controls_seen) {
    controls_seen = true;
    options_save();
    render_frame();
    compose(false, false);
    gui_open(GUI_CONTROLS);
  }
}

void game_save_quit(void) {
  if (in_game) save_world();
}


static void act(void) {
  int a = menu_action;
  menu_action = ACT_NONE;
  switch (a) {
    case ACT_QUIT: plat_exit(); break;
    case ACT_PLAY:
      loading("Loading world");
      if (load_world(cur_world)) start_game();
      else to_title();
      break;
    case ACT_NEW: {
      int s = world_free_slot();
      if (!s) break;
      loading("Building terrain");
      uint32_t seed = plat_ms() * 2654435761u ^ (uint32_t)rnd(0x7FFFFF);
#ifdef NB_SEED
      seed = NB_SEED;   /* (tests: a world known in advance) */
#endif
      new_world(s, cur_name, seed, new_mode);
      save_world();
      start_game();
      break;
    }
    case ACT_SAVE_QUIT:
      loading("Saving world");
      save_world();
      to_title();
      break;
    case ACT_RESPAWN:
      player_spawn();
      xp_level = xp_points = 0;
      world_center(FXI(pl.x), FXI(pl.y), FXI(pl.z), true);
      gui = GUI_NONE;
      break;
    case ACT_TITLE:
      save_world();
      to_title();
      break;
  }
}

/* looking around: faster the longer an arrow is held (60 to 180 degrees a second) */
static void look(uint32_t keys, uint32_t dt) {
  int dx = (keys & K_RIGHT ? 1 : 0) - (keys & K_LEFT ? 1 : 0), dy = (keys & K_DOWN ? 1 : 0) - (keys & K_UP ? 1 : 0);
  if (!dx && !dy) {
    look_held = 0;
    return;
  }
  look_held += (int)dt;
  int deg = 60 + (look_held > 600 ? 120 : look_held * 120 / 600);   /* degrees a second */
  int32_t step = (int32_t)deg * 182 * (int32_t)dt / 1000;
  pl.yaw = (uint16_t)(pl.yaw + dx * step);
  int32_t p = pl.pitch + dy * step;
  if (p > 16383) p = 16383;
  if (p < -16383) p = -16383;
  pl.pitch = (int16_t)p;
}

static void play(uint32_t keys, uint32_t pressed) {
  uint32_t now = plat_ms(), dt = now - last_ms;
  last_ms = now;
  if (dt > 500) dt = 500;   /* (the game's 20 ticks a second kept down to 2 pictures a second) */
  if (gui == GUI_NONE) look(keys, dt);
  tick_acc += dt;
  int n = 0;
  while (tick_acc >= 50 && n < 10) {
    tick_acc -= 50, n++;
    if (gui == GUI_NONE) player_tick(keys, n == 1 ? pressed : 0);
    gui_tick();
    world_time++;
    if (!(world_time & 63)) render_sky_palette();
    if (++ticks_since_save >= 20 * 60 * 5) ticks_since_save = 0, save_world();
  }
  if (tick_acc > 200) tick_acc = 0;
  world_center(FXI(pl.x), FXI(pl.y) + 1, FXI(pl.z), false);
  /* a row a picture, more when the view would see past them */
  for (int k = 0; world_busy() && (!k || world_behind()); k++) world_step(), plat_poll();
  if (gui == GUI_NONE) {
    if (pressed & K_INV) {
      gui_open(pl.mode ? GUI_CREATIVE : GUI_INVENTORY);
      return;
    }
    if (pressed & K_PAUSE) {
      gui_open(GUI_PAUSE);
      return;
    }
    bool drawn = render_frame();
    plat_poll();
    if (hud_draw() || drawn) compose(true, true);
  }
}

int main(void) {
  plat_init();
  wblk = plat_spare();
  wlight = wblk + WS * WS * WH;
  hud_layer = wblk + 2 * WS * WS * WH;
  render_init();
  options_load();
  canvas_screen();
#ifdef NB_BENCH
#include <stdio.h>
  {
    /* (timing, built with -DNB_BENCH: each part's ms x 10, as text in the appvar NBBENCH) */
    static char out[700];
    int n = 0;
    uint32_t t = plat_ms();
#define LAP(name)                                                                            \
  do {                                                                                       \
    uint32_t now = plat_ms();                                                                \
    n += sprintf(out + n, "%s %lu\n", name, (unsigned long)(now - t));                       \
    t = plat_ms();                                                                           \
  } while (0)
#if NB_BENCH == 3
    {
      /* the arithmetic's costs: 3000 of each (volatile: not folded away) */
      static volatile int a = 12345, b = 77, r;
      static volatile int32_t la = 1234567, lb = 777, lr;
      for (int i = 0; i < 3000; i++) r = a + b;
      LAP("loop");
      for (int i = 0; i < 3000; i++) r = a * b;
      LAP("imul");
      for (int i = 0; i < 3000; i++) r = a / b;
      LAP("idiv");
      for (int i = 0; i < 3000; i++) r = a >> 5;
      LAP("ishr5");
      for (int i = 0; i < 3000; i++) r = a & 4095;
      LAP("iand");
      for (int i = 0; i < 3000; i++) lr = la * lb;
      LAP("lmul");
      for (int i = 0; i < 3000; i++) lr = la / lb;
      LAP("ldiv");
      for (int i = 0; i < 3000; i++) lr = la >> 12;
      LAP("lshr12");
      static volatile uint8_t ba = 200, bb = 3, br;
      for (int i = 0; i < 3000; i++) br = (uint8_t)(ba & 0xF0);
      LAP("band");
      for (int i = 0; i < 3000; i++) br = (uint8_t)(ba * bb);
      LAP("bmul");
      static volatile int16_t sa = 1234, sb = 56;
      static volatile int32_t sr;
      for (int i = 0; i < 3000; i++) sr = (int32_t)sa * sb;
      LAP("s16mul32");
      plat_save("NBBENCH", out, (unsigned)n, 0, 0);
      plat_exit();
    }
#endif
    strcpy(cur_name, "Bench");
    new_world(1, cur_name, 12345, 0);
    LAP("world");
    render_sky_palette();
    LAP("sky");
    {
      extern const uint8_t *rs_flash;
      n += sprintf(out + n, "flash %p\n", (const void *)rs_flash);
    }
#if NB_BENCH == 11
    /* the window's blocks and light (to check world.c's loops in assembly against the C, -DNB_WORLD_C) */
    plat_save("NBWB", wblk, WS * WS * WH / 2, 0, 0);
    plat_save("NBWB2", wblk + WS * WS * WH / 2, WS * WS * WH / 2, 0, 0);
    plat_save("NBWL", wlight, WS * WS * WH / 2, 0, 0);
    plat_save("NBWL2", wlight + WS * WS * WH / 2, WS * WS * WH / 2, 0, 0);
    plat_save("NBBENCH", out, (unsigned)n, 0, 0);
    plat_exit();
#endif
#if NB_BENCH == 8 || NB_BENCH == 15
    {
      /* a screen opened over the world: the creative inventory */
      in_game = true, gui = GUI_NONE;
      pl.mode = 1;
      render_frame();
      compose(true, true);
      LAP("frame");
      gui_open(GUI_CREATIVE);
      LAP("open");
      gui_draw();
      LAP("draw");
      for (int k = 0; k < (NB_BENCH == 15 ? 20 : 0); k++) gui_redraw(), gui_draw();
      LAP("draws");
      for (uint32_t t = plat_ms(); plat_ms() - t < 2000;) plat_frame();   /* (its picture taken) */
      plat_save("NBBENCH", out, (unsigned)n, 0, 0);
      plat_exit();
    }
#endif
#if NB_BENCH == 10
    {
      /* rs_clip2 against its C twin */
      extern uint8_t rsc_rs_clip2(const SV *in, uint8_t n, SV *out, uint8_t k);
      static SV in[4] = {{-300, 100, 0, 0}, {900, -50, 65535, 0}, {1200, 700, 65535, 65535}, {-40, 900, 0, 65535}};
      static SV oa[16], ob[16];
      for (uint8_t k = 0; k < 4; k++) {
        uint8_t na = rs_clip2(in, 4, oa, k), nb = rsc_rs_clip2(in, 4, ob, k);
        n += sprintf(out + n, "k%d n %d %d\n", k, na, nb);
        for (int i = 0; i < na && i < nb; i++)
          if (memcmp(&oa[i], &ob[i], sizeof(SV)))
            n += sprintf(out + n, " %d: %d %d %d %d / %d %d %d %d\n", i, oa[i].x, oa[i].y, oa[i].u, oa[i].v, ob[i].x,
                         ob[i].y, ob[i].u, ob[i].v);
      }
      plat_save("NBBENCH", out, (unsigned)n, 0, 0);
      plat_exit();
    }
#endif
#if NB_BENCH == 9
    {
      /* the picture with the archive's loops and without: the same? */
      in_game = true, gui = GUI_NONE;
      extern const uint8_t *rs_flash;
      const uint8_t *fl = rs_flash;
      uint32_t ha = 0, hb = 0;
      for (int pass = 0; pass < 2; pass++) {
        if (pass) rs_flash_init(0, 0);
        render_stale = 1;
        render_frame();
        uint32_t h = 0;
        for (unsigned i = 0; i < RW * RH; i++) h = h * 31 + rbuf[i];
        if (pass) hb = h; else ha = h;
      }
      n += sprintf(out + n, "flash %p a %lx b %lx\n", (void *)fl, (unsigned long)ha, (unsigned long)hb);
      plat_save("NBBENCH", out, (unsigned)n, 0, 0);
      plat_exit();
    }
#endif
#if NB_BENCH == 7
    {
      /* one picture, as the computer build makes it (to compare) */
      in_game = true, gui = GUI_NONE;
#ifdef NB_V7YAW
      pl.yaw = NB_V7YAW, pl.pitch = NB_V7PITCH;
#endif
      render_frame();
      hud_draw();
      compose(true, true);
      for (uint32_t t = plat_ms(); plat_ms() - t < 6000;) plat_frame();
      plat_exit();
    }
#endif
#if NB_BENCH >= 5 && NB_BENCH <= 6 || NB_BENCH == 16
    {
      /* rows of the window made (5: 16 of them, 16: 64, 6: none) */
      for (int f = 0; f < (NB_BENCH == 5 ? 16 : NB_BENCH == 16 ? 64 : 0); f++) {
        pl.x += FX1;
        world_center(FXI(pl.x), FXI(pl.y) + 1, FXI(pl.z), false);
        while (world_busy()) world_step();
      }
      LAP("rows");
      plat_save("NBBENCH", out, (unsigned)n, 0, 0);
      plat_exit();
    }
#endif
#if NB_BENCH == 4
    {
      /* walking: 16 frames of 3 ticks each, each part's average */
      static const char *const part[5] = {"ticks", "rows", "render", "hud", "compose"};
      uint32_t sum[5] = {0, 0, 0, 0, 0};
      in_game = true, gui = GUI_NONE;
      for (int f = 0; f < 16; f++) {
        uint32_t t0 = plat_ms();
        for (int k = 0; k < 3; k++) player_tick(K_FWD | K_SPRINT, 0);
        uint32_t t1 = plat_ms();
        world_center(FXI(pl.x), FXI(pl.y) + 1, FXI(pl.z), false);
        for (int k = 0; k < 2 && world_busy(); k++) world_step();
        uint32_t t2 = plat_ms();
        render_frame();
        uint32_t t3 = plat_ms();
        hud_draw();
        uint32_t t4 = plat_ms();
        compose(true, true);
        uint32_t t5 = plat_ms();
        sum[0] += t1 - t0, sum[1] += t2 - t1, sum[2] += t3 - t2, sum[3] += t4 - t3, sum[4] += t5 - t4;
      }
      for (int k = 0; k < 5; k++) n += sprintf(out + n, "%s %lu\n", part[k], (unsigned long)(sum[k] / 16));
      plat_save("NBBENCH", out, (unsigned)n, 0, 0);
      plat_exit();
    }
#endif
    for (int v = 0; v < (NB_BENCH >= 12 ? 96 : NB_BENCH > 1 ? 24 : 4); v++) {
      /* (13: the fourth view over and over, a little apart) */
      int w = NB_BENCH == 13 ? 3 : NB_BENCH == 14 ? 0 : v;
      pl.yaw = (uint16_t)((w & 3) * 16384 + (NB_BENCH >= 13 ? (v & 1) * 64 : 0));
      pl.pitch = (w & 3) == 3 ? 6000 : 0;
      render_frame();
      LAP("render");
      extern uint32_t stat_faces, stat_cells;
      if (v < 4) n += sprintf(out + n, " cells %lu faces %lu\n", (unsigned long)stat_cells, (unsigned long)stat_faces);
      hud_draw();
      LAP("hud");
      compose(true, true);
      LAP("compose");
    }
    pl.x += FX1 * 4;
    world_center(FXI(pl.x), FXI(pl.y), FXI(pl.z), false);
    world_step();
    LAP("row");
    world_step();
    LAP("row");
    player_tick(K_FWD, 0);
    LAP("tick");
    plat_save("NBBENCH", out, (unsigned)n, 0, 0);
    plat_exit();
  }
#endif
#ifdef HOST
  /* (tests: straight into a new world, NB_NEW=seed,mode[,x,y,z,yaw,pitch,time]) */
  extern bool host_start(uint32_t *seed, int *mode, int *at);
  uint32_t hseed;
  int hmode, at[6];
  if (host_start(&hseed, &hmode, at)) {
    strcpy(cur_name, "Test");
    new_world(1, cur_name, hseed, hmode);
    if (at[0] != -9999) {
      pl.x = (fx)at[0] * FX1 + FX1 / 2, pl.y = (fx)at[1] * FX1, pl.z = (fx)at[2] * FX1 + FX1 / 2;
      pl.yaw = (uint16_t)at[3], pl.pitch = (int16_t)at[4], world_time = (uint32_t)at[5];
      pl.flying = pl.mode;
      world_center(at[0], at[1], at[2], true);
    }
    in_game = true;
    render_sky_palette();
    last_ms = plat_ms();
  } else
#endif
  to_title();
  uint32_t prev = ~0u;
  for (;;) {
    uint32_t keys = plat_keys(), pressed = keys & ~prev;
    prev = keys;
    if (keys & K_HOME) {
      if (in_game) {
        loading("Saving world");
        save_world();
      }
      break;
    }
    if (gui != GUI_NONE) gui_input(keys, pressed), pressed = 0;   /* (a press a screen took: not the game's too) */
    if (menu_action) act();
    if (in_game) play(keys, pressed);
    /* back on the title from another menu: its world behind it again */
    if (gui != GUI_TITLE) backdrop_ok = false;
    else if (!backdrop_ok && !in_game) title_backdrop(), gui_redraw();
    gui_draw();
    plat_frame();
  }
  plat_exit();
  return 0;
}
