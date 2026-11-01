/* Famidash's game code in C: the level decoder, collision, the eight gamemodes, orbs, pads and portals,
 * the camera, practice mode and the sprites. A port of tfdsoft's Famidash (MIT): SAUCE/gamestates/
 * state_game.h, SAUCE/gamemodes/, SAUCE/functions/ and the parts of LIB/asm/nesdash.s they call. The
 * variable names, their sizes and the order of every step are the NES's, so its physics come out the same
 * to the subpixel. What only the NES needed (banks, the PPU's nametables, the sound driver) is gone. */
#include "fd.h"
#ifdef FD_BOT /* (tests/bot.c: the game's state in a section of its own, to save and restore it) */
#pragma clang section bss = "__DATA,__fdbss" data = "__DATA,__fddata"
#endif

#define OAM_FLIP_H 0x40
#define OAM_FLIP_V 0x80
#define OAM_BEHIND 0x20
#include "sprites_fd.h"

/* ---------------------------------------------------------------- the NES's globals (famidash.h) */
pad_t joypad1, joypad2, *controllingplayer = &joypad1;
progress_t prog;

static uint8_t tmp1, tmp2, tmp3, tmp4, tmp7, tmp8, tmp9;
static uint16_t tmp5, tmp6, tmpA;
static int16_t tmpfallspeed, tmpgravity;

static uint8_t currplayer_mini;
static uint16_t currplayer_relx, currplayer_rely;
static int16_t currplayer_vel_x, currplayer_vel_y;
static uint8_t currplayer_gravity;
static int8_t currplayer_slope_frames;
static uint8_t currplayer_was_on_slope_counter, currplayer_slope_type, currplayer_last_slope_type, currplayer_direction;
static uint8_t currplayer_table_idx; /* bit 0 gravity, 1 mini, 2 framerate (always NTSC here) */

uint8_t gamemode, cube_data[2];
static uint16_t cube_rotate[2];
static uint8_t collision, eject_D, eject_U;
static uint16_t old_x;
static uint8_t temp_x;
static uint16_t temp_y;
uint8_t dual;
static int8_t slope_frames[2];
static uint8_t slope_type[2], was_on_slope_counter[2], last_slope_type[2];
static struct { uint8_t x, y, width, height; } Generic, Generic2;
static uint16_t transitional_linear_absolute_currplayer_y;
static uint8_t transitional_sixteen_minus_generic_height_halved;

uint8_t icon, icon_colors[3] = {0x2A, 0x2C, 0x0F}, options, invisible, invisblocks, retro_mode, discomode, cam_seesaw,
    viseffects = 1, trails, auto_practicepoints = 1, gameboy_mode, twoplayer;
#define color1 icon_colors[0]
#define color2 icon_colors[1]
#define color3 icon_colors[2]

uint8_t practice_point_count, curr_practice_point;
static uint8_t latest_practice_point, practicebuffer, practice_sprite_x_pos;
static uint16_t auto_practicepoint_timer;
static struct {
  uint16_t p1x, p2x, p1y, p2y, p1vx, p2vx, p1vy, p2vy, rot1, rot2;
  uint8_t g1, g2, m1, m2, wos1, wos2;
  int8_t sf1, sf2;
  uint8_t st1, st2, lst1, lst2;
  uint32_t scroll_x;
  uint16_t scroll_y, target_scroll_y, min_scroll_y;
  uint8_t scroll_y_subpx, nocamlockforced, disco_sprites, slowmode, forced_trails, gravity_mod, kandoframecnt;
  uint8_t player_invis, gamemode, dual, speed, parallax_scroll_x, bgtype, gtype, outline, orbactive, null_active,
      null_type;
} practice[MAX_PRACTICE_POINTS];

uint8_t gameState = STATE_MENU, level;
uint16_t player_relx[2], player_rely[2];
int16_t player_vel_x[2], player_vel_y[2];
uint8_t player_gravity[2], player_mini[2];
static uint8_t orbhitonthisframe[2], chargepower[2];
static uint8_t orbed[2], ball_switched[2], processXMovement;
uint16_t jumps;
uint8_t speed, coins, kandoframecnt;
static uint8_t currplayer, shuffle_offset;
static uint8_t spiderframe[2], robotframe[2], ballframe, robotjumpframe[2], robotjumptime[2];
static uint8_t hblocked[2], jblocked[2], fblocked[2], dblocked[2], ninjajumps[2];
static uint8_t slowmode, kandokidshack4;
static uint16_t triggers;
static uint8_t triggers_hit[3];
uint8_t current_deco_type, current_spike_set, current_block_set, current_saw_set;
static uint8_t nocamlock, nocamlockforced, nullscapes_orb_type, nullscapes_active, teleport_output;
static uint8_t level_resetting_flag, timewarp_done;
uint8_t parallax_scroll_x;
static uint8_t invincible_counter;
uint32_t scroll_x;
uint16_t scroll_y;
static uint8_t scroll_y_subpx;
static uint16_t old_trail_scroll_y, target_scroll_y;
static uint8_t make_cube_jump_higher, fartmode, animating;
static uint8_t coin1_timer, coin2_timer, coin3_timer;
static uint16_t coin1_speed, coin2_speed, coin3_speed;
static uint16_t spawn_y_pos, spawn_scroll_y_pos;
static uint8_t max_fallspeed_7, orbactive, trail_sprites_visible[9], ufo_orbed[2], black_orbed[2], dashing[2];
static uint8_t wrap_mode;
uint8_t minicoins;
static uint16_t activesprites_x[max_loaded_sprites], activesprites_y[max_loaded_sprites];
static uint8_t activesprites_type[max_loaded_sprites], activesprites_anim_frame[max_loaded_sprites];
static int8_t activesprites_anim_frame_count[max_loaded_sprites];
static uint8_t activesprites_realx[max_loaded_sprites], activesprites_realy[max_loaded_sprites];
static uint8_t activesprites_active[max_loaded_sprites];
/* activated, then animated, one after the other as on the NES: the pogo and the snake bump
 * activated[16] (the loop's last index), which is animated[0] */
static uint8_t act_anim[2 * max_loaded_sprites];
#define activesprites_activated act_anim
#define activesprites_animated (act_anim + max_loaded_sprites)
uint8_t lastgcolortype, lastbgcolortype;
static uint8_t player_old_rely[9], discorefreshrate, discoframe;
uint8_t no_parallax;
static uint8_t force_platformer;
uint8_t outline_color;
static uint8_t forced_trails;
uint8_t attemptCounter[7], pauseStatus;
static uint16_t target_x_scroll_stop, curr_x_scroll_stop;
static uint8_t disco_sprites, gravity_mod;
uint8_t END_LEVEL_TIMER;
static uint8_t player_invis, exitPortalTimer;
static uint8_t spr_index; /* the original's "index" */
uint8_t all_levels_complete;

/* the level decoder and its window (nesdash.s) */
static uint8_t rld_value, rld_run, rld_load_value, rld_column, extceil, drawing_frame;
static uint16_t min_scroll_y;
static uint8_t parallax_scroll_column, parallax_scroll_column_start;
static const uint8_t *sprite_data; /* NULL once the list has ended */

uint8_t PAL_BUF_RAW[32];

/* ---------------------------------------------------------------- small things from neslib/nesdash.s */
uint32_t rng_seed = 0xFDFDFDFD;
uint8_t newrand(void) { /* neslib's newrand: 8 steps of a 32 bit Galois LFSR */
  uint8_t a = (uint8_t)rng_seed;
  uint32_t hi = rng_seed >> 8;
  for (int i = 0; i < 8; i++) {
    uint8_t c = a >> 7;
    a = (uint8_t)(a << 1);
    uint32_t c2 = (hi >> 23) & 1;
    hi = ((hi << 1) | c) & 0xFFFFFF;
    if (c2) a ^= 0xC5;
  }
  rng_seed = hi << 8 | a;
  return a;
}

static const uint8_t bright_flat[16 * 9 + 48] = {
#define F16(v) v, v, v, v, v, v, v, v, v, v, v, v, v, v, v, v
    F16(0x0f), F16(0x0f), F16(0x0f), F16(0x0f),
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x10, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    F16(0x30), F16(0x30), F16(0x30), F16(0x30)};
uint8_t colBrightness(uint8_t c, uint8_t b) { return bright_flat[16 * b + (c & 0x3F)]; }
uint8_t oneShadeDarker(uint8_t c) { return colBrightness(c, 3); }
void pal_col(uint8_t i, uint8_t c) { PAL_BUF_RAW[i & 0x1F] = c; }

static void update_currplayer_table_idx(void) {
  currplayer_table_idx = (uint8_t)((currplayer_gravity & 0x80 ? 1 : 0) | (currplayer_mini ? 2 : 0) | 4);
}
#define T4(tbl) ((int16_t)tbl[currplayer_table_idx & 3])
#define invert_gravity(v) ((v) ^= 0xFF)

/* ---------------------------------------------------------------- the level: LZ, then a vertical RLE */
static const uint8_t *lz_addr;
static uint8_t lz_done, lz_buf[256], lz_buf2[257], lz_buf_addr, lz_copy_left;
static void lz_init(const uint8_t *p) {
  lz_addr = p, lz_done = 0, lz_buf_addr = 0, lz_copy_left = 0;
  memset(lz_buf, 0, sizeof lz_buf);
}
static uint8_t lz_get(void) {
  if (lz_done) return 0;
  if (lz_copy_left) {
    uint8_t y = (uint8_t)(lz_copy_left - 1), a = lz_buf2[y];
    lz_buf[lz_buf_addr++] = a;
    lz_copy_left = y;
    return a;
  }
  uint8_t b = *lz_addr++;
  if (b == 0xFF) { lz_done = 0xFF; return 0; }
  if (b == 0x80) {
    uint8_t a = *lz_addr++;
    lz_buf[lz_buf_addr++] = a;
    return a;
  }
  uint8_t y = *lz_addr++;
  for (int x = b - 1; x >= 0; x--) lz_buf2[x + 1] = lz_buf[y++];
  lz_buf2[0] = *lz_addr++;
  uint8_t yy = b, a = lz_buf2[yy];
  lz_buf[lz_buf_addr++] = a;
  lz_copy_left = yy;
  return a;
}

/* the decoded columns: a ring of 32 (the NES kept 16; this screen is wider) */
#define RING 32
static uint8_t ring[RING][57];
static int32_t ring_next;   /* columns decoded so far */
static int32_t coll_cols;   /* columns the NES has put into its collision map (its draw_screen) */
static uint8_t column_buffer[57];

static void unrle_next_column(void) {
  uint8_t x = rld_load_value, a = rld_value;
  for (;;) {
    column_buffer[(uint8_t)(x - (0x100 - 57))] = a;
    if ((int8_t)--rld_run < 0) {
      uint8_t col_idx = x;
    read_new:;
      uint8_t b = lz_get();
      if (b & 0x80) {
        rld_value = a = b & 0x7f, rld_run = 0;
      } else {
        rld_run = b;
        uint8_t v = lz_get();
        if (v == rld_run && v == 0x7F) { /* meta sequence: next chunk (the full levels here have none) */
          lz_get();
          goto read_new;
        }
        rld_value = a = v;
      }
      x = (uint8_t)(col_idx + 1);
      if (!(x & 0x80)) break;
      continue;
    }
    x++;
    if (!(x & 0x80)) break;
  }
  rld_column = (rld_column + 1) & 0x0F;
}

static void decode_column(void) {
  unrle_next_column();
  memcpy(ring[ring_next & (RING - 1)], column_buffer, 57);
  ring_next++;
}
static void decode_to(int32_t n) {
  while (ring_next < n) decode_column();
}

uint8_t level_metatile(int32_t col, int row) {
  if (row >= 57) return row < 60 ? ground_mt[(row - 57) * 16 + (col & 15)] : 0;
  if (row < 0 || col < 0 || col >= ring_next || col < ring_next - RING) return 0;
  return ring[col & (RING - 1)][row];
}

static uint8_t spawn_dummy;
static void init_rld(uint8_t lv) {
  const level_t *L = &level_list[lv];
  const uint8_t *h = L->hdr;
  sprite_data = L->spr;
  no_parallax = 0, max_fallspeed_7 = 0, rld_column = 0;
  spawn_dummy = h[0]; /* (the song) */
  speed = h[1] >> 4, gamemode = h[1] & 0x0F;
  spawn_y_pos = (uint16_t)(h[2] << 8);
  spawn_scroll_y_pos = (uint16_t)(0x200 | h[3]);
  force_platformer = (uint8_t)((force_platformer << 1) | (h[4] & 1));
  no_parallax = h[4] >> 1;
  max_fallspeed_7 = h[5] >> 7;
  current_deco_type = h[5] & 0x7F;
  current_spike_set = h[6], current_block_set = h[7];
  uint8_t c = h[8] & 0x3F;
  PAL_BUF_RAW[0] = c;
  if (lastbgcolortype == 0xFF) lastbgcolortype = c;
  PAL_BUF_RAW[1] = PAL_BUF_RAW[9] = PAL_BUF_RAW[13] = oneShadeDarker(c);
  c = h[9] & 0x3F;
  PAL_BUF_RAW[6] = c;
  if (lastgcolortype == 0xFF) lastgcolortype = c;
  PAL_BUF_RAW[5] = oneShadeDarker(c);
  uint8_t height = h[10];
  rld_load_value = (uint8_t)(0x100 - height);
  extceil = height >= 28 ? 0x80 : 0;
  min_scroll_y = (uint16_t)((57 - height) * 16 + 8);
  lz_init(L->lz);
  uint8_t b = lz_get();
  if (b & 0x80) rld_value = b & 0x7f, rld_run = 0;
  else rld_run = b, rld_value = lz_get();
  ring_next = 0, coll_cols = 0;
  memset(ring, 0, sizeof ring);
}

/* draw_screen, for its timing: a column goes into the collision map when the scroll reaches it and the
 * picture work of the column before is done (three frames a column) */
static void draw_screen(void) {
  if (drawing_frame) {
    drawing_frame = drawing_frame == 1 ? 2 : 0;
    return;
  }
  /* (rld_column is the count of columns put in the map, mod 16: the decoder may have run ahead of it for
   * the wider screen) */
  if ((uint8_t)((scroll_x & 0xFF) >> 4) == (uint8_t)(coll_cols & 0x0F)) {
    decode_to(coll_cols + 1);
    coll_cols++;
    drawing_frame = 1;
  }
}

void increase_parallax_scroll_column(void) {
  parallax_scroll_column++;
  if (parallax_scroll_column >= 6) {
    parallax_scroll_column = 0;
    parallax_scroll_column_start += 3;
    if (parallax_scroll_column_start >= 9) parallax_scroll_column_start = 0;
  }
}

/* ---------------------------------------------------------------- collision (collision.h) */
#define COL_TOP_CENTER_SPIKE 0x3d
#define COL_BOTTOM_CENTER_SPIKE 0x3c
#define COL_TOP_SPIKES 0x3b
#define COL_TOP_LEFT_STAIRS 0x3a
#define COL_TOP_RIGHT_STAIRS 0x39
#define COL_BOTTOM_LEFT_STAIRS 0x38
#define COL_BOTTOM_RIGHT_STAIRS 0x37
#define COL_DEATH_BOTTOM_LEFT 0x36
#define COL_DEATH_BOTTOM_RIGHT 0x35
#define COL_DEATH_TOP_LEFT 0x34
#define COL_DEATH_TOP_RIGHT 0x33
#define COL_UP_BOTH_SPIKES 0x32
#define COL_UP_RIGHT_SPIKE 0x31
#define COL_UP_LEFT_SPIKE 0x30
#define COL_BOTTOM_SPIKES 0x2f
#define COL_BOTTOM_RIGHT_SPIKE 0x2e
#define COL_BOTTOM_LEFT_SPIKE 0x2d
#define COL_RIGHT_SPIKE_BLOCK 0x2c
#define COL_LEFT_SPIKE_BLOCK 0x2b
#define COL_DOWN_BOTH_SPIKES 0x2a
#define COL_DOWN_RIGHT_SPIKE 0x29
#define COL_DOWN_LEFT_SPIKE 0x28
#define COL_TOP_RIGHT_BOTTOM_LEFT 0x27
#define COL_TOP_LEFT_BOTTOM_RIGHT 0x26
#define COL_RIGHT 0x25
#define COL_LEFT 0x24
#define COL_DOWN_RIGHT 0x23
#define COL_DOWN_LEFT 0x22
#define COL_UP_RIGHT 0x21
#define COL_UP_LEFT 0x20
#define COL_NO_SIDE 0x1f
#define COL_SLOPE_LU66_BOT 0x1e
#define COL_SLOPE_RU66_BOT 0x1d
#define COL_SLOPE_LU66_TOP 0x1c
#define COL_SLOPE_RU66_TOP 0x1b
#define COL_SLOPE_LU22_RIGHT 0x1a
#define COL_SLOPE_LU22_LEFT 0x19
#define COL_SLOPE_RU22_RIGHT 0x18
#define COL_SLOPE_RU22_LEFT 0x17
#define COL_SLOPE_LU45 0x16
#define COL_SLOPE_RU45 0x15
#define COL_SLOPE_LD66_BOT 0x14
#define COL_SLOPE_RD66_BOT 0x13
#define COL_SLOPE_LD66_TOP 0x12
#define COL_SLOPE_RD66_TOP 0x11
#define COL_SLOPE_LD22_LEFT 0x10
#define COL_SLOPE_RD22_LEFT 0x0f
#define COL_SLOPE_LD22_RIGHT 0x0e
#define COL_SLOPE_RD22_RIGHT 0x0d
#define COL_SLOPE_LD45 0x0c
#define COL_SLOPE_RD45 0x0b
#define COL_FLOOR_CEIL 0x09
#define COL_DEATH 0x08
#define COL_ALL 0x07
#define COL_BOTTOM 0x06
#define COL_TOP 0x05
#define COL_DEATH_BOTTOM 0x04
#define COL_DEATH_TOP 0x03
#define COL_DEATH_LEFT 0x02
#define COL_DEATH_RIGHT 0x01

#define SLOPE_NONE 0
#define SLOPE_45DEG 0x01
#define SLOPE_22DEG 0x02
#define SLOPE_66DEG 0x03
#define SLOPE_RISING 0x04
#define SLOPE_UPSIDEDOWN 0x08
#define SLOPE_DEGREES_MASK 0x03
#define SLOPE_45DEG_UP (SLOPE_45DEG | SLOPE_RISING)
#define SLOPE_45DEG_DOWN (SLOPE_45DEG)
#define SLOPE_22DEG_UP (SLOPE_22DEG | SLOPE_RISING)
#define SLOPE_22DEG_DOWN (SLOPE_22DEG)
#define SLOPE_66DEG_UP (SLOPE_66DEG | SLOPE_RISING)
#define SLOPE_66DEG_DOWN (SLOPE_66DEG)
#define SLOPE_45DEG_UP_UD (SLOPE_45DEG | SLOPE_RISING | SLOPE_UPSIDEDOWN)
#define SLOPE_45DEG_DOWN_UD (SLOPE_45DEG | SLOPE_UPSIDEDOWN)
#define SLOPE_22DEG_UP_UD (SLOPE_22DEG | SLOPE_RISING | SLOPE_UPSIDEDOWN)
#define SLOPE_22DEG_DOWN_UD (SLOPE_22DEG | SLOPE_UPSIDEDOWN)
#define SLOPE_66DEG_UP_UD (SLOPE_66DEG | SLOPE_RISING | SLOPE_UPSIDEDOWN)
#define SLOPE_66DEG_DOWN_UD (SLOPE_66DEG | SLOPE_UPSIDEDOWN)

#define IN_RANGE(v, lo, hi) ((uint8_t)((uint8_t)(v) - (lo)) <= (uint8_t)((hi) - (lo)))

/* bg_collision_sub: the metatile under (temp_x, temp_y). The NES's map holds 16 columns; temp_x is its
 * low byte, so a column is the one of the last 16 written that matches it. */
static void bg_collision_sub(void) {
  int row = (temp_y >> 4) & 0x3F;
  int32_t base = coll_cols - 16;
  int32_t col = base + (((temp_x >> 4) - base) & 15);
  collision = mt_coll[level_metatile(col, row)];
  if (row >= 60) collision = 0;
}

static uint8_t bg_coll_sides(void) {
  switch (collision) {
  case COL_ALL: return 1;
  case COL_BOTTOM:
    if ((uint8_t)(temp_y & 0x08) && !force_platformer) return 1;
    break;
  case COL_TOP:
    if (!(uint8_t)(temp_y & 0x08) && !force_platformer) return 1;
    break;
  case COL_FLOOR_CEIL:
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) return 0;
  }
  return 0;
}

static uint8_t col_death_bottom_routine(void) {
  if ((uint8_t)(temp_y & 0x0f) > 0x0a && IN_RANGE(temp_x & 0x0f, 0x05, 0x07)) { cube_data[currplayer] = 1; return 1; }
  return 0;
}
static uint8_t col_death_top_routine(void) {
  if ((uint8_t)(temp_y & 0x0f) < 0x06 && IN_RANGE(temp_x & 0x0f, 0x05, 0x07)) { cube_data[currplayer] = 1; return 1; }
  return 0;
}
static uint8_t col_death_right_routine(void) {
  if ((uint8_t)(temp_x & 0x0f) >= 0x0a && IN_RANGE(temp_y & 0x0f, 0x06, 0x08)) { cube_data[currplayer] = 1; return 1; }
  return 0;
}
static uint8_t col_death_left_routine(void) {
  if ((uint8_t)(temp_x & 0x0f) < 0x06 && IN_RANGE(temp_y & 0x0f, 0x06, 0x08)) { cube_data[currplayer] = 1; return 1; }
  return 0;
}

static uint8_t bg_coll_spikes(void) {
  switch (collision) {
  case COL_DEATH_LEFT: return col_death_left_routine();
  case COL_DEATH_RIGHT: return col_death_right_routine();
  case COL_DEATH_TOP: return col_death_top_routine();
  case COL_TOP_CENTER_SPIKE:
  case COL_TOP_SPIKES:
  case COL_DEATH_BOTTOM: return col_death_bottom_routine();
  case COL_DEATH_BOTTOM_LEFT: tmp2 = col_death_left_routine(); return col_death_bottom_routine() | tmp2;
  case COL_DEATH_BOTTOM_RIGHT: tmp2 = col_death_right_routine(); return col_death_bottom_routine() | tmp2;
  case COL_DEATH_TOP_LEFT: tmp2 = col_death_left_routine(); return col_death_top_routine() | tmp2;
  case COL_DEATH_TOP_RIGHT: tmp2 = col_death_right_routine(); return col_death_top_routine() | tmp2;
  case COL_DEATH:
    tmp2 = (uint8_t)(temp_y & 0x0f);
    if (tmp2 >= 0x04 && tmp2 < 0x0c && IN_RANGE(temp_x & 0x0f, 0x04, 0x08)) goto die;
    return 0;
  case COL_LEFT_SPIKE_BLOCK:
  case COL_BOTTOM_LEFT_SPIKE:
  case COL_UP_LEFT_SPIKE:
    if (!(uint8_t)(temp_y & 0x08) && IN_RANGE(temp_x & 0x0f, 0x02, 0x05)) goto die;
    return 0;
  case COL_RIGHT_SPIKE_BLOCK:
  case COL_BOTTOM_RIGHT_SPIKE:
  case COL_UP_RIGHT_SPIKE:
    if (!(uint8_t)(temp_y & 0x08) && IN_RANGE(temp_x & 0x0f, 0x0a, 0x0c)) goto die;
    return 0;
  case COL_BOTTOM_CENTER_SPIKE:
    if (!(uint8_t)(temp_y & 0x08) && IN_RANGE(temp_x & 0x0f, 0x07, 0x0a)) goto die;
    return 0;
  case COL_BOTTOM_SPIKES:
  case COL_UP_BOTH_SPIKES:
    if (!(uint8_t)(temp_y & 0x08) && IN_RANGE(temp_x & 0x07, 0x02, 0x05)) goto die;
    return 0;
  case COL_DOWN_LEFT_SPIKE:
    if ((uint8_t)(temp_y & 0x08) && IN_RANGE(temp_x & 0x0f, 0x02, 0x05)) goto die;
    return 0;
  case COL_DOWN_RIGHT_SPIKE:
    if ((uint8_t)(temp_y & 0x08) && IN_RANGE(temp_x & 0x0f, 0x0a, 0x0c)) goto die;
    return 0;
  case COL_DOWN_BOTH_SPIKES:
    if ((uint8_t)(temp_y & 0x08) && IN_RANGE(temp_x & 0x07, 0x02, 0x05)) goto die;
    return 0;
  default: return 0;
  }
die:
  cube_data[currplayer] = 1;
  return 1;
}

static void bg_coll_floor_spikes(void) {
  if (currplayer_vel_y) currplayer_direction = high_byte(currplayer_vel_y) & 0x80;
  temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + 3);
  temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                      (uint8_t)((currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0) + Generic.height - 2));
  bg_collision_sub();
  if (collision && bg_coll_spikes()) return;
  temp_x = (uint8_t)(temp_x + Generic.width - 6);
  bg_collision_sub();
  if (collision && bg_coll_spikes()) return;
  temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + 3);
  temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                      (uint8_t)(currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 2));
  bg_collision_sub();
  if (collision && bg_coll_spikes()) return;
  temp_x = (uint8_t)(temp_x + Generic.width - 6);
  bg_collision_sub();
  if (collision && bg_coll_spikes()) return;
}

static uint8_t bg_coll_top_bottom_slabs(void) {
  switch (collision) {
  case COL_BOTTOM:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp8 != tmp2) return 1;
    break;
  case COL_TOP:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp8 == tmp2) return 1;
    break;
  }
  return 0;
}

static uint8_t bg_coll_mini_blocks(void) {
  if (collision != COL_FLOOR_CEIL && high_byte(currplayer_relx) < 0x10) return 0;
  uint8_t tmp4_;
  switch (collision) {
  case COL_UP_LEFT:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp2 < 0x08 && (uint8_t)(temp_x & 0x0f) < 0x08) return 1;
    break;
  case COL_UP_RIGHT:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp2 < 0x08 && (uint8_t)(temp_x & 0x0f) >= 0x08) return 1;
    break;
  case COL_DOWN_LEFT:
  case COL_LEFT_SPIKE_BLOCK:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp2 >= 0x08 && (uint8_t)(temp_x & 0x0f) < 0x08) return 1;
    break;
  case COL_DOWN_RIGHT:
  case COL_RIGHT_SPIKE_BLOCK:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp2 >= 0x08 && (uint8_t)(temp_x & 0x0f) >= 0x08) return 1;
    break;
  case COL_BOTTOM_LEFT_SPIKE:
  case COL_BOTTOM_RIGHT_SPIKE:
  case COL_BOTTOM_CENTER_SPIKE:
  case COL_BOTTOM_SPIKES:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp8 != tmp2) return 1;
    break;
  case COL_TOP_CENTER_SPIKE:
  case COL_TOP_SPIKES:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if (tmp8 == tmp2) return 1;
    break;
  case COL_LEFT:
    tmp8 = (uint8_t)(temp_y & 0x0f);
    if ((uint8_t)(temp_x & 0x0f) < 0x08) return 1;
    break;
  case COL_RIGHT:
    tmp8 = (uint8_t)(temp_y & 0x0f);
    if ((uint8_t)(temp_x & 0x0f) >= 0x08) return 1;
    break;
  case COL_TOP_LEFT_BOTTOM_RIGHT:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if ((uint8_t)(temp_x & 0x0f) < 0x08) {
      if (tmp2 < 0x08) return 1;
    } else if (tmp2 >= 0x08) return 1;
    break;
  case COL_TOP_RIGHT_BOTTOM_LEFT:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp8 = tmp2 & 0x07;
    if ((uint8_t)(temp_x & 0x0f) < 0x08) {
      if (tmp2 >= 0x08) return 1;
    } else if (tmp2 < 0x08) return 1;
    break;
  case COL_TOP_RIGHT_STAIRS:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp4_ = (uint8_t)(temp_x & 0x0f), tmp8 = tmp2 & 0x07;
    tmp4 = tmp4_;
    if (tmp2 >= 0x08 && tmp4_ < 0x08) break;
    return 1;
  case COL_TOP_LEFT_STAIRS:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp4_ = (uint8_t)(temp_x & 0x0f), tmp8 = tmp2 & 0x07;
    tmp4 = tmp4_;
    if (tmp2 >= 0x08 && tmp4_ >= 0x08) break;
    return 1;
  case COL_BOTTOM_RIGHT_STAIRS:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp4_ = (uint8_t)(temp_x & 0x0f), tmp8 = tmp2 & 0x07;
    tmp4 = tmp4_;
    if (tmp2 < 0x08 && tmp4_ < 0x08) break;
    return 1;
  case COL_BOTTOM_LEFT_STAIRS:
    tmp2 = (uint8_t)(temp_y & 0x0f), tmp4_ = (uint8_t)(temp_x & 0x0f), tmp8 = tmp2 & 0x07;
    tmp4 = tmp4_;
    if (tmp2 < 0x08 && tmp4_ >= 0x08) break;
    return 1;
  }
  return 0;
}

static const uint8_t a_check_lookup[] = {1, 0, 0, 1, 1, 0, 0, 1};

static uint8_t bg_coll_slope(void) {
  tmp8 = (uint8_t)(temp_y & 0x0f);
  if (collision < COL_SLOPE_RD45 || collision > COL_SLOPE_LU66_BOT) return 0;
  switch (collision) {
  case COL_SLOPE_LU45:
    if ((gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) && !currplayer_mini) return 0;
    tmp7 = (uint8_t)(temp_x & 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_45DEG_DOWN_UD;
    break;
  case COL_SLOPE_LD45:
    tmp7 = (uint8_t)(temp_x & 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_45DEG_DOWN;
    break;
  case COL_SLOPE_RU45:
    tmp7 = (uint8_t)((temp_x & 0x0f) ^ 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_45DEG_UP_UD;
    break;
  case COL_SLOPE_RD45:
    tmp7 = (uint8_t)((temp_x & 0x0f) ^ 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_45DEG_UP;
    break;
  case COL_SLOPE_RU22_RIGHT:
    tmp7 = (uint8_t)(((temp_x >> 1) & 0x07) ^ 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_22DEG_UP_UD;
    break;
  case COL_SLOPE_RU22_LEFT:
    tmp7 = (uint8_t)((((temp_x >> 1) | 0x8) & 0x0f) ^ 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_22DEG_UP_UD;
    break;
  case COL_SLOPE_RD22_RIGHT:
    tmp7 = (uint8_t)(((temp_x >> 1) & 0x07) ^ 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_22DEG_UP;
    break;
  case COL_SLOPE_RD22_LEFT:
    tmp7 = (uint8_t)((((temp_x >> 1) | 0x8) & 0x0f) ^ 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_22DEG_UP;
    break;
  case COL_SLOPE_LU22_RIGHT:
    tmp7 = (uint8_t)((temp_x >> 1) & 0x07), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_22DEG_DOWN_UD;
    break;
  case COL_SLOPE_LU22_LEFT:
    tmp7 = (uint8_t)(((temp_x >> 1) | 0x8) & 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_22DEG_DOWN_UD;
    break;
  case COL_SLOPE_LD22_RIGHT:
    tmp7 = (uint8_t)((temp_x >> 1) & 0x07), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_22DEG_DOWN;
    break;
  case COL_SLOPE_LD22_LEFT:
    tmp7 = (uint8_t)(((temp_x >> 1) | 0x8) & 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_22DEG_DOWN;
    break;
  case COL_SLOPE_RD66_TOP:
    if ((uint8_t)(temp_x & 0x0f) < 0x08) return 0;
    tmp7 = (uint8_t)((((temp_x & 0x07) << 1) & 0x0f) ^ 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_66DEG_UP;
    break;
  case COL_SLOPE_RD66_BOT:
    if ((uint8_t)(temp_x & 0x0f) >= 0x08) return 1;
    tmp7 = (uint8_t)((((temp_x & 0x0f) << 1) & 0x0f) ^ 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_66DEG_UP;
    break;
  case COL_SLOPE_LD66_TOP:
    if ((uint8_t)(temp_x & 0x0f) >= 0x08) return 0;
    tmp7 = (uint8_t)(((temp_x & 0x07) << 1) & 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_66DEG_DOWN;
    break;
  case COL_SLOPE_LD66_BOT:
    if ((uint8_t)(temp_x & 0x0f) < 0x08) return 1;
    tmp7 = (uint8_t)(((temp_x & 0x0f) << 1) & 0x0f), tmp4 = (uint8_t)(temp_y & 0x0f);
    currplayer_slope_type = SLOPE_66DEG_DOWN;
    break;
  case COL_SLOPE_RU66_TOP:
    if ((uint8_t)(temp_x & 0x0f) < 0x08) return 0;
    tmp7 = (uint8_t)((((temp_x & 0x07) << 1) & 0x0f) ^ 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_66DEG_UP_UD;
    break;
  case COL_SLOPE_RU66_BOT:
    if ((uint8_t)(temp_x & 0x0f) >= 0x08) return 1;
    tmp7 = (uint8_t)((((temp_x & 0x0f) << 1) & 0x0f) ^ 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_66DEG_UP_UD;
    break;
  case COL_SLOPE_LU66_TOP:
    if ((gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) && currplayer_mini) return 0;
    if ((uint8_t)(temp_x & 0x0f) >= 0x08) return 0;
    tmp7 = (uint8_t)(((temp_x & 0x07) << 1) & 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_66DEG_DOWN_UD;
    break;
  case COL_SLOPE_LU66_BOT:
    if ((gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) && currplayer_mini) return 0;
    if ((uint8_t)(temp_x & 0x0f) < 0x08) return 1;
    tmp7 = (uint8_t)(((temp_x & 0x0f) << 1) & 0x0f), tmp4 = (uint8_t)((temp_y & 0x0f) ^ 0x0f);
    currplayer_slope_type = SLOPE_66DEG_DOWN_UD;
    break;
  default: return 0;
  }
  if (tmp4 >= tmp7) {
    tmp8 = (uint8_t)(tmp4 - tmp7);
    if (gamemode == GAMEMODE_CUBE || gamemode == GAMEMODE_ROBOT || gamemode == GAMEMODE_NINJA) {
      if (controllingplayer->hold & (PAD_A | PAD_UP)) make_cube_jump_higher = 1;
      else currplayer_slope_frames = 1, currplayer_was_on_slope_counter = 3;
    } else {
      tmp4 = 0;
      if (currplayer_slope_type & SLOPE_RISING) tmp4 |= 4;
      if (currplayer_slope_type & SLOPE_UPSIDEDOWN) tmp4 |= 2;
      if (currplayer_gravity) tmp4 |= 1;
      if (a_check_lookup[tmp4]) {
        if (controllingplayer->hold & (PAD_A | PAD_UP)) tmp8 = 4;
      } else if (!(controllingplayer->hold & (PAD_A | PAD_UP))) tmp8 = 4;
      currplayer_slope_frames = 1, currplayer_was_on_slope_counter = 3;
    }
    return 1;
  } else if (!currplayer_was_on_slope_counter) {
    currplayer_slope_type = 0, tmp8 = 0;
  }
  return 0;
}

static uint8_t bg_side_coll_common(void) {
  tmp1 = (uint8_t)((currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0) + (Generic.height >> 1));
  if (currplayer_mini && (gamemode == GAMEMODE_CUBE || gamemode == GAMEMODE_ROBOT || gamemode == GAMEMODE_NINJA))
    tmp1 = (uint8_t)(tmp1 + (currplayer_gravity ? 3 : -2));
  if (currplayer_was_on_slope_counter | (uint8_t)currplayer_slope_frames) return 0;
  temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y + tmp1);
  bg_collision_sub();
  if (collision) {
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) {
      if (bg_coll_slope() && !dblocked[currplayer]) cube_data[currplayer] |= 1;
    } else if (!currplayer_was_on_slope_counter && bg_coll_slope()) {
      high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) + (currplayer_slope_type & SLOPE_UPSIDEDOWN ? 2 : -2));
    }
    if (bg_coll_spikes()) return 0;
    return bg_coll_sides() || bg_coll_mini_blocks() || bg_coll_top_bottom_slabs();
  }
  return 0;
}

static uint8_t bg_coll_R(void) {
  temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + Generic.width + ((options & platformer || force_platformer) ? 3 : 0));
  return bg_side_coll_common();
}
static uint8_t bg_coll_L(void) {
  temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x - ((options & platformer || force_platformer) ? 3 : 0));
  return bg_side_coll_common();
}

static uint8_t bg_coll_U_D_checks(void) {
  switch (collision) {
  case COL_NO_SIDE: return 1;
  case COL_ALL: return high_byte(currplayer_relx) < 0x10 ? 0 : 1;
  case COL_DEATH_TOP: col_death_top_routine(); break;
  case COL_DEATH_BOTTOM: col_death_bottom_routine(); break;
  case COL_FLOOR_CEIL: dblocked[currplayer] = 1; return 1;
  }
  return 0;
}

static uint8_t bg_coll_return_D(void) {
  tmp1 = bg_coll_U_D_checks() || bg_coll_mini_blocks() || bg_coll_top_bottom_slabs();
  eject_D = tmp8;
  return tmp1;
}
static uint8_t bg_coll_return_U(void) {
  tmp3 = bg_coll_U_D_checks();
  tmp1 = bg_coll_mini_blocks() || bg_coll_top_bottom_slabs();
  eject_U = (uint8_t)((tmp3 ? 0xf0 : 0xf8) | tmp8);
  return tmp1 | tmp3;
}
static uint8_t bg_coll_return_slope_D(void) {
  tmp1 = bg_coll_slope();
  if (!tmp2) {
    if (currplayer_slope_type & SLOPE_RISING) { currplayer_slope_type = currplayer_last_slope_type; return 0; }
  } else if (!(currplayer_slope_type & SLOPE_RISING)) {
    currplayer_slope_type = currplayer_last_slope_type;
    return 0;
  }
  if (tmp1) {
    if ((currplayer_last_slope_type & SLOPE_RISING) && !(currplayer_slope_type & SLOPE_RISING))
      if (currplayer_last_slope_type != 0 && currplayer_slope_type != 0)
        currplayer_slope_type = currplayer_last_slope_type, tmp8 = high_byte(currplayer_vel_x);
    if (currplayer_slope_type != 0) currplayer_last_slope_type = currplayer_slope_type;
    eject_D = tmp8;
  }
  return tmp1;
}
static uint8_t bg_coll_return_slope_U(void) {
  tmp1 = bg_coll_slope();
  if (!tmp2) {
    if (currplayer_slope_type & SLOPE_RISING) { currplayer_slope_type = currplayer_last_slope_type; return 0; }
  } else if (!(currplayer_slope_type & SLOPE_RISING)) {
    currplayer_slope_type = currplayer_last_slope_type;
    return 0;
  }
  if (tmp1) {
    if ((currplayer_last_slope_type & SLOPE_RISING) && !(currplayer_slope_type & SLOPE_RISING))
      if (currplayer_last_slope_type != 0 && currplayer_slope_type != 0)
        currplayer_slope_type = currplayer_last_slope_type, tmp8 = high_byte(currplayer_vel_x);
    if (currplayer_slope_type != 0) currplayer_last_slope_type = currplayer_slope_type;
    eject_U = (uint8_t)-tmp8;
  }
  return tmp1;
}

#define COLL_CHECK_BOTTOM                                                                                              \
  bg_collision_sub();                                                                                                  \
  if (collision && bg_coll_return_D()) { cube_data[currplayer] &= 0x7E; return 1; }
#define COLL_CHECK_TOP                                                                                                 \
  bg_collision_sub();                                                                                                  \
  if (collision && bg_coll_return_U()) { cube_data[currplayer] &= 0x7E; return 1; }

static uint8_t bg_coll_U(void) {
  transitional_linear_absolute_currplayer_y = (uint16_t)(Generic.y + scroll_y);
  if (high_byte(currplayer_relx) >= 0x10) {
    temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                        (uint8_t)(transitional_sixteen_minus_generic_height_halved + (currplayer_mini ? 1 : 2) +
                                  (gamemode == GAMEMODE_SHIP ? 1 : 0)));
    temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x);
    tmp2 = 0, tmp3 = 0;
    do {
      bg_collision_sub();
      if (collision) tmp3 = (uint8_t)(bg_coll_return_slope_U() | tmp3);
      temp_x = (uint8_t)(temp_x + Generic.width);
    } while (++tmp2 < 2);
    if (tmp3) return 1;
  }
  if (high_byte(currplayer_vel_y) & 0x80) {
    bool wv = gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE;
    temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + (wv ? 4 : 0));
    temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                        (uint8_t)((currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0) + (wv ? 0 : 1)));
    tmp8 = (uint8_t)(temp_y & 0x0f);
    COLL_CHECK_TOP
    temp_x = (uint8_t)(temp_x + (Generic.width >> 1));
    COLL_CHECK_TOP
    temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + Generic.width);
    COLL_CHECK_TOP
  }
  return 0;
}

static uint8_t bg_coll_D(void) {
  transitional_linear_absolute_currplayer_y = (uint16_t)(Generic.y + scroll_y);
  if (high_byte(currplayer_relx) >= 0x10) {
    temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y - 2 +
                        (uint8_t)(Generic.height + (currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0)));
    temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x);
    tmp2 = 0, tmp3 = 0;
    do {
      bg_collision_sub();
      if (collision) tmp3 = (uint8_t)(bg_coll_return_slope_D() | tmp3);
      temp_x = (uint8_t)(temp_x + Generic.width);
    } while (++tmp2 < 2);
    if (tmp3) return 1;
  }
  if (!(high_byte(currplayer_vel_y) & 0x80)) {
    bool wv = gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE;
    temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + (wv ? 4 : 0));
    temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                        (uint8_t)(Generic.height + (currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0)));
    tmp8 = (uint8_t)(temp_y & 0x0f);
    COLL_CHECK_BOTTOM
    temp_x = (uint8_t)(temp_x + (Generic.width >> 1));
    COLL_CHECK_BOTTOM
    temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + Generic.width);
    COLL_CHECK_BOTTOM
  }
  return 0;
}

static uint8_t bg_coll_U_spider(void) {
  temp_x = tmp7;
  temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                      (uint8_t)(currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0));
  tmp8 = (uint8_t)(temp_y & 0x0f);
  COLL_CHECK_TOP
  temp_x = tmp9;
  COLL_CHECK_TOP
  return 0;
}
static uint8_t bg_coll_D_spider(void) {
  temp_x = tmp7;
  temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                      (uint8_t)(Generic.height + (currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0)));
  tmp8 = (uint8_t)(temp_y & 0x0f);
  COLL_CHECK_BOTTOM
  temp_x = tmp9;
  COLL_CHECK_BOTTOM
  return 0;
}

static void bg_coll_death(void) {
  temp_x = (uint8_t)(Generic.x + (uint8_t)scroll_x + (Generic.width >> 1) - 1);
  temp_y = (uint16_t)(transitional_linear_absolute_currplayer_y +
                      (uint8_t)((Generic.height >> 1) + (currplayer_mini ? transitional_sixteen_minus_generic_height_halved : 0)));
  bg_collision_sub();
  if (collision) {
    if (!dblocked[currplayer] || gamemode != GAMEMODE_WAVE) {
      if (!force_platformer) {
        if (bg_coll_U_D_checks() || bg_coll_mini_blocks() || bg_coll_top_bottom_slabs() || bg_coll_spikes() || bg_coll_slope())
          cube_data[currplayer] |= 1;
      } else if (bg_coll_U_D_checks() || bg_coll_mini_blocks() || bg_coll_spikes() || bg_coll_slope())
        cube_data[currplayer] |= 1;
    } else if (bg_coll_mini_blocks() || bg_coll_top_bottom_slabs() || bg_coll_spikes() || bg_coll_slope())
      cube_data[currplayer] |= 1;
  }
}

/* ---------------------------------------------------------------- the camera (scroll.h) */
static void cap_scroll_y_at_top(void) {
  uint16_t d = (uint16_t)(scroll_y - min_scroll_y);
  if (!(d & 0x8000)) return;
  uint8_t y = (uint8_t)d;
  scroll_y = min_scroll_y;
  currplayer_rely = (uint16_t)(currplayer_rely + scroll_y_subpx + (y << 8));
  player_rely[0] = currplayer_rely;
  player_rely[1] = (uint16_t)(player_rely[1] + scroll_y_subpx + (y << 8));
  scroll_y_subpx = 0;
}
static void cap_scroll_y_at_bottom(void) {
  uint16_t d = (uint16_t)(scroll_y - 0x02CF);
  if (d & 0x8000) return;
  uint8_t y = (uint8_t)d;
  scroll_y = 0x02CF;
  currplayer_rely = (uint16_t)(currplayer_rely + scroll_y_subpx + (y << 8));
  player_rely[0] = currplayer_rely;
  player_rely[1] = (uint16_t)(player_rely[1] + scroll_y_subpx + (y << 8));
  scroll_y_subpx = 0;
}

static void process_x_scroll(void) {
  switch (cam_seesaw) {
  case 1:
    if (curr_x_scroll_stop < 0xD000) target_x_scroll_stop = 0xD000;
    else cam_seesaw = 2;
    break;
  case 2:
    if (curr_x_scroll_stop > 0x1000) target_x_scroll_stop = 0x1000;
    else cam_seesaw = 1;
    break;
  }
  if (curr_x_scroll_stop < target_x_scroll_stop) curr_x_scroll_stop += 0x200;
  else if (curr_x_scroll_stop > target_x_scroll_stop) curr_x_scroll_stop -= 0x200;
  if (currplayer_relx > curr_x_scroll_stop) {
    tmp1 = MSB(currplayer_relx - curr_x_scroll_stop);
    scroll_x += tmp1;
    parallax_scroll_x = (uint8_t)(parallax_scroll_x + (tmp1 ? tmp1 - 1 : 0));
    if (parallax_scroll_x >= 144) parallax_scroll_x -= 144;
    high_byte(currplayer_relx) = (uint8_t)(high_byte(currplayer_relx) - tmp1);
    high_byte(player_relx[1]) = (uint8_t)(high_byte(player_relx[1]) - tmp1);
  } else if (currplayer_relx < 0x0200) {
    tmp1 = MSB(currplayer_relx + 0x0200);
    scroll_x -= tmp1;
    if (tmp1) {
      if (tmp1 > parallax_scroll_x) parallax_scroll_x = (uint8_t)(parallax_scroll_x + 144);
      parallax_scroll_x = (uint8_t)(parallax_scroll_x - (tmp1 - 1));
    }
    high_byte(currplayer_relx) = (uint8_t)(high_byte(currplayer_relx) + tmp1);
    high_byte(player_relx[1]) = (uint8_t)(high_byte(player_relx[1]) + tmp1);
  }
}

static void process_y_scroll(void) {
  uint16_t p;
  if ((!dual || twoplayer) && (gamemode == GAMEMODE_CUBE || gamemode == GAMEMODE_ROBOT || gamemode == GAMEMODE_NINJA ||
                               gamemode == GAMEMODE_POGO || gamemode == GAMEMODE_FOOTBALL || nocamlock || nocamlockforced)) {
    if (exitPortalTimer) exitPortalTimer--;
    if (currplayer_rely < 0x4000 && (scroll_y >= min_scroll_y && (scroll_y_subpx || scroll_y != min_scroll_y))) {
      p = (uint16_t)(0x4000 - currplayer_rely);
      if (exitPortalTimer) {
        uint8_t t = (uint8_t)(11 - exitPortalTimer);
        if (MSB(p) >= t) p = (uint16_t)(t << 8);
      }
      currplayer_rely += p;
      player_rely[1] += p;
      uint8_t old = scroll_y_subpx;
      scroll_y_subpx = (uint8_t)(scroll_y_subpx - LSB(p));
      if (old < LSB(p)) ++high_byte(p);
      scroll_y = (uint16_t)(scroll_y - MSB(p));
    }
    cap_scroll_y_at_top();
    if (scroll_y < 0x2CF && high_byte(currplayer_rely) >= 0xA0) {
      p = (uint16_t)(currplayer_rely - 0xA000);
      if (exitPortalTimer) {
        uint8_t t = (uint8_t)(11 - exitPortalTimer);
        if (MSB(p) >= t) p = (uint16_t)(t << 8);
      }
      currplayer_rely -= p;
      player_rely[1] -= p;
      uint16_t s = (uint16_t)(scroll_y_subpx + LSB(p));
      scroll_y_subpx = (uint8_t)s;
      if (s > 0xFF) ++high_byte(p);
      scroll_y = (uint16_t)(scroll_y + MSB(p));
    }
    cap_scroll_y_at_bottom();
  } else {
    if (target_scroll_y > scroll_y) {
      p = SHIP_SCROLL_SPEED[0];
      currplayer_rely -= p;
      player_rely[1] -= p;
      uint16_t s = (uint16_t)(scroll_y_subpx + LSB(p));
      scroll_y_subpx = (uint8_t)s;
      if (s > 0xFF) ++high_byte(p);
      scroll_y = (uint16_t)(scroll_y + MSB(p));
    }
    if (target_scroll_y < scroll_y) {
      p = SHIP_SCROLL_SPEED[0];
      currplayer_rely += p;
      player_rely[1] += p;
      uint8_t old = scroll_y_subpx;
      scroll_y_subpx = (uint8_t)(scroll_y_subpx - LSB(p));
      if (old < LSB(p)) ++high_byte(p);
      scroll_y = (uint16_t)(scroll_y - MSB(p));
    }
    cap_scroll_y_at_top();
    cap_scroll_y_at_bottom();
  }
}

static void do_the_scroll_thing(void) {
  process_x_scroll();
  process_y_scroll();
}

/* ---------------------------------------------------------------- movement (gamemodes/, x_movement.h) */
static void common_gravity_routine(void) {
  int16_t tmpaccel;
  uint8_t d = dashing[currplayer];
  if (!d) {
    tmpaccel = tmpgravity;
    if (!currplayer_gravity ? currplayer_vel_y > tmpfallspeed : currplayer_vel_y < tmpfallspeed) tmpaccel = (int16_t)-tmpaccel;
    switch (gravity_mod) {
    case 1: tmpaccel /= 3; break;
    case 2: tmpaccel /= 2; break;
    case 3: tmpaccel = (int16_t)(tmpaccel / 3 * 2); break;
    case 4: tmpaccel = (int16_t)(tmpaccel * 2); break;
    }
    currplayer_vel_y = (int16_t)(currplayer_vel_y + tmpaccel);
  } else if (d == 2) currplayer_vel_y = (int16_t)-currplayer_vel_x;
  else if (d == 3) currplayer_vel_y = currplayer_vel_x;
  else if (d == 4) {
    currplayer_vel_y = (int16_t)(currplayer_vel_x * 2);
    currplayer_rely = (uint16_t)(currplayer_rely - currplayer_vel_y);
    return;
  } else if (d == 5) currplayer_vel_y = (int16_t)(currplayer_vel_x * 2);
  else {
    currplayer_vel_y = currplayer_gravity ? -1 : 1;
    return;
  }
  currplayer_rely = (uint16_t)(currplayer_rely + currplayer_vel_y);
}

static void cube_eject(void) {
  if (!currplayer_gravity || (currplayer_gravity && (hblocked[currplayer] | fblocked[currplayer]))) {
    if (bg_coll_D()) {
      high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D);
      low_byte(currplayer_rely) = 0;
      if (!hblocked[currplayer]) currplayer_vel_y = 0;
      else currplayer_vel_y = currplayer_gravity ? (int16_t)0xffff : 0;
      orbactive = 0;
      if (fblocked[currplayer]) currplayer_gravity = GRAVITY_DOWN, update_currplayer_table_idx();
    }
  }
  if (currplayer_gravity || (!currplayer_gravity && (hblocked[currplayer] | fblocked[currplayer]))) {
    if (bg_coll_U()) {
      high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U);
      low_byte(currplayer_rely) = 0;
      if (!hblocked[currplayer]) currplayer_vel_y = 0;
      else currplayer_vel_y = !currplayer_gravity ? 1 : 0;
      orbactive = 0;
      if (fblocked[currplayer]) currplayer_gravity = GRAVITY_UP, update_currplayer_table_idx();
    }
  }
}

static void slope_jump_check(void) {
  if (make_cube_jump_higher) {
    if ((currplayer_slope_type & SLOPE_DEGREES_MASK) != SLOPE_22DEG)
      currplayer_vel_y = (int16_t)(currplayer_vel_y + T4(MAKE_CUBE_JUMP_HIGHER));
    make_cube_jump_higher = 0;
  }
}

static void cube_movement(void) {
  tmpfallspeed = T4(CUBE_MAX_FALLSPEED);
  tmpgravity = T4(CUBE_GRAVITY);
  if (max_fallspeed_7) tmpfallspeed = (int16_t)(tmpfallspeed + T4(DASH_END_VEL_RESET));
  common_gravity_routine();
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
  cube_eject();
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
  update_currplayer_table_idx();
  uint8_t hold = controllingplayer->hold & (PAD_A | PAD_UP), press = controllingplayer->press & (PAD_A | PAD_UP);
  if ((gamemode == GAMEMODE_CUBE && currplayer_vel_y == 0 && dashing[currplayer] == 0) || (gamemode == GAMEMODE_NINJA && !retro_mode)) {
    if (gamemode == GAMEMODE_NINJA && currplayer_vel_y == 0) ninjajumps[currplayer] = 3;
    if (gamemode != GAMEMODE_NINJA) cube_data[currplayer] &= 1;
    else if (ninjajumps[currplayer]) cube_data[currplayer] &= 1;
    if (hold && (!jblocked[currplayer] && !fblocked[currplayer] &&
                 (gamemode == GAMEMODE_CUBE || (gamemode == GAMEMODE_NINJA && currplayer_vel_y == 0)))) {
      if (!orbed[currplayer]) {
        jumps++;
        currplayer_vel_y = T4(JUMP_VEL);
        slope_jump_check();
        if (gamemode == GAMEMODE_NINJA) ninjajumps[currplayer]--, orbhitonthisframe[currplayer] = 1;
      }
    } else if (press && (jblocked[currplayer] || fblocked[currplayer] ||
                         (gamemode == GAMEMODE_NINJA && ninjajumps[currplayer] && !orbhitonthisframe[currplayer] && !orbed[currplayer]))) {
      jumps++;
      currplayer_vel_y = T4(JUMP_VEL);
      slope_jump_check();
      if (gamemode == GAMEMODE_NINJA) ninjajumps[currplayer]--, orbhitonthisframe[currplayer] = 1;
    }
  } else if (gamemode == GAMEMODE_ROBOT || (retro_mode && gamemode == GAMEMODE_NINJA)) {
    if (gamemode == GAMEMODE_NINJA && currplayer_vel_y == 0) ninjajumps[currplayer] = 3;
    else if (gamemode == GAMEMODE_NINJA && currplayer_vel_y != 0) {
      if (ninjajumps[currplayer]) cube_data[currplayer] &= 1;
    }
    if (press) cube_data[currplayer] |= 4;
    if ((!retro_mode && currplayer_vel_y == 0 && !hblocked[currplayer] && dashing[currplayer] == 0 && cube_data[currplayer] & 4)) {
      cube_data[currplayer] &= 1;
      if (hold && !orbed[currplayer]) {
        if (press) jumps++;
        currplayer_vel_y = T4(ROBOT_JUMP_VEL);
        robotjumptime[currplayer] = ROBOT_JUMP_TIME;
        robotjumpframe[0] = 1;
      }
    } else if ((gamemode == GAMEMODE_ROBOT && retro_mode && currplayer_vel_y == 0 && !hblocked[currplayer] &&
                dashing[currplayer] == 0 && cube_data[currplayer] & 4) ||
               (gamemode == GAMEMODE_NINJA && retro_mode && !orbhitonthisframe[currplayer] && ninjajumps[currplayer] &&
                !hblocked[currplayer] && dashing[currplayer] == 0 && cube_data[currplayer] & 4)) {
      cube_data[currplayer] &= 1;
      if (hold && !orbed[currplayer]) {
        if (press) jumps++;
        currplayer_vel_y = T4(ROBOT_JUMP_VEL);
        if (gamemode == GAMEMODE_NINJA) ninjajumps[currplayer]--, orbhitonthisframe[currplayer] = 1;
        robotjumptime[currplayer] = ROBOT_JUMP_TIME;
        robotjumpframe[0] = 1;
      }
    } else if (robotjumptime[currplayer] && !hblocked[currplayer]) {
      cube_data[currplayer] &= 0xFB;
      cube_data[currplayer] = 0;
      if (robotjumptime[currplayer]) robotjumptime[currplayer]--;
      if (hold && !jblocked[currplayer] && !orbed[currplayer]) {
        if (press) jumps++;
        if (robotjumpframe[0]) robotjumpframe[0]++;
        if (robotjumpframe[0] > 3) robotjumpframe[0] = 3;
        currplayer_vel_y = T4(ROBOT_JUMP_VEL);
      } else if (press && jblocked[currplayer] && !orbed[currplayer]) {
        jumps++;
        if (robotjumpframe[0]) robotjumpframe[0]++;
        if (robotjumpframe[0] > 3) robotjumpframe[0] = 3;
        currplayer_vel_y = T4(ROBOT_JUMP_VEL);
      } else {
        robotjumptime[currplayer] = 0, robotjumpframe[0] = 0;
      }
    } else if (currplayer_vel_y != 0) {
      robotjumpframe[0] = 3;
    }
  } else if (gamemode == GAMEMODE_FOOTBALL) {
    if (hold && !orbed[currplayer]) chargepower[currplayer]++;
    if (chargepower[currplayer] >= 50) chargepower[currplayer] = 0, orbed[currplayer] = 1;
  }
  if (gamemode == GAMEMODE_FOOTBALL && !hold) {
    orbed[currplayer] = 0;
    tmp3 = chargepower[currplayer] > 45 ? 45 : chargepower[currplayer];
    tmpA = (uint16_t)(tmp3 * 0x004C);
    if (!currplayer_gravity) tmpA = (uint16_t)-tmpA;
    if (chargepower[currplayer] && currplayer_vel_y == 0) currplayer_vel_y = (int16_t)tmpA;
    chargepower[currplayer] = 0;
  }
  fblocked[currplayer] = 0, hblocked[currplayer] = 0, jblocked[currplayer] = 0;
}

static void ufo_ship_eject(void) {
  if (bg_coll_U()) {
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U - 1);
    currplayer_vel_y = 0;
  }
  if (bg_coll_D()) {
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D);
    currplayer_vel_y = 0;
  }
}

static void ship_movement(void) {
  tmp1 = currplayer_gravity ? (currplayer_vel_y < 0) : (currplayer_vel_y > 0);
  tmp2 = (controllingplayer->hold & (PAD_A | PAD_UP)) != 0;
  if (tmp2) tmpgravity = T4(SHIP_GRAVITY_BASE);
  else if (!tmp2 && !tmp1) tmpgravity = T4(SHIP_GRAVITY_AFTER_HOLD);
  else tmpgravity = T4(SHIP_GRAVITY);
  if (tmp2 && tmp1) tmpgravity = T4(SHIP_GRAVITY_HOLD_FALL);
  if ((currplayer_gravity ? 1 : 0) ^ tmp2) tmpgravity = (int16_t)-tmpgravity;
  tmpfallspeed = 0x4443;
  common_gravity_routine();
  if (currplayer_gravity) {
    if (currplayer_vel_y < -0x0369) currplayer_vel_y = -0x0369;
    if (currplayer_vel_y > 0x0443) currplayer_vel_y = 0x0443;
  } else {
    if (currplayer_vel_y < -0x0443) currplayer_vel_y = -0x0443;
    if (currplayer_vel_y > 0x0369) currplayer_vel_y = 0x0369;
  }
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
  ufo_ship_eject();
}

#define YELLOW_ORB_T (0x00 << 3)
#define YELLOW_PAD_T (0x01 << 3)
#define PINK_ORB_T (0x02 << 3)
#define PINK_PAD_T (0x03 << 3)
#define RED_ORB_T (0x04 << 3)
#define YLW_BIGGER_T (0x05 << 3)
#define BLACK_ORB_T (0x06 << 3)
#define YLW_SMALLER_T (0x07 << 3)
#define RED_PAD_T (0x08 << 3)
#define table_offset tmp3
#define collided tmp4
static void sprite_gamemode_controller_check(void);
static uint16_t sprite_gamemode_y_adjust(void);

static void ball_eject(void) {
  if (bg_coll_U()) {
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U);
    if (gamemode != GAMEMODE_POGO) currplayer_vel_y = 0;
    else {
      if (!currplayer_gravity) currplayer_vel_y = 0;
      else if (!orbhitonthisframe[currplayer]) {
        currplayer_vel_y = (int16_t)((-currplayer_vel_y / 3) * 2);
        table_offset = YELLOW_PAD_T;
        tmpA = sprite_gamemode_y_adjust();
        if (currplayer_vel_y < (int16_t)tmpA) currplayer_vel_y = (int16_t)tmpA;
        robotjumptime[currplayer] = 8;
      }
    }
    orbactive = 0;
    cube_data[currplayer] &= 1;
  }
  if (bg_coll_D()) {
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D);
    if (gamemode != GAMEMODE_POGO) currplayer_vel_y = 0;
    else {
      if (currplayer_gravity) currplayer_vel_y = 0;
      else if (!orbhitonthisframe[currplayer]) {
        currplayer_vel_y = (int16_t)((-currplayer_vel_y / 3) * 2);
        table_offset = YELLOW_PAD_T;
        tmpA = sprite_gamemode_y_adjust();
        if (currplayer_vel_y > (int16_t)tmpA) currplayer_vel_y = (int16_t)tmpA;
        robotjumpframe[currplayer] = 8;
      }
    }
    orbactive = 0;
    cube_data[currplayer] &= 1;
  }
}

static void ball_movement(void) {
  if (gamemode == GAMEMODE_SWING || gamemode == GAMEMODE_POGO) {
    tmpfallspeed = T4(SWING_MAX_FALLSPEED);
    tmpgravity = T4(SWING_GRAVITY);
  } else {
    tmpfallspeed = T4(BALL_MAX_FALLSPEED);
    tmpgravity = T4(BALL_GRAVITY);
  }
  common_gravity_routine();
  Generic.x = high_byte(currplayer_relx);
  Generic.y = (uint8_t)(currplayer_gravity ? high_byte(currplayer_rely) - 1 : high_byte(currplayer_rely) + 1);
  ball_eject();
  Generic.x = high_byte(currplayer_relx);
  Generic.y = (uint8_t)(currplayer_gravity ? high_byte(currplayer_rely) - 1 : high_byte(currplayer_rely) + 1);
  transitional_linear_absolute_currplayer_y = (uint16_t)(Generic.y + scroll_y);
  uint8_t hold = controllingplayer->hold & (PAD_A | PAD_UP), press = controllingplayer->press & (PAD_A | PAD_UP);
  if (gamemode == GAMEMODE_BALL) {
    if (hold && ball_switched[currplayer] == 0 && currplayer_vel_y == 0 && !orbed[currplayer]) {
      jumps++;
      invert_gravity(currplayer_gravity);
      update_currplayer_table_idx();
      ball_switched[currplayer] = 1;
      currplayer_vel_y = T4(BALL_SWITCH_VEL);
      bg_coll_floor_spikes();
    }
    if (ball_switched[currplayer] && !hold) {
      cube_data[currplayer] &= 1;
      ball_switched[currplayer] = 0;
    }
  } else if (gamemode == GAMEMODE_SWING) {
    if (press && !ufo_orbed[currplayer] && !orbed[currplayer]) {
      invert_gravity(currplayer_gravity);
      update_currplayer_table_idx();
      bg_coll_floor_spikes();
    }
  } else {
    if (press && !orbhitonthisframe[currplayer]) {
      table_offset = BLACK_ORB_T;
      collided = 0x44; /* BLACK_ORB */
      sprite_gamemode_controller_check();
      cube_data[currplayer] |= 0x02;
    }
  }
  ufo_orbed[currplayer] = 0;
}

static void ufo_movement(void) {
  tmpfallspeed = T4(UFO_MAX_FALLSPEED);
  tmpgravity = T4(UFO_GRAVITY);
  common_gravity_routine();
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
  ufo_ship_eject();
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
  if ((controllingplayer->press & (PAD_A | PAD_UP)) && !ufo_orbed[currplayer]) {
    currplayer_vel_y = T4(UFO_JUMP_VEL);
    jumps++;
  }
  ufo_orbed[currplayer] = 0;
}

static void process_y_scroll(void);
static void spider_up_wait(void) {
  tmp7 = (uint8_t)(Generic.x + (uint8_t)scroll_x + 3);
  tmp9 = (uint8_t)(Generic.x + (uint8_t)scroll_x + Generic.width - 3);
  transitional_linear_absolute_currplayer_y = (uint16_t)(MSB(scroll_y_subpx + currplayer_rely) + scroll_y);
  do {
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - 0x08);
    transitional_linear_absolute_currplayer_y -= 0x08;
    process_y_scroll();
    if (high_byte(currplayer_rely) <= 0x07) { cube_data[currplayer] |= 1; break; }
    Generic.y = high_byte(currplayer_rely);
  } while (!bg_coll_U_spider());
}
static void spider_down_wait(void) {
  tmp7 = (uint8_t)(Generic.x + (uint8_t)scroll_x + 3);
  tmp9 = (uint8_t)(Generic.x + (uint8_t)scroll_x + Generic.width - 3);
  transitional_linear_absolute_currplayer_y = (uint16_t)(MSB(scroll_y_subpx + currplayer_rely) + scroll_y);
  do {
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) + 0x08);
    transitional_linear_absolute_currplayer_y += 0x08;
    process_y_scroll();
    if (high_byte(currplayer_rely) >= 0xF8) { cube_data[currplayer] |= 1; break; }
    Generic.y = high_byte(currplayer_rely);
  } while (!bg_coll_D_spider());
}

static void spider_eject(void) {
  if (!currplayer_gravity) {
    if (bg_coll_D()) high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D), currplayer_vel_y = 0;
  } else if (bg_coll_U()) {
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U - 1), currplayer_vel_y = 0;
  }
}

static void spider_movement(void) {
  tmpfallspeed = T4(SPIDER_MAX_FALLSPEED);
  tmpgravity = T4(SPIDER_GRAVITY);
  common_gravity_routine();
  Generic.x = high_byte(currplayer_relx);
  Generic.y = (uint8_t)(currplayer_gravity ? high_byte(currplayer_rely) - 2 : high_byte(currplayer_rely) + 1);
  spider_eject();
  uint8_t hold = controllingplayer->hold & (PAD_A | PAD_UP), press = controllingplayer->press & (PAD_A | PAD_UP);
  if (!currplayer_gravity) {
    if ((press || (hold && black_orbed[currplayer]) || cube_data[currplayer] & 2) && currplayer_vel_y == 0 &&
        !orbed[currplayer] && !dashing[currplayer]) {
      cube_data[currplayer] &= 0xFD;
      jumps++;
      currplayer_gravity = GRAVITY_UP;
      update_currplayer_table_idx();
      spider_up_wait();
      high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U);
      currplayer_vel_y = 0;
      black_orbed[currplayer] = 0;
    } else if (!hold) black_orbed[currplayer] = 0;
  } else {
    if ((press || (hold && black_orbed[currplayer]) || cube_data[currplayer] & 2) && currplayer_vel_y == 0 &&
        !orbed[currplayer] && !dashing[currplayer]) {
      cube_data[currplayer] &= 0xFD;
      jumps++;
      currplayer_gravity = GRAVITY_DOWN;
      update_currplayer_table_idx();
      spider_down_wait();
      high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D);
      currplayer_vel_y = 0;
      black_orbed[currplayer] = 0;
    } else if (!hold) black_orbed[currplayer] = 0;
  }
  Generic.y = high_byte(currplayer_rely);
}

static void wave_eject(void) {
  if (high_byte(currplayer_vel_y) & 0x80) {
    if (bg_coll_U()) {
      if (dblocked[currplayer]) {
        high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U);
        currplayer_vel_y = 0;
        cube_data[currplayer] &= 1;
      } else cube_data[currplayer] |= 1;
    }
  } else if (bg_coll_D()) {
    if (dblocked[currplayer]) {
      high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D);
      currplayer_vel_y = 0;
      cube_data[currplayer] &= 1;
    } else cube_data[currplayer] |= 1;
  }
}

static void wave_movement(void) {
  switch (dashing[currplayer]) {
  case 0:
    currplayer_vel_y = !currplayer_mini ? (int16_t)(currplayer_gravity ? -currplayer_vel_x : currplayer_vel_x)
                                        : (int16_t)(currplayer_gravity ? -(currplayer_vel_x << 1) : (currplayer_vel_x << 1));
    if (controllingplayer->hold & (PAD_A | PAD_UP) && gamemode != GAMEMODE_SNAKE) currplayer_vel_y = (int16_t)-currplayer_vel_y;
    else if (controllingplayer->hold & (PAD_A | PAD_UP)) {
      collided = 0x46; /* DASH_GRAVITY_ORB */
      sprite_gamemode_controller_check();
    }
    if (!currplayer_slope_frames && !currplayer_was_on_slope_counter) currplayer_rely = (uint16_t)(currplayer_rely + currplayer_vel_y);
    else currplayer_vel_y = 0;
    break;
  case 1: currplayer_vel_y = 1; break;
  case 2: currplayer_vel_y = (int16_t)-currplayer_vel_x; currplayer_rely = (uint16_t)(currplayer_rely + currplayer_vel_y); break;
  case 3: currplayer_vel_y = currplayer_vel_x; currplayer_rely = (uint16_t)(currplayer_rely + currplayer_vel_y); break;
  case 4: currplayer_vel_y = currplayer_vel_x; currplayer_rely = (uint16_t)(currplayer_rely - currplayer_vel_y); break;
  case 5: currplayer_vel_y = currplayer_vel_x; currplayer_rely = (uint16_t)(currplayer_rely + currplayer_vel_y); break;
  }
  Generic.x = (uint8_t)(high_byte(currplayer_relx) + 4);
  Generic.y = (uint8_t)(high_byte(currplayer_rely) + (currplayer_mini ? 0 : 4));
  wave_eject();
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
}

static void movement(void) {
  if (gamemode >= 12) return;
  if (retro_mode && gamemode == 1) { ufo_movement(); return; }
  switch (gamemode) {
  case 0: case 4: case 8: case 11: cube_movement(); break;
  case 1: ship_movement(); break;
  case 2: case 7: case 9: ball_movement(); break;
  case 3: ufo_movement(); break;
  case 5: spider_movement(); break;
  case 6: case 10: wave_movement(); break;
  }
}

static void slope_vel(void) {
  switch (tmp8 & SLOPE_DEGREES_MASK) {
  case SLOPE_22DEG: tmp5 = (uint16_t)(currplayer_vel_x >> 1); break;
  case SLOPE_45DEG: tmp5 = (uint16_t)currplayer_vel_x; break;
  case SLOPE_66DEG: tmp5 = (uint16_t)(currplayer_vel_x << 1); break;
  }
}
static void apply_slope_vel(void) {
  tmp8 = currplayer_slope_type;
  slope_vel();
  if (currplayer_slope_type & SLOPE_RISING)
    currplayer_vel_y = (currplayer_slope_type & SLOPE_UPSIDEDOWN) ? (int16_t)tmp5 : (int16_t)-tmp5;
  else
    currplayer_vel_y = (currplayer_slope_type & SLOPE_UPSIDEDOWN) ? (int16_t)-tmp5 : (int16_t)tmp5;
}

static void x_movement_coll(void) {
  if (currplayer_slope_frames) {
    currplayer_slope_frames -= 1;
    if (currplayer_slope_type) apply_slope_vel();
  }
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
  transitional_linear_absolute_currplayer_y = (uint16_t)(Generic.y + scroll_y);
  if (high_byte(currplayer_relx) > 0x10) {
    bg_coll_floor_spikes();
    if (bg_coll_R()) {
      if (!(options & platformer) && !force_platformer) cube_data[currplayer] |= 0x01;
      else currplayer_vel_x = 0;
    }
  }
}

static void x_movement(void) {
  old_x = currplayer_relx;
  currplayer_vel_x = (int16_t)cube_speed[speed];
  if (dashing[currplayer] == 4 || dashing[currplayer] == 5) {
    if (currplayer_rely < 0x0600 && scroll_y == min_scroll_y) cube_data[currplayer] |= 0x01;
  }
  if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) Generic.width = 8, Generic.height = 8;
  else Generic.width = currplayer_mini ? 0x08 : 0x0F, Generic.height = currplayer_mini ? 0x07 : 0x0F;
  transitional_sixteen_minus_generic_height_halved = (uint8_t)((uint8_t)(0x10 - Generic.height) >> 1);
  Generic.x = high_byte(currplayer_relx), Generic.y = high_byte(currplayer_rely);
  transitional_linear_absolute_currplayer_y = (uint16_t)(Generic.y + scroll_y);
  if (!(options & platformer) && !force_platformer) {
    currplayer_relx = (uint16_t)(currplayer_relx + currplayer_vel_x);
  } else {
    tmp7 = bg_coll_R();
    tmp8 = bg_coll_L();
    if (!tmp7 && (controllingplayer->hold & PAD_RIGHT)) currplayer_relx = (uint16_t)(currplayer_relx + currplayer_vel_x);
    else if (!tmp8 && (controllingplayer->hold & PAD_LEFT) && currplayer_relx > 0x1200)
      currplayer_relx = (uint16_t)(currplayer_relx - currplayer_vel_x);
    else currplayer_vel_x = 0;
    if (tmp7 && (controllingplayer->hold & PAD_RIGHT)) {
      tmp7 = (uint8_t)(high_byte(currplayer_relx) + (uint8_t)scroll_x);
      high_byte(currplayer_relx) = (uint8_t)(high_byte(currplayer_relx) - (((tmp7 + 4) & 0x07) - 4 + currplayer_mini));
    } else if (tmp8 && (controllingplayer->hold & PAD_LEFT)) {
      tmp8 = (uint8_t)(high_byte(currplayer_relx) + (uint8_t)scroll_x);
      high_byte(currplayer_relx) = (uint8_t)(high_byte(currplayer_relx) - (((tmp8 + 4) & 0x07) - 4));
    }
  }
  if (currplayer_relx > 0xf000) {
    currplayer_relx = old_x >= 0xf000 ? 0xf000 : 0x0000;
    currplayer_vel_x = 0;
  }
  if (!wrap_mode) {
    if (currplayer_rely < 0x0600) cube_data[currplayer] |= 0x01;
    else if (!(controllingplayer->hold & (PAD_A | PAD_UP))) cube_data[currplayer] &= 1;
  } else {
    if (currplayer_rely < 0x0600 && !dual && !twoplayer) currplayer_rely = 0xF900;
    else if (currplayer_rely > 0xF900 && !dual && !twoplayer) currplayer_rely = 0x0600;
    else if (!(controllingplayer->hold & (PAD_A | PAD_UP))) cube_data[currplayer] &= 1;
  }
}

static void runthecolls(void) {
  if (!invincible_counter) x_movement_coll();
  if (processXMovement) x_movement();
  if (!invincible_counter) bg_coll_death();
}

static void decrement_was_on_slope(void) {
  if (currplayer_was_on_slope_counter) {
    currplayer_was_on_slope_counter--;
    if (!currplayer_was_on_slope_counter) {
      if (gamemode == GAMEMODE_CUBE || gamemode == GAMEMODE_BALL) {
        uint8_t y = (uint8_t)((currplayer_table_idx & ~1) | ((currplayer_slope_type & SLOPE_UPSIDEDOWN) ? 1 : 0)) & 3;
        if (gamemode == GAMEMODE_BALL) {
          switch (currplayer_slope_type) {
          case SLOPE_22DEG_UP:
          case SLOPE_22DEG_UP_UD: currplayer_vel_y = (int16_t)(currplayer_vel_y + (int16_t)EXIT_SLOPE_BALL_22[y]); break;
          case SLOPE_66DEG_UP:
          case SLOPE_66DEG_UP_UD: currplayer_vel_y = (int16_t)(currplayer_vel_y + (int16_t)EXIT_SLOPE_BALL_66[y]); break;
          }
        } else {
          switch (currplayer_slope_type) {
          case SLOPE_22DEG_UP:
          case SLOPE_22DEG_UP_UD: currplayer_vel_y = (int16_t)(currplayer_vel_y + (int16_t)EXIT_SLOPE_CUBE_22[y]); break;
          }
        }
      }
      currplayer_slope_type = 0;
    }
  } else {
    currplayer_last_slope_type = 0;
    currplayer_slope_type = 0;
  }
}

/* ---------------------------------------------------------------- objects (sprite_loading.h) */
#define OUTL 0xFC
#define COLR 0xFD
#define DECO 0xFE
#define SPBH 0xFF
static const uint8_t OUTLINES[] = {0x30, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x21, 0x17, 0x0F};
#define PORTAL_TO_TOP_DIFF 0x5A
static const uint8_t sprite_heights[256] = {
    0x34, 0x34, 0x34, 0x34, 0x34, 0x12, 0x12, SPBH, 0x28, 0x28, 0x03, 0x12, 0x03, 0x03, 0x03, SPBH,
    0x0e, 0x0e, 0x0e, 0x0e, 0x24, 0x24, 0x24, 0x34, 0x34, 0x34, SPBH, SPBH, SPBH, SPBH, SPBH, 0x12,
    0x24, 0x24, 0x34, 0x34, 0x34, 0x03, 0x03, 0x12, 0x12, 0x12, DECO, DECO, DECO, DECO, DECO, DECO,
    DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO, DECO,
    DECO, DECO, DECO, DECO, 0x12, 0x12, 0x12, 0x28, 0x28, DECO, DECO, 0x34, 0x12, 0x12, 0x30, SPBH,
    0x12, 0x12, 0x03, 0x03, 0x12, 0x12, 0x03, 0x03, 0x34, 0x10, SPBH, 0x12, 0x12, 0x12, 0x12, 0x34,
    0x34, 0x34, 0x34, 0x34, 0x34, 0x02, 0x10, SPBH, 0x10, SPBH, 0x34, 0x34, 0x34, 0x20, 0x08, SPBH,
    SPBH, SPBH, SPBH, SPBH, SPBH, 0x10, SPBH, 0x10, SPBH, 0x12, 0x12, 0x12, 0x12, SPBH, SPBH, SPBH,
    COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, 0x00, SPBH, COLR,
    COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, 0x00, SPBH, COLR,
    COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, 0x00, COLR, OUTL,
    OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL, OUTL,
    COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, 0x00, 0x00, COLR,
    COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, SPBH, SPBH, SPBH,
    COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, COLR, SPBH, SPBH, SPBH,
    SPBH, SPBH, SPBH, SPBH, SPBH, SPBH, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x10, 0x10, 0x03, 0x03, 0x00};
static const uint8_t sprite_widths[256] = {
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0e, 0x0e, 0x0F, 0x10, 0x0F, 0x0F, 0x0F, 0x10,
    0x28, 0x28, 0x28, 0x28, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x0F, 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0e, 0x0e, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x0F, 0x0F, 0x10, 0x10, 0x0F, 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x0E, 0x30, 0x30, 0x30, 0x30, 0x10, 0x10, 0x10, 0x10, 0x08, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x30, 0x30, 0x30, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    [0x80 ... 0xF7] = 0x10, 0x10, 0x08, 0x1B, 0x10, 0x10, 0x0E, 0x0E, 0x10};
static const int8_t sprite_x_offset[256] = {
    [0x08] = 1, [0x09] = 1, [0x10] = 4, [0x11] = 4, [0x12] = 4, [0x13] = 4, [0x18] = 8, [0x19] = 8,
    [0x47] = 1, [0x48] = 1, [0x6E] = 4, [0xF9] = 8, [0xFA] = -7};
static const int8_t sprite_y_offset[256] = {
    -2, -2, -2, -2, -2, -1, -1, 0, 4, 4, 5, -1, 0, 5, 0, 0,
    1, 1, 1, 1, -2, -2, -2, -2, -2, -2, 0, 0, 0, 0, 0, -1,
    -2, -2, -2, -2, -2, 5, 0, -1, -1, -1, 0, 0, 0, 0, 0, 0,
    [0x44] = -1, -1, -1, 4, 4, 0, 0, -2, 0, -1, -1, 0,
    -1, -1, 5, 0, -1, -1, 5, 0, -2, 0, 0, -1, -1, -1, -1, -2,
    -2, -2, -2, -2, -2, 0, 0, 0, 0, 0, -2, -2, -2, 0, 4, 0,
    [0x79] = -1, -1, -1, -1,
    [0xFA] = -7, 0, 0, 5, 2, 0};

/* object ids (objdefines.h) */
enum {
  O_COIN1 = 0x07, O_LEVEL_END_TRIGGER = 0x0F, O_COIN2 = 0x1A, O_COIN3 = 0x1B, O_COINGOTTEN1 = 0x1C, O_COINGOTTEN2 = 0x1D,
  O_COINGOTTEN3 = 0x1E, O_BLUE_ORB = 0x05, O_PINK_ORB = 0x06, O_YELLOW_ORB = 0x0B, O_GREEN_ORB = 0x27, O_RED_ORB = 0x28,
  O_BLACK_ORB = 0x44, O_DASH_ORB = 0x45, O_DASH_GRAVITY_ORB = 0x46, O_DASH_ORB_45DEG_UP = 0x4C,
  O_DASH_GRAVITY_ORB_45DEG_UP = 0x4D, O_DASH_ORB_45DEG_DOWN = 0x50, O_DASH_GRAVITY_ORB_45DEG_DOWN = 0x51,
  O_TELEPORT_PORTAL_EXIT = 0x4F, O_TELEPORT_SQUARE_EXIT = 0x5A, O_DASH_ORB_UPWARDS = 0x5B, O_DASH_GRAVITY_ORB_UPWARDS = 0x5C,
  O_DASH_ORB_DOWNWARDS = 0x5D, O_DASH_GRAVITY_ORB_DOWNWARDS = 0x5E, O_TELEPORT_PORTAL_DOWNWARDS_EXIT = 0x67,
  O_TELEPORT_PORTAL_UPWARDS_EXIT = 0x69, O_PLAYER_INVIS_ON = 0x6F, O_GRAVITY_13_PORTAL_INVIS = 0x70,
  O_GRAVITY_12_PORTAL_INVIS = 0x71, O_GRAVITY_23_PORTAL_INVIS = 0x72, O_GRAVITY_2X_PORTAL_INVIS = 0x73,
  O_GRAVITY_1X_PORTAL_INVIS = 0x74, O_INVIS_TELEPORT_PORTAL_DOWNWARDS_EXIT = 0x76,
  O_INVIS_TELEPORT_PORTAL_UPWARDS_EXIT = 0x78, O_SKULL_ORB = 0x79, O_BLUE_ORB_MULTI = 0x7B, O_GREEN_ORB_MULTI = 0x7C,
  O_NULLSCAPES_ORB = 0x7D, O_PLAYER_INVIS_OFF = 0x7F, O_WRAP_MODE_ON = 0x8E, O_WRAP_MODE_OFF = 0x9E,
  O_FORCED_FREECAM_ON = 0xDD, O_X_SCROLL_SETTING = 0xDE, O_DEATH_CHANCE = 0xDF, O_FORCED_FREECAM_OFF = 0xED,
  O_MASK_SPRITES_ON = 0xEE, O_MASK_SPRITES_OFF = 0xEF, O_FORCED_TRAILS_ON = 0xF0, O_FORCED_TRAILS_OFF = 0xF1,
  O_PLAYER_TRAILS_ON = 0xF2, O_PLAYER_TRAILS_OFF = 0xF3, O_SLOWMODE_ON = 0xF4, O_SLOWMODE_OFF = 0xF5
};

static void animate_coin(uint8_t *timer, uint16_t *spd) {
  low_byte(activesprites_y[spr_index]) = (uint8_t)(low_byte(activesprites_y[spr_index]) - high_byte(*spd));
  *spd = (uint16_t)(*spd - 0x0040);
  (*timer)++;
  if (*timer == 40) activesprites_type[spr_index] = 0xFF, animating = 0;
}

static uint8_t sprite_load_special_behavior(void) {
#define killSprite_return0 do { activesprites_type[spr_index] = 0xFF; return 0; } while (0)
  switch (tmp4) {
  case O_DEATH_CHANCE: {
    uint8_t a = newrand(), b = newrand();
    if (((a ^ b) & 63) == 0) cube_data[currplayer] |= 1;
    triggers_hit[0]++;
    if (triggers_hit[0] == 10) triggers_hit[0] = 0, triggers_hit[1]++;
    if (triggers_hit[1] == 10) triggers_hit[1] = 0, triggers_hit[2]++;
    triggers++;
    killSprite_return0;
  }
  case O_MASK_SPRITES_ON: disco_sprites = 1; killSprite_return0;
  case O_MASK_SPRITES_OFF: disco_sprites = 0; killSprite_return0;
  case O_PLAYER_INVIS_ON: player_invis = 1; killSprite_return0;
  case O_PLAYER_INVIS_OFF: player_invis = 0; killSprite_return0;
  case O_WRAP_MODE_ON: wrap_mode = 1; killSprite_return0;
  case O_WRAP_MODE_OFF: wrap_mode = 0; killSprite_return0;
  case O_GRAVITY_13_PORTAL_INVIS: gravity_mod = 1; killSprite_return0;
  case O_GRAVITY_12_PORTAL_INVIS: gravity_mod = 2; killSprite_return0;
  case O_GRAVITY_23_PORTAL_INVIS: gravity_mod = 3; killSprite_return0;
  case O_GRAVITY_2X_PORTAL_INVIS: gravity_mod = 4; killSprite_return0;
  case O_GRAVITY_1X_PORTAL_INVIS: gravity_mod = 0; killSprite_return0;
  case O_NULLSCAPES_ORB:
    nullscapes_active++;
    if (nullscapes_active == 3) nullscapes_active = 0;
    killSprite_return0;
  case O_FORCED_FREECAM_ON: nocamlockforced = 1; killSprite_return0;
  case O_FORCED_FREECAM_OFF: nocamlockforced = 0; killSprite_return0;
  case O_X_SCROLL_SETTING:
    if (!cam_seesaw) {
      target_x_scroll_stop = (uint16_t)((activesprites_realy[spr_index] & 0xF0) << 8);
      killSprite_return0;
    }
    return 0;
  case O_SLOWMODE_ON:
    if (!level_resetting_flag) slowmode = 1;
    else if (level_resetting_flag && !timewarp_done) slowmode = 1, timewarp_done = 1;
    killSprite_return0;
  case O_SLOWMODE_OFF:
    if (!level_resetting_flag) slowmode = 0;
    else if (level_resetting_flag && !timewarp_done) slowmode = 0, timewarp_done = 1;
    killSprite_return0;
  case O_FORCED_TRAILS_ON: forced_trails = 1; killSprite_return0;
  case O_FORCED_TRAILS_OFF:
  case O_PLAYER_TRAILS_OFF: forced_trails = 0; killSprite_return0;
  case O_PLAYER_TRAILS_ON: forced_trails = 2; killSprite_return0;
  case O_TELEPORT_SQUARE_EXIT: teleport_output = activesprites_realy[spr_index]; return 0x0f;
  case O_INVIS_TELEPORT_PORTAL_DOWNWARDS_EXIT:
  case O_INVIS_TELEPORT_PORTAL_UPWARDS_EXIT:
  case O_TELEPORT_PORTAL_DOWNWARDS_EXIT:
  case O_TELEPORT_PORTAL_UPWARDS_EXIT:
    teleport_output = (uint8_t)(activesprites_realy[spr_index] + 0x10);
    return 0x10;
  case O_TELEPORT_PORTAL_EXIT: teleport_output = (uint8_t)(activesprites_realy[spr_index] + 0x10); return 0x30;
  case O_COIN1:
    if (!invisblocks ? prog.coin1[level] : prog.icoin1[level]) activesprites_type[spr_index] = O_COINGOTTEN1;
    /* fall through */
  case O_COINGOTTEN1:
    if (coin1_timer) animate_coin(&coin1_timer, &coin1_speed);
    return 0x10;
  case O_COIN2:
    if (!invisblocks ? prog.coin2[level] : prog.icoin2[level]) activesprites_type[spr_index] = O_COINGOTTEN2;
    /* fall through */
  case O_COINGOTTEN2:
    if (coin2_timer) animate_coin(&coin2_timer, &coin2_speed);
    return 0x10;
  case O_COIN3:
    if (!invisblocks ? prog.coin3[level] : prog.icoin3[level]) activesprites_type[spr_index] = O_COINGOTTEN3;
    /* fall through */
  case O_COINGOTTEN3:
    if (level == 20 /* fingerdash */ && minicoins != 10) {
      activesprites_type[spr_index] = 0xFF;
      return 0x10;
    }
    if (coin3_timer) animate_coin(&coin3_timer, &coin3_speed);
    return 0x10;
  case O_LEVEL_END_TRIGGER:
    gameState = STATE_LVLDONE, kandokidshack4 = 0;
  }
  return 0;
#undef killSprite_return0
}

static void settrailstuff(void) {
  if (forced_trails != 2 && !orbactive) orbactive = 1;
}
static void clearrobotjumpframes(void) { robotjumpframe[0] = 0, robotjumpframe[1] = 0; }
static void clear_slope_stuff(void) {
  currplayer_was_on_slope_counter = 0, currplayer_slope_frames = 0, currplayer_slope_type = 0, currplayer_last_slope_type = 0;
}
static void dual_cap_check(void) {
  if (dual && !twoplayer) {
    int i = (currplayer ^ 1) & 0x7F;
    player_vel_y[i] = (int16_t)(player_vel_y[i] / 2);
  }
}
static void common_dash_orb_routine(void) {
  invert_gravity(currplayer_gravity);
  update_currplayer_table_idx();
}

static uint16_t sprite_gamemode_y_adjust(void) {
  uint8_t gm = gamemode;
  if (gm == GAMEMODE_POGO) gm = GAMEMODE_SWING;
  else if (gm == GAMEMODE_SNAKE) gm = GAMEMODE_WAVE;
  else if (gm == GAMEMODE_FOOTBALL) gm = GAMEMODE_CUBE;
  uint8_t i = ((retro_mode && gm == GAMEMODE_ROBOT) || gm == GAMEMODE_NINJA) ? table_offset : (uint8_t)(gm | table_offset);
  return adjust_heights[currplayer_table_idx & 3][i];
}
static void gamemode_stuff(void) {
  clearrobotjumpframes();
  if (!dual || twoplayer) target_scroll_y = (uint16_t)(activesprites_y[spr_index] - PORTAL_TO_TOP_DIFF);
}
static void pad_stuff(void) {
  clear_slope_stuff();
  settrailstuff();
  currplayer_vel_y = (int16_t)sprite_gamemode_y_adjust();
  orbhitonthisframe[currplayer] = 1;
}

static void orb_effect(bool main_path) {
  switch (collided) {
  case O_BLUE_ORB_MULTI:
  case O_BLUE_ORB:
    if (!activesprites_activated[spr_index]) {
      invert_gravity(currplayer_gravity), invert_gravity(player_gravity[0]), invert_gravity(player_gravity[1]);
      update_currplayer_table_idx();
      dual_cap_check();
      currplayer_vel_y = (int16_t)(gamemode != GAMEMODE_BALL ? PAD_HEIGHT_BLUE[currplayer_table_idx & 3]
                                                             : ORB_BALL_HEIGHT_BLUE[currplayer_table_idx & 3]);
    }
    break;
  case O_GREEN_ORB_MULTI:
  case O_GREEN_ORB:
    if (!activesprites_activated[spr_index]) {
      invert_gravity(currplayer_gravity), invert_gravity(player_gravity[0]), invert_gravity(player_gravity[1]);
      update_currplayer_table_idx();
      dual_cap_check();
      currplayer_vel_y = (int16_t)sprite_gamemode_y_adjust();
    }
    break;
  case O_DASH_GRAVITY_ORB:
    if (!dashing[currplayer]) common_dash_orb_routine();
    /* fall through */
  case O_DASH_ORB: currplayer_vel_y = 0, dashing[currplayer] = 1; break;
  case O_DASH_GRAVITY_ORB_45DEG_UP:
    if (!dashing[currplayer]) common_dash_orb_routine();
    /* fall through */
  case O_DASH_ORB_45DEG_UP: currplayer_vel_y = (int16_t)-currplayer_vel_x, dashing[currplayer] = 2; break;
  case O_DASH_GRAVITY_ORB_45DEG_DOWN:
    if (!dashing[currplayer]) common_dash_orb_routine();
    /* fall through */
  case O_DASH_ORB_45DEG_DOWN: currplayer_vel_y = currplayer_vel_x, dashing[currplayer] = 3; break;
  case O_DASH_GRAVITY_ORB_UPWARDS:
    if (!dashing[currplayer]) common_dash_orb_routine();
    /* fall through */
  case O_DASH_ORB_UPWARDS:
    if (main_path) currplayer_vel_y = (int16_t)(currplayer_vel_x * 4);
    else currplayer_vel_y = currplayer_vel_x, currplayer_vel_x = 0;
    dashing[currplayer] = 4;
    break;
  case O_DASH_GRAVITY_ORB_DOWNWARDS:
    if (!dashing[currplayer]) common_dash_orb_routine();
    /* fall through */
  case O_DASH_ORB_DOWNWARDS:
    if (main_path) currplayer_vel_y = (int16_t)(-currplayer_vel_x * 4);
    else currplayer_vel_y = (int16_t)-currplayer_vel_x, currplayer_vel_x = 0;
    dashing[currplayer] = 5;
    break;
  default: currplayer_vel_y = (int16_t)sprite_gamemode_y_adjust();
  }
  if (activesprites_type[spr_index] != O_BLUE_ORB_MULTI && activesprites_type[spr_index] != O_GREEN_ORB_MULTI)
    activesprites_activated[spr_index]++;
}

static void sprite_gamemode_main(void) {
  if (controllingplayer->hold & (PAD_A | PAD_UP)) {
    if (gamemode == GAMEMODE_BALL) ball_switched[currplayer] = 1;
    if ((cube_data[currplayer] & 2) || controllingplayer->press & (PAD_A | PAD_UP)) {
      if (gamemode == GAMEMODE_SPIDER && collided == O_BLACK_ORB) black_orbed[currplayer] = 1;
      if (gamemode == GAMEMODE_ROBOT) orbed[currplayer] = 1;
      cube_data[currplayer] &= 1;
      settrailstuff();
      clear_slope_stuff();
      orb_effect(true);
      orbhitonthisframe[currplayer] = 1;
    }
  }
}

static void sprite_gamemode_controller_check(void) {
  if (controllingplayer->press & (PAD_A | PAD_UP)) {
    cube_data[currplayer] &= 0x01;
    settrailstuff();
    orb_effect(false);
  }
}

static bool orb_capable(void) {
  return gamemode == GAMEMODE_CUBE || gamemode == GAMEMODE_POGO || gamemode == GAMEMODE_BALL || gamemode == GAMEMODE_ROBOT ||
         gamemode == GAMEMODE_NINJA || gamemode == GAMEMODE_SPIDER || gamemode >= GAMEMODE_SWING;
}

static void sprite_collide_lookup(void) {
  if (activesprites_activated[spr_index] && !dual && !(options & platformer) && !force_platformer) return;
  switch (collided) {
  case 0x79: /* skull orb */
    activesprites_animated[spr_index] = 1;
    if (orb_capable() && cube_data[currplayer] & 0x02) {
      if (controllingplayer->hold & (PAD_A | PAD_UP)) cube_data[currplayer] |= 0x01, activesprites_animated[spr_index] = 0;
    } else if (controllingplayer->press & (PAD_A | PAD_UP)) cube_data[currplayer] |= 0x01, activesprites_animated[spr_index] = 0;
    return;
  case 0x7A: /* white orb */
    if (orb_capable() && cube_data[currplayer] & 0x02) {
      if (controllingplayer->hold & (PAD_A | PAD_UP)) currplayer_vel_y = 0, cube_data[currplayer] &= 0x01, activesprites_activated[spr_index] = 1;
    } else if (controllingplayer->press & (PAD_A | PAD_UP))
      currplayer_vel_y = 0, cube_data[currplayer] &= 0x01, activesprites_activated[spr_index] = 1;
    return;
  case 0x00: /* cube portal */
    orbactive = 0;
    robotjumpframe[0] = 0, robotjumpframe[1] = 0;
    exitPortalTimer = 10;
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = 0;
    gamemode = retro_mode ? GAMEMODE_ROBOT : GAMEMODE_CUBE;
    clearrobotjumpframes();
    return;
  case 0x01: case 0x03: /* ship, ufo */
    settrailstuff();
    /* fall through */
  case 0x02: /* ball */
    if (gamemode != collided) currplayer_vel_y = (int16_t)(currplayer_vel_y / 2);
    gamemode = collided;
    activesprites_activated[spr_index] = 1;
    gamemode_stuff();
    return;
  case 0x04: /* robot */
    exitPortalTimer = 10;
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = (int16_t)(currplayer_vel_y / 2);
    gamemode = GAMEMODE_ROBOT;
    clearrobotjumpframes();
    return;
  case 0x17: /* spider */
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = 0;
    gamemode = GAMEMODE_SPIDER;
    gamemode_stuff();
    return;
  case 0x24: settrailstuff(); gamemode = GAMEMODE_WAVE; gamemode_stuff(); return;
  case 0x6B: settrailstuff(); gamemode = GAMEMODE_SNAKE; gamemode_stuff(); return;
  case 0x6C: gamemode = GAMEMODE_FOOTBALL; gamemode_stuff(); return;
  case 0x6A:
    settrailstuff();
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = 0;
    gamemode = GAMEMODE_POGO;
    gamemode_stuff();
    return;
  case 0x4B:
    settrailstuff();
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = 0;
    gamemode = GAMEMODE_SWING;
    gamemode_stuff();
    return;
  case 0x58:
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = 0;
    gamemode = GAMEMODE_NINJA;
    clearrobotjumpframes();
    return;
  case 0x64:
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = 0;
    gamemode = newrand() & 7;
    activesprites_activated[spr_index]++;
    gamemode_stuff();
    return;
  case 0x7E:
    if (gamemode == GAMEMODE_WAVE || gamemode == GAMEMODE_SNAKE) currplayer_vel_y = 0;
    do gamemode = newrand() & 15;
    while (gamemode > 0x0B);
    activesprites_activated[spr_index]++;
    gamemode_stuff();
    return;
  case 0x6E: minicoins++; activesprites_type[spr_index] = 0xFF; return;
  case 0x08: case 0x10: case 0x11: case 0x47: case 0xFC: /* gravity down portals */
    if (!currplayer_gravity) return;
    currplayer_gravity = GRAVITY_DOWN;
    goto gravity_common;
  case 0x09: case 0x12: case 0x13: case 0x48: case 0xFB: /* gravity up portals */
    if (currplayer_gravity) return;
    currplayer_gravity = GRAVITY_UP;
  gravity_common:
    settrailstuff();
    currplayer_vel_y = (int16_t)(currplayer_vel_y / 2);
    robotjumptime[currplayer] = 0;
    activesprites_activated[spr_index]++;
    update_currplayer_table_idx();
    return;
  case 0x14: speed = 1; return;
  case 0x15: speed = 0; return;
  case 0x16: speed = 2; return;
  case 0x20: speed = 3; return;
  case 0x21: speed = 4; return;
  case 0x6D: speed = 5; return;
  case 0x63: gravity_mod = 0; return;
  case 0x5F: gravity_mod = 1; return;
  case 0x60: gravity_mod = 2; return;
  case 0x61: gravity_mod = 3; return;
  case 0x62: gravity_mod = 4; return;
  case 0x18: currplayer_mini = 1, player_mini[0] = 1, player_mini[1] = 1; update_currplayer_table_idx(); return;
  case 0x19: currplayer_mini = 0, player_mini[0] = 0, player_mini[1] = 0; update_currplayer_table_idx(); return;
  case 0x22: /* dual */
    if (!activesprites_activated[spr_index]) {
      dual = 1;
      target_scroll_y = (uint16_t)(activesprites_y[spr_index] - PORTAL_TO_TOP_DIFF);
      if (twoplayer) player_gravity[1] = player_gravity[0] ^ 0xFF;
      else {
        player_relx[1] = player_relx[0], player_rely[1] = currplayer_rely;
        player_gravity[1] = currplayer_gravity ^ 0xFF;
        player_vel_y[1] = (int16_t)-currplayer_vel_y, player_mini[1] = player_mini[0];
      }
      activesprites_activated[spr_index] = 1;
    }
    return;
  case 0x23: /* single */
    if (!activesprites_activated[spr_index]) {
      if (!twoplayer) dual = 0, player_rely[0] = currplayer_rely, player_gravity[0] = currplayer_gravity, player_vel_y[0] = currplayer_vel_y;
      else player_gravity[1] = player_gravity[0];
      activesprites_activated[spr_index] = 1;
      exitPortalTimer = 10;
    }
    return;
  case 0x59: /* teleport square */
    if ((cube_data[currplayer] & 2) || (controllingplayer->press & (PAD_A | PAD_UP))) {
      currplayer_vel_y = 0;
      orbed[currplayer] = 1;
      cube_data[currplayer] &= 1;
      high_byte(currplayer_rely) = teleport_output;
    }
    return;
  case 0x4E: case 0x66: case 0x68: case 0x75: case 0x77: case 0xED: /* teleport portals */
    high_byte(currplayer_rely) = teleport_output;
    return;
  case 0xF9:
    if (dashing[currplayer]) dashing[currplayer] = 0, orbed[currplayer] = 1, currplayer_vel_y = 0;
    return;
  case 0xF8: hblocked[currplayer] = 1; return;
  case 0xF7: jblocked[currplayer] = 1, orbed[currplayer] = 1; return;
  case 0xFA: dblocked[currplayer] = 1; return;
  case 0xF6: fblocked[currplayer] = 1; return;
  case 0x07: case 0x1C: /* coins */
    if (practice_point_count || coin1_timer) return;
    coins |= COIN_1, coin1_timer = 1, coin1_speed = 0x0200;
    animating = 1;
    return;
  case 0x1A: case 0x1D:
    if (practice_point_count || coin2_timer) return;
    coins |= COIN_2, coin2_timer = 1, coin2_speed = 0x0200;
    animating = 1;
    return;
  case 0x1B: case 0x1E:
    if (practice_point_count || coin3_timer) return;
    coins |= COIN_3, coin3_timer = 1, coin3_speed = 0x0200;
    animating = 1;
    return;
  case 0x0A: case 0x0C: table_offset = YELLOW_PAD_T; pad_stuff(); return;
  case 0x25: case 0x26: table_offset = PINK_PAD_T; pad_stuff(); return;
  case 0x52: case 0x53: table_offset = RED_PAD_T; pad_stuff(); return;
  case 0x65: /* green pad */
    invert_gravity(currplayer_gravity), invert_gravity(player_gravity[0]), invert_gravity(player_gravity[1]);
    update_currplayer_table_idx();
    table_offset = YELLOW_ORB_T;
    pad_stuff();
    activesprites_activated[spr_index]++;
    return;
  case 0x0D: case 0xFD: /* gravity pad (down) */
    clear_slope_stuff();
    if (!currplayer_gravity) {
      settrailstuff();
      currplayer_gravity = GRAVITY_UP;
      update_currplayer_table_idx();
      currplayer_vel_y = (int16_t)PAD_HEIGHT_BLUE[currplayer_table_idx & 3];
      orbhitonthisframe[currplayer] = 1;
    }
    activesprites_activated[spr_index]++;
    return;
  case 0x0E: case 0xFE: /* gravity pad (up) */
    clear_slope_stuff();
    if (currplayer_gravity) {
      settrailstuff();
      currplayer_gravity = GRAVITY_DOWN;
      update_currplayer_table_idx();
      currplayer_vel_y = (int16_t)PAD_HEIGHT_BLUE[currplayer_table_idx & 3];
      orbhitonthisframe[currplayer] = 1;
    }
    activesprites_activated[spr_index]++;
    return;
  case 0x54: /* spider orb up */
    if (!((cube_data[currplayer] & 2) || (controllingplayer->press & (PAD_A | PAD_UP)))) return;
    cube_data[currplayer] &= 1;
    /* fall through */
  case 0x56: /* spider pad up */
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D);
    currplayer_vel_y = 0;
    currplayer_gravity = GRAVITY_UP;
    update_currplayer_table_idx();
    spider_up_wait();
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U);
    currplayer_vel_y = 0;
    orbed[currplayer] = 1;
    activesprites_activated[spr_index]++;
    return;
  case 0x55: /* spider orb down */
    if (!((cube_data[currplayer] & 2) || (controllingplayer->press & (PAD_A | PAD_UP)))) return;
    cube_data[currplayer] &= 1;
    /* fall through */
  case 0x57: /* spider pad down */
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_U - 1);
    currplayer_vel_y = 0;
    currplayer_gravity = GRAVITY_DOWN;
    update_currplayer_table_idx();
    spider_down_wait();
    high_byte(currplayer_rely) = (uint8_t)(high_byte(currplayer_rely) - eject_D);
    currplayer_vel_y = 0;
    orbed[currplayer] = 1;
    activesprites_activated[spr_index]++;
    return;
  case 0x0B: table_offset = YELLOW_ORB_T; goto orb_cmn;
  case 0x27: table_offset = YELLOW_ORB_T; goto orb_cmn;
  case 0x1F: table_offset = YLW_BIGGER_T; goto orb_cmn;
  case 0x29: table_offset = YLW_SMALLER_T; goto orb_cmn;
  case 0x06: table_offset = PINK_ORB_T; goto orb_cmn;
  case 0x44: table_offset = BLACK_ORB_T; goto orb_cmn;
  case 0x28: table_offset = RED_ORB_T; goto orb_cmn;
  case 0x05: case 0x45: case 0x46: case 0x4C: case 0x4D: case 0x50: case 0x51: case 0x5B: case 0x5C: case 0x5D: case 0x5E:
  case 0x7B: case 0x7C:
  orb_cmn:
    ufo_orbed[currplayer] = 1;
    if (orb_capable()) sprite_gamemode_main();
    else sprite_gamemode_controller_check();
    update_currplayer_table_idx();
    return;
  default: return;
  }
}

static uint8_t check_collision(void) {
  uint16_t a = (uint16_t)(Generic.x + Generic.width);
  if (a <= 0xFF && (uint8_t)a < Generic2.x) return 0;
  a = (uint16_t)(Generic2.x + Generic2.width);
  if (a <= 0xFF && (uint8_t)a < Generic.x) return 0;
  a = (uint16_t)(Generic.y + Generic.height);
  if (a <= 0xFF && (uint8_t)a < Generic2.y) return 0;
  a = (uint16_t)(Generic2.y + Generic2.height);
  if (a > 0xFF) return 1;
  if ((uint8_t)a < Generic.y) return 0;
  return 1;
}

static void player_box(void) {
  if (gamemode != GAMEMODE_WAVE) Generic.width = currplayer_mini ? 0x08 : 0x0F, Generic.height = currplayer_mini ? 0x07 : 0x0F;
  else Generic.width = 8, Generic.height = 8;
  transitional_sixteen_minus_generic_height_halved = (uint8_t)((uint8_t)(0x10 - Generic.height) >> 1);
}

static void sprite_collide(void) {
  player_box();
  Generic.x = (uint8_t)(high_byte(currplayer_relx) + 1);
  Generic.y = (uint8_t)(high_byte(currplayer_rely) + transitional_sixteen_minus_generic_height_halved);
  spr_index = 0;
  do {
    tmp3 = activesprites_active[spr_index];
    if (!tmp3) continue;
    tmp4 = activesprites_type[spr_index];
    tmp2 = sprite_heights[tmp4];
    tmp9 = sprite_widths[tmp4];
    switch (tmp2) {
    case DECO:
      if (twoplayer || !viseffects) activesprites_type[spr_index] = 0xFF;
      continue;
    case COLR:
      if (discomode) { activesprites_type[spr_index] = 0xFF; continue; }
      tmp2 = tmp4 & 0x3F;
      if (tmp2 == 0x20) tmp2 = 0x30;
      if (tmp4 == 0x9F) {
        pal_col(0, color1), pal_col(1, oneShadeDarker(color1)), pal_col(9, oneShadeDarker(color1));
        lastbgcolortype = tmp4;
      } else if (tmp4 == 0xAE) {
        pal_col(6, color2), pal_col(5, oneShadeDarker(color2));
        lastgcolortype = tmp4;
      } else if (tmp4 >= 0xC0 && !invisblocks) {
        pal_col(6, tmp2), pal_col(5, oneShadeDarker(tmp2));
        lastgcolortype = tmp4;
      } else if (tmp4 < 0xC0) {
        pal_col(0, tmp2), pal_col(1, oneShadeDarker(tmp2)), pal_col(9, oneShadeDarker(tmp2)), pal_col(0x0D, oneShadeDarker(tmp2));
        lastbgcolortype = tmp4;
        if (invisblocks) pal_col(6, tmp2), pal_col(5, oneShadeDarker(tmp2));
      }
      activesprites_type[spr_index] = 0xFF;
      continue;
    case 0: continue;
    case OUTL:
      outline_color = tmp4 == 0xAF ? color1 : OUTLINES[tmp4 & 0x0F];
      activesprites_type[spr_index] = 0xFF;
      continue;
    case SPBH:
      if ((tmp2 = sprite_load_special_behavior()) == 0) continue;
    }
    Generic2.height = tmp2, Generic2.width = tmp9;
    int16_t v = (int16_t)(sprite_x_offset[tmp4] + activesprites_realx[spr_index]);
    Generic2.x = (v & 0xFF00) ? ((v & 0x8000) ? 0x00 : 0xFF) : (uint8_t)v;
    v = (int16_t)(sprite_y_offset[tmp4] + activesprites_realy[spr_index]);
    Generic2.y = (v & 0xFF00) ? ((v & 0x8000) ? 0x00 : 0xFF) : (uint8_t)v;
    if (check_collision()) sprite_collide_lookup();
    else if (activesprites_type[spr_index] == O_SKULL_ORB && activesprites_animated[spr_index] == 1) activesprites_animated[spr_index] = 2;
  } while (++spr_index < max_loaded_sprites);
  player_box();
}
#undef table_offset
#undef collided

/* load_next_sprite / check_spr_objects / init_sprites (nesdash.s) */
static void load_next_sprite(uint8_t slot) {
  if (!sprite_data) return;
  if (sprite_data[0] == 0xFF) { sprite_data = NULL; return; }
  activesprites_x[slot] = (uint16_t)(sprite_data[0] | sprite_data[1] << 8);
  activesprites_realx[slot] = sprite_data[0];
  activesprites_y[slot] = (uint16_t)(sprite_data[2] | sprite_data[3] << 8);
  activesprites_realy[slot] = sprite_data[2];
  activesprites_type[slot] = sprite_data[4];
  activesprites_activated[slot] = 0, activesprites_animated[slot] = 0;
  sprite_data += 5;
}

static bool is_coin(uint8_t t) { return t == O_COIN1 || t == O_COIN2 || t == O_COIN3; }

static void check_spr_objects(void) {
  for (int x = max_loaded_sprites - 1; x >= 0; x--) {
    uint8_t act;
    if (activesprites_type[x] == 0xFF) goto dead;
    uint16_t d = (uint16_t)(activesprites_x[x] - (uint16_t)scroll_x);
    if (d & 0x8000) {
      if (is_coin(activesprites_type[x])) animating = 0;
    dead:
      load_next_sprite((uint8_t)x);
      act = 0;
    } else if (d >> 8) act = 0;
    else {
      uint16_t ry = (uint16_t)(activesprites_y[x] - scroll_y - 1);
      activesprites_realy[x] = (uint8_t)ry;
      if ((ry >> 8) && !(animating && is_coin(activesprites_type[x]))) act = 0;
      else activesprites_realx[x] = (uint8_t)d, act = 1;
    }
    activesprites_active[x] = act;
  }
}

static void init_sprites(void) {
  for (int i = 0; i < max_loaded_sprites; i++) activesprites_type[i] = 0xFF;
  uint8_t shi = (uint8_t)(scroll_x >> 8), slo = (uint8_t)scroll_x;
  if (shi)
    while (sprite_data[0] != 0xFF && shi > sprite_data[1]) sprite_data += 5;
  if (slo)
    while (sprite_data[0] != 0xFF) {
      uint16_t diff = (uint16_t)(((shi << 8) | slo) - (sprite_data[0] | sprite_data[1] << 8));
      if (!(diff >> 8) || (diff & 0x8000)) break;
      sprite_data += 5;
    }
  for (int x = max_loaded_sprites; x > 0;) {
    if (sprite_data[0] == 0xFF) return;
    x--;
    load_next_sprite((uint8_t)x);
    if (!sprite_data) return;
  }
}

/* ---------------------------------------------------------------- practice (practice_state.h) */
void game_store_practice(void) {
  if (!practice_point_count) latest_practice_point = 0;
  else latest_practice_point = (latest_practice_point + 1) & 7;
  if (practice_point_count < MAX_PRACTICE_POINTS) practice_point_count++;
  uint8_t i = latest_practice_point;
  curr_practice_point = latest_practice_point;
  typeof(practice[0]) *p = &practice[i];
  p->p1x = player_relx[0], p->p2x = player_relx[1], p->p1y = player_rely[0], p->p2y = player_rely[1];
  p->p1vx = (uint16_t)player_vel_x[0], p->p2vx = (uint16_t)player_vel_x[1];
  p->p1vy = (uint16_t)player_vel_y[0], p->p2vy = (uint16_t)player_vel_y[1];
  p->rot1 = cube_rotate[0], p->rot2 = cube_rotate[1];
  p->nocamlockforced = nocamlockforced;
  p->g1 = player_gravity[0], p->g2 = player_gravity[1], p->m1 = player_mini[0], p->m2 = player_mini[1];
  p->sf1 = slope_frames[0], p->sf2 = slope_frames[1], p->wos1 = was_on_slope_counter[0], p->wos2 = was_on_slope_counter[1];
  p->st1 = slope_type[0], p->st2 = slope_type[1], p->lst1 = last_slope_type[0], p->lst2 = last_slope_type[1];
  p->scroll_x = scroll_x, p->scroll_y = scroll_y, p->scroll_y_subpx = scroll_y_subpx;
  p->target_scroll_y = target_scroll_y, p->min_scroll_y = min_scroll_y;
  p->player_invis = player_invis, p->gamemode = gamemode, p->dual = dual, p->speed = speed;
  p->parallax_scroll_x = parallax_scroll_x, p->bgtype = lastbgcolortype, p->gtype = lastgcolortype;
  p->outline = outline_color, p->orbactive = orbactive, p->null_active = nullscapes_active, p->null_type = nullscapes_orb_type;
  p->disco_sprites = disco_sprites, p->slowmode = slowmode, p->forced_trails = forced_trails;
  p->gravity_mod = gravity_mod, p->kandoframecnt = kandoframecnt;
  practice_sprite_x_pos = high_byte(player_relx[0]);
  auto_practicepoint_timer = 200;
}

static void load_practice_state(void) {
  typeof(practice[0]) *p = &practice[curr_practice_point];
  player_relx[0] = p->p1x, player_relx[1] = p->p2x, player_rely[0] = p->p1y, player_rely[1] = p->p2y;
  player_vel_x[0] = (int16_t)p->p1vx, player_vel_x[1] = (int16_t)p->p2vx;
  player_vel_y[0] = (int16_t)p->p1vy, player_vel_y[1] = (int16_t)p->p2vy;
  cube_rotate[0] = p->rot1, cube_rotate[1] = p->rot2;
  currplayer_gravity = player_gravity[0] = p->g1, player_gravity[1] = p->g2;
  currplayer_mini = player_mini[0] = p->m1, player_mini[1] = p->m2;
  currplayer_slope_frames = slope_frames[0] = p->sf1, slope_frames[1] = p->sf2;
  currplayer_was_on_slope_counter = was_on_slope_counter[0] = p->wos1, was_on_slope_counter[1] = p->wos2;
  currplayer_slope_type = slope_type[0] = p->st1, slope_type[1] = p->st2;
  nocamlockforced = p->nocamlockforced;
  disco_sprites = p->disco_sprites, slowmode = p->slowmode, forced_trails = p->forced_trails;
  gravity_mod = p->gravity_mod, kandoframecnt = p->kandoframecnt;
  currplayer_last_slope_type = last_slope_type[0] = p->lst1, last_slope_type[1] = p->lst2;
  scroll_x = p->scroll_x;
  old_trail_scroll_y = scroll_y = p->scroll_y;
  scroll_y_subpx = p->scroll_y_subpx;
  target_scroll_y = p->target_scroll_y, min_scroll_y = p->min_scroll_y;
  player_invis = p->player_invis, gamemode = p->gamemode, dual = p->dual, speed = p->speed;
  parallax_scroll_x = p->parallax_scroll_x;
  nullscapes_active = p->null_active, nullscapes_orb_type = p->null_type;
  practice_sprite_x_pos = high_byte(p->p1x);
  outline_color = p->outline;
  lastgcolortype = p->gtype, lastbgcolortype = p->bgtype;
  scroll_x -= 256 + 16;
  uint8_t c = lastbgcolortype & 0x3F;
  pal_col(0, c), pal_col(0x0D, c);
  c = oneShadeDarker(c);
  pal_col(1, c), pal_col(9, c), pal_col(13, c);
  c = lastgcolortype & 0x3F;
  pal_col(6, c), pal_col(5, oneShadeDarker(c));
  c = outline_color & 0x3F;
  pal_col(3, c), pal_col(7, c);
  currplayer_relx = player_relx[currplayer], currplayer_rely = player_rely[currplayer];
  currplayer_vel_x = player_vel_x[currplayer], currplayer_vel_y = player_vel_y[currplayer];
  currplayer_gravity = player_gravity[currplayer];
  auto_practicepoint_timer = 200;
  update_currplayer_table_idx();
}

static void check_practice_point_deletion(void) {
  if (practicebuffer || (practice_point_count > 1 && (joypad1.press & PAD_SELECT) && !(joypad1.hold & (PAD_UP | PAD_DOWN)))) {
    curr_practice_point--;
    practicebuffer = 0;
    if (latest_practice_point) latest_practice_point--;
    if (curr_practice_point >= practice_point_count) curr_practice_point = practice_point_count - 1;
  }
}

/* ---------------------------------------------------------------- attempts and progress */
static void increment_attempt_count(void) {
  for (int i = 0; i < 6; i++) {
    if (++attemptCounter[i] != 10) return;
    attemptCounter[i] = 0;
  }
  attemptCounter[6]++;
}

uint8_t level_percent(void) {
  uint32_t len = level_list[level].length;
  uint32_t p = len ? scroll_x / len : 0;
  if ((int32_t)scroll_x < 0) p = 0;
  return p > 99 ? 99 : (uint8_t)p;
}

void update_level_completeness(void) {
  uint8_t y = level_percent();
  uint8_t *t = practice_point_count ? (invisblocks ? prog.ipractice : prog.practice) : (invisblocks ? prog.inormal : prog.normal);
  if (y >= t[level]) t[level] = y;
}

void set_completion_data(void) {
  if (!practice_point_count) {
    if (!invisblocks) {
      prog.complete[level] = 1;
      if (coins & COIN_1) prog.coin1[level] = 1;
      if (coins & COIN_2) prog.coin2[level] = 1;
      if (coins & COIN_3) prog.coin3[level] = 1;
      prog.normal[level] = 100;
    } else {
      prog.icomplete[level] = 1;
      if (coins & COIN_1) prog.icoin1[level] = 1;
      if (coins & COIN_2) prog.icoin2[level] = 1;
      if (coins & COIN_3) prog.icoin3[level] = 1;
      prog.inormal[level] = 100;
    }
  } else if (!invisblocks) prog.practice[level] = 100;
  else prog.ipractice[level] = 100;
}

/* ---------------------------------------------------------------- reset_level (reset_level.h, level_loading.h) */
static void unrle_first_screen(void) {
  increment_attempt_count();
  if (practice_point_count) {
    int32_t ii = (int32_t)(practice[curr_practice_point].scroll_x >> 4), skip = ii > 2 ? ii - 2 : 0;
    for (int32_t k = 0; k < skip; k++) unrle_next_column();
    ring_next = skip;
    decode_to(ii); /* (two columns more on the left: this screen is wider) */
    coll_cols = ii;
    rld_column = (uint8_t)(ii & 0x0F);
    load_practice_state();
    for (int i = 0; i < 16; i++) draw_screen(), scroll_x++;
  } else {
    scroll_x = (uint32_t)-256;
    parallax_scroll_x = 0, parallax_scroll_column = 0, parallax_scroll_column_start = 0;
  }
  for (int i = 0; i < 256; i++) draw_screen(), scroll_x++;
  init_sprites();
}

static void reset_level(void) {
  gameState = STATE_GAME;
  init_rld(level);
  scroll_y = spawn_scroll_y_pos;
  scroll_y_subpx = 0;
  currplayer_was_on_slope_counter = 0, was_on_slope_counter[0] = was_on_slope_counter[1] = 0;
  ufo_orbed[0] = ufo_orbed[1] = 0, black_orbed[0] = black_orbed[1] = 0;
  currplayer_slope_frames = 0;
  player_mini[0] = player_mini[1] = currplayer_mini = 0;
  player_vel_x[0] = player_vel_x[1] = currplayer_vel_x = 0;
  player_vel_y[0] = player_vel_y[1] = currplayer_vel_y = 0;
  cube_rotate[0] = cube_rotate[1] = 0;
  chargepower[0] = chargepower[1] = 0;
  coins = 0, orbactive = 0, discoframe = 0;
  cube_data[0] = cube_data[1] = 0;
  scroll_x = 0, drawing_frame = 0, gravity_mod = 0, disco_sprites = 0, nullscapes_active = 0;
  slowmode = 0, wrap_mode = 0, minicoins = 0, kandoframecnt = 0, forced_trails = 0, make_cube_jump_higher = 0;
  nullscapes_orb_type = 0, player_invis = 0, robotjumpframe[0] = robotjumpframe[1] = 0;
  slope_frames[0] = slope_frames[1] = 0, nocamlockforced = 0;
  coin1_timer = coin2_timer = coin3_timer = 0;
  slope_type[0] = slope_type[1] = currplayer_slope_type = SLOPE_NONE;
  last_slope_type[0] = last_slope_type[1] = currplayer_last_slope_type = SLOPE_NONE;
  curr_practice_point = latest_practice_point;
  outline_color = 0x30;
  dual = twoplayer ? 1 : 0;
  player_gravity[0] = GRAVITY_DOWN;
  for (int i = 0; i < max_loaded_sprites; i++) activesprites_active[i] = 0, activesprites_anim_frame[i] = 0;
  player_rely[0] = player_rely[1] = currplayer_rely = spawn_y_pos;
  target_scroll_y = scroll_y;
  player_gravity[1] = twoplayer ? GRAVITY_DOWN : GRAVITY_UP;
  currplayer_gravity = GRAVITY_DOWN;
  if (!(options & platformer) && !force_platformer) player_relx[0] = player_relx[1] = currplayer_relx = 0;
  else player_relx[0] = player_relx[1] = currplayer_relx = 0x1110;
  curr_x_scroll_stop = target_x_scroll_stop = 0x5000;
  if (!practice_point_count) {
    memset(player_old_rely, 0, sizeof player_old_rely);
    memset(trail_sprites_visible, 0, sizeof trail_sprites_visible);
    invincible_counter = 8;
  }
  update_currplayer_table_idx();
  unrle_first_screen();
  current_saw_set = 14; /* SAWBLADESA */
}

/* ---------------------------------------------------------------- state_game in steps */
static uint8_t kandokidshack2;

void game_begin(void) {
  /* (what earlier levels left behind: the NES keeps it from attempt to attempt, a new level starts clean) */
  exitPortalTimer = 0, invincible_counter = 0, ballframe = 0, shuffle_offset = 0;
  memset(orbed, 0, sizeof orbed), memset(ball_switched, 0, sizeof ball_switched), memset(dashing, 0, sizeof dashing);
  memset(robotjumptime, 0, sizeof robotjumptime), memset(robotframe, 0, sizeof robotframe), memset(spiderframe, 0, sizeof spiderframe);
  memset(hblocked, 0, sizeof hblocked), memset(jblocked, 0, sizeof jblocked), memset(fblocked, 0, sizeof fblocked);
  memset(dblocked, 0, sizeof dblocked), memset(ninjajumps, 0, sizeof ninjajumps), memset(black_orbed, 0, sizeof black_orbed);
  memset(attemptCounter, 0, sizeof attemptCounter);
  jumps = 0, triggers = 0, practice_point_count = 0, force_platformer = 0, slowmode = 0, disco_sprites = 0;
  triggers_hit[0] = triggers_hit[1] = triggers_hit[2] = 0;
  cube_rotate[0] = cube_rotate[1] = 0;
}

void game_start(void) {
  fartmode = newrand();
  coin1_timer = coin2_timer = coin3_timer = 0;
  kandokidshack2 = 0, kandokidshack4 = 0, animating = 0, orbactive = 0, nocamlock = 0, currplayer = 0;
  END_LEVEL_TIMER = 0, kandoframecnt = 0, practice_point_count = 0;
  memset(trail_sprites_visible, 0, sizeof trail_sprites_visible);
  memset(attemptCounter, 0, sizeof attemptCounter);
  outline_color = 0x30;
  lastgcolortype = 0xFF, lastbgcolortype = 0xFF;
  memcpy(PAL_BUF_RAW, pal_paletteDefault, 16);
  memcpy(PAL_BUF_RAW + 16, pal_paletteDefaultSP, 16);
  controllingplayer = &joypad1;
  reset_level();
  pal_col(0x0A, 0x0F);
  pal_col(0x0B, color1);
  memset(player_old_rely, 0, sizeof player_old_rely);
  switch (discomode) {
  default: pal_col(0x1D, color3), pal_col(0x1E, color1), pal_col(0x1F, color2); break;
  case 0x01: discorefreshrate = 0x3F; break;
  case 0x02: discorefreshrate = 0x1F; break;
  case 0x04: discorefreshrate = 0x0F; break;
  case 0x08: discorefreshrate = 0x07; break;
  case 0x10: discorefreshrate = 0x03; break;
  }
}

void game_reset(void) { reset_level(); }

static const uint8_t BG_Table[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C};
static const uint8_t G_Table[] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C};

static void check_for_cube_data_2_set(void) {
  if ((controllingplayer->press & (PAD_A | PAD_UP)) && currplayer_vel_y != 0 && (gamemode != GAMEMODE_UFO && gamemode != GAMEMODE_SHIP))
    cube_data[currplayer] |= 0x02;
}

static void store_player(int i) {
  player_relx[i] = currplayer_relx, player_rely[i] = currplayer_rely;
  player_vel_x[i] = currplayer_vel_x, player_vel_y[i] = currplayer_vel_y;
  player_gravity[i] = currplayer_gravity, player_mini[i] = currplayer_mini;
  slope_frames[i] = currplayer_slope_frames, was_on_slope_counter[i] = currplayer_was_on_slope_counter;
  slope_type[i] = currplayer_slope_type, last_slope_type[i] = currplayer_last_slope_type;
}
static void load_player(int i) {
  currplayer_relx = player_relx[i], currplayer_rely = player_rely[i];
  currplayer_vel_x = player_vel_x[i], currplayer_vel_y = player_vel_y[i];
  currplayer_gravity = player_gravity[i], currplayer_mini = player_mini[i];
  currplayer_slope_frames = slope_frames[i], currplayer_was_on_slope_counter = was_on_slope_counter[i];
  currplayer_slope_type = slope_type[i], currplayer_last_slope_type = last_slope_type[i];
  update_currplayer_table_idx();
}

static void everything_else(void) {
  orbhitonthisframe[0] = orbhitonthisframe[1] = 0;
  if (!twoplayer) joypad2 = joypad1;
  if (nullscapes_active && !(kandoframecnt & 7)) nullscapes_orb_type++;
  if (nullscapes_active == 1 && nullscapes_orb_type >= 4) nullscapes_orb_type = 0;
  else if (nullscapes_active == 2 && nullscapes_orb_type >= 6) nullscapes_orb_type = 0;
  else if (!nullscapes_active && nullscapes_orb_type >= 1) nullscapes_orb_type = 0;
  if (options & platformer || force_platformer) twoplayer = 0;
  if (twoplayer) dual = 1;
  if (!(joypad1.hold & (PAD_A | PAD_UP))) {
    if (dashing[0]) currplayer_vel_y = 0;
    dashing[0] = 0;
  }
  if (practice_point_count > 1 && (joypad1.press & PAD_SELECT) && !(joypad1.hold & (PAD_UP | PAD_DOWN))) {
    curr_practice_point--;
    if (latest_practice_point) latest_practice_point--;
    if (curr_practice_point >= practice_point_count) curr_practice_point = practice_point_count - 1;
  }
  if ((controllingplayer->press & PAD_B) && practice_point_count) game_store_practice();
  if (auto_practicepoints && auto_practicepoint_timer) auto_practicepoint_timer--;
  if (practicebuffer) practicebuffer--;
  if (practice_point_count && !auto_practicepoint_timer) {
    if (gamemode == 0 || gamemode == 2 || gamemode == 4 || gamemode == 5 || gamemode == 8) {
      if (currplayer_vel_y == 0) game_store_practice();
      practicebuffer = 4;
    } else game_store_practice();
  }
  decrement_was_on_slope();
  check_for_cube_data_2_set();
  if (orbed[currplayer] && !(controllingplayer->hold & (PAD_A | PAD_UP))) orbed[currplayer] = 0;
  sprite_collide();
  if (retro_mode && gamemode == GAMEMODE_CUBE) gamemode = GAMEMODE_ROBOT;
  movement();
  processXMovement = 1;
  runthecolls();
  processXMovement = 0;
  dblocked[currplayer] = 0;
  if (invincible_counter) invincible_counter--;
  do_the_scroll_thing();
  check_spr_objects();
  store_player(0);
  if (dual) {
    currplayer = 1;
    if (!(joypad2.hold & (PAD_A | PAD_UP))) {
      if (dashing[1]) currplayer_vel_y = currplayer_gravity ? -0x0100 : 0x0100;
      dashing[1] = 0;
    }
    controllingplayer = &joypad2;
    load_player(1);
    check_for_cube_data_2_set();
    decrement_was_on_slope();
    if (orbed[currplayer] && !(controllingplayer->hold & (PAD_A | PAD_UP))) orbed[currplayer] = 0;
    sprite_collide();
    movement();
    if (dual && ((options & platformer) || force_platformer) && !twoplayer) currplayer_relx = player_relx[0], currplayer_vel_x = player_vel_x[0];
    else if (dual && !(options & platformer) && !force_platformer) currplayer_relx = player_relx[0], currplayer_vel_x = player_vel_x[0];
    processXMovement = 0;
    runthecolls();
    processXMovement = 1;
    dblocked[currplayer] = 0;
    currplayer = 0;
    controllingplayer = &joypad1;
    store_player(1);
    load_player(0);
  }
}

/* one NES frame of state_game's loop, without the drawing (draw_screen's timing is kept) */
void game_step(void) {
  newrand();
  if (kandokidshack2 && !(kandoframecnt & 0x0F)) icon = icon == MAX_ICONS - 1 ? 0 : icon + 1;
  pal_col(3, outline_color), pal_col(7, outline_color);
  if (((forced_trails == 2) || trails == 2 || !(kandoframecnt & 0x1)) && !(kandoframecnt & 1)) {
    tmp6 = (uint16_t)(old_trail_scroll_y - scroll_y);
    for (int k = 7; k >= 0; k--) {
      uint16_t t = (uint16_t)(player_old_rely[k] + tmp6);
      if (high_byte(t) != 0) low_byte(t) = 0;
      player_old_rely[k + 1] = (uint8_t)t;
    }
    player_old_rely[0] = high_byte(player_rely[0]);
    old_trail_scroll_y = scroll_y;
  }
  if (discomode && !(kandoframecnt & discorefreshrate)) {
    uint8_t c = (uint8_t)((G_Table[discoframe] + 0x80) & 0x3F);
    pal_col(0, c), pal_col(1, oneShadeDarker(c)), pal_col(9, oneShadeDarker(c));
    c = (uint8_t)((0xC0 + BG_Table[discoframe]) & 0x3F);
    pal_col(6, c), pal_col(5, oneShadeDarker(c)), pal_col(0x0D, oneShadeDarker(c));
    discoframe++;
    if (discoframe == 12) discoframe = 0;
  }
  kandoframecnt++;
  if (slowmode && kandokidshack4 == 15) {
    if (kandoframecnt & 3) check_for_cube_data_2_set();
    else everything_else();
  } else if ((slowmode || kandokidshack4 == 15) && !(kandoframecnt & 1)) {
    check_for_cube_data_2_set();
  } else everything_else();
  draw_screen();
  if (level_resetting_flag) level_resetting_flag--;
  timewarp_done = 0;
  if (!(high_byte(player_relx[0]) > 0x20)) cube_data[0] = cube_data[1] = 0;
}

bool game_platformer(void) { return force_platformer || (options & platformer); }
bool game_dead(void) { return high_byte(player_relx[0]) > 0x20 && (cube_data[0] & 1 || cube_data[1] & 1); }

/* ---------------------------------------------------------------- sprites: draw_sprites and the player */
static uint8_t death_player;
static const uint8_t drawcube_rounding_table[13] = {0, 0xFF, 0xFE, 3, 2, 1, 0, 0xFF, 0xFE, 3, 2, 1, (uint8_t)-24};
static const uint8_t rounding_slope_table[15] = {0x09, 0x08, 0x09, 0x00, 0x03, 0x04, 0x08, 0x00,
                                                 0x09, 0x16, 0x1a, 0x00, 0x03, 0x1a, 0x17};
#define NF 0x00
#define HF 0x40
#define VF 0x80
#define HV 0xC0
static const uint8_t drawcube_sprite_table[24] = {NF | 0, NF | 1, NF | 2, NF | 3, NF | 4, NF | 5, NF | 6, VF | 5, VF | 4, VF | 3, VF | 2, VF | 1,
                                                 HV | 0, HV | 1, HV | 2, HV | 3, HV | 4, HV | 5, HV | 6, HF | 5, HF | 4, HF | 3, HF | 2, HF | 1};
static const uint8_t drawcube_sprite_way[24] = {NF | 0, NF | 1, NF | 2, NF | 3, NF | 4, NF | 5, NF | 6, HF | 5, HF | 4, HF | 3, HF | 2, HF | 1,
                                               HF | 0, HV | 1, HV | 2, HV | 3, HV | 4, HV | 5, VF | 6, VF | 5, VF | 4, VF | 3, VF | 2, VF | 1};
static const uint8_t drawcube_sprite_none[24] = {0, 1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5};
static const uint8_t drawplayer_center_offsets[24] = {8, 8, 8, 8, 4, 4, 8, 8, 8, 8, 8, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

/* the animation tables, laid out as cc65 put them (some indices run into the table that follows) */
typedef struct { const unsigned char *const *t; uint8_t n; } anim_t;
#define A(x) {x, sizeof x / sizeof x[0]}
static anim_t mini_robot_cat(const unsigned char *const *a, uint8_t na, const unsigned char *const *b, uint8_t nb,
                             const unsigned char **buf) {
  for (int i = 0; i < na; i++) buf[i] = a[i];
  for (int i = 0; i < nb; i++) buf[na + i] = b[i];
  return (anim_t){(const unsigned char *const *)buf, (uint8_t)(na + nb)};
}
static const unsigned char *mr_buf[3][25];
static anim_t P1_tables[48], P2_tables[48];
static void build_tables(void) {
  static bool done;
  if (done) return;
  done = true;
  anim_t mr = mini_robot_cat(MINI_ROBOT, 20, MINI_ROBOT_JUMP, 5, mr_buf[0]);
  anim_t mra = mini_robot_cat(MINI_ROBOT_ALT, 20, MINI_ROBOT_JUMP_ALT, 5, mr_buf[1]);
  anim_t mr2 = mini_robot_cat(MINI_ROBOT2, 20, MINI_ROBOT_JUMP2, 5, mr_buf[2]);
  anim_t p1[48] = {A(CUBE), A(SHIP), A(BALL), A(UFO), A(ROBOT), A(SPIDER), A(WAVE), A(SWING), A(CUBE), A(POGO), A(SNAKE), A(CUBE),
                   A(MINI_CUBE), A(MINI_SHIP), A(MINI_BALL), A(MINI_UFO), mr, A(MINI_SPIDER), A(MINI_WAVE), A(MINI_SWING), A(MINI_CUBE), A(MINI_POGO), A(MINI_SNAKE), A(MINI_CUBE),
                   A(CUBE), A(SHIP), A(BALL), A(UFO), A(ROBOT_ALT), A(SPIDER_ALT), A(WAVE), A(SWING), A(ROBOT_ALT), A(POGO), A(SNAKE), A(CUBE),
                   A(MINI_CUBE), A(MINI_SHIP), A(MINI_BALL_ALT), A(MINI_UFO), mra, A(MINI_SPIDER_ALT), A(MINI_WAVE), A(MINI_SWING_ALT), mra, A(MINI_POGO), A(MINI_SNAKE), A(MINI_CUBE)};
  anim_t p2[48] = {A(CUBE2), A(SHIP2), A(BALL2), A(UFO2), A(ROBOT2), A(SPIDER2), A(WAVE2), A(SWING2), A(CUBE2), A(POGO2), A(SNAKE2), A(CUBE2),
                   A(MINI_CUBE2), A(MINI_SHIP2), A(MINI_BALL2), A(MINI_UFO2), mr2, A(MINI_SPIDER2), A(MINI_WAVE2), A(MINI_SWING2), A(MINI_CUBE2), A(MINI_POGO2), A(MINI_SNAKE2), A(MINI_CUBE2),
                   A(CUBE2), A(SHIP2), A(BALL2), A(UFO2), A(ROBOT_ALT2), A(SPIDER_ALT2), A(WAVE2), A(SWING2), A(CUBE2), A(POGO2), A(SNAKE2), A(CUBE2),
                   A(MINI_CUBE2), A(MINI_SHIP2), A(MINI_BALL_ALT), A(MINI_UFO2), mra, A(MINI_SPIDER_ALT), A(MINI_WAVE2), A(MINI_SWING_ALT), A(MINI_CUBE2), A(MINI_POGO2), A(MINI_SNAKE2), A(MINI_CUBE2)};
  memcpy(P1_tables, p1, sizeof p1);
  memcpy(P2_tables, p2, sizeof p2);
}

typedef struct { uint8_t flip_gm, anim; int16_t x, y; bool p2; } pspr_t;
static pspr_t playerSpriteFirst, playerSpriteSecond;
uint8_t titleicon, title_mode_on;

static uint8_t ship_wave_rot(int p, bool wave) {
  int16_t v = player_vel_y[p];
  uint16_t r = (uint16_t)(0x0400 - (uint16_t)v);
  uint8_t a = high_byte(r);
  if (a >= 0x08) r = a < 0x80 ? 0x07FF : 0x0000;
  cube_rotate[p] = r;
  a = high_byte(r);
  if (wave && high_byte(player_vel_x[0]) >= 0x04 && !player_mini[p]) a = (uint8_t)(8 - a);
  if (player_gravity[p]) a = (uint8_t)(8 - a);
  return a;
}

static pspr_t process_player_sprite(int p) {
  uint8_t flip, gm, y = 0;
  if (slope_frames[p] | slope_type[p]) cube_data[p] |= 0x80;
  else cube_data[p] &= 0x7F;
  flip = player_gravity[p] ? 0x80 : 0;
  int16_t sy = (int16_t)(high_byte(player_rely[p]) - 1);
  uint8_t base = (uint8_t)((player_mini[p] ? 12 : 0) + gamemode);
  gm = (uint8_t)(base + (retro_mode ? 24 : 0));
  uint8_t tx = high_byte(player_relx[0]);
  if ((uint8_t)(tx - 1) >= 0xFC) tx = 1;
  int16_t sx = (int16_t)(uint8_t)(tx + drawplayer_center_offsets[base]);
  int mode = gamemode;
  if (mode == GAMEMODE_NINJA) mode = retro_mode ? GAMEMODE_ROBOT : GAMEMODE_CUBE;
  if (mode == GAMEMODE_SWING) mode = GAMEMODE_SHIP;
  if (mode == GAMEMODE_SNAKE) mode = GAMEMODE_WAVE;
  switch (mode) {
  case GAMEMODE_SHIP: y = ship_wave_rot(p, false); break;
  case GAMEMODE_WAVE: y = ship_wave_rot(p, true); break;
  case GAMEMODE_BALL:
    if (p == 0) {
      if (((options & platformer) || force_platformer) && !low_byte(player_vel_x[0])) y = 0;
      else y = ballframe, ballframe = (ballframe + 1) & 7;
    } else {
      if ((options & platformer) && !low_byte(player_vel_x[1])) y = 0;
      else y = ballframe;
    }
    break;
  case GAMEMODE_POGO:
    y = 0;
    if (robotjumpframe[p]) y = 1, robotjumpframe[p]--;
    break;
  case GAMEMODE_UFO: {
    int16_t v = player_vel_y[p];
    y = 1;
    if (high_byte(v)) { if (high_byte(v) & 0x80) y = 2; }
    else if (!low_byte(v)) y = 0;
    break;
  }
  case GAMEMODE_ROBOT:
    if (was_on_slope_counter[p] || player_vel_y[p] == 0) {
      if (p == 0 && retro_mode) {
        y = robotframe[0];
        robotframe[0] = (uint8_t)(robotframe[0] + 1 >= 15 ? 0 : robotframe[0] + 1);
      } else if ((options & platformer) && !low_byte(player_vel_x[p])) {
        robotframe[0] = 0, y = 0;
      } else {
        y = robotframe[0];
        robotframe[0] = (uint8_t)(robotframe[0] + 1 >= 20 ? 0 : robotframe[0] + 1);
      }
    } else y = (uint8_t)(21 + robotjumpframe[0]);
    break;
  case GAMEMODE_SPIDER:
    if (player_vel_y[p] == 0) {
      if ((options & platformer) && !low_byte(player_vel_x[0])) spiderframe[0] = 0, y = 0;
      else y = spiderframe[p], spiderframe[p] = (spiderframe[p] + 1) & 0x0F;
    } else y = (uint8_t)(16 + robotjumpframe[0]);
    break;
  default: { /* cube and football */
    bool round;
    if (gamemode == GAMEMODE_FOOTBALL && player_vel_y[p] == 0) {
      uint8_t x;
      if (orbed[p]) x = 6;
      else if (!chargepower[p]) {
        if (p == 1) goto spin;
        x = 0;
      } else {
        uint8_t c = chargepower[p];
        x = c < 10 ? 23 : c < 20 ? 22 : c < 30 ? 21 : c < 50 ? 20 : 6;
      }
      high_byte(cube_rotate[p]) = x;
      y = x;
      goto pick;
    }
    round = (cube_data[p] & 0x80) || player_vel_y[p] == 0;
    if (round) {
      low_byte(cube_rotate[p]) = 0;
      uint8_t r = high_byte(cube_rotate[p]), xi = r;
      if (r >= 12) xi = (uint8_t)(r - 12);
      r = (uint8_t)(r + drawcube_rounding_table[xi]);
      high_byte(cube_rotate[p]) = r;
      if (cube_data[p] & 0x80) r = (uint8_t)(r + rounding_slope_table[(slope_type[p] - 1) & 15]);
      y = r;
    } else {
    spin:;
      uint16_t g = CUBE_GRAVITY[0] & 0xFF;
      if (!player_gravity[p]) {
        uint16_t s = (uint16_t)(low_byte(cube_rotate[p]) + g);
        low_byte(cube_rotate[p]) = (uint8_t)s;
        if (s > 0xFF) {
          uint8_t h = (uint8_t)(high_byte(cube_rotate[p]) + 1);
          if (h >= 24) h = 0;
          high_byte(cube_rotate[p]) = h;
        }
      } else {
        uint8_t lo = low_byte(cube_rotate[p]);
        low_byte(cube_rotate[p]) = (uint8_t)(lo - g);
        if (lo < g) {
          int8_t h = (int8_t)(high_byte(cube_rotate[p]) - 1);
          if (h < 0) h = 23;
          high_byte(cube_rotate[p]) = (uint8_t)h;
        }
      }
      y = high_byte(cube_rotate[p]);
    }
  pick:;
    uint8_t ic = (p == 0 && gameState == STATE_MENU) ? titleicon : icon, e;
    if (y >= 24) y = 23;
    if (ic == 0x12 || ic == 0x16 || ic == 0x0F) e = drawcube_sprite_none[y];
    else if (ic == 2) e = drawcube_sprite_way[y];
    else e = drawcube_sprite_table[y];
    flip = e & 0xC0;
    y = e & 0x07;
    break;
  }
  }
  /* fin: the ninja's resting rotation */
  if (gamemode == GAMEMODE_NINJA && player_vel_y[p] == 0) cube_rotate[p] = player_gravity[p] ? 0x0C00 : 0;
  return (pspr_t){(uint8_t)(gm | flip), y, sx, sy, p == 1};
}

static void render_player_sprite(const pspr_t *s, int dx, int dy) {
  uint8_t flip = s->flip_gm & 0xC0, g = s->flip_gm & 0x3F;
  if (g >= 48) g = 47;
  build_tables();
  const anim_t *t = s->p2 ? &P2_tables[g] : &P1_tables[g];
  uint8_t i = s->anim < t->n ? s->anim : (uint8_t)(t->n - 1);
  if (gameState == STATE_MENU) flip |= 0x20;
  oam_meta_spr_flipped(flip, s->x + dx, s->y + dy, t->t[i]);
}

static void trail_loop(void) {
  if (kandoframecnt & 1) {
    uint16_t t6 = (uint16_t)(currplayer_vel_x << 1), t5 = (uint16_t)(currplayer_relx - t6);
    for (int k = 1; k < 8; k++) {
      if (trail_sprites_visible[k]) oam_spr(high_byte(t5) + 0, player_old_rely[k] - 4, 0xF9, 3);
      t5 = (uint16_t)(t5 - t6);
    }
  } else {
    uint16_t t6 = (uint16_t)(currplayer_vel_x << 1);
    uint16_t t5 = (uint16_t)((currplayer_vel_x << 1) + currplayer_relx - (uint16_t)(currplayer_vel_x << 4));
    for (int k = 7; k > 0; k--) {
      if (trail_sprites_visible[k]) oam_spr(high_byte(t5) + 0, player_old_rely[k] - 4, 0xF9, 3);
      t5 = (uint16_t)(t5 + t6);
    }
  }
}

/* objects for the wider screen: the ones the NES keeps (its 256 columns) come from its slots, and the
 * ones just outside, in the 32 columns at each side, straight from the level's list */
static bool draws_as_margin(uint8_t t) {
  if (t >= 0x80) return false; /* triggers and colours have no picture */
  if (sprite_heights[t] == SPBH && !(t == O_COIN1 || t == O_COIN2 || t == O_COIN3 || (t >= 0x1C && t <= 0x1E))) return false;
  if (sprite_heights[t] == DECO && (twoplayer || !viseffects)) return false;
  return true;
}

static const unsigned char *frame_of(uint8_t t, uint8_t frame) {
  const struct SpriteFrame *a = (const struct SpriteFrame *)animation_frame_list[t & 0x7F];
  if (a) {
    if (frame >= animation_frame_length[t]) frame = 0;
    return a[frame].ptr;
  }
  return Metasprites[t & 0x7F];
}

static void draw_one(int x, int y, const unsigned char *d) {
  if (!disco_sprites) oam_meta_spr(x, y, d);
  else oam_meta_spr_disco(x, y, d);
}

static void draw_margin_objects(void) {
  const uint8_t *p = level_list[level].spr;
  int32_t sx = (int32_t)scroll_x;
  for (; p[0] != 0xFF; p += 5) {
    int32_t x = p[0] | p[1] << 8;
    if (x < sx - 48) continue;
    if (x >= sx + 256 + 32) break;
    bool left = x < sx, right = x >= sx + 256 - 15;
    if (!left && !right) continue;
    uint8_t t = p[4];
    if (!draws_as_margin(t)) continue;
    /* is it in a slot? then the slot draws it (when it is on the NES's screen) */
    bool slot = false;
    for (int i = 0; i < max_loaded_sprites; i++)
      if (activesprites_type[i] != 0xFF && activesprites_x[i] == (uint16_t)x && activesprites_y[i] == (uint16_t)(p[2] | p[3] << 8) &&
          activesprites_active[i]) slot = true;
    if (slot && !left) continue;
    if (left && slot) continue;
    if (t == O_COIN1 && (coins & COIN_1)) continue;
    if (t == O_COIN2 && (coins & COIN_2)) continue;
    if (t == O_COIN3 && (coins & COIN_3)) continue;
    if (t == O_COIN1 && (!invisblocks ? prog.coin1[level] : prog.icoin1[level])) t = O_COINGOTTEN1;
    if (t == O_COIN2 && (!invisblocks ? prog.coin2[level] : prog.icoin2[level])) t = O_COINGOTTEN2;
    if (t == O_COIN3 && (!invisblocks ? prog.coin3[level] : prog.icoin3[level])) t = O_COINGOTTEN3;
    if (t == O_COIN3 && level == 20 && minicoins != 10) continue;
    int y = (int)((p[2] | p[3] << 8) - (int32_t)scroll_y);
    if (y < -48 || y > 260) continue;
    int fx = (int)(x - sx);
    const struct SpriteFrame *a = (const struct SpriteFrame *)animation_frame_list[t & 0x7F];
    uint8_t fr = 0;
    if (a && animation_frame_length[t]) fr = (uint8_t)((kandoframecnt >> 2) % animation_frame_length[t]);
    draw_one(fx, y - 1, frame_of(t, fr));
  }
}

void game_draw(void) {
  oam_clear();
  build_tables();
  /* the columns this wider screen shows past the NES's right edge (the decoder runs ahead; the
   * collision map still takes them in at the NES's pace) */
  if (!(scroll_x & 0x80000000u)) {
    int32_t want = (int32_t)((scroll_x + NES_X0 + 256 + 15) >> 4) + 1;
    if (want > coll_cols + 15) want = coll_cols + 15;
    decode_to(want);
  }
  if (!(invisible | player_invis)) {
    playerSpriteFirst = process_player_sprite(0);
    if (dual) {
      playerSpriteSecond = process_player_sprite(1);
      if (kandoframecnt & 1) render_player_sprite(&playerSpriteSecond, 0, 0), render_player_sprite(&playerSpriteFirst, 0, 0);
      else render_player_sprite(&playerSpriteFirst, 0, 0), render_player_sprite(&playerSpriteSecond, 0, 0);
    } else render_player_sprite(&playerSpriteFirst, 0, 0);
  }
  if (invisblocks) return;
  if (practice_point_count) {
    uint8_t py = (uint8_t)(high_byte(practice[curr_practice_point].p1y) - 1);
    if (practice_sprite_x_pos > 10) {
      practice_sprite_x_pos -= 3;
      oam_meta_spr(practice_sprite_x_pos, py, Practice_Sprites[0]);
    }
  }
  shuffle_offset = (shuffle_offset + 11) & 0x0F;
  for (int count = 0; count < max_loaded_sprites; count++) {
    shuffle_offset = (shuffle_offset + 9) & 0x0F;
    spr_index = shuffle_offset;
    if ((int8_t)activesprites_type[spr_index] < 0) continue;
    if (!activesprites_active[spr_index]) continue;
    temp_x = activesprites_realx[spr_index];
    if (temp_x > 0xf0) continue;
    if (temp_x == 0) temp_x = 1;
    uint8_t ty = activesprites_realy[spr_index];
    if (ty > 0xf0) continue;
    if (nullscapes_active) {
      uint8_t t = activesprites_type[spr_index];
      if (t == O_YELLOW_ORB || t == O_RED_ORB || t == O_BLACK_ORB || t == O_BLUE_ORB_MULTI || t == O_PINK_ORB || t == O_GREEN_ORB ||
          t == O_GREEN_ORB_MULTI || t == O_BLUE_ORB) {
        static const uint8_t null_types[6] = {O_RED_ORB, O_BLUE_ORB_MULTI, O_YELLOW_ORB, O_BLACK_ORB, O_GREEN_ORB_MULTI, O_PINK_ORB};
        if (nullscapes_orb_type < 6) activesprites_type[spr_index] = null_types[nullscapes_orb_type];
        activesprites_activated[spr_index] = 0;
      }
    }
    uint8_t spr_type = activesprites_type[spr_index];
    const struct SpriteFrame *a = (const struct SpriteFrame *)animation_frame_list[spr_type & 0x7F];
    const unsigned char *d;
    if (a) {
      uint8_t frame;
      bool reload = false;
      int8_t cnt = --activesprites_anim_frame_count[spr_index];
      if (cnt < 0) {
        if ((spr_type == O_SKULL_ORB && activesprites_animated[spr_index] == 1) || spr_type != O_SKULL_ORB)
          frame = ++activesprites_anim_frame[spr_index];
        else if (spr_type == O_SKULL_ORB && activesprites_animated[spr_index] == 2) {
          if (activesprites_anim_frame[spr_index]) frame = --activesprites_anim_frame[spr_index];
          else frame = activesprites_anim_frame[spr_index];
        } else frame = activesprites_anim_frame[spr_index];
        if (frame >= animation_frame_length[spr_type]) {
          if (spr_type != O_SKULL_ORB) activesprites_anim_frame[spr_index] = 0, frame = 0;
          else activesprites_anim_frame[spr_index]--, frame--;
        }
        reload = true;
      } else frame = activesprites_anim_frame[spr_index];
      if (frame >= animation_frame_length[spr_type]) frame = 0;
      d = a[frame].ptr;
      if (reload) activesprites_anim_frame_count[spr_index] = (int8_t)a[frame].frame_count;
    } else d = Metasprites[spr_type & 0x7F];
    draw_one(temp_x, ty, d);
  }
  draw_margin_objects();
  if (!dual) {
    if (kandoframecnt & 1) {
      for (int k = 8; k > 0; k--) trail_sprites_visible[k] = trail_sprites_visible[k - 1];
      trail_sprites_visible[0] = (trails == 1 || orbactive);
    }
    if (viseffects) {
      if ((trails == 1) || (trails != 2 && forced_trails != 2 && !invisible)) trail_loop();
      else if ((forced_trails == 2 || trails == 2) && !(kandoframecnt & 1)) {
        int8_t step = (int8_t)(high_byte(currplayer_vel_x) << 1 | ((high_byte(currplayer_vel_x) & 0x80) ? 0x80 : 0));
        pspr_t s = playerSpriteFirst;
        s.x = (int16_t)(uint8_t)(s.x - step);
        s.y = (int16_t)(uint8_t)(s.y + (uint8_t)(player_old_rely[0] - high_byte(player_rely[0])));
        render_player_sprite(&s, 0, 0);
        s.x = (int16_t)(uint8_t)(s.x - step);
        s.y = (int16_t)(uint8_t)(s.y + (uint8_t)(player_old_rely[1] - player_old_rely[0]));
        render_player_sprite(&s, 0, 0);
        s.x = (int16_t)(uint8_t)(s.x - step);
        s.y = (int16_t)(uint8_t)(s.y + (uint8_t)(player_old_rely[2] - player_old_rely[1]));
        if (gamemode == GAMEMODE_CUBE && !currplayer_mini) s.flip_gm = (uint8_t)(s.flip_gm + 12);
        render_player_sprite(&s, 0, 0);
      }
    }
  }
}

/* death_animation's frames: the screen stays as it was, the player's first sprites go and the explosion
 * takes their place (oam_clear_player) */
static spr_t oam_last[MAX_SPR];
static int oam_last_n;
void game_death_frame(int f) {
  if (f == 0) {
    death_player = (cube_data[0] & 1) ? 0 : (cube_data[1] & 1) ? 1 : death_player;
    update_level_completeness();
    memcpy(oam_last, oam, sizeof oam_last), oam_last_n = oam_n;
  }
  check_practice_point_deletion();
  memcpy(oam, oam_last, sizeof oam), oam_n = oam_last_n;
  int hide = dual ? 4 : 2;
  if (gamemode == 4 || gamemode == 5) hide += dual ? 2 : 1;
  for (int i = 0; i < hide && i < oam_n; i++) oam[i].y = 0xFF;
  memcpy(oam_last, oam, sizeof oam_last);
  if (robotjumpframe[0] < 20) {
    const unsigned char *const *tbl =
        (retro_mode && (gamemode == GAMEMODE_ROBOT || gamemode == GAMEMODE_NINJA || gamemode == GAMEMODE_UFO || gamemode == GAMEMODE_SHIP))
            ? ExplodeR_Sprites : Explode_Sprites;
    int n = oam_n;
    oam_n = 0;
    oam_meta_spr((uint8_t)(high_byte(player_relx[death_player]) - 2), (uint8_t)(high_byte(player_rely[death_player]) - 2),
                 tbl[robotjumpframe[0] & 0x7F]);
    if (oam_n < n) oam_n = n;
    ++robotjumpframe[0];
  }
}

/* the metasprites the menus use */
const unsigned char *fd_number_sprite(int i) { return Number_Sprites[i]; }
const unsigned char *fd_metasprite(int t) { return Metasprites[t & 0x7F]; }
pspr_t fd_title_player(void);
void fd_draw_title_player(uint8_t x, uint8_t y, uint8_t gm, uint8_t mini) {
  gamemode = gm, player_mini[0] = mini, player_relx[0] = (uint16_t)(x << 8), player_rely[0] = (uint16_t)(y << 8);
  player_gravity[0] = 0;
  pspr_t s = process_player_sprite(0);
  render_player_sprite(&s, 0, 0);
}
#ifdef HOST
/* for tests/play.c: the player as the physics see it */
void fd_test_set(uint8_t gm, uint8_t mini, uint8_t grav, int16_t vy) {
  gamemode = gm, currplayer_mini = mini, currplayer_gravity = grav, currplayer_vel_y = vy;
  update_currplayer_table_idx();
}
int16_t fd_test_vy(void) { return currplayer_vel_y; }
uint16_t fd_test_y(void) { return currplayer_rely; }
uint16_t fd_test_x(void) { return currplayer_relx; }
uint8_t fd_test_speed(void) { return speed; }
#endif
#ifdef FD_BOT
#pragma clang section bss = "" data = ""
#endif
