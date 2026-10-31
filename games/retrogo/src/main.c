/* Retro-Go: Game Boy, Game Boy Color, NES, Master System and Game Gear games on the calculator.
 *
 * Retro-Go (ducalex) is a launcher and a set of emulators for ESP32 handhelds whose 320 x 240
 * screen is the calculator's. This is its look and its ways, made again for the keypad: the
 * launcher's system tabs and game lists, its menus (Save & Continue, Save & Quit, Load game,
 * Reset, Options...), its scaling (1x, Fit, Full), its frame skipping (a frame drawn, then at
 * least one skipped, more when the calculator can't keep up), save states in 4 slots and the
 * cartridges' battery saves. The emulators are gnuboy (Game Boy, Game Boy Color) and nofrendo
 * (NES), the ones Retro-Go started from, and for the Master System and Game Gear a machine
 * written for NumPlay around superzazu's Z80 (sms/); Retro-Go's own code is GPL-2.0 only, so
 * none of it is here (see README.md).
 *
 * The games come in a table in flash, np_roms (tools/romtable.py, or the customizer website),
 * read in place. Inside NumPlay the app draws with the launcher's strips and fonts and saves
 * through its file functions; on its own (RetroGo.nwa) it brings them along. */
#include <eadk.h>
#include <string.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"
#include "rg.h"
#include "romtable.h"
#include "pack.h"
#include "gb/gb.h"
#include "nes/nes_core.h"
#include "sms/sms.h"

#if NP_MODULE /* (np_file_read, np_file_write, np_storage_delete: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#define file_delete np_storage_delete
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#define file_delete ef_remove
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Retro-Go";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

/* The games: np_roms, up to the end of Retro-Go's block of flash (np_retrogo_end: NumPlay's
   linker script, or the one of RetroGo.nwa on its own). */
#if PLATFORM_DEVICE
extern const uint8_t np_roms[], np_retrogo_end[];
#define TABLE_LIMIT ((uintptr_t)np_retrogo_end > (uintptr_t)np_roms ? (uint32_t)((uintptr_t)np_retrogo_end - (uintptr_t)np_roms) : 0)
#else
extern const uint8_t np_roms[];
extern const uint32_t np_roms_size;
#define TABLE_LIMIT np_roms_size
#endif

#define KEY(k) (1ull << (k))
#define WHITE 0xFFFF
#define BLACK 0x0000

/* ------------------------------------------------------------ memory */
static uint8_t heap[RG_HEAP_SIZE] __attribute__((aligned(8)));
static uint32_t heap_used;
bool rg_drawing = true;

void *rg_alloc(uint32_t size) {
  size = (size + 7) & ~7u;
  if (size > sizeof heap - heap_used) return NULL;
  void *p = heap + heap_used;
  heap_used += size;
  memset(p, 0, size);
  return p;
}
void rg_alloc_reset(void) { heap_used = 0; }

/* one strip of the screen: the launcher's drawing functions draw there, and the emulators'
   pictures go through it */
static color_t strip[SCREEN_W * STRIP_H] __attribute__((aligned(4)));

static uint32_t millis(void) { return (uint32_t)eadk_timing_millis(); }

/* ------------------------------------------------------------ settings (retrogo.cfg) */
enum { SCALE_OFF, SCALE_FIT, SCALE_FULL, SCALE_COUNT };
static struct {
  uint8_t magic, version;
  uint8_t scaling, speed;          /* speed: 0.5x a step, from 0.5x */
  uint8_t gb_palette, nes_palette;
  uint8_t overscan, crop_sides, sprite_limit;
  uint8_t autosave;                /* the battery save, so many seconds after the game wrote it; 0 off */
  uint8_t flags;                   /* 1: the controls were shown */
  uint8_t tab, browse;
  uint8_t cursor[RG_SYS_COUNT];
  uint8_t keys[8];                 /* the calculator's key for each of the pad's buttons, in pad_bits' order */
} cfg;
#define CFG_NAME "retrogo.cfg"

/* ------------------------------------------------------------ the keys */
/* the pad's buttons: the D-pad, then A, B, Select and Start */
static const uint8_t pad_bits[8] = {RG_PAD_UP, RG_PAD_DOWN, RG_PAD_LEFT, RG_PAD_RIGHT,
                                    RG_PAD_A,  RG_PAD_B,    RG_PAD_SELECT, RG_PAD_START};
static const uint8_t default_keys[8] = {eadk_key_up, eadk_key_down, eadk_key_left, eadk_key_right,
                                        eadk_key_ok, eadk_key_back, eadk_key_shift, eadk_key_exe};
/* the calculator's keys by name; NULL for those a button can't have (Home, On/Off and Backspace, Retro-Go's menu) */
static const char *const key_names[eadk_key_exe + 1] = {
  [eadk_key_left] = T("left"), [eadk_key_up] = T("up"), [eadk_key_down] = T("down"), [eadk_key_right] = T("right"),
  [eadk_key_ok] = "OK", [eadk_key_back] = T("Back"), [eadk_key_shift] = T("shift"), [eadk_key_alpha] = "alpha",
  [eadk_key_xnt] = "x,n,t", [eadk_key_var] = "var", [eadk_key_toolbox] = T("toolbox"), [eadk_key_exp] = "e^x",
  [eadk_key_ln] = "ln", [eadk_key_log] = "log", [eadk_key_imaginary] = "i", [eadk_key_comma] = ",",
  [eadk_key_power] = "x^y", [eadk_key_sine] = "sin", [eadk_key_cosine] = "cos", [eadk_key_tangent] = "tan",
  [eadk_key_pi] = "pi", [eadk_key_sqrt] = "sqrt", [eadk_key_square] = "x^2", [eadk_key_seven] = "7",
  [eadk_key_eight] = "8", [eadk_key_nine] = "9", [eadk_key_left_parenthesis] = "(",
  [eadk_key_right_parenthesis] = ")", [eadk_key_four] = "4", [eadk_key_five] = "5", [eadk_key_six] = "6",
  [eadk_key_multiplication] = "*", [eadk_key_division] = "/", [eadk_key_one] = "1", [eadk_key_two] = "2",
  [eadk_key_three] = "3", [eadk_key_plus] = "+", [eadk_key_minus] = "-", [eadk_key_zero] = "0", [eadk_key_dot] = ".",
  [eadk_key_ee] = "x10^x", [eadk_key_ans] = "ans", [eadk_key_exe] = "EXE"};

/* the keys are for buttons, one each */
static bool keys_ok(const uint8_t *k) {
  for (int i = 0; i < 8; i++) {
    if (k[i] > eadk_key_exe || !key_names[k[i]]) return false;
    for (int j = 0; j < i; j++)
      if (k[j] == k[i]) return false;
  }
  return true;
}

static void cfg_defaults(void) {
  memset(&cfg, 0, sizeof cfg);
  cfg.magic = 'R', cfg.version = 2;
  cfg.scaling = SCALE_FIT, cfg.speed = 1;
  cfg.gb_palette = GB_PAL_DMG, cfg.nes_palette = NES_PAL_NTSC;
  cfg.overscan = 1, cfg.crop_sides = 0, cfg.sprite_limit = 1, cfg.autosave = 10;
  memcpy(cfg.keys, default_keys, sizeof cfg.keys);
}

static void cfg_load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(CFG_NAME, &len);
  cfg_defaults();
  /* (version 1: the same, before the keys) */
  if (!d || d[0] != 'R' || (!(len == sizeof cfg && d[1] == 2) && !(len == sizeof cfg - sizeof cfg.keys && d[1] == 1)))
    return;
  memcpy(&cfg, d, len);
  cfg.version = 2;
  if (!keys_ok(cfg.keys)) memcpy(cfg.keys, default_keys, sizeof cfg.keys);
  /* whatever it holds, settings Retro-Go has */
  if (cfg.scaling >= SCALE_COUNT) cfg.scaling = SCALE_FIT;
  if (cfg.speed > 4) cfg.speed = 1;
  if (cfg.gb_palette >= GB_PAL_COUNT) cfg.gb_palette = 0;
  if (cfg.nes_palette >= NES_PAL_COUNT) cfg.nes_palette = 0;
  cfg.overscan &= 1, cfg.sprite_limit &= 1, cfg.browse &= 1;
  if (cfg.crop_sides > 2) cfg.crop_sides = 0;
  if (cfg.autosave > 99) cfg.autosave = 10;
}

static void cfg_save(void) { np_file_write(CFG_NAME, &cfg, sizeof cfg); }

/* ------------------------------------------------------------ the games */
static rg_rom_t roms[RG_MAX_ROMS];
static int nroms;
/* Retro-Go's tabs, in its order */
static const uint8_t tab_order[] = {RG_SYS_NES, RG_SYS_GB, RG_SYS_GBC, RG_SYS_SMS, RG_SYS_GG};
static uint8_t tabs[5], ntabs;
static const char *const sys_short[RG_SYS_COUNT] = {"", "GB", "GBC", "NES", "SMS", "GG"};
/* the tab's banner: the maker, then the system, as Retro-Go's banners show them */
static const char *const sys_maker[RG_SYS_COUNT] = {"", "Nintendo", "Nintendo", "Nintendo", "SEGA", "SEGA"};
static const char *const sys_model[RG_SYS_COUNT] = {"Retro-Go", "Game Boy", "Game Boy Color", "Entertainment System",
                                                    "Master System", "Game Gear"};

static int tab_first(int sys) {
  for (int i = 0; i < nroms; i++)
    if (roms[i].system == sys) return i;
  return 0;
}
static int tab_count(int sys) {
  int n = 0;
  for (int i = 0; i < nroms; i++) n += roms[i].system == sys;
  return n;
}

/* CRC32 of a game (its save files are named after it, and Properties shows it) */
static uint32_t crc32(const uint8_t *p, uint32_t n) {
  static const uint32_t nib[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                   0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                   0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  uint32_t c = 0xFFFFFFFF;
  for (uint32_t i = 0; i < n; i++) {
    c ^= p[i];
    c = (c >> 4) ^ nib[c & 15];
    c = (c >> 4) ^ nib[c & 15];
  }
  return ~c;
}
static const rg_rom_t *crc_rom;
static uint32_t crc_value;
static uint32_t rom_crc(const rg_rom_t *r) {
  if (crc_rom != r) crc_rom = r, crc_value = crc32(r->data, r->size);
  return crc_value;
}

static char *hex8(char *o, uint32_t v) {
  for (int i = 7; i >= 0; i--, v >>= 4) o[i] = "0123456789ABCDEF"[v & 15];
  o[8] = 0;
  return o + 8;
}
static char *num(char *o, uint32_t v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}

/* "rg_XXXXXXXX.srm" (the battery save) and ".st0" to ".st3" (the states) */
static void save_name(char *o, const rg_rom_t *r, int slot) {
  o = cat(o, "rg_");
  o = hex8(o, rom_crc(r));
  if (slot < 0) cat(o, ".srm");
  else o = cat(o, ".st"), o[0] = (char)('0' + slot), o[1] = 0;
}

/* ------------------------------------------------------------ drawing in a rectangle */
/* The scene for rows [y0, y1), pushed for columns [x0, x1) only (what is around stays). */
static void render_rect(gfx_scene_t scene, void *ctx, int x0, int y0, int x1, int y1) {
  color_t *their = gfx_buf;
  gfx_buf = strip;
  x0 = NP_CLAMP(x0, 0, SCREEN_W), x1 = NP_CLAMP(x1, x0, SCREEN_W);
  y0 = NP_CLAMP(y0, 0, SCREEN_H), y1 = NP_CLAMP(y1, y0, SCREEN_H);
  int w = x1 - x0;
  for (int y = y0; y < y1 && w > 0; y += STRIP_H) {
    gfx_y0 = y;
    gfx_y1 = NP_MIN(y + STRIP_H, y1);
    scene(ctx);
    int h = gfx_y1 - y;
    if (w < SCREEN_W)
      for (int j = 0; j < h; j++) memmove(strip + j * w, strip + j * SCREEN_W + x0, (size_t)w * sizeof(color_t));
    np_push(x0, y, w, h, strip);
  }
  gfx_buf = their;
}

static void fill_screen(int x, int y, int w, int h, color_t c) {
  if (w > 0 && h > 0)
    eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}

/* ------------------------------------------------------------ the emulators' picture */
static struct {
  int src_x, src_y, src_w, src_h; /* the part of the core's picture shown */
  int x, y, w, h;                 /* where, on the screen */
} view;
static uint8_t xmap[SCREEN_W];     /* screen column -> picture column */
static uint8_t row_first[241];     /* picture row -> its first screen row */
static uint32_t line_sum[240];     /* each picture row as it was last pushed (Retro-Go's partial updates) */
static bool redraw_all;
static int pend_y, pend_n, covered_y = SCREEN_H; /* rows waiting in the strip; rows from covered_y down are left alone */

static void flush(void) {
  int n = NP_MIN(pend_n, covered_y - pend_y);
  if (n > 0) np_push(view.x, pend_y, view.w, n, strip);
  pend_n = 0;
}

static uint32_t checksum(const uint16_t *p, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i + 1 < n; i += 2) h = (h ^ (p[i] | (uint32_t)p[i + 1] << 16)) * 16777619u;
  return h;
}

void rg_line(int y, const uint16_t *px) {
  int sy = y - view.src_y;
  if (sy < 0 || sy >= view.src_h) return;
  int d0 = row_first[sy], d1 = row_first[sy + 1];
  if (d0 >= d1) return;
  const uint16_t *src = px + view.src_x;
  uint32_t sum = checksum(src, view.src_w);
  if (!redraw_all && sum == line_sum[sy]) { /* (unchanged since it was pushed) */
    flush();
    return;
  }
  line_sum[sy] = sum;
  for (int d = d0; d < d1; d++) {
    if (pend_n && (pend_y + pend_n != view.y + d || pend_n == STRIP_H)) flush();
    if (!pend_n) pend_y = view.y + d;
    color_t *row = strip + pend_n * view.w;
    if (d > d0 && pend_n) memcpy(row, row - view.w, (size_t)view.w * 2);
    else if (view.w == view.src_w) memcpy(row, src, (size_t)view.w * 2);
    else
      for (int x = 0; x < view.w; x++) row[x] = src[xmap[x]];
    pend_n++;
  }
}

/* the picture's place for the game running (Retro-Go's update_viewport_scaling) */
static int sys_now;
static void set_view(void) {
  int sx = 0, sy = 0, sw = 160, sh = 144;
  if (sys_now == RG_SYS_NES) {
    sy = cfg.overscan ? 8 : 0;
    sx = cfg.crop_sides ? 8 : 0;
    sw = NES_WIDTH - 2 * sx, sh = NES_HEIGHT - 2 * sy;
  } else if (sys_now == RG_SYS_SMS || sys_now == RG_SYS_GG) {
    sms_view(&sx, &sy, &sw, &sh); /* (the Game Gear's screen, or the taller pictures of some games) */
  }
  view.src_x = sx, view.src_y = sy, view.src_w = sw, view.src_h = sh;
  int w = sw, h = sh;
  if (cfg.scaling == SCALE_FULL) {
    w = SCREEN_W, h = SCREEN_H;
  } else if (cfg.scaling == SCALE_FIT) {
    w = (int)(SCREEN_H * ((float)sw / sh) + 0.1f), h = SCREEN_H;
    if (w > SCREEN_W) w = SCREEN_W, h = (int)(SCREEN_W * ((float)sh / sw) + 0.1f);
  }
  w &= ~1, h &= ~1;
  view.w = NP_MIN(w, SCREEN_W), view.h = NP_MIN(h, SCREEN_H);
  view.x = (SCREEN_W - view.w) / 2, view.y = (SCREEN_H - view.h) / 2;
  for (int x = 0; x < view.w; x++) xmap[x] = (uint8_t)(x * sw / view.w);
  for (int y = 0; y <= sh; y++) row_first[y] = (uint8_t)((y * view.h + sh - 1) / sh);
  redraw_all = true;
}

static void core_redraw(void);

/* ------------------------------------------------------------ the systems' looks */
/* Each system's colours (Retro-Go's theme) and a shape of the console, tilted, behind its tab:
   rounded boxes and discs in the console's own frame, the first one its body. */
typedef struct { int16_t u0, v0, u1, v1; uint8_t r, hole; } shape_t;
typedef struct { uint32_t bg, ink; const char *year; const shape_t *shapes; uint8_t n; } look_t;
static const shape_t gb_shape[] = {
  {-62, -100, 62, 100, 12, 0}, {-48, -84, 48, -6, 6, 1}, {-40, 22, -12, 34, 2, 1}, {-32, 14, -20, 42, 2, 1},
  {16, 26, 32, 42, 8, 1}, {34, 14, 50, 30, 8, 1}, {-14, 64, 2, 70, 3, 1}, {6, 64, 22, 70, 3, 1}};
static const shape_t gbc_shape[] = {
  {-58, -96, 58, 100, 22, 0}, {-44, -80, 44, -10, 6, 1}, {-38, 22, -10, 34, 2, 1}, {-30, 14, -18, 42, 2, 1},
  {14, 24, 30, 40, 8, 1}, {32, 12, 48, 28, 8, 1}, {-14, 66, 0, 72, 3, 1}, {6, 66, 20, 72, 3, 1}};
static const shape_t nes_shape[] = {
  {-150, -62, 150, 62, 6, 0}, {-120, -16, -60, 4, 2, 1}, {-100, -36, -80, 24, 2, 1}, {-34, -2, -8, 8, 4, 1},
  {6, -2, 32, 8, 4, 1}, {58, -14, 86, 14, 14, 1}, {98, -14, 126, 14, 14, 1}};
static const shape_t sms_shape[] = {
  {-150, -50, 150, 50, 4, 0}, {-120, -30, -20, 30, 6, 1}, {30, -20, 60, 10, 15, 1}, {75, -20, 105, 10, 15, 1}};
static const shape_t gg_shape[] = {
  {-140, -62, 140, 62, 40, 0}, {-56, -46, 56, 40, 6, 1}, {-112, -12, -84, 0, 2, 1}, {-104, -20, -92, 8, 2, 1},
  {84, -8, 100, 8, 8, 1}, {106, -18, 122, -2, 8, 1}};
static const look_t looks[RG_SYS_COUNT] = {
  {0x202020, 0x303030, "", gb_shape, 0},
  {0xE8811F, 0xDF6C10, "1989", gb_shape, sizeof gb_shape / sizeof gb_shape[0]},
  {0x3199DC, 0x2481BA, "1998", gbc_shape, sizeof gbc_shape / sizeof gbc_shape[0]},
  {0xE84B39, 0xC23627, "1985", nes_shape, sizeof nes_shape / sizeof nes_shape[0]},
  {0x3199DB, 0x2481BA, "1986", sms_shape, sizeof sms_shape / sizeof sms_shape[0]},
  {0x0EA186, 0x13BC9D, "1990", gg_shape, sizeof gg_shape / sizeof gg_shape[0]},
};

static bool in_shape(const shape_t *s, int u, int v) {
  if (u < s->u0 || u >= s->u1 || v < s->v0 || v >= s->v1) return false;
  int r = s->r, du = u < s->u0 + r ? s->u0 + r - u : u >= s->u1 - r ? u - (s->u1 - r) + 1 : 0;
  int dv = v < s->v0 + r ? s->v0 + r - v : v >= s->v1 - r ? v - (s->v1 - r) + 1 : 0;
  return du * du + dv * dv <= r * r;
}

/* the tab's background: its colour, the console tilted 30 degrees, darker by `shade` (Retro-Go
   divides the colours by 4 behind a list) */
static void background(int sys, int shade) {
  const look_t *l = &looks[sys];
  color_t bg = gfx_rgb(l->bg), ink = gfx_rgb(l->ink);
  if (shade) {
    bg = (color_t)(((bg >> 11) / shade) << 11 | (((bg >> 5) & 63) / shade) << 5 | (bg & 31) / shade);
    ink = (color_t)(((ink >> 11) / shade) << 11 | (((ink >> 5) & 63) / shade) << 5 | (ink & 31) / shade);
  }
  enum { COS = 887, SIN = 512 }; /* (30 degrees, in 1024ths) */
  int cx = 196, cy = 150;
  for (int y = gfx_y0; y < gfx_y1; y++) {
    color_t *p = gfx_buf + (y - gfx_y0) * SCREEN_W;
    int dy = y - cy;
    for (int x = 0; x < SCREEN_W; x++) {
      int dx = x - cx, u = (dx * COS + dy * SIN) >> 10, v = (dy * COS - dx * SIN) >> 10;
      color_t c = bg;
      if (l->n && in_shape(&l->shapes[0], u, v)) {
        c = ink;
        for (int k = 1; k < l->n; k++)
          if (in_shape(&l->shapes[k], u, v)) { c = bg; break; }
      }
      p[x] = c;
    }
  }
}

/* Retro-Go's tab header: the console's card on the left, its name and a line on the right */
#define HEADER_H 50
#define LOGO_W 46
static void header(int sys, int y) {
  const look_t *l = &looks[sys];
  gfx_rrect(1, y + 1, LOGO_W - 2, HEADER_H - 2, 4, WHITE, 256);
  gfx_rrect_outline(1, y + 1, LOGO_W - 2, HEADER_H - 2, 4, 1, HEX(0x9AA0A6), 256);
  /* a little console: the body, its screen or its pad */
  if (sys == RG_SYS_NES || sys == RG_SYS_SMS) {
    gfx_fill(7, y + 11, 32, 15, HEX(0x8C8C8C));
    gfx_fill(7, y + 22, 32, 4, HEX(0x3C3C3C));
    gfx_fill(10, y + 14, 12, 4, HEX(0xD8D8D8));
  } else {
    gfx_rrect(14, y + 4, 18, 25, 3, gfx_rgb(sys == RG_SYS_GB || !sys ? 0xB8B8B0 : l->bg), 256);
    gfx_fill(17, y + 7, 12, 9, HEX(0x6E8A3A));
    gfx_fill(17, y + 21, 5, 2, HEX(0x303030));
    gfx_fill(18, y + 20, 2, 4, HEX(0x303030));
    gfx_circle(27, y + 22, 2, HEX(0xA0204A), 256);
  }
  gfx_text_center(&np_font_small, LOGO_W / 2, y + 39, sys_short[sys], HEX(0x303030), 256);
  if (*l->year) { /* the year it came out, on a blue label */
    gfx_rrect(LOGO_W / 2 - 12, y + 40, 24, 9, 3, HEX(0x2F6FD0), 256);
    gfx_text_center(&np_font_small, LOGO_W / 2, y + 48, l->year, WHITE, 256);
  }
  /* the maker, small, then the name, big, and its line to the edge */
  gfx_text(&np_font_small, LOGO_W + 9, y + 11, sys_maker[sys], WHITE, 256);
  int w = gfx_text(&np_font_title, LOGO_W + 8, y + 30, sys_model[sys], WHITE, 256);
  if (w < SCREEN_W - 4) gfx_fill(w + 4, y + 23, SCREEN_W - w - 4, 2, WHITE);
}

/* ------------------------------------------------------------ Retro-Go's dialogs */
/* A box in the middle, its title, then its items: a label, and a value after ": " for an
   option; the selected item in reverse. Up/Down choose, Left/Right change a value, OK takes, Back
   goes back. */
#define DLG_BG 0x0010   /* Retro-Go's theme: dark blue, */
#define DLG_BORDER 0x6B4D /* a grey border, */
#define DLG_DIM 0x8410  /* grey for what can't be taken, */
#define DLG_MSG 0xBDF7  /* light grey for messages */
#define ROW_H 16
#define PAD 6
#define ROW_PAD 8
enum { IT_NORMAL, IT_DISABLED, IT_MESSAGE, IT_HIDDEN };
enum { EV_SHOW, EV_PREV, EV_NEXT, EV_ENTER };
enum { CB_NONE, CB_SELECT, CB_REDRAW };
enum { DLG_BACK = -1, DLG_HOME = -2 };
typedef struct item_s item_t;
typedef int (*item_cb)(item_t *it, int event);
struct item_s {
  int id;
  const char *label;
  char value[42];
  uint8_t flags, has_value;
  item_cb cb;
  intptr_t arg;
};

typedef struct {
  const char *title;
  item_t *items;
  int n, sel, top, rows;
  int x, y, w, h, col1, col2;
} dialog_t;

static void (*restore_behind)(void); /* the picture behind the dialogs, drawn again */

static void dialog_layout(dialog_t *d) {
  int sep = gfx_text_width(&np_font_body, ": "), maxw = SCREEN_W * 82 / 100;
  int inner = d->title ? gfx_text_width(&np_font_body, d->title) : 0, rows = 0;
  d->col1 = 0, d->col2 = 0;
  for (int i = 0; i < d->n; i++) {
    item_t *it = &d->items[i];
    if (it->flags == IT_HIDDEN) continue;
    rows++;
    int lw = gfx_text_width(&np_font_body, it->label);
    if (it->has_value) {
      d->col1 = NP_MAX(d->col1, lw);
      d->col2 = NP_MAX(d->col2, gfx_text_width(&np_font_body, it->value));
    } else {
      inner = NP_MAX(inner, lw);
    }
  }
  if (d->col2) inner = NP_MAX(inner, d->col1 + sep + d->col2);
  inner = NP_MIN(inner, maxw - 2 * (PAD + ROW_PAD));
  int maxrows = (SCREEN_H * 82 / 100 - 2 * PAD - (d->title ? ROW_H + 6 : 0)) / ROW_H;
  d->rows = NP_MIN(rows, maxrows);
  d->w = inner + 2 * (PAD + ROW_PAD);
  d->h = 2 * PAD + (d->title ? ROW_H + 6 : 0) + d->rows * ROW_H;
  d->x = (SCREEN_W - d->w) / 2, d->y = (SCREEN_H - d->h) / 2;
}

/* text cut with "..." to fit maxw pixels */
static void text_fit(const np_font_t *f, int x, int base, const char *s, int maxw, color_t c) {
  if (gfx_text_width(f, s) <= maxw) {
    gfx_text(f, x, base, s, c, 256);
    return;
  }
  char b[48];
  int n = 0;
  while (s[n] && n < 44) n++;
  for (; n > 0; n--) {
    memcpy(b, s, (size_t)n);
    strcpy(b + n, "...");
    if (gfx_text_width(f, b) <= maxw) break;
  }
  if (!n) strcpy(b, "...");
  gfx_text(f, x, base, b, c, 256);
}

static void dialog_scene(void *ctx) {
  dialog_t *d = ctx;
  gfx_fill(d->x - 1, d->y - 1, d->w + 2, d->h + 2, DLG_BORDER);
  gfx_fill(d->x, d->y, d->w, d->h, DLG_BG);
  int y = d->y + PAD, sep = gfx_text_width(&np_font_body, ": ");
  if (d->title) {
    gfx_text_center(&np_font_body, d->x + d->w / 2, y + 12, d->title, WHITE, 256);
    y += ROW_H + 6;
  }
  int shown = 0;
  for (int i = d->top; i < d->n && shown < d->rows; i++) {
    item_t *it = &d->items[i];
    if (it->flags == IT_HIDDEN) continue;
    color_t c = it->flags == IT_NORMAL ? WHITE : it->flags == IT_MESSAGE ? DLG_MSG : DLG_DIM;
    bool hl = i == d->sel && it->flags == IT_NORMAL;
    color_t fg = hl ? DLG_BG : c;
    if (hl) gfx_fill(d->x + PAD, y, d->w - 2 * PAD, ROW_H, c);
    int x = d->x + PAD + ROW_PAD, inner = d->w - 2 * (PAD + ROW_PAD);
    if (it->has_value) {
      gfx_text(&np_font_body, x, y + 12, it->label, fg, 256);
      gfx_text(&np_font_body, x + d->col1, y + 12, ": ", fg, 256);
      text_fit(&np_font_body, x + d->col1 + sep, y + 12, it->value, inner - d->col1 - sep, fg);
    } else {
      text_fit(&np_font_body, x, y + 12, it->label, inner, fg);
    }
    y += ROW_H;
    shown++;
  }
  /* (more above or below: Retro-Go's little arrows) */
  int ax = d->x + d->w - 10, ay = d->y + PAD + 6;
  if (d->top > 0)
    for (int k = 0; k < 3; k++) gfx_fill(ax + k, ay - 2 * k, 6 - 2 * k, 2, WHITE);
  ay = d->y + d->h - 6;
  int last = d->top, shown2 = 0;
  for (; last < d->n && shown2 < d->rows; last++) shown2 += d->items[last].flags != IT_HIDDEN;
  if (last < d->n)
    for (int k = 0; k < 3; k++) gfx_fill(ax + k, ay - 4 + 2 * k, 6 - 2 * k, 2, WHITE);
}

static void dialog_draw(dialog_t *d) {
  dialog_layout(d);
  /* keep the selection on screen */
  if (d->sel < d->top) d->top = d->sel;
  int vis = 0;
  for (int i = d->top; i <= d->sel && i < d->n; i++) vis += d->items[i].flags != IT_HIDDEN;
  while (vis > d->rows && d->top < d->sel) vis -= d->items[d->top++].flags != IT_HIDDEN;
  render_rect(dialog_scene, d, d->x - 1, d->y - 1, d->x + d->w + 1, d->y + d->h + 1);
}

static bool takeable(const item_t *it) { return it->flags == IT_NORMAL; }

/* keys as they are pressed, the arrows repeating while held */
static uint64_t held_keys;
static uint32_t repeat_at;
static uint64_t key_hits(void) {
  uint64_t k = eadk_keyboard_scan(), hit = k & ~held_keys;
  const uint64_t arrows = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
  uint32_t now = millis();
  if (hit & arrows) repeat_at = now + 300;
  else if ((k & arrows) && (int32_t)(now - repeat_at) >= 0) hit |= k & arrows, repeat_at = now + 90;
  held_keys = k;
  return hit | (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off)));
}
static void wait_release(void) {
  while (eadk_keyboard_scan() & ~(KEY(eadk_key_home) | KEY(eadk_key_on_off))) eadk_timing_msleep(10);
  held_keys = eadk_keyboard_scan();
}

/* Runs a dialog; the item's id, DLG_BACK or DLG_HOME. */
static int dialog_run(const char *title, item_t *items, int n, int sel) {
  dialog_t d = {title, items, n, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < n; i++)
    if (items[i].cb) items[i].cb(&items[i], EV_SHOW);
  if (sel < 0 || sel >= n || !takeable(&items[sel]))
    for (sel = 0; sel < n && !takeable(&items[sel]); sel++) {}
  d.sel = sel < n ? sel : 0;
  dialog_draw(&d);
  int result = DLG_BACK;
  for (;;) {
    eadk_timing_msleep(16);
    uint64_t hit = key_hits();
    if (hit & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) { result = DLG_HOME; break; }
    if (hit & KEY(eadk_key_back)) break;
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    if (dy) {
      int s = d.sel;
      for (int k = 0; k < n; k++) {
        s = (s + dy + n) % n;
        if (takeable(&items[s])) break;
      }
      if (takeable(&items[s]) && s != d.sel) d.sel = s, dialog_draw(&d);
      continue;
    }
    item_t *it = &items[d.sel];
    if (!takeable(it)) continue;
    int ev = hit & KEY(eadk_key_left) ? EV_PREV : hit & KEY(eadk_key_right) ? EV_NEXT : 0;
    if (ev && it->cb) {
      int r = it->cb(it, ev);
      it->cb(it, EV_SHOW);
      if (r == CB_REDRAW && restore_behind) {
        dialog_layout(&d);
        restore_behind();
      }
      dialog_draw(&d);
      continue;
    }
    if (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) {
      if (!it->cb) { result = it->id; break; }
      int r = it->cb(it, EV_ENTER);
      if (r == CB_SELECT) { result = it->id; break; }
      if (restore_behind) restore_behind();
      for (int i = 0; i < n; i++)
        if (items[i].cb) items[i].cb(&items[i], EV_SHOW);
      dialog_draw(&d);
    }
  }
  wait_release();
  if (restore_behind) restore_behind();
  return result;
}

#define ITEM(id, label) {(id), (label), "", IT_NORMAL, 0, NULL, 0}
#define OPTION(label, cb, arg) {0, (label), "", IT_NORMAL, 1, (cb), (arg)}
#define MESSAGE(label) {0, (label), "", IT_MESSAGE, 0, NULL, 0}

static void message_box(const char *title, const char *text) { /* drawn, not waited on */
  item_t it[1] = {MESSAGE(text)};
  dialog_t d = {title, it, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  dialog_draw(&d);
}

static int alert(const char *title, const char *text) {
  item_t it[] = {MESSAGE(text), ITEM(1, T("OK"))};
  return dialog_run(title, it, 2, 1);
}

/* Retro-Go's yes or no: 1 yes, 0 no (DLG_BACK too), DLG_HOME */
static int confirm(const char *title, const char *text) {
  item_t it[] = {MESSAGE(text ? text : ""), ITEM(1, T("Yes")), ITEM(0, T("No"))};
  int r = text ? dialog_run(title, it, 3, 2) : dialog_run(title, it + 1, 2, 1);
  return r == DLG_BACK ? 0 : r;
}

/* ------------------------------------------------------------ saves */
/* A save file: a header, the size of each part as it is and packed, a checksum of the packed
   data, then the parts packed. */
#define SAVE_MAGIC_STATE 0x54534752u /* "RGST" */
#define SAVE_MAGIC_SRAM 0x52534752u  /* "RGSR" */
#define SAVE_VERSION 1
typedef struct {
  uint32_t magic;
  uint8_t version, system;
  uint16_t parts;
  uint32_t rom;
  uint32_t seq;   /* states: the higher, the more recent (Retro-Go's "last used") */
  uint32_t check;
} save_head_t;
static uint32_t save_seq;

static uint32_t fnv(const uint8_t *p, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* (the packer's table is the strip: nothing is drawn while a save is made) */
static bool save_parts(const char *name, uint32_t magic, uint32_t rom, const rg_chunk_t *parts, int n) {
  uint16_t *table = (uint16_t *)(void *)strip;
  uint32_t packed[RG_MAX_CHUNKS], total = sizeof(save_head_t) + 8u * (uint32_t)n;
  for (int i = 0; i < n; i++) {
    packed[i] = rg_pack(parts[i].at, parts[i].size, NULL, table);
    total += packed[i];
  }
  if (total > 0xFF00 || !np_file_write(name, NULL, total)) return false;
  uint32_t len = 0;
  uint8_t *f = (uint8_t *)(uintptr_t)np_file_read(name, &len);
  if (!f || len != total) return false;
  uint8_t *o = f + sizeof(save_head_t) + 8 * n, *body = o;
  for (int i = 0; i < n; i++) {
    wr32(f + sizeof(save_head_t) + 8 * i, parts[i].size);
    wr32(f + sizeof(save_head_t) + 8 * i + 4, packed[i]);
    o += rg_pack(parts[i].at, parts[i].size, o, table);
  }
  save_head_t h = {magic, SAVE_VERSION, (uint8_t)sys_now, (uint16_t)n, rom, save_seq, fnv(body, (uint32_t)(o - body))};
  memcpy(f, &h, sizeof h);
  return true;
}

/* a state's header, if it is one of this game's: its sequence number, 0 if none */
static uint32_t state_seq(const char *name, uint32_t rom, int system) {
  uint32_t len = 0;
  const uint8_t *f = np_file_read(name, &len);
  save_head_t h;
  if (!f || len < sizeof h) return 0;
  memcpy(&h, f, sizeof h);
  if (h.magic != SAVE_MAGIC_STATE || h.version != SAVE_VERSION || h.rom != rom || h.system != system) return 0;
  return h.seq ? h.seq : 1;
}

/* 1: read; 0: no such file, or one for something else (nothing changed); -1: damaged half way
   (the parts are not to be trusted) */
static int load_parts(const char *name, uint32_t magic, uint32_t rom, const rg_chunk_t *parts, int n) {
  uint32_t len = 0;
  const uint8_t *f = np_file_read(name, &len);
  save_head_t h;
  if (!f || len < sizeof h) return 0;
  memcpy(&h, f, sizeof h);
  if (h.magic != magic || h.version != SAVE_VERSION || h.system != sys_now || h.rom != rom || h.parts != n) return 0;
  uint32_t at = sizeof h + 8u * (uint32_t)n, sum = 0;
  if (len < at) return 0;
  for (int i = 0; i < n; i++) {
    uint32_t raw = rd32(f + sizeof h + 8 * i), size = rd32(f + sizeof h + 8 * i + 4);
    if (raw != parts[i].size || size > len - at - sum) return 0;
    sum += size;
  }
  if (at + sum != len || fnv(f + at, sum) != h.check) return 0;
  for (int i = 0; i < n; i++) {
    uint32_t size = rd32(f + sizeof h + 8 * i + 4);
    if (!rg_unpack(f + at, size, parts[i].at, parts[i].size)) return -1;
    at += size;
  }
  return 1;
}

/* ------------------------------------------------------------ the game running */
static const rg_rom_t *game;
static uint32_t game_crc;
static int last_slot = -1;           /* the slot saved last (Retro-Go's "last used") */
static int session_slot = -1;        /* the slot this game was loaded from or saved to since it started */
static int frameskip = 1, skip;      /* Retro-Go's frame skipping */
static uint32_t autosave_at;         /* when the battery save is due, 0 none */

/* the three emulators: gnuboy, nofrendo, and the Master System's (for the Game Gear too) */
enum { CORE_GB, CORE_NES, CORE_SMS };
static int core;
static bool is_gb(void) { return core == CORE_GB; }

static int core_chunks(rg_chunk_t *c) {
  return core == CORE_GB ? gb_state_chunks(c) : core == CORE_NES ? nes_state_chunks(c) : sms_state_chunks(c);
}

/* the battery RAM, NULL if none (a Sega cartridge's only once the game has turned it on) */
static uint8_t *core_sram(uint32_t *size) {
  if (core == CORE_SMS) return sms_sram(size);
  return core == CORE_GB ? gb_sram(size) : nes_sram(size);
}
static bool core_sram_dirty(void) {
  return core == CORE_GB ? gb_sram_dirty() : core == CORE_NES ? nes_sram_dirty() : sms_sram_dirty();
}
static void core_sram_saved(void) {
  if (core == CORE_GB) gb_sram_saved();
  else if (core == CORE_NES) nes_sram_saved();
  else sms_sram_saved();
}

static void core_redraw(void) {
  redraw_all = true;
  pend_n = 0;
  if (core == CORE_GB) gb_redraw();
  else if (core == CORE_NES) nes_redraw();
  else sms_redraw();
  flush();
  redraw_all = false;
}

static bool sram_save(void) {
  uint32_t size = 0;
  uint8_t *ram = core_sram(&size);
  if (!ram || (core == CORE_SMS && !sms_sram_used())) return true;
  char name[20];
  save_name(name, game, -1);
  rg_chunk_t parts[2] = {{ram, size}, {NULL, 0}};
  int n = 1;
  if (is_gb() && gb_has_rtc()) parts[n].at = gb_rtc_state(&parts[n].size), n++;
  bool ok = !size && n == 1 ? true : save_parts(name, SAVE_MAGIC_SRAM, game_crc, parts, n);
  if (ok) core_sram_saved();
  autosave_at = 0;
  return ok;
}

static void sram_load(void) {
  uint32_t size = 0;
  uint8_t *ram = core_sram(&size);
  if (!ram) return;
  char name[20];
  save_name(name, game, -1);
  rg_chunk_t parts[2] = {{ram, size}, {NULL, 0}};
  int n = 1;
  if (is_gb() && gb_has_rtc()) parts[n].at = gb_rtc_state(&parts[n].size), n++;
  int r = load_parts(name, SAVE_MAGIC_SRAM, game_crc, parts, n);
  if (r < 0) memset(ram, 0xFF, size);
  if (is_gb() && gb_has_rtc()) gb_rtc_loaded();
  if (core == CORE_SMS && r > 0) sms_sram_loaded();
  core_sram_saved();
}

static uint32_t slot_seq(const rg_rom_t *r, int slot) {
  char name[20];
  save_name(name, r, slot);
  return state_seq(name, rom_crc(r), r->system);
}
static bool state_exists(const rg_rom_t *r, int slot) { return slot_seq(r, slot) != 0; }

/* the slot saved last, -1 if none */
static int newest_slot(const rg_rom_t *r) {
  int best = -1;
  uint32_t top = 0;
  for (int i = 0; i < 4; i++) {
    uint32_t s = slot_seq(r, i);
    if (s > top) top = s, best = i;
  }
  return best;
}

static bool state_save(int slot) {
  rg_chunk_t parts[RG_MAX_CHUNKS];
  int n = core_chunks(parts);
  char name[20];
  save_seq = 1;
  for (int i = 0; i < 4; i++) save_seq = NP_MAX(save_seq, slot_seq(game, i) + 1);
  save_name(name, game, slot);
  bool ok = save_parts(name, SAVE_MAGIC_STATE, game_crc, parts, n);
  if (ok) last_slot = session_slot = slot;
  return ok;
}

static void core_reset(bool hard);
/* false when it couldn't (the game as it was, or, if the state broke half way, started again) */
static bool state_load(int slot) {
  rg_chunk_t parts[RG_MAX_CHUNKS];
  int n = core_chunks(parts);
  char name[20];
  save_name(name, game, slot);
  int r = load_parts(name, SAVE_MAGIC_STATE, game_crc, parts, n);
  if (r > 0) {
    if (core == CORE_GB) gb_state_loaded();
    else if (core == CORE_NES) nes_state_loaded();
    else sms_state_loaded();
    last_slot = session_slot = slot, skip = 0;
    return true;
  }
  if (r < 0) { /* (as Retro-Go: a hard reset, and the battery save) */
    core_reset(true);
    sram_load();
  }
  return false;
}

static void core_reset(bool hard) {
  if (core == CORE_GB) gb_reset(hard);
  else if (core == CORE_NES) nes_reset_machine(hard);
  else sms_reset(hard);
  skip = 0;
}

static void apply_settings(void) {
  if (core == CORE_GB) gb_set_palette(cfg.gb_palette);
  else if (core == CORE_NES) nes_set_palette(cfg.nes_palette), nes_set_sprite_limit(cfg.sprite_limit);
}

/* The pad's buttons as the system running names them (a Sega pad's are 1 and 2; the Master
   System's pause button is on the console), NULL for none; with no game, every system's. */
static const char *button_name(int k) {
  static const char *const names[] = {T("D-pad"), "A", "B", T("Select"), T("Start")};
  bool sega = sys_now == RG_SYS_SMS || sys_now == RG_SYS_GG;
  if (!game) {
    bool any = false;
    for (int i = 0; i < ntabs; i++) any |= tabs[i] == RG_SYS_SMS || tabs[i] == RG_SYS_GG;
    static const char *const both[] = {NULL, T("A, 2"), T("B, 1"), NULL, T("Start, Pause")};
    return any && both[k] ? both[k] : names[k];
  }
  if (!sega) return names[k];
  static const char *const sms[] = {T("D-pad"), "2", "1", NULL, T("Pause")};
  return k == 4 && sys_now == RG_SYS_GG ? T("Start") : sms[k];
}
/* the D-pad's keys: "arrows", else the four (up, down, left, right) */
static char *dpad_keys(char *b) {
  if (!memcmp(cfg.keys, default_keys, 4)) return strcpy(b, T("arrows"));
  b[0] = 0;
  for (int i = 0; i < 4; i++) {
    if (i) strcat(b, " ");
    strcat(b, key_names[cfg.keys[i]]);
  }
  return b;
}

/* the game's picture again, with what is around it */
static void borders_scene(void *ctx) {
  (void)ctx;
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, BLACK);
  if (view.x < 72) return;
  /* (room on the sides: the keys) */
  const char *keys[7][2];
  char dpad[42];
  int n = 0;
  for (int k = 0; k < 5; k++)
    if (button_name(k)) keys[n][0] = k ? key_names[cfg.keys[k + 3]] : dpad_keys(dpad), keys[n][1] = button_name(k), n++;
  keys[n][0] = T("Backspace"), keys[n++][1] = T("menu");
  keys[n][0] = T("Home"), keys[n++][1] = T("quit");
  for (int i = 0; i < n; i++) {
    int y = view.y + 10 + i * 20;
    gfx_text_right(&np_font_small, view.x - 6, y, keys[i][0], HEX(0x9AA0A6), 256);
    gfx_text_right(&np_font_small, view.x - 6, y + 9, keys[i][1], WHITE, 256);
  }
}
static void game_screen(void) {
  if (view.y > 0) fill_screen(0, 0, SCREEN_W, view.y, BLACK), fill_screen(0, view.y + view.h, SCREEN_W, SCREEN_H - view.y - view.h, BLACK);
  if (view.x > 0) {
    render_rect(borders_scene, NULL, 0, view.y, view.x, view.y + view.h);
    fill_screen(view.x + view.w, view.y, SCREEN_W - view.x - view.w, view.h, BLACK);
  }
  core_redraw();
}

/* ---- the options (Retro-Go's, those the calculator has) */
static const char *const scale_names[] = {"1x", T("Fit"), T("Full")};
static int scaling_cb(item_t *it, int ev) {
  if (ev == EV_PREV) cfg.scaling = (uint8_t)((cfg.scaling + SCALE_COUNT - 1) % SCALE_COUNT);
  if (ev == EV_NEXT) cfg.scaling = (uint8_t)((cfg.scaling + 1) % SCALE_COUNT);
  strcpy(it->value, scale_names[cfg.scaling]);
  if (ev == EV_PREV || ev == EV_NEXT) {
    if (game) set_view();
    return CB_REDRAW;
  }
  return CB_NONE;
}
static int speed_cb(item_t *it, int ev) {
  if (ev == EV_PREV && cfg.speed > 0) cfg.speed--;
  if (ev == EV_NEXT && cfg.speed < 4) cfg.speed++;
  if (ev == EV_PREV || ev == EV_NEXT) frameskip = NP_MAX(1, cfg.speed * 3 / 2); /* (Retro-Go: (speed - 0.5) x 3) */
  char *o = num(it->value, (cfg.speed + 1) / 2);
  cat(o, cfg.speed & 1 ? ".0x" : ".5x");
  return CB_NONE;
}
static const char *const gb_pal_names[GB_PAL_COUNT] = {"DMG", T("Pocket"), T("Light"), "GBC", "SGB"};
static int gb_palette_cb(item_t *it, int ev) {
  if (gb_is_cgb()) {
    strcpy(it->value, "GBC");
    return CB_NONE;
  }
  int p = cfg.gb_palette;
  if (ev == EV_PREV) p = (p + GB_PAL_COUNT - 1) % GB_PAL_COUNT;
  if (ev == EV_NEXT) p = (p + 1) % GB_PAL_COUNT;
  strcpy(it->value, gb_pal_names[p]);
  if (p != cfg.gb_palette) {
    cfg.gb_palette = (uint8_t)p;
    gb_set_palette(p);
    return CB_REDRAW;
  }
  return CB_NONE;
}
static int rtc_field_cb(item_t *it, int ev) {
  int t[4];
  gb_get_time(&t[0], &t[1], &t[2], &t[3]);
  static const int top[4] = {512, 24, 60, 60};
  int k = (int)it->arg;
  if (ev == EV_PREV) t[k] = (t[k] + top[k] - 1) % top[k];
  if (ev == EV_NEXT) t[k] = (t[k] + 1) % top[k];
  gb_set_time(t[0], t[1], t[2], t[3]);
  char *o = it->value;
  if (t[k] < 10) *o++ = '0';
  num(o, (uint32_t)t[k]);
  return CB_NONE;
}
static int rtc_cb(item_t *it, int ev) {
  if (ev == EV_ENTER) {
    item_t items[] = {OPTION(T("Day"), rtc_field_cb, 0), OPTION(T("Hour"), rtc_field_cb, 1),
                      OPTION(T("Min"), rtc_field_cb, 2), OPTION(T("Sec"), rtc_field_cb, 3)};
    if (dialog_run(it->label, items, 4, 0) == DLG_HOME) return CB_SELECT;
  }
  int h, m;
  gb_get_time(NULL, &h, &m, NULL);
  char *o = it->value;
  *o++ = (char)('0' + h / 10), *o++ = (char)('0' + h % 10), *o++ = ':';
  *o++ = (char)('0' + m / 10), *o++ = (char)('0' + m % 10), *o = 0;
  return CB_NONE;
}
static int autosave_cb(item_t *it, int ev) {
  static const uint8_t steps[] = {0, 5, 10, 30, 60};
  int k = 0;
  while (k < 4 && steps[k] < cfg.autosave) k++;
  if (ev == EV_PREV && k > 0) k--;
  if (ev == EV_NEXT && k < 4) k++;
  cfg.autosave = steps[k];
  if (!cfg.autosave) strcpy(it->value, T("Off"));
  else cat(num(it->value, cfg.autosave), "s");
  return CB_NONE;
}
static const char *const nes_pal_names[NES_PAL_COUNT] = {"Nofrendo", "NTSC"};
static int nes_palette_cb(item_t *it, int ev) {
  int p = cfg.nes_palette;
  if (ev == EV_PREV) p = (p + NES_PAL_COUNT - 1) % NES_PAL_COUNT;
  if (ev == EV_NEXT) p = (p + 1) % NES_PAL_COUNT;
  strcpy(it->value, nes_pal_names[p]);
  if (p != cfg.nes_palette) {
    cfg.nes_palette = (uint8_t)p;
    nes_set_palette(p);
    return CB_REDRAW;
  }
  return CB_NONE;
}
static int overscan_cb(item_t *it, int ev) {
  if (ev == EV_PREV || ev == EV_NEXT) cfg.overscan ^= 1;
  strcpy(it->value, cfg.overscan ? T("Auto") : T("Off"));
  if (ev == EV_PREV || ev == EV_NEXT) {
    set_view();
    return CB_REDRAW;
  }
  return CB_NONE;
}
static int crop_cb(item_t *it, int ev) {
  static const char *const names[] = {T("Never"), T("Auto"), T("Always")};
  int v = cfg.crop_sides;
  if (ev == EV_PREV) v = (v + 2) % 3;
  if (ev == EV_NEXT) v = (v + 1) % 3;
  strcpy(it->value, names[v]);
  if (v != cfg.crop_sides) {
    cfg.crop_sides = (uint8_t)v;
    set_view();
    return CB_REDRAW;
  }
  return CB_NONE;
}
static int sprite_limit_cb(item_t *it, int ev) {
  if (ev == EV_PREV || ev == EV_NEXT) cfg.sprite_limit ^= 1, nes_set_sprite_limit(cfg.sprite_limit);
  strcpy(it->value, cfg.sprite_limit ? T("On") : T("Off"));
  return CB_NONE;
}
static int emulator_options_cb(item_t *it, int ev) {
  if (ev != EV_ENTER) return CB_NONE;
  int r;
  if (is_gb()) {
    item_t items[] = {OPTION(T("Palette"), gb_palette_cb, 0), OPTION(T("RTC config"), rtc_cb, 0),
                      OPTION(T("SRAM autosave"), autosave_cb, 0)};
    if (!gb_has_rtc()) items[1].flags = IT_DISABLED;
    r = dialog_run(it->label, items, 3, 0);
  } else if (core == CORE_NES) {
    item_t items[] = {OPTION(T("Palette"), nes_palette_cb, 0), OPTION(T("Overscan"), overscan_cb, 0),
                      OPTION(T("Crop sides"), crop_cb, 0), OPTION(T("Sprite limit"), sprite_limit_cb, 0),
                      OPTION(T("SRAM autosave"), autosave_cb, 0)};
    r = dialog_run(it->label, items, 5, 0);
  } else {
    item_t items[] = {OPTION(T("SRAM autosave"), autosave_cb, 0)};
    r = dialog_run(it->label, items, 1, 0);
  }
  return r == DLG_HOME ? CB_SELECT : CB_NONE;
}
static int options_menu(void) {
  item_t items[] = {OPTION(T("Scaling"), scaling_cb, 0), OPTION(T("Speed"), speed_cb, 0),
                    {1, T("Emulator options"), "", IT_NORMAL, 0, emulator_options_cb, 0}};
  int r = dialog_run(T("Options"), items, game ? 3 : 1, 0);
  cfg_save();
  return r;
}

/* a button's new key, pressed; false when Backspace, Home or On/Off is (nothing changed) */
static bool ask_key(const char *title, int b) {
  message_box(title, T("Press its new key"));
  wait_release();
  uint64_t k;
  while (!(k = eadk_keyboard_scan())) eadk_timing_msleep(10);
  int key = 0;
  while (!(k >> key & 1)) key++;
  wait_release();
  if (key > eadk_key_exe || !key_names[key]) return false;
  for (int j = 0; j < 8; j++) /* (a key another button had: that one takes this one's) */
    if (j != b && cfg.keys[j] == key) cfg.keys[j] = cfg.keys[b];
  cfg.keys[b] = (uint8_t)key;
  return true;
}
static int key_cb(item_t *it, int ev) {
  int b = (int)it->arg;
  if (ev == EV_ENTER) {
    static const char *const dirs[] = {T("Up"), T("Down"), T("Left"), T("Right")};
    if (b) ask_key(it->label, b);
    else
      for (int i = 0; i < 4 && ask_key(dirs[i], i); i++) {}
  }
  if (b) strcpy(it->value, key_names[cfg.keys[b]]);
  else dpad_keys(it->value);
  return CB_NONE;
}
static int default_keys_cb(item_t *it, int ev) {
  (void)it;
  if (ev == EV_ENTER) memcpy(cfg.keys, default_keys, sizeof cfg.keys);
  return CB_NONE;
}
static int controls(void) {
  item_t items[9];
  for (int i = 0; i < 5; i++) {
    const char *label = button_name(i);
    items[i] = (item_t){0, label ? label : "", "", label ? IT_NORMAL : IT_HIDDEN, 1, key_cb, i ? i + 3 : 0};
  }
  items[5] = (item_t){0, T("Menu"), "", IT_MESSAGE, 1, NULL, 0}, strcpy(items[5].value, T("Backspace"));
  items[6] = (item_t){0, T("Quit"), "", IT_MESSAGE, 1, NULL, 0}, strcpy(items[6].value, T("Home (saves)"));
  items[7] = (item_t){0, T("Default keys"), "", IT_NORMAL, 0, default_keys_cb, 0};
  items[8] = (item_t)ITEM(1, T("OK"));
  int r = dialog_run(T("Controls"), items, 9, 8);
  cfg_save();
  return r;
}

static int about(void) {
  item_t items[] = {
    {0, T("Retro-Go"), "ducalex", IT_MESSAGE, 1, NULL, 0}, {0, "Game Boy", "gnuboy", IT_MESSAGE, 1, NULL, 0},
    {0, "NES", "nofrendo", IT_MESSAGE, 1, NULL, 0},        {0, "SMS, GG", T("z80 by superzazu"), IT_MESSAGE, 1, NULL, 0},
    {0, T("Port"), "NumPlay", IT_MESSAGE, 1, NULL, 0},     MESSAGE(T("Not affiliated with Retro-Go")),
    ITEM(1, T("OK"))};
  return dialog_run(T("About Retro-Go"), items, 7, 6);
}

/* Retro-Go's save slots: 0 to 3; the slot, DLG_BACK or DLG_HOME */
static int slot_menu(const char *title, const rg_rom_t *r, bool used_only) {
  item_t items[4];
  int sel = last_slot >= 0 ? last_slot : 0;
  for (int i = 0; i < 4; i++) {
    static const char *const labels[] = {T("Slot 0"), T("Slot 1"), T("Slot 2"), T("Slot 3")};
    bool used = state_exists(r, i);
    items[i] = (item_t){i, labels[i], "", IT_NORMAL, 1, NULL, 0};
    strcpy(items[i].value, !used ? T("empty") : i == last_slot ? T("last used") : T("saved"));
    if (used_only && !used) items[i].flags = IT_DISABLED;
  }
  if (used_only && !state_exists(r, sel))
    for (sel = 0; sel < 4 && items[sel].flags != IT_NORMAL; sel++) {}
  return dialog_run(title, items, 4, sel);
}

enum { GAME_ON, GAME_PICKER, GAME_EXIT };

static void saving(void) { message_box(NULL, T("Saving...")); }

/* Home: the battery save, and a state in the slot this game came from or went to (a new game:
   the first empty slot; none, no state rather than over another game), then out */
static void save_and_exit(void) {
  sram_save();
  int slot = session_slot;
  for (int i = 0; i < 4 && slot < 0; i++)
    if (!state_exists(game, i)) slot = i;
  if (slot >= 0) state_save(slot);
  cfg_save();
}

static int game_menu(void) {
  if (core_sram_dirty()) sram_save(); /* (in case the game ends here) */
  enum { M_SAVE = 1000, M_SAVE_QUIT = 2000, M_LOAD = 3001, M_RESET = 3000, M_OPTIONS = 5500, M_CONTROLS = 5600,
         M_ABOUT = 6000, M_QUIT = 7000 };
  item_t items[] = {ITEM(M_SAVE, T("Save & Continue")), ITEM(M_SAVE_QUIT, T("Save & Quit")),
                    ITEM(M_LOAD, T("Load game")),       ITEM(M_RESET, T("Reset")),
                    ITEM(M_OPTIONS, T("Options")),      ITEM(M_CONTROLS, T("Controls")),
                    ITEM(M_ABOUT, T("About")),          ITEM(M_QUIT, T("Quit"))};
  int sel = dialog_run("Retro-Go", items, 8, 0), slot;
  switch (sel) {
  case DLG_HOME:
    save_and_exit();
    return GAME_EXIT;
  case M_SAVE:
  case M_SAVE_QUIT:
    slot = slot_menu(T("Save"), game, false);
    if (slot == DLG_HOME) return save_and_exit(), GAME_EXIT;
    if (slot < 0) break;
    saving();
    if (!state_save(slot) || !sram_save()) {
      if (alert(T("Not enough room"), T("Delete other saves to make room.")) == DLG_HOME) return save_and_exit(), GAME_EXIT;
      break;
    }
    if (sel == M_SAVE_QUIT) return GAME_PICKER;
    break;
  case M_LOAD:
    slot = slot_menu(T("Load"), game, true);
    if (slot == DLG_HOME) return save_and_exit(), GAME_EXIT;
    if (slot >= 0 && !state_load(slot) && alert(T("Load game"), T("This save can't be read.")) == DLG_HOME)
      return save_and_exit(), GAME_EXIT;
    break;
  case M_RESET: {
    item_t r[] = {ITEM(1, T("Soft reset")), ITEM(2, T("Hard reset"))};
    int k = dialog_run(T("Reset Emulation?"), r, 2, 0);
    if (k == DLG_HOME) return save_and_exit(), GAME_EXIT;
    if (k > 0) core_reset(k == 2);
    break;
  }
  case M_OPTIONS:
    if (options_menu() == DLG_HOME) return save_and_exit(), GAME_EXIT;
    break;
  case M_CONTROLS:
    if (controls() == DLG_HOME) return save_and_exit(), GAME_EXIT;
    break;
  case M_ABOUT:
    if (about() == DLG_HOME) return save_and_exit(), GAME_EXIT;
    break;
  case M_QUIT: {
    int k = confirm(T("Quit game?"), T("The battery save is kept."));
    if (k == DLG_HOME) return save_and_exit(), GAME_EXIT;
    if (k == 1) return sram_save(), GAME_PICKER;
    break;
  }
  default:
    break;
  }
  return GAME_ON;
}

/* the frame counters (perf_frames: tools/emu.py prints the frame rate from it) */
uint32_t perf_frames, perf_drawn;

static int play(const rg_rom_t *r, int slot) {
  sys_now = r->system;
  core = sys_now == RG_SYS_NES ? CORE_NES : sys_now == RG_SYS_SMS || sys_now == RG_SYS_GG ? CORE_SMS : CORE_GB;
  game = r;
  game_crc = rom_crc(r);
  int err = core == CORE_GB    ? gb_load(r->data, r->size)
            : core == CORE_NES ? nes_load(r->data, r->size)
                               : sms_load(r->data, r->size, game_crc, sys_now == RG_SYS_GG);
  if (err != RG_OK) {
    game = NULL;
    const char *why = err == RG_ERR_MAPPER ? T("This cartridge type isn't supported.")
                      : err == RG_ERR_MEMORY ? T("This game needs more memory than the calculator has.")
                                             : T("This file isn't a game Retro-Go can read.");
    return alert(T("Can't start the game"), why) == DLG_HOME ? GAME_EXIT : GAME_PICKER;
  }
  apply_settings();
  sram_load();
  last_slot = newest_slot(r), session_slot = -1;
  if (slot >= 0 && !state_load(slot)) {
    if (alert(T("Load game"), T("This save can't be read.")) == DLG_HOME) return GAME_EXIT;
  }
  set_view();
  restore_behind = game_screen;
  fill_screen(0, 0, SCREEN_W, SCREEN_H, BLACK);
  if (!(cfg.flags & 1)) {
    cfg.flags |= 1;
    cfg_save();
    if (controls() == DLG_HOME) return save_and_exit(), GAME_EXIT;
  }
  game_screen();
  wait_release();

  /* a frame: the Game Boy's 70224 clocks at 4.194304 MHz, the NES's 29780.5 at 1.789773 MHz, the
     Master System's 59736 at 3.579545 MHz (or 50 a second) */
  const uint32_t frame_us = core == CORE_GB ? 16742 : core == CORE_NES ? 16639 : sms_frame_us();
  uint32_t next_us = millis() * 1000u, second = millis(), frames = 0, busy = 0;
  int result = GAME_ON;
  frameskip = NP_MAX(1, cfg.speed * 3 / 2), skip = 0, autosave_at = 0;
  while (result == GAME_ON) {
    uint64_t k = eadk_keyboard_scan();
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) {
      saving();
      save_and_exit();
      result = GAME_EXIT;
      break;
    }
    if (k & KEY(eadk_key_backspace)) {
      wait_release();
      result = game_menu();
      if (result != GAME_ON) break;
      game_screen();
      wait_release();
      next_us = millis() * 1000u;
      continue;
    }
    int pad = 0;
    for (int i = 0; i < 8; i++)
      if (k & KEY(cfg.keys[i])) pad |= pad_bits[i];

    uint32_t t0 = millis();
    bool draw = skip == 0;
    if (core == CORE_GB) gb_set_pad(pad), gb_run_frame(draw);
    else if (core == CORE_NES) nes_set_pad(pad), nes_run_frame(draw);
    else sms_set_pad(pad), sms_run_frame(draw);
    if (draw) flush(), redraw_all = false, perf_drawn++;
    if (core == CORE_SMS) { /* (a game that changes the picture's height) */
      int x, y, w, h;
      sms_view(&x, &y, &w, &h);
      if (y != view.src_y || h != view.src_h) set_view(), game_screen();
    }
    perf_frames++, frames++;
    uint32_t elapsed = millis() - t0;
    busy += elapsed;

    /* the battery save, a while after the game wrote it */
    if (cfg.autosave && core_sram_dirty()) {
      if (!autosave_at) autosave_at = t0 + cfg.autosave * 1000u + 1;
      else if ((int32_t)(t0 - autosave_at) >= 0) sram_save();
    }

    /* Retro-Go's frame skipping: after a frame drawn, `frameskip` frames not drawn (one at least) */
    if (skip == 0) skip = frameskip;
    else skip--;

    /* the game's own speed (times Retro-Go's Speed), whatever the frames cost */
    next_us += frame_us * 2 / (cfg.speed + 1);
    uint32_t now_us = millis() * 1000u;
    if ((int32_t)(next_us - now_us) > 0) eadk_timing_usleep(next_us - now_us);
    else if ((int32_t)(now_us - next_us) > 100000) next_us = now_us; /* (too far behind: start again from now) */

    /* once a second, Retro-Go's monitor: more frames skipped when the speed falls, fewer when it's back */
    uint32_t now = millis();
    if (now - second >= 1000) {
      uint32_t span = now - second, target = (uint32_t)(span * 1000u / frame_us * (cfg.speed + 1) / 2);
      uint32_t speed = target ? frames * 100 / target : 100, busy_pc = busy * 100 / span;
      if (speed > 99 && busy_pc < 85 && frameskip > 1) frameskip--;
      else if (speed < 96 && busy_pc > 85 && frameskip < 5) frameskip++;
      second = now, frames = 0, busy = 0;
    }
  }
  restore_behind = NULL;
  game = NULL;
  return result;
}

/* ------------------------------------------------------------ the launcher */
static int tab, cursor; /* the tab shown (in tabs[]), the game chosen (in roms[]) */
static bool browse;

static void picker_scene(void *ctx) {
  (void)ctx;
  int sys = ntabs ? tabs[tab] : 0;
  background(sys, browse ? 4 : 0);
  if (!ntabs) { /* (no games: Retro-Go's welcome, and where they come from) */
    header(0, 0);
    static const char *const lines[] = {T("Welcome to Retro-Go!"), "", T("Add games on NumPlay's"),
                                        T("customizer website:"), T("Game Boy, Game Boy Color, NES,"), T("Master System and Game Gear."),
                                        "", T("Home: quit")};
    for (int i = 0; i < 8; i++) gfx_text(&np_font_body, 8, 80 + 18 * i, lines[i], i ? 0xAD55 : WHITE, 256);
    return;
  }
  if (!browse) {
    header(sys, (SCREEN_H - HEADER_H) / 2);
    gfx_text_center(&np_font_small, SCREEN_W / 2, SCREEN_H - 6, T("Left/Right: system    OK: games    Home: quit"),
                    WHITE, 220);
    return;
  }
  header(sys, 0);
  int first = tab_first(sys), count = tab_count(sys), at = cursor - first;
  char s[24], *o = num(s, (uint32_t)at + 1);
  o = cat(o, " / ");
  num(o, (uint32_t)count);
  gfx_text(&np_font_small, LOGO_W + 12, HEADER_H - 5, s, 0xFFDE, 256);
  /* the list, the game chosen in the middle line (Retro-Go's center scrolling) */
  int top = HEADER_H + 6, bottom = SCREEN_H - 14, lines = (bottom - top) / ROW_H;
  top += (bottom - top - lines * ROW_H) / 2;
  for (int i = 0; i < lines; i++) {
    int idx = at - lines / 2 + i, y = top + i * ROW_H;
    if (idx < 0 || idx >= count) continue;
    char name[41];
    rg_rom_name(&roms[first + idx], name, sizeof name);
    bool sel = idx == at;
    if (sel) gfx_fill(0, y, SCREEN_W, ROW_H, WHITE);
    gfx_text(&np_font_body, 4, y + 12, name, sel ? BLACK : 0xAD55, 256);
  }
  gfx_text_center(&np_font_small, SCREEN_W / 2, SCREEN_H - 4,
                  T("OK: play    Back: systems    Left/Right: system"), WHITE, 200);
}

static void picker_draw(void) { render_rect(picker_scene, NULL, 0, 0, SCREEN_W, SCREEN_H); }

static int properties(const rg_rom_t *r) {
  char name[41], size[16], crc[12];
  rg_rom_name(r, name, sizeof name);
  cat(num(size, r->size / 1024), " KB");
  message_box(NULL, T("Reading..."));
  hex8(crc, rom_crc(r));
  item_t items[] = {{0, T("Name"), "", IT_MESSAGE, 1, NULL, 0}, {0, T("System"), "", IT_MESSAGE, 1, NULL, 0},
                    {0, T("Size"), "", IT_MESSAGE, 1, NULL, 0}, {0, "CRC32", "", IT_MESSAGE, 1, NULL, 0},
                    ITEM(1, T("Close"))};
  memcpy(items[0].value, name, sizeof items[0].value - 1);
  items[0].value[sizeof items[0].value - 1] = 0;
  strcpy(items[1].value, sys_short[r->system]);
  strcpy(items[2].value, size);
  strcpy(items[3].value, crc);
  return dialog_run(T("File properties"), items, 5, 4);
}

/* Retro-Go's menu for a game: Resume game, New game, Delete save, Properties */
static int file_menu(const rg_rom_t *r) {
  message_box(NULL, T("Reading..."));
  bool has_save = false, has_sram = false;
  for (int i = 0; i < 4; i++) has_save |= state_exists(r, i);
  char name[20];
  uint32_t len;
  save_name(name, r, -1);
  has_sram = np_file_read(name, &len) != NULL;
  item_t items[] = {ITEM(0, T("Resume game")), ITEM(1, T("New game")), ITEM(2, T("Delete save")),
                    ITEM(4, T("Properties"))};
  if (!has_save) items[0].flags = IT_DISABLED;
  if (!has_save && !has_sram) items[2].flags = IT_DISABLED;
  int sel = dialog_run(NULL, items, 4, has_save ? 0 : 1), slot = -1;
  switch (sel) {
  case 0:
    last_slot = newest_slot(r);
    slot = slot_menu(T("Resume"), r, true);
    if (slot == DLG_HOME) return GAME_EXIT;
    if (slot < 0) return GAME_PICKER;
    /* fall through */
  case 1:
    cfg_save();
    return play(r, slot);
  case 2:
    for (;;) {
      last_slot = newest_slot(r);
      slot = slot_menu(T("Delete save?"), r, true);
      if (slot == DLG_HOME) return GAME_EXIT;
      if (slot < 0) break;
      save_name(name, r, slot);
      file_delete(name);
    }
    if (has_sram) {
      int k = confirm(T("Delete sram file?"), NULL);
      if (k == DLG_HOME) return GAME_EXIT;
      if (k == 1) save_name(name, r, -1), file_delete(name);
    }
    return GAME_PICKER;
  case 4:
    return properties(r) == DLG_HOME ? GAME_EXIT : GAME_PICKER;
  case DLG_HOME:
    return GAME_EXIT;
  default:
    return GAME_PICKER;
  }
}

static void launcher_menu_redraw(void) { picker_draw(); }

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  cfg_load();
  int n = rg_romtable_read(np_roms, TABLE_LIMIT, roms, RG_MAX_ROMS);
  nroms = n > 0 ? n : 0;
  ntabs = 0;
  for (unsigned i = 0; i < sizeof tab_order; i++)
    if (tab_count(tab_order[i])) tabs[ntabs++] = tab_order[i];
  tab = cfg.tab < ntabs ? cfg.tab : 0;
  browse = cfg.browse;
  cursor = ntabs ? tab_first(tabs[tab]) + NP_MIN(cfg.cursor[tabs[tab]], tab_count(tabs[tab]) - 1) : 0;
  restore_behind = launcher_menu_redraw;
  picker_draw();
  wait_release();
  bool quit = false;
  while (!quit) {
    eadk_timing_msleep(16);
    uint64_t hit = key_hits();
    if (hit & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    if (hit & KEY(eadk_key_backspace)) { /* Retro-Go's launcher menu */
      item_t items[] = {ITEM(1, T("Options")), ITEM(2, T("Controls")), ITEM(3, T("About")), ITEM(4, T("Quit"))};
      int k = dialog_run("Retro-Go", items, 4, 0);
      if (k == DLG_HOME || k == 4) break;
      if (k == 1 && options_menu() == DLG_HOME) break;
      if (k == 2 && controls() == DLG_HOME) break;
      if (k == 3 && about() == DLG_HOME) break;
      continue;
    }
    if (!ntabs) {
      if (hit & KEY(eadk_key_back)) break;
      continue;
    }
    int sys = tabs[tab], first = tab_first(sys), count = tab_count(sys), change = 0;
    bool redraw = false;
    if (!browse) {
      if (hit & (KEY(eadk_key_left) | KEY(eadk_key_up))) change = -1;
      if (hit & (KEY(eadk_key_right) | KEY(eadk_key_down))) change = 1;
      if (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) browse = true, redraw = true;
      if (hit & KEY(eadk_key_back)) break;
    } else {
      if (hit & KEY(eadk_key_up)) cursor = first + (cursor - first + count - 1) % count, redraw = true;
      if (hit & KEY(eadk_key_down)) cursor = first + (cursor - first + 1) % count, redraw = true;
      if (hit & KEY(eadk_key_left)) change = -1;
      if (hit & KEY(eadk_key_right)) change = 1;
      if (hit & KEY(eadk_key_back)) browse = false, redraw = true;
      if (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) {
        cfg.tab = (uint8_t)tab, cfg.browse = browse, cfg.cursor[sys] = (uint8_t)(cursor - first);
        wait_release();
        int r = file_menu(&roms[cursor]);
        restore_behind = launcher_menu_redraw;
        if (r == GAME_EXIT) quit = true;
        else picker_draw();
        wait_release();
        continue;
      }
    }
    if (change && ntabs > 1) {
      cfg.cursor[sys] = (uint8_t)(cursor - first);
      tab = (tab + change + ntabs) % ntabs;
      sys = tabs[tab];
      cursor = tab_first(sys) + NP_MIN(cfg.cursor[sys], tab_count(sys) - 1);
      redraw = true;
    }
    if (redraw) picker_draw();
  }
  if (ntabs) {
    int sys = tabs[tab];
    cfg.tab = (uint8_t)tab, cfg.browse = browse, cfg.cursor[sys] = (uint8_t)(cursor - tab_first(sys));
  }
  cfg_save();
  gfx_buf = their_strip;
  return np_app_end();
}
