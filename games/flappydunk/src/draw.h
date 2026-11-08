/* Flappy Dunk's drawing (draw.c), for main.c's screens. */
#ifndef FD_DRAW_H
#define FD_DRAW_H
#include "fd.h"
#include "np.h"

extern uint8_t dr_ball, dr_wing, dr_hoop, dr_fever;
void dr_init(void);
void dr_world(bool ball);
float dr_sx(float x), dr_sy(float y);
void dr_dunky(float x, float y, float ang_deg, float wing, int a256);
void dr_blit(const fd_img_t *im, float sx, float sy, float m00, float m01, float m10, float m11, int a256, int tint,
             color_t tc);
void dr_image(const fd_img_t *im, int x, int y, float k, int a256);
void dr_image_c(const fd_img_t *im, int cx, int cy, float k, int a256);
void dr_image_dark(const fd_img_t *im, int cx, int cy, float k, color_t c);
/* align: 0 left, 1 centre, 2 right; fo: the outlined font drawn under it, or none */
void dr_text(const np_font_t *f, const np_font_t *fo, int x, int base, const char *s, color_t c, int a256, int align);
#endif
