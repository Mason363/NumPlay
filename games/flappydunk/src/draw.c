/* Flappy Dunk's pictures: the brick wall, the hoops in their back and front halves with the ball between,
 * its wings and fire, the stars and blasts, all from the game's own sprites (data.c), turned and scaled by
 * one affine sampler. The screen is drawn in strips of 40 rows (gfx_y0..gfx_y1 of gfx_buf). */
#include <math.h>
#include "draw.h"
#include "gfx.h"

#define PX 12.0f   /* screen pixels a world unit */
#define BALL_SX 96 /* the ball's column */
#define MID_Y 120

uint8_t dr_ball, dr_wing, dr_hoop, dr_fever; /* the skins drawn */
static color_t wall_c[4];

static inline color_t blend(color_t d, color_t s, int a255) { return gfx_mix(d, s, (a255 + 4) >> 3); }

/* world to screen, with the challenge's mirrors */
float dr_sx(float x) {
  float s = BALL_SX + (x - G.cam) * PX;
  return G.hflip ? SCREEN_W - s : s;
}
float dr_sy(float y) {
  float s = MID_Y - y * PX;
  return G.vflip ? SCREEN_H - s : s;
}

/* an image drawn with its pivot at (sx, sy): screen offset = m x (image pixel - pivot); opacity 0..256;
   tint: draw the image's alpha in that colour instead of its own */
void dr_blit(const fd_img_t *im, float sx, float sy, float m00, float m01, float m10, float m11, int a256, int tint,
             color_t tc) {
  float det = m00 * m11 - m01 * m10;
  if (det > -1e-5f && det < 1e-5f) return;
  float i00 = m11 / det, i01 = -m01 / det, i10 = -m10 / det, i11 = m00 / det;
  float pu = (float)im->px * 0.25f, pv = (float)im->py * 0.25f;
  float cu[4] = {-pu, (float)im->w - pu, -pu, (float)im->w - pu}, cv[4] = {-pv, -pv, (float)im->h - pv, (float)im->h - pv};
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int k = 0; k < 4; k++) {
    float x = sx + m00 * cu[k] + m01 * cv[k], y = sy + m10 * cu[k] + m11 * cv[k];
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  int X0 = (int)floorf(x0), X1 = (int)ceilf(x1), Y0 = (int)floorf(y0), Y1 = (int)ceilf(y1);
  if (X0 < 0) X0 = 0;
  if (X1 > SCREEN_W) X1 = SCREEN_W;
  if (Y0 < gfx_y0) Y0 = gfx_y0;
  if (Y1 > gfx_y1) Y1 = gfx_y1;
  if (X0 >= X1 || Y0 >= Y1) return;
  bool ss = (det < 0 ? -det : det) < 0.7f; /* shrunk: four samples a pixel */
  int w = im->w, h = im->h;
  const uint16_t *C = im->c;
  const uint8_t *A = im->a;
  for (int y = Y0; y < Y1; y++) {
    color_t *row = gfx_buf + (y - gfx_y0) * SCREEN_W;
    float dy = (float)y + 0.5f - sy, dx = (float)X0 + 0.5f - sx;
    float u = pu + i00 * dx + i01 * dy, v = pv + i10 * dx + i11 * dy;
    for (int x = X0; x < X1; x++, u += i00, v += i10) {
      int a, r = 0, g = 0, b = 0;
      if (!ss) {
        int iu = (int)floorf(u), iv = (int)floorf(v);
        if ((unsigned)iu >= (unsigned)w || (unsigned)iv >= (unsigned)h) continue;
        int k = iv * w + iu;
        a = A[k];
        if (!a) continue;
        a = a * a256 >> 8;
        row[x] = blend(row[x], tint ? tc : C[k], a);
        continue;
      }
      a = 0;
      for (int s = 0; s < 4; s++) {
        float ou = (s & 1 ? 0.25f : -0.25f), ov = (s & 2 ? 0.25f : -0.25f);
        float su = u + i00 * ou + i01 * ov, sv = v + i10 * ou + i11 * ov;
        int iu = (int)floorf(su), iv = (int)floorf(sv);
        if ((unsigned)iu >= (unsigned)w || (unsigned)iv >= (unsigned)h) continue;
        int k = iv * w + iu, al = A[k];
        if (!al) continue;
        color_t c = C[k];
        a += al, r += (c >> 11) * al, g += (c >> 5 & 63) * al, b += (c & 31) * al;
      }
      if (!a) continue;
      color_t c = tint ? tc : (color_t)((r / a) << 11 | (g / a) << 5 | (b / a));
      row[x] = blend(row[x], c, (a >> 2) * a256 >> 8);
    }
  }
}

/* an image at its own size (times k), its top left at (x, y) */
void dr_image(const fd_img_t *im, int x, int y, float k, int a256) {
  dr_blit(im, (float)x + (float)im->px * 0.25f * k, (float)y + (float)im->py * 0.25f * k, k, 0, 0, k, a256, 0, 0);
}
void dr_image_c(const fd_img_t *im, int cx, int cy, float k, int a256) {
  dr_blit(im, (float)cx, (float)cy, k, 0, 0, k, a256, 0, 0);
}
void dr_image_dark(const fd_img_t *im, int cx, int cy, float k, color_t c) {
  dr_blit(im, (float)cx, (float)cy, k, 0, 0, k, 256, 1, c);
}

/* a world sprite: its pixels per world unit (x, y), turned by deg, its pivot at world (x, y) */
static void world_sprite(const fd_img_t *im, float x, float y, float deg, float ppux, float ppuy, int a256, int tint,
                         color_t tc) {
  float c = cosf(deg * 0.017453293f), s = sinf(deg * 0.017453293f);
  float fx = G.hflip ? -PX : PX, fy = G.vflip ? PX : -PX; /* world to screen */
  /* image pixel (u right, v down) to world: (u / ppux, -v / ppuy), turned */
  float a = 1 / ppux, d = -1 / ppuy;
  float w00 = c * a, w01 = -s * d, w10 = s * a, w11 = c * d;
  dr_blit(im, dr_sx(x), dr_sy(y), fx * w00, fx * w01, fy * w10, fy * w11, a256, tint, tc);
}

/* ---------------------------------------------------------------- the scene's parts */
void dr_init(void) {
  /* the camera's #d4bb93 under background.png (white bricks outlined in grey) at 0.266, tinted (1, .98, .94) */
  for (int s = 0; s < 4; s++) {
    float v = 249 - (float)s * (249 - 115) / 3;
    int r = (int)(212 * 0.734f + v * 0.266f), g = (int)(187 * 0.734f + v * 0.98f * 0.266f),
        b = (int)(147 * 0.734f + v * 0.94f * 0.266f);
    wall_c[s] = RGB(r, g, b);
  }
}

/* the brick wall, fixed in the world: a 20-unit tile (240 px) */
static void wall(void) {
  int off = (int)floorf(G.cam * PX) - BALL_SX;
  for (int y = gfx_y0; y < gfx_y1; y++) {
    color_t *row = gfx_buf + (y - gfx_y0) * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) row[x] = wall_c[0];
    int ty = (G.vflip ? SCREEN_H - 1 - y : y) % 240;
    for (int i = fd_wall_rows[ty]; i < fd_wall_rows[ty + 1]; i++) {
      const uint8_t *r = fd_wall_runs + 3 * i;
      for (int base = -(((off % 240) + 240) % 240); base < SCREEN_W; base += 240)
        for (int k = 0; k < r[1]; k++) {
          int x = base + r[0] + k;
          if (x >= 0 && x < SCREEN_W) row[G.hflip ? SCREEN_W - 1 - x : x] = wall_c[r[2]];
        }
    }
  }
}

static int hoop_alpha(const hoop_t *h) {
  if (!h->passed) return 256;
  float t = 1 - h->t / 0.5f; /* FadeOut */
  return t <= 0 ? 0 : (int)(t * 256);
}

static void hoop_half(const hoop_t *h, bool front) {
  int a = hoop_alpha(h);
  if (!a) return;
  float sx = dr_sx(h->hx);
  if (sx < -40 || sx > SCREEN_W + 40) return;
  const fd_img_t *im = front ? fd_hoop_front[dr_hoop] : fd_hoop_back[dr_hoop];
  /* stored at 24 px across and 12 down a unit of its Net (x scale w) */
  world_sprite(im, h->hx, h->hy, h->angle, 24.0f / h->w, 12.0f, a, 0, 0);
}

/* a moving hoop's track (MovingHoop's line, 2 bound + 1 long, half see-through) */
static void track(const hoop_t *h) {
  if (h->type != H_VERTICAL && h->type != H_LATERAL) return;
  if (h->passed) return;
  float L = h->bound + 0.5f, dx = h->type == H_LATERAL ? h->c : -h->s, dy = h->type == H_LATERAL ? h->s : h->c;
  float x0 = dr_sx(h->x - dx * L), y0 = dr_sy(h->y - dy * L), x1 = dr_sx(h->x + dx * L), y1 = dr_sy(h->y + dy * L);
  float n = fabsf(x1 - x0) > fabsf(y1 - y0) ? fabsf(x1 - x0) : fabsf(y1 - y0);
  if ((y0 < gfx_y0 - 2 && y1 < gfx_y0 - 2) || (y0 > gfx_y1 + 2 && y1 > gfx_y1 + 2)) return;
  for (int i = 0; i <= (int)n; i++) {
    int x = (int)(x0 + (x1 - x0) * (float)i / (n + 0.01f)), y = (int)(y0 + (y1 - y0) * (float)i / (n + 0.01f));
    for (int o = 0; o < 2; o++) {
      int yy = y + o;
      if (yy >= gfx_y0 && yy < gfx_y1 && x >= 0 && x + 1 < SCREEN_W) {
        color_t *p = gfx_buf + (yy - gfx_y0) * SCREEN_W + x;
        p[0] = blend(p[0], 0xFFFF, 130), p[1] = blend(p[1], 0xFFFF, 130);
      }
    }
  }
}

/* the ball with its wings, turned with it (the back wing behind, the front one over it) */
void dr_dunky(float x, float y, float ang_deg, float wing, int a256) {
  float c = cosf(ang_deg * 0.017453293f), s = sinf(ang_deg * 0.017453293f);
  const float k = 80.0f / 3; /* pixels a world unit of the ball's and wings' pictures */
  /* the right (back) wing at (0.5, 0.5) x 0.6, the left (front) one at (-0.76, 0.5) x 0.6 */
  float bx = 0.3f, by = 0.3f, fx = -0.456f, fy = 0.3f;
  world_sprite(fd_wings[dr_wing], x + bx * c - by * s, y + bx * s + by * c, ang_deg + wing, k, k, a256, 0, 0);
  world_sprite(fd_balls[dr_ball], x, y, ang_deg, k, k, a256, 0, 0);
  world_sprite(fd_wings[dr_wing], x + fx * c - fy * s, y + fx * s + fy * c, ang_deg + wing, k, k, a256, 0, 0);
}

static void ring(float x, float y, float r, color_t col, int a256) {
  float cx = dr_sx(x), cy = dr_sy(y);
  int R = (int)r;
  if (cy + R + 2 < gfx_y0 || cy - R - 2 > gfx_y1) return;
  for (int yy = (int)cy - R - 2; yy <= (int)cy + R + 2; yy++) {
    if (yy < gfx_y0 || yy >= gfx_y1) continue;
    for (int xx = (int)cx - R - 2; xx <= (int)cx + R + 2; xx++) {
      if (xx < 0 || xx >= SCREEN_W) continue;
      float d = sqrtf((xx - cx) * (xx - cx) + (yy - cy) * (yy - cy)) - r;
      if (d < -2 || d > 1) continue;
      color_t *p = gfx_buf + (yy - gfx_y0) * SCREEN_W + xx;
      *p = blend(*p, col, a256 * 255 >> 8);
    }
  }
}

static void particles(bool behind) {
  color_t fire = HEX(fd_fever_rgb[dr_fever]);
  for (int i = 0; i < FD_NPART; i++) {
    const part_t *p = &G.p[i];
    if (p->life <= 0) continue;
    float t = p->life / p->max; /* 1 to 0 */
    bool back = p->kind == PT_SMOKE || p->kind == PT_FIRE;
    if (back != behind) continue;
    if (p->kind == PT_STAR)
      world_sprite(fd_hoop_star[dr_hoop], p->x, p->y, p->vang * (1 - t) * 40, 12, 12, (int)(t * 256), 0, 0);
    else if (p->kind == PT_SMOKE) {
      float sx = dr_sx(p->x), sy = dr_sy(p->y);
      if (sy > gfx_y0 - 8 && sy < gfx_y1 + 8) gfx_circle((int)sx, (int)sy, (int)(3 + (1 - t) * 4), HEX(0x8A8A8A), (int)(t * 150));
    } else if (p->kind == PT_FIRE) { /* the fire's trail: the Fever's colour round a light core, shrinking */
      float sx = dr_sx(p->x), sy = dr_sy(p->y), r = 1.5f + t * 6.5f;
      if (sy + r + 1 > gfx_y0 && sy - r - 1 < gfx_y1) {
        gfx_circle((int)sx, (int)sy, (int)r, fire, (int)(t * 220));
        if (t > 0.35f) gfx_circle((int)sx, (int)sy, (int)(r * 0.55f), gfx_mix(fire, HEX(0xFFF6B0), 20), (int)(t * 240));
      }
    } else if (p->kind == PT_RING || p->kind == PT_BIGRING) {
      float r = (1 - t) * (p->kind == PT_RING ? 14 : 26) + 4;
      ring(p->x, p->y, r, p->kind == PT_RING ? 0xFFFF : fire, (int)(t * 256));
    }
  }
}

static void finish_line(void) {
  if (G.challenge < 0) return;
  int x = (int)dr_sx(G.finish_x);
  if (x < -4 || x > SCREEN_W + 4) return;
  int top = (int)dr_sy(G.vflip ? -G.top : G.top), bot = (int)dr_sy(G.vflip ? G.top : -G.top);
  for (int y = gfx_y0; y < gfx_y1; y++) {
    if (y < top || y >= bot) continue;
    for (int k = -2; k < 2; k++) {
      int xx = x + k;
      if (xx < 0 || xx >= SCREEN_W) continue;
      bool dark = (((y - top) >> 2) + (k < 0)) & 1;
      gfx_buf[(y - gfx_y0) * SCREEN_W + xx] = dark ? HEX(0x2A2A2A) : HEX(0xF4F4F4);
    }
  }
}

/* the narrow challenges' floor and ceiling */
static void bounds(void) {
  if (G.top >= FD_TOP) return;
  int a = (int)dr_sy(G.top), b = (int)dr_sy(-G.top);
  if (a > b) { int t = a; a = b; b = t; }
  for (int y = gfx_y0; y < gfx_y1; y++)
    if (y < a || y >= b) {
      color_t *row = gfx_buf + (y - gfx_y0) * SCREEN_W;
      for (int x = 0; x < SCREEN_W; x++) row[x] = blend(row[x], HEX(0x6B5A44), 120);
    }
}

/* the picture of a challenge with pixel: blocks of 8 */
static void pixelate(void) {
  for (int y = gfx_y0; y < gfx_y1; y += 8)
    for (int x = 0; x < SCREEN_W; x += 8) {
      int cy = y + 4 < gfx_y1 ? y + 4 : y;
      color_t c = gfx_buf[(cy - gfx_y0) * SCREEN_W + x + 4];
      for (int j = y; j < y + 8 && j < gfx_y1; j++)
        for (int i = x; i < x + 8; i++) gfx_buf[(j - gfx_y0) * SCREEN_W + i] = c;
    }
}

/* the ball on fire: the game's flames behind it, against its flight */
static void flames(void) {
  if (G.fever != 2 || G.state > ST_PLAY) return;
  /* (flames.png: its base on the right, its tongues to the left) */
  float v = sqrtf(G.vx * G.vx + G.vy * G.vy) + 1e-4f, dx = G.vx / v, dy = G.vy / v;
  float a = atan2f(G.vy, G.vx) * 57.29578f;
  color_t fire = HEX(fd_fever_rgb[dr_fever]);
  float flick = 0.92f + 0.08f * (float)((int)(G.time * 30) & 1);
  world_sprite(&fd_flames_big, G.x - dx * 0.9f, G.y - dy * 0.9f, a, 15 / flick, 15 / flick, 235, 1, fire);
  world_sprite(&fd_flames_big, G.x - dx * 0.6f, G.y - dy * 0.6f, a, 24 / flick, 24 / flick, 235, 1,
               gfx_mix(fire, HEX(0xFFF6B0), 18));
}

void dr_world(bool ball) {
  wall();
  bounds();
  particles(true);
  flames();
  for (int i = 0; i < G.nh; i++) track(&G.h[i]);
  for (int i = 0; i < G.nh; i++) hoop_half(&G.h[i], false);
  if (ball) dr_dunky(G.x, G.y, G.ang * 57.29578f, G.wing, 256);
  for (int i = 0; i < G.nh; i++) hoop_half(&G.h[i], true);
  particles(false);
  finish_line();
  if (G.pixel) pixelate();
}

/* ---------------------------------------------------------------- text */
void dr_text(const np_font_t *f, const np_font_t *fo, int x, int base, const char *s, color_t c, int a256, int align) {
  int w = gfx_text_width(f, s);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  if (fo) gfx_text(fo, x, base, s, HEX(0x1A1A1A), a256);
  gfx_text(f, x, base, s, c, a256);
}
