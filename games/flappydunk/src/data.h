/* Made by tools/data.py from Flappy Dunk's own files (see its docstring). */
#ifndef FD_DATA_H
#define FD_DATA_H
#include <stdint.h>
#include "font.h"

typedef struct {
  uint8_t w, h;
  int16_t px, py; /* the pivot, in quarter pixels */
  const uint16_t *c; /* RGB565 */
  const uint8_t *a; /* alpha */
} fd_img_t;
typedef struct { float t, v, in, out; } fd_key_t;
typedef struct { int8_t type; float min, max; int16_t start, end, key, nkeys; } fd_param_t;
typedef struct {
  uint8_t difficulty, condition; /* 0 exactly, 1 at least */
  int8_t swishes; /* -1: no swish goal */
  float scroll, gravity, flap, finish;
  uint8_t narrow, pixel, mirror; /* mirror: 1 horizontal, 2 vertical, 3 both */
  uint16_t first, count;
} fd_challenge_t;
typedef struct { uint8_t type; float height, angle, width, speed, distance, bound; } fd_choop_t;

#define FD_NBALLS 26
#define FD_NWINGS 10
#define FD_NHOOPS 9
#define FD_NFEVERS 6
extern const fd_img_t fd_ball_0;
extern const fd_img_t fd_ball_1;
extern const fd_img_t fd_ball_2;
extern const fd_img_t fd_ball_3;
extern const fd_img_t fd_ball_4;
extern const fd_img_t fd_ball_5;
extern const fd_img_t fd_ball_6;
extern const fd_img_t fd_ball_7;
extern const fd_img_t fd_ball_8;
extern const fd_img_t fd_ball_9;
extern const fd_img_t fd_ball_10;
extern const fd_img_t fd_ball_11;
extern const fd_img_t fd_ball_12;
extern const fd_img_t fd_ball_13;
extern const fd_img_t fd_ball_14;
extern const fd_img_t fd_ball_15;
extern const fd_img_t fd_ball_16;
extern const fd_img_t fd_ball_17;
extern const fd_img_t fd_ball_18;
extern const fd_img_t fd_ball_19;
extern const fd_img_t fd_ball_20;
extern const fd_img_t fd_ball_21;
extern const fd_img_t fd_ball_22;
extern const fd_img_t fd_ball_23;
extern const fd_img_t fd_ball_24;
extern const fd_img_t fd_ball_25;
extern const fd_img_t fd_wing_0;
extern const fd_img_t fd_wing_1;
extern const fd_img_t fd_wing_2;
extern const fd_img_t fd_wing_3;
extern const fd_img_t fd_wing_4;
extern const fd_img_t fd_wing_5;
extern const fd_img_t fd_wing_6;
extern const fd_img_t fd_wing_7;
extern const fd_img_t fd_wing_8;
extern const fd_img_t fd_wing_9;
extern const fd_img_t fd_hoopb_0;
extern const fd_img_t fd_hoopf_0;
extern const fd_img_t fd_hoopstar_0;
extern const fd_img_t fd_hoopb_1;
extern const fd_img_t fd_hoopf_1;
extern const fd_img_t fd_hoopstar_1;
extern const fd_img_t fd_hoopb_2;
extern const fd_img_t fd_hoopf_2;
extern const fd_img_t fd_hoopstar_2;
extern const fd_img_t fd_hoopb_3;
extern const fd_img_t fd_hoopf_3;
extern const fd_img_t fd_hoopstar_3;
extern const fd_img_t fd_hoopb_4;
extern const fd_img_t fd_hoopf_4;
extern const fd_img_t fd_hoopstar_4;
extern const fd_img_t fd_hoopb_5;
extern const fd_img_t fd_hoopf_5;
extern const fd_img_t fd_hoopstar_5;
extern const fd_img_t fd_hoopb_6;
extern const fd_img_t fd_hoopf_6;
extern const fd_img_t fd_hoopstar_6;
extern const fd_img_t fd_hoopb_7;
extern const fd_img_t fd_hoopf_7;
extern const fd_img_t fd_hoopstar_7;
extern const fd_img_t fd_hoopb_8;
extern const fd_img_t fd_hoopf_8;
extern const fd_img_t fd_hoopstar_8;
extern const fd_img_t fd_icon_flames;
extern const fd_img_t fd_flames_big;
extern const fd_img_t fd_logo;
extern const fd_img_t fd_star;
extern const fd_img_t fd_star_small;
extern const fd_img_t fd_tile;
extern const fd_img_t fd_cloud;
extern const fd_img_t fd_icon_balls;
extern const fd_img_t fd_icon_hoops;
extern const fd_img_t fd_icon_wings;
extern const fd_img_t fd_icon_challenge;
extern const uint8_t *const fd_wall_runs;
extern const uint16_t *const fd_wall_rows;
extern const np_font_t fd_font_big;
extern const np_font_t fd_font_big_o;
extern const np_font_t fd_font_mid;
extern const np_font_t fd_font_mid_o;
extern const np_font_t fd_font_small;
extern const np_font_t fd_font_small_o;
enum { P_SCROLL, P_MOVE_SPEED, P_HEIGHT, P_WIDTH, P_MOVING, P_ANGLE, P_LATERAL, P_GRAVITY, P_FLAP, P_COUNT };
#define FD_WTF_N 121 /* 15 a second, looping */
extern const int16_t *const fd_wtf_angle, *const fd_wtf_x, *const fd_wtf_y; /* 64ths of a degree, 256ths of a unit */
extern const fd_img_t *const fd_balls[FD_NBALLS], *const fd_wings[FD_NWINGS];
extern const fd_img_t *const fd_hoop_back[FD_NHOOPS], *const fd_hoop_front[FD_NHOOPS], *const fd_hoop_star[FD_NHOOPS];
extern const uint32_t fd_fever_rgb[FD_NFEVERS];
extern const fd_param_t fd_params[P_COUNT];
extern const fd_key_t fd_param_keys[];
extern const fd_challenge_t fd_challenges[51];
extern const fd_choop_t fd_choops[];
extern const char *const fd_challenge_text[51];
#endif
