/* A PICO-8 cart on the calculator: femto8's Lua and API (femto8/) with what the calculator gives it.
 *
 * - Memory: PICO-8's first 32 KB (0x0000 to 0x7fff) here; the cart's ROM is read in place in
 *   flash (reload()). Lua gets all the RAM left (heap.c).
 * - Frames: _update (30 a second) or _update60 (60) at the cart's own speed, read from the clock;
 *   when the calculator can't keep up, _draw is skipped now and then, as PICO-8 does when a cart
 *   is too slow for it. A cart that runs its own loop with flip() is paced the same way.
 * - The picture: PICO-8's 128 x 128 pixels on 240 x 240 of the screen.
 * - Keys: arrows (and 8 4 6 2) the D-pad, OK or EXE O, Back or Ans X, Backspace the pause menu
 *   (PICO-8's own: Continue, the cart's items, Reset cart, Quit), Home leaves from anywhere.
 * - cartdata(): 256 bytes for each id, in a file of the calculator's (p8_<id>.dat), written at
 *   the end of the frame it changed in. */
#include "pico.h"
#include <setjmp.h>
#include <string.h>
#include "heap.h"
#include "plat.h"
#include "femto8/p8_emu.h"
#include "femto8/p8_input.h"
#include "femto8/p8_lua.h"
#include "femto8/p8_lua_helper.h"
#include "femto8/p8_pause_menu.h"
#include "femto8/lua/lauxlib.h"
#include "femto8/lua/lua.h"

extern lua_State *L; /* (femto8/p8_lua.c) */

/* ------------------------------------------------------------ the state */
static uint8_t ram[P8_RAM_SIZE] __attribute__((aligned(4)));
uint8_t *m_memory = ram;
const uint8_t *m_cart_memory;
unsigned m_fps = 30;
unsigned m_frames;
char m_param_string[256];
uint16_t m_buttons[PLAYER_COUNT];
uint16_t m_buttonsp[PLAYER_COUNT];
p8_custom_menuitem_t m_custom_menuitems[MAX_CUSTOM_MENUITEMS];

p8_cart_t pico_carts[P8_MAX_CARTS];
int pico_ncarts;
pico_error_t pico_error;
int pico_next = -1;
uint32_t pico_peak;

static int cart = -1, breadcrumb = -1;
static jmp_buf run_jmp;
static bool in_lua;            /* (a Lua error can only be raised from inside it) */
static uint32_t keys_now;      /* the calculator's keys this frame */
static uint32_t keys_held;     /* those held when the cart started, ignored until let go */
static uint32_t t0, ticks;     /* the clock: frame `ticks` is due at t0 + ticks / fps seconds */
static unsigned clock_fps;
static uint32_t frame_start;
static uint16_t down_frames[6];
static bool first_repeat[6];
static char cartdata_name[40];
static bool cartdata_dirty;

enum { END_RESTART = 16 };

#define KEYS_HOME PK_HOME
static _Noreturn void end_run(int how) { longjmp(run_jmp, how + 1); }

/* ------------------------------------------------------------ PICO-8's colours, RGB565 */
const uint16_t pico_rgb565[32] = {
  0x0000, 0x194a, 0x792a, 0x042a, 0xaa86, 0x5aa9, 0xc618, 0xff9d, 0xf809, 0xfd00, 0xff64, 0x0726, 0x2d7f, 0x83b3, 0xfbb5, 0xfe75,
  0x28c2, 0x10e6, 0x4106, 0x128b, 0x7165, 0x4987, 0xa44f, 0xf76f, 0xb88a, 0xfb64, 0xaf25, 0x05a9, 0x02d6, 0x722c, 0xfb6b, 0xfcf0};

static inline int color_index(uint8_t c) { return ((c >> 3) & 0x10) | (c & 0xf); }

bool pico_glyph_px(uint8_t c, int x, int y) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return false;
  int sx = (c & 15) * 8 + x, sy = (c >> 4) * 8 + y;
  uint8_t b = font_map[sy * 64 + (sx >> 1)];
  return ((sx & 1) ? b >> 4 : b & 15) == 7;
}

int pico_text_w(const char *s) {
  int w = 0;
  for (; *s; s++) w += (uint8_t)*s >= 0x80 ? 8 : 4;
  return w;
}

/* ------------------------------------------------------------ the pause menu, over the picture */
#define MENU_MAX (MAX_CUSTOM_MENUITEMS + 3)
static struct {
  bool on;
  int n, sel;
  const char *title;
  const char *items[MENU_MAX];
} menu;

/* the menu's box, in PICO-8's pixels */
static void menu_box(int *x0, int *y0, int *x1, int *y1) {
  int w = 0;
  for (int i = 0; i < menu.n; i++) w = MAX(w, pico_text_w(menu.items[i]));
  if (menu.title) w = MAX(w, pico_text_w(menu.title) - 8);
  w += 18;
  int h = menu.n * 8 + 7 + (menu.title ? 10 : 0);
  *x0 = 64 - w / 2, *x1 = *x0 + w - 1;
  *y0 = 64 - h / 2, *y1 = *y0 + h - 1;
}

static void text_row(int8_t *out, const char *s, int x, int row, int col) {
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    int w = c >= 0x80 ? 8 : 4;
    for (int gx = 0; gx < w; gx++)
      if (x + gx >= 0 && x + gx < 128 && pico_glyph_px(c, gx, row)) out[x + gx] = (int8_t)col;
    x += w;
  }
}

/* one row of the menu: -1 where the picture shows */
static void menu_row(int y, int8_t *out) {
  int x0, y0, x1, y1;
  menu_box(&x0, &y0, &x1, &y1);
  if (y < y0 || y > y1) return;
  for (int x = x0; x <= x1; x++) out[x] = (y == y0 || y == y1 || x == x0 || x == x1) ? 7 : 0;
  int top = y0 + 4;
  if (menu.title) {
    if (y >= top && y < top + 6) text_row(out, menu.title, x0 + 5, y - top, 6);
    top += 10;
  }
  int i = (y - top) / 8, r = (y - top) % 8;
  if (y < top || i >= menu.n || r >= 6) return;
  text_row(out, menu.items[i], x0 + 12, r, 7);
  if (i == menu.sel) text_row(out, "\x17", x0 + 4, r, 7); /* (▶) */
}

/* ------------------------------------------------------------ the picture */
static uint16_t lines[2 * PIC_W] __attribute__((aligned(4)));

/* where 0x5f2c (the screen's mode) takes an output pixel from */
static void transform(uint8_t mode, int ox, int oy, int *sx, int *sy) {
  switch (mode) {
  default: *sx = ox, *sy = oy; break;
  case 1: *sx = ox >> 1, *sy = oy; break;
  case 2: *sx = ox, *sy = oy >> 1; break;
  case 3: *sx = ox >> 1, *sy = oy >> 1; break;
  case 5: *sx = ox < 64 ? ox : 127 - ox, *sy = oy; break;
  case 6: *sx = ox, *sy = oy < 64 ? oy : 127 - oy; break;
  case 7: *sx = ox < 64 ? ox : 127 - ox, *sy = oy < 64 ? oy : 127 - oy; break;
  case 129: *sx = 127 - ox, *sy = oy; break;
  case 130: *sx = ox, *sy = 127 - oy; break;
  case 131: case 134: *sx = 127 - ox, *sy = 127 - oy; break;
  case 133: *sx = 127 - oy, *sy = ox; break;
  case 135: *sx = oy, *sy = 127 - ox; break;
  }
}

/* (femto8's high colour modes, 0x5f5f) */
static uint8_t high_color(uint8_t hc, uint8_t pix, int sx, int sy) {
  if (hc == 0x10) {
    uint8_t bf = m_memory[0x5f70 + (sy >> 3)];
    return color_get((bf & (1 << (sy & 7))) ? PALTYPE_SECONDARY : PALTYPE_SCREEN, pix);
  }
  if (hc == 0x20) {
    uint8_t v = m_memory[p8_screen_base() + ((sx + 64) >> 1) + sy * 64];
    uint8_t hidden = IS_EVEN(sx + 64) ? (v & 0xF) : (v >> 4);
    return color_get(hidden ? PALTYPE_SECONDARY : PALTYPE_SCREEN, pix);
  }
  if ((hc & 0xf0) == 0x30) {
    uint8_t c = color_get(PALTYPE_SCREEN, pix);
    if ((c & 0x0f) == (hc & 0x0f)) {
      int section = sy >> 3;
      if (m_memory[0x5f70 + (sy >> 3)] & (1 << (sy & 7))) section = (section + 1) & 0x0f;
      return color_get(PALTYPE_SECONDARY, section);
    }
    return c;
  }
  return color_get(PALTYPE_SCREEN, pix);
}

/* (for tools/emu.py and tests/perf.py: pictures shown, frames run, Lua's memory at its most) */
__attribute__((used)) uint32_t perf_frames, perf_updates, perf_heap, perf_heap_size, perf_end;

void p8_render(void) {
  perf_frames++;
  const uint8_t *scr = m_memory + p8_screen_base();
  const uint8_t *pal = m_memory + MEMORY_PALETTES + PALTYPE_SCREEN * 16;
  uint8_t mode = m_memory[MEMORY_SCREEN_TRANSFORM], hc = m_memory[MEMORY_HIGH_COLOUR_MODE];
  uint16_t rgb[16];
  for (int i = 0; i < 16; i++) rgb[i] = pico_rgb565[color_index(pal[i])];
  int oy = 0;
  for (int y = 0; y < 128; y++) {
    int8_t over[128];
    if (menu.on) {
      memset(over, -1, sizeof over);
      menu_row(y, over);
    }
    uint16_t *d = lines;
    if (mode == 0 && hc == 0 && !menu.on) {
      const uint8_t *s = scr + y * 64;
      for (int x = 0; x < 64; x += 4) { /* (8 pixels: 15 on the screen) */
        uint16_t c;
        c = rgb[s[x] & 15], d[0] = d[1] = c;
        c = rgb[s[x] >> 4], d[2] = d[3] = c;
        c = rgb[s[x + 1] & 15], d[4] = d[5] = c;
        c = rgb[s[x + 1] >> 4], d[6] = d[7] = c;
        c = rgb[s[x + 2] & 15], d[8] = d[9] = c;
        c = rgb[s[x + 2] >> 4], d[10] = d[11] = c;
        c = rgb[s[x + 3] & 15], d[12] = d[13] = c;
        d[14] = rgb[s[x + 3] >> 4];
        d += 15;
      }
    } else {
      for (int x = 0; x < 128; x++) {
        uint16_t c;
        if (menu.on && over[x] >= 0) {
          c = pico_rgb565[over[x]];
        } else {
          int sx, sy;
          transform(mode, x, y, &sx, &sy);
          uint8_t v = scr[(sx >> 1) + sy * 64], pix = IS_EVEN(sx) ? v & 0xF : v >> 4;
          c = hc ? pico_rgb565[color_index(high_color(hc, pix, sx, sy))] : rgb[pix];
          if (menu.on) c = (c >> 1) & 0x7BEF; /* (the picture dimmed behind the menu) */
        }
        *d++ = c;
        if ((x & 7) != 7) *d++ = c;
      }
    }
    int h = (y & 7) == 7 ? 1 : 2;
    if (h == 2) memcpy(lines + PIC_W, lines, PIC_W * sizeof *lines);
    plat_push(PIC_X, oy, PIC_W, h, lines);
    oy += h;
  }
}

void pico_redraw(void) { p8_render(); }

/* ------------------------------------------------------------ keys and buttons */
static uint16_t buttons_of(uint32_t k) {
  uint16_t b = 0;
  if (k & PK_LEFT) b |= BUTTON_MASK_LEFT;
  if (k & PK_RIGHT) b |= BUTTON_MASK_RIGHT;
  if (k & PK_UP) b |= BUTTON_MASK_UP;
  if (k & PK_DOWN) b |= BUTTON_MASK_DOWN;
  if (k & PK_O) b |= BUTTON_MASK_ACTION1;
  if (k & PK_X) b |= BUTTON_MASK_ACTION2;
  return b;
}

static void flush_cartdata(void) {
  if (cartdata_dirty && cartdata_name[0]) plat_save(cartdata_name, m_memory + MEMORY_CARTDATA, MEMORY_CARTDATA_SIZE);
  cartdata_dirty = false;
}

static void home(void) {
  flush_cartdata();
  end_run(PICO_HOME);
}

/* the keys for this frame: btn() and btnp() (repeating after 15 frames, every 4, or 0x5f5c and
   0x5f5d's; twice as many at 60 a second) */
static void read_keys(void) {
  uint32_t k = plat_keys();
  keys_held &= k;
  if (k & PK_HOME) home();
  uint32_t fresh = k & ~keys_now;
  keys_now = k;
  k &= ~keys_held;
  m_buttons[0] = buttons_of(k);
  m_buttonsp[0] = 0;
  m_buttons[1] = m_buttonsp[1] = 0;
  unsigned delay = m_memory[MEMORY_AUTO_REPEAT_DELAY] ? m_memory[MEMORY_AUTO_REPEAT_DELAY] : DEFAULT_AUTO_REPEAT_DELAY;
  unsigned interval = m_memory[MEMORY_AUTO_REPEAT_INTERVAL] ? m_memory[MEMORY_AUTO_REPEAT_INTERVAL] : DEFAULT_AUTO_REPEAT_INTERVAL;
  if (m_fps == 60) delay *= 2, interval *= 2;
  for (int i = 0; i < 6; i++) {
    if (!(m_buttons[0] & (1 << i))) {
      down_frames[i] = 0, first_repeat[i] = false;
      continue;
    }
    unsigned n = ++down_frames[i];
    if (n == 1) m_buttonsp[0] |= 1 << i;
    else if (delay != 255 && !first_repeat[i] && n > delay) m_buttonsp[0] |= 1 << i, first_repeat[i] = true, down_frames[i] = 1;
    else if (first_repeat[i] && n > interval) m_buttonsp[0] |= 1 << i, down_frames[i] = 1;
  }
  if (fresh & PK_MENU) {
    m_buttons[0] |= BUTTON_MASK_PAUSE, m_buttonsp[0] |= BUTTON_MASK_PAUSE;
    if (m_memory[MEMORY_SUPPRESS_PAUSE]) m_memory[MEMORY_SUPPRESS_PAUSE] = 0; /* (the cart takes the press) */
    else p8_show_pause_menu();
  }
}

void pico_update_buttons(void) { read_keys(); }

/* ------------------------------------------------------------ frames */
static void clock_reset(void) {
  t0 = plat_millis(), ticks = 0, clock_fps = m_fps ? m_fps : 30;
}

/* the time the next frame is due; a calculator far behind starts the count again (the cart slows
   down rather than skipping on for ever) */
static uint32_t due(void) {
  if (clock_fps != m_fps) clock_reset();
  return t0 + (uint32_t)((uint64_t)ticks * 1000 / clock_fps);
}

static void wait_frame(void) {
  ticks++;
  uint32_t d = due(), now = plat_millis();
  if ((int32_t)(d - now) > 0) plat_sleep(d - now);
  else if ((int32_t)(now - d) > 250) clock_reset();
  frame_start = plat_millis();
}

static void end_frame(void) {
  perf_updates++;
  if (heap_peak() > perf_heap) perf_heap = heap_peak();
  perf_heap_size = heap_size();
  flush_cartdata();
  m_frames++;
}

unsigned p8_elapsed_time(void) { return plat_millis() - frame_start; }

/* flip(): the picture shown, the next frame's keys */
void p8_flip(void) {
  p8_render();
  end_frame();
  wait_frame();
  read_keys();
}

void p8_check_for_next_frame(void) {}
void p8_pump_events(void) {}

/* every few thousand instructions: Home, even in a cart that never ends its frame */
void pico_hook(lua_State *l) {
  (void)l;
  if (plat_keys() & PK_HOME) home();
}

/* ------------------------------------------------------------ the pause menu */
static const char *const PAUSE_ITEMS[] = {"continue", "reset cart", "quit"};

static uint32_t menu_keys(uint32_t *held) {
  uint32_t k = plat_keys();
  if (k & PK_HOME) home();
  uint32_t hit = k & ~*held;
  *held = k;
  return hit;
}

/* a cart's menu item: its function gets the buttons (1 left, 2 right, 16 O, 32 X); true keeps the menu */
static bool call_item(int i, int b) {
  int ref = m_custom_menuitems[i].lua_callback_ref;
  if (ref == LUA_NOREF || ref == LUA_REFNIL || !L) return false;
  menu.on = false;
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  lua_pushinteger(L, b);
  int st = lua_pcall(L, 1, 1, 0);
  if (st) {
    lua_get_error(&(const char *){0}, pico_error.msg, sizeof pico_error.msg, NULL, &pico_error.line);
    strcpy(pico_error.what, "runtime error");
    end_run(st == LUA_ERRMEM ? PICO_MEMORY : PICO_ERROR);
  }
  bool keep = lua_toboolean(L, -1);
  lua_settop(L, top);
  return keep;
}

static bool confirm_quit(void) {
  static const char *const items[] = {"no", "yes"};
  menu.title = "quit this cart?";
  menu.n = 2, menu.sel = 0;
  memcpy(menu.items, items, sizeof items);
  uint32_t held = plat_keys();
  for (;;) {
    p8_render();
    uint32_t k = menu_keys(&held);
    if (k & (PK_UP | PK_DOWN)) menu.sel ^= 1;
    if (k & (PK_BACK | PK_X | PK_MENU)) return false;
    if (k & (PK_OK | PK_O)) return menu.sel == 1;
    plat_sleep(16);
  }
}

void p8_show_pause_menu(void) {
  int map[MENU_MAX]; /* each item: -1 continue, -2 reset, -3 quit, else the cart's item */
  int sel = 0;
  for (;;) {
    menu.title = NULL;
    menu.n = 0;
    map[menu.n] = -1, menu.items[menu.n++] = PAUSE_ITEMS[0];
    for (int i = 0; i < MAX_CUSTOM_MENUITEMS; i++)
      if (m_custom_menuitems[i].active) map[menu.n] = i, menu.items[menu.n++] = m_custom_menuitems[i].label;
    map[menu.n] = -2, menu.items[menu.n++] = PAUSE_ITEMS[1];
    map[menu.n] = -3, menu.items[menu.n++] = PAUSE_ITEMS[2];
    menu.sel = sel < menu.n ? sel : 0;
    menu.on = true;
    uint32_t held = plat_keys();
    int chosen = -100, b = 0;
    while (chosen == -100) {
      p8_render();
      uint32_t k = menu_keys(&held);
      if (k & PK_UP) menu.sel = (menu.sel + menu.n - 1) % menu.n;
      if (k & PK_DOWN) menu.sel = (menu.sel + 1) % menu.n;
      if (k & (PK_BACK | PK_X | PK_MENU)) chosen = -1;
      else if (k & (PK_OK | PK_O)) chosen = map[menu.sel], b = 16;
      else if ((k & (PK_LEFT | PK_RIGHT)) && map[menu.sel] >= 0) chosen = map[menu.sel], b = k & PK_LEFT ? 1 : 2;
      plat_sleep(16);
    }
    sel = menu.sel;
    if (chosen == -1) break;
    if (chosen == -2) {
      menu.on = false;
      end_run(END_RESTART);
    }
    if (chosen == -3) {
      if (confirm_quit()) {
        menu.on = false;
        flush_cartdata();
        end_run(PICO_QUIT);
      }
      continue;
    }
    if (!call_item(chosen, b)) break;
  }
  menu.on = false;
  p8_render();
  keys_held = plat_keys(); /* (the key that closed the menu isn't the cart's) */
  clock_reset();
}

void p8_menuitem_set(int index, const char *label, int ref) {
  p8_custom_menuitem_t *m = &m_custom_menuitems[index - 1];
  if (m->active && m->lua_callback_ref != LUA_NOREF && L) luaL_unref(L, LUA_REGISTRYINDEX, m->lua_callback_ref);
  m->active = true;
  m->lua_callback_ref = ref;
  p8_menuitem_set_label(index, label);
}

void p8_menuitem_set_label(int index, const char *label) {
  p8_custom_menuitem_t *m = &m_custom_menuitems[index - 1];
  if (!m->active) m->lua_callback_ref = LUA_NOREF;
  m->active = true;
  /* (16 letters, as PICO-8 shows) */
  int n = 0;
  for (; label[n] && n < 16 && n < (int)sizeof m->label - 1; n++) m->label[n] = label[n];
  m->label[n] = 0;
}

void p8_menuitem_clear(int index) {
  p8_custom_menuitem_t *m = &m_custom_menuitems[index - 1];
  if (m->active && m->lua_callback_ref != LUA_NOREF && L) luaL_unref(L, LUA_REGISTRYINDEX, m->lua_callback_ref);
  m->active = false;
  m->lua_callback_ref = LUA_NOREF;
}

void p8_menuitem_reset_all(void) {
  for (int i = 0; i < MAX_CUSTOM_MENUITEMS; i++) m_custom_menuitems[i].active = false, m_custom_menuitems[i].lua_callback_ref = LUA_NOREF;
}

/* ------------------------------------------------------------ the rest of femto8's platform */
#if !defined(HOST)
/* (setjmp's unwind table names the C++ unwinder's first routine; nothing here unwinds) */
void __aeabi_unwind_cpp_pr0(void) {}
#endif

void p8_seed_rng_state(uint32_t seed) {
  uint32_t hi, lo;
  if (seed == 0) {
    hi = 0x60009755;
    lo = 0xdeadbeef;
  } else {
    seed &= 0x7fffffff;
    hi = seed ^ 0xbead29ba;
    lo = seed;
  }
  for (int i = 0; i < 32; i++) {
    hi = (hi << 16) | (hi >> 16);
    hi += lo;
    lo += hi;
  }
  memcpy(m_memory + MEMORY_RNG_STATE, &hi, 4);
  memcpy(m_memory + MEMORY_RNG_STATE + 4, &lo, 4);
}

/* (femto8's p8_reset) */
void p8_reset(void) {
  uint8_t cursor_x = m_memory[MEMORY_CURSOR], cursor_y = m_memory[MEMORY_CURSOR + 1];
  memset(m_memory + MEMORY_DRAWSTATE, 0, MEMORY_DRAWSTATE_SIZE);
  memset(m_memory + MEMORY_HARDWARESTATE, 0, MEMORY_HARDWARESTATE_SIZE);
  m_memory[MEMORY_SCREEN_PHYS] = 0x60;
  m_memory[MEMORY_MAP_START] = 0x20;
  m_memory[MEMORY_MAP_WIDTH] = 128;
  m_memory[MEMORY_RW_MASK] = 0xff;
  pencolor_set(6);
  reset_color();
  clip_set(0, 0, P8_WIDTH, P8_HEIGHT);
  p8_seed_rng_state(plat_random() & 0x7fffffff);
  m_memory[MEMORY_CURSOR] = cursor_x;
  m_memory[MEMORY_CURSOR + 1] = cursor_y;
}

bool p8_open_cartdata(const char *id) {
  if (cartdata_name[0] || !id || !*id) return false;
  char *n = cartdata_name;
  strcpy(n, "p8_");
  int k = 3;
  for (; *id && k < 3 + 32; id++) {
    char c = *id;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    n[k++] = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ? c : '_';
  }
  strcpy(n + k, ".dat");
  uint32_t len = 0;
  const uint8_t *d = plat_load(n, &len);
  memset(m_memory + MEMORY_CARTDATA, 0, MEMORY_CARTDATA_SIZE);
  if (d) memcpy(m_memory + MEMORY_CARTDATA, d, MIN(len, (uint32_t)MEMORY_CARTDATA_SIZE));
  return true;
}

void p8_delayed_flush_cartdata(void) { cartdata_dirty = true; }

_Noreturn void p8_abort(void) { end_run(PICO_QUIT); }
_Noreturn void p8_restart(void) { end_run(END_RESTART); }
_Noreturn void pico_quit_cart(void) {
  flush_cartdata();
  end_run(PICO_QUIT);
}

_Noreturn void pico_stop_cart(void) {
  strcpy(pico_error.what, "stopped");
  pico_error.msg[0] = 0, pico_error.line = 0;
  flush_cartdata();
  end_run(PICO_ERROR);
}

bool p8_load_cart(const char *name, const char *param) {
  int i = carts_find(pico_carts, pico_ncarts, name);
  if (i < 0) return false;
  strncpy(m_param_string, param ? param : "", sizeof m_param_string - 1);
  breadcrumb = cart;
  pico_next = i;
  flush_cartdata();
  end_run(PICO_LOAD);
}

void pico_breadcrumb(void) {
  if (breadcrumb < 0) return;
  pico_next = breadcrumb;
  m_param_string[0] = 0;
  flush_cartdata();
  end_run(PICO_LOAD);
}

const uint8_t *p8_cart_rom(const char *name) {
  int i = carts_find(pico_carts, pico_ncarts, name);
  return i < 0 ? NULL : pico_carts[i].rom;
}

#ifdef HOST_MALLOC
#include <stdlib.h>
void *pico_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  if (!nsize) return free(ptr), NULL;
  return realloc(ptr, nsize);
}
#else
void *pico_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) { return heap_alloc(ud, ptr, osize, nsize); }
#endif

/* an error outside any pcall (Lua's panic): out of memory, nearly always */
int pico_panic(lua_State *l) {
  (void)l;
  end_run(PICO_MEMORY);
}

#if !defined(HOST)
/* (Lua's last resort, after a panic that returned: never here, but not the C library's, which needs
   the calculator to have a process to kill) */
void abort(void) { end_run(PICO_MEMORY); }
#endif

/* ------------------------------------------------------------ a cart from start to end */
static int lua_failed(int status) {
  const char *what = NULL;
  pico_error.msg[0] = 0, pico_error.line = 0;
  lua_get_error(&what, pico_error.msg, sizeof pico_error.msg, NULL, &pico_error.line);
  strncpy(pico_error.what, what ? what : "error", sizeof pico_error.what - 1);
  return status == LUA_ERRMEM ? PICO_MEMORY : PICO_ERROR;
}

static int start(void) {
  const p8_cart_t *c = &pico_carts[cart];
  m_cart_memory = c->rom;
  memcpy(ram, c->rom, CART_MEMORY_SIZE);
  memset(ram + CART_MEMORY_SIZE, 0, sizeof ram - CART_MEMORY_SIZE);
  p8_reset();
  m_memory[MEMORY_CURSOR] = m_memory[MEMORY_CURSOR + 1] = 0;
  cartdata_name[0] = 0, cartdata_dirty = false;
  p8_menuitem_reset_all();
  memset(&menu, 0, sizeof menu);
  memset(down_frames, 0, sizeof down_frames);
  memset(first_repeat, 0, sizeof first_repeat);
  m_fps = 30, m_frames = 0;
  keys_now = keys_held = plat_keys();
  m_buttons[0] = m_buttonsp[0] = 0;

  uint8_t *a, *b;
  plat_heap(&a, &b);
  heap_init(a, b);
  if (lua_load_api() != 0) return PICO_MEMORY;
  lua_gc(L, LUA_GCSETPAUSE, 150);
  in_lua = true;
  int st = lua_init_script(NULL, c->code, c->code_len);
  if (!st) st = lua_init();
  in_lua = false;
  if (st) return lua_failed(st);

  if (!lua_has_main_loop_callbacks()) {
    /* (a cart that ran its course in its own loop: its last picture, until a key) */
    p8_render();
    uint32_t held = plat_keys();
    for (;;) {
      uint32_t k = menu_keys(&held);
      if (k & (PK_OK | PK_O | PK_BACK | PK_X | PK_MENU)) return PICO_QUIT;
      plat_sleep(20);
    }
  }

  clock_reset();
  frame_start = plat_millis();
  int skipped = 0;
  for (;;) {
    read_keys();
    in_lua = true;
    st = lua_update();
    in_lua = false;
    if (st) return lua_failed(st);
    /* drawn when on time, else skipped (never more than 3 in a row) */
    uint32_t now = plat_millis(), d = due() + 1000 / clock_fps;
    if ((int32_t)(now - d) <= 0 || skipped >= 3) {
      in_lua = true;
      st = lua_draw();
      in_lua = false;
      if (st) return lua_failed(st);
      p8_render();
      skipped = 0;
    } else {
      skipped++;
    }
    end_frame();
    wait_frame();
  }
}

int pico_run(int index) {
  cart = index;
  pico_next = -1;
  for (;;) {
    int volatile how = setjmp(run_jmp);
    if (how == 0) how = start() + 1;
    in_lua = false;
    menu.on = false;
    flush_cartdata();
    pico_peak = heap_peak();
    lua_shutdown_api();
    how--;
    perf_end = (uint32_t)how + 1;
    if (how != END_RESTART) return how;
    m_param_string[0] = 0;
  }
}
