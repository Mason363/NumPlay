/* The Game & Watch for Retro-Go's screens: LCD-Game-Emulator's machine (gw_system.c, the SM5xx
 * processors) fed from a .gw file read in place, and its LCD drawn a band of lines at a time.
 *
 * LCD-Game-Emulator draws the whole 320 x 240 screen into a frame buffer: the background picture,
 * then each segment that is on, its pixels multiplied with what is under them. The calculator
 * hasn't the RAM for that frame, so here the same is done for 16 lines at a time, and only when a
 * segment changed. The segment rules (which RAM bits light which segment, the 2, 4 and 8-bit
 * pixels, the transparent colour) are gw_graphic.c's.
 *
 * Based on LCD-Game-Emulator by bzhxx (GPL-3.0-or-later, see LICENSE). */
#include <string.h>
#include "gw.h"
#include "gw_type_defs.h"
#include "gw_system.h"
#include "gw_romloader.h"
#include "gw_graphic.h"
#include "sm510.h"
#include "sm500.h"

/* (gw_state.c: the processor's fixed setup kept at the start and put back after a state) */
void gw_chip_keep(void);
void gw_chip_fix(void);

/* the file's parts, in flash (gw_romloader.h) */
gwromheader_t gw_head;
unsigned short *gw_background;
unsigned char *gw_segments;
unsigned short *gw_segments_x, *gw_segments_y, *gw_segments_width, *gw_segments_height;
unsigned int *gw_segments_offset;
unsigned char *gw_program, *gw_melody;
unsigned int *gw_keyboard;

#define BAND 16
static uint16_t *band;                /* BAND lines of the picture (rg_alloc) */
static uint8_t seg_on[256], seg_drawn[256];
static bool drawn_once;
static int bpp;                       /* the segments' pixels: 2, 4 or 8 bits */
static bool sm500;                    /* an SM500 or SM5A: its LCD is driven from its O pins */
static uint32_t nsegs;                /* the segments the file has */
static int pad_now, keys_now, soft;   /* soft: B+Time (bit 10) or B+Game (bit 11) held */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int gw_load(const uint8_t *data, uint32_t size) {
  if (size < sizeof(gwromheader_t) || memcmp(data, "SM5", 3)) return RG_ERR_ROM;
  memcpy(&gw_head, data, sizeof gw_head);
  /* every part inside the file, the keyboard (the last) ending it */
  const unsigned *parts = &gw_head.background_pixel;
  for (int i = 0; i < 20; i += 2) {
    uint32_t at = parts[i], n = parts[i + 1];
    if (at > size || n > size - at) return RG_ERR_ROM;
  }
  if (gw_head.keyboard + gw_head.keyboard_size != size || gw_head.keyboard_size < 40 || !gw_head.program_size ||
      gw_head.background_pixel_size != GW_WIDTH * GW_HEIGHT * 2 || (gw_head.background_pixel & 1) ||
      (gw_head.segments_offset & 3) || (gw_head.keyboard & 3) || ((gw_head.segments_x | gw_head.segments_y |
       gw_head.segments_width | gw_head.segments_height) & 1))
    return RG_ERR_ROM; /* (a JPEG background, or not a .gw file at all) */
  uint8_t *rom = (uint8_t *)(uintptr_t)data;
  gw_background = (unsigned short *)(void *)(rom + gw_head.background_pixel);
  gw_segments = rom + gw_head.segments_pixel;
  gw_segments_x = (unsigned short *)(void *)(rom + gw_head.segments_x);
  gw_segments_y = (unsigned short *)(void *)(rom + gw_head.segments_y);
  gw_segments_width = (unsigned short *)(void *)(rom + gw_head.segments_width);
  gw_segments_height = (unsigned short *)(void *)(rom + gw_head.segments_height);
  gw_segments_offset = (unsigned int *)(void *)(rom + gw_head.segments_offset);
  gw_program = rom + gw_head.program;
  gw_melody = gw_head.melody_size ? rom + gw_head.melody : NULL;
  gw_keyboard = (unsigned int *)(void *)(rom + gw_head.keyboard);
  /* the segments: each one's place inside the screen and its pixels inside the file */
  bpp = gw_head.flags & FLAG_SEGMENTS_2BITS ? 2 : gw_head.flags & FLAG_SEGMENTS_4BITS ? 4 : 8;
  nsegs = gw_head.segments_offset_size / 4;
  for (int a = 0; a < 4; a++) {
    uint32_t n = (a == 0 ? gw_head.segments_x_size : a == 1 ? gw_head.segments_y_size
                : a == 2 ? gw_head.segments_width_size : gw_head.segments_height_size) / 2;
    if (n < nsegs) nsegs = n;
  }
  if (nsegs > 256) nsegs = 256;
  for (uint32_t s = 0; s < nsegs; s++) {
    uint32_t x = gw_segments_x[s], y = gw_segments_y[s], w = gw_segments_width[s], h = gw_segments_height[s];
    uint64_t end = bpp == 8 ? rd32((const uint8_t *)&gw_segments_offset[s]) + (uint64_t)w * h
                 : bpp == 4 ? (rd32((const uint8_t *)&gw_segments_offset[s]) + (uint64_t)w * h + 1) / 2
                            : (rd32((const uint8_t *)&gw_segments_offset[s]) + (uint64_t)w * h + 3) / 4;
    if (x + w > GW_WIDTH || y + h > GW_HEIGHT || end > gw_head.segments_pixel_size) return RG_ERR_ROM;
  }
  sm500 = !strncmp(gw_head.cpu_name, ROM_CPU_SM500, 5) || !strncmp(gw_head.cpu_name, ROM_CPU_SM5A, 5);
  if (gw_melody && gw_head.melody_size < 0x100) return RG_ERR_ROM;
  rg_alloc_reset();
  if (!gw_system_config()) return RG_ERR_MAPPER; /* (a processor it doesn't have) */
  band = rg_alloc(GW_WIDTH * BAND * 2);
  if (!band) return RG_ERR_MEMORY;
  gw_system_sound_init();
  gw_system_start();
  gw_chip_keep();
  gw_reset(true);
  return RG_OK;
}

/* the buttons LCD-Game-Emulator reads: left, up, right, down, A, B, Time, Game, then B+Time and
   B+Game as the console's shortcuts (bits 10 and 11) */
unsigned int gw_get_buttons(void) {
  unsigned b = 0;
  if (pad_now & RG_PAD_LEFT) b |= GW_BUTTON_LEFT;
  if (pad_now & RG_PAD_UP) b |= GW_BUTTON_UP;
  if (pad_now & RG_PAD_RIGHT) b |= GW_BUTTON_RIGHT;
  if (pad_now & RG_PAD_DOWN) b |= GW_BUTTON_DOWN;
  if (pad_now & RG_PAD_A) b |= GW_BUTTON_A;
  if (pad_now & RG_PAD_B) b |= GW_BUTTON_B;
  if (keys_now & GW_KEY_TIME) b |= GW_BUTTON_TIME;
  if (keys_now & GW_KEY_GAME_A) b |= GW_BUTTON_GAME;
  return b | (unsigned)soft;
}

void gw_set_pad(int pad, int keys) {
  pad_now = pad, keys_now = keys;
  soft = keys & GW_KEY_ALARM ? 1 << 10 : keys & GW_KEY_GAME_B ? 1 << 11 : 0;
}

int gw_keys(void) {
  int k = 0;
  for (int s = 0; s < 8; s++)
    for (int i = 0; i < 4; i++) {
      unsigned v = (rd32((const uint8_t *)&gw_keyboard[s]) >> (8 * i)) & 0xFF;
      if (v == GW_BUTTON_GAME) k |= GW_KEY_GAME_A;
      if (v == GW_BUTTON_TIME) k |= GW_KEY_TIME;
      if (v == GW_BUTTON_B + GW_BUTTON_GAME) k |= GW_KEY_GAME_B;
      if (v == GW_BUTTON_B + GW_BUTTON_TIME) k |= GW_KEY_ALARM;
    }
  return k;
}

static void run(int cycles, int times) {
  for (int i = 0; i < times; i++) gw_system_run(cycles);
}

void gw_reset(bool hard) {
  gw_system_reset();
  memset(seg_drawn, 0, sizeof seg_drawn), drawn_once = false;
  if (!hard) return;
  /* as Retro-Go starts one: the watch let run out of its setting mode, set, and running (the
     calculator has no clock for apps: 12:00) */
  int pad = pad_now, keys = keys_now;
  gw_set_pad(0, 0);
  run(GW_SYSTEM_CYCLES, GW_REFRESH_RATE * 2);
  gw_set_pad(0, GW_KEY_TIME);
  run(GW_SYSTEM_CYCLES, GW_REFRESH_RATE / 2);
  gw_set_pad(0, 0);
  run(GW_SYSTEM_CYCLES, GW_REFRESH_RATE * 2);
  gw_system_set_time((gw_time_t){12, 0, 0});
  run(GW_SYSTEM_CYCLES, GW_REFRESH_RATE);
  gw_set_pad(RG_PAD_A, 0); /* (some games want A) */
  run(GW_SYSTEM_CYCLES, GW_REFRESH_RATE / 2);
  gw_set_pad(0, 0);
  run(GW_SYSTEM_CYCLES, GW_REFRESH_RATE);
  gw_set_pad(pad, keys);
}

/* ---- which segments are on (gw_graphic.c's two LCD controllers) */
static void segments_sm510(void) {
  bool blank = m_bc || !m_bp, defl = flag_lcd_deflicker_level != 0;
  for (int y = 0; y < NB_SEGS_ROW; y++) {
    uint8_t a = (defl ? gw_ram_state : gw_ram)[ADD_SEGA_BASE + y] & 0xf;
    uint8_t b = (defl ? gw_ram_state : gw_ram)[ADD_SEGB_BASE + y] & 0xf;
    uint8_t c = (defl ? gw_ram_state : gw_ram)[ADD_SEGC_BASE + y] & 0xf;
    for (int z = 0; z < NB_SEGS_COL; z++) {
      int p = 4 * y + z;
      seg_on[p] = !blank && (a >> z & 1);
      seg_on[p + 64] = !blank && (b >> z & 1);
      seg_on[p + 192] = !blank && (c >> z & 1);
    }
  }
  uint8_t blink = (m_div & 0x4000) ? m_y : 0;
  for (int z = 0; z < NB_SEGS_COL; z++) {
    seg_on[128 + z] = !blank && ((m_l & ~blink) >> z & 1);
    seg_on[132 + z] = !blank && ((m_x & ~blink) >> z & 1);
  }
}
static void segments_sm500(void) {
  bool defl = flag_lcd_deflicker_level != 0;
  for (int h = 0; h < 2; h++)
    for (int o = 0; o < m_o_pins && o < 9; o++) {
      uint8_t seg = defl ? (h ? m_ox_state[o] : m_o_state[o]) : (h ? m_ox[o] : m_o[o]);
      for (int k = 0; k < 4; k++)
        if (8 * o + 2 * k + h < 256) seg_on[8 * o + 2 * k + h] = m_bp && (seg >> k & 1);
    }
}

/* (gw_system.c's hooks into gw_graphic.c: the LCD is drawn here instead) */
void gw_gfx_init(void) { flag_lcd_deflicker_level = (gw_head.flags & FLAG_LCD_DEFLICKER_MASK) >> 6; }
void gw_gfx_sm500_rendering(uint16 *framebuffer) { (void)framebuffer; }
void gw_gfx_sm510_rendering(uint16 *framebuffer) { (void)framebuffer; }

/* rgb_multiply_8bits: the background's colour dimmed by the segment's pixel */
static inline uint16_t multiply(uint32_t bg, uint32_t sg) {
  uint32_t r = ((bg >> 11) * sg) >> 8, g = (((bg >> 5) & 63) * sg) >> 8, b = ((bg & 31) * sg) >> 8;
  return (uint16_t)(r << 11 | g << 5 | b);
}

/* lines [y0, y1) of the screen into band */
static void draw_band(int y0, int y1) {
  bool inverted = gw_head.flags & FLAG_RENDERING_LCD_INVERTED;
  uint8_t clear = inverted ? 0x00 : 0xFF; /* (the segments' transparent pixel) */
  if (inverted) memset(band, 0, (size_t)(y1 - y0) * GW_WIDTH * 2);
  else memcpy(band, gw_background + y0 * GW_WIDTH, (size_t)(y1 - y0) * GW_WIDTH * 2);
  for (uint32_t s = 0; s < nsegs; s++) {
    if (!seg_on[s]) continue;
    int sx = gw_segments_x[s], sy = gw_segments_y[s], w = gw_segments_width[s], h = gw_segments_height[s];
    int ys = sy > y0 ? sy : y0, ye = sy + h < y1 ? sy + h : y1;
    if (ys >= ye || !w) continue;
    uint32_t off = rd32((const uint8_t *)&gw_segments_offset[s]);
    for (int y = ys; y < ye; y++) {
      uint16_t *row = band + (y - y0) * GW_WIDTH + sx;
      const uint16_t *mix = inverted ? gw_background + y * GW_WIDTH + sx : row;
      uint32_t idx = (uint32_t)(y - sy) * (uint32_t)w;
      for (int x = 0; x < w; x++, idx++) {
        uint8_t px;
        if (bpp == 8) {
          px = gw_segments[off + idx];
        } else if (bpp == 4) {
          uint32_t i = (off & 1) + idx;
          px = (i & 1) ? (uint8_t)(gw_segments[(off >> 1) + (i >> 1)] << 4) : gw_segments[(off >> 1) + (i >> 1)] & 0xF0;
          px |= px >> 4;
        } else {
          uint32_t i = (off & 3) + idx;
          uint8_t v = (gw_segments[(off >> 2) + (i >> 2)] >> (2 * (i & 3))) & 3;
          px = (uint8_t)(v | v << 2 | v << 4 | v << 6);
          if (px != clear && !px) px = 39; /* (black lets a little through) */
        }
        if (px != clear) row[x] = multiply(mix[x], px);
      }
    }
  }
}

static void draw(void) {
  for (int y = 0; y < GW_HEIGHT; y += BAND) {
    draw_band(y, y + BAND);
    for (int j = 0; j < BAND; j++) rg_line(y + j, band + j * GW_WIDTH);
  }
}

void gw_run_frame(bool want) {
  run(GW_SYSTEM_CYCLES, 2);
  if (!want) return;
  if (sm500) segments_sm500();
  else segments_sm510();
  if (drawn_once && !memcmp(seg_on, seg_drawn, sizeof seg_on)) return;
  memcpy(seg_drawn, seg_on, sizeof seg_on), drawn_once = true;
  draw();
}

void gw_redraw(void) { draw(); }

int gw_state_chunks(rg_chunk_t *out) {
  out[0] = (rg_chunk_t){&sm510, sizeof sm510};
  return 1;
}
void gw_state_loaded(void) {
  gw_chip_fix();
  drawn_once = false;
}
