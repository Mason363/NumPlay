/* Sprite animations as 2D Toolkit plays them (tk2dSpriteAnimator), and sprite frames placed in the world. */
#include <math.h>
#include "hk.h"

enum { WRAP_LOOP, WRAP_LOOP_SECTION, WRAP_ONCE, WRAP_PING_PONG, WRAP_RANDOM_FRAME, WRAP_RANDOM_LOOP, WRAP_SINGLE };

typedef struct {
  uint16_t first, n;
  float fps;
  uint8_t wrap, loop;
  uint16_t pad;
} ClipRec;

static const ClipRec *clip_rec(int clip) { return (const ClipRec *)(const void *)(section(SEC_CLIP) + 4) + clip; }
static const uint16_t *clip_frames(const ClipRec *c) {
  return (const uint16_t *)(const void *)(section(SEC_CLIP) + 4 + sizeof(ClipRec) * rd32(section(SEC_CLIP))) + c->first;
}

const SpriteRec *sprite_rec(int id) { return (const SpriteRec *)(const void *)section(SEC_SPR) + id; }

int sprite_collider(int sprite, const float **data, int *n) {
  if (sprite < 0) return SC_UNSET;
  unsigned c = sprite_rec(sprite)->col;
  if (c < 2) return (int)c;
  const uint8_t *s = section(SEC_SCOL), *e = s + rd32(s + 4 + 4 * (c - 2));
  *data = (const float *)(const void *)(e + 4);
  *n = e[1];
  return e[0] == 2 ? SC_BOX : SC_SHAPE;
}

float clip_duration(int clip) {
  const ClipRec *c = clip_rec(clip);
  return c->n / c->fps;
}
int clip_frames_count(int clip) { return clip_rec(clip)->n; }
int clip_frame_sprite(int clip, int frame) { return clip_frames(clip_rec(clip))[frame] & 0x7FFF; }

static uint32_t rng = 0x9E3779B9u;
static int rand_below(int n) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return (int)(rng % (uint32_t)n);
}

/* shows frame f (and its event, as WarpClipToLocalTime does) */
static void warp(Anim *a, const ClipRec *c, float t) {
  a->time = t;
  int f = (int)t % c->n;
  a->frame = (int16_t)f;
  a->sprite = (int16_t)(clip_frames(c)[f] & 0x7FFF);
  if (clip_frames(c)[f] & 0x8000) a->events |= ANIM_TRIGGER;
}

void anim_play_from(Anim *a, int clip, float start) {
  if (clip < 0) return;   /* (none: a clip the object does not have, as tk2dSpriteAnimator.Play of an empty name) */
  const ClipRec *c = clip_rec(clip);
  if (!c->n) {
    a->events |= ANIM_DONE, a->playing = false;
    return;
  }
  if (start == 0 && a->playing && a->clip == clip) return;   /* (already playing: goes on) */
  a->playing = true;
  a->clip = (int16_t)clip;
  a->fps = c->fps;
  if (c->wrap == WRAP_SINGLE) {
    warp(a, c, 0);
    a->playing = false;
  } else if (c->wrap == WRAP_RANDOM_FRAME || c->wrap == WRAP_RANDOM_LOOP) {
    warp(a, c, (float)rand_below(c->n));
    if (c->wrap == WRAP_RANDOM_FRAME) a->frame = -1, a->playing = false;
  } else {
    float t = start * a->fps;
    if (c->wrap == WRAP_ONCE && t >= a->fps * c->n) {
      warp(a, c, (float)(c->n - 1));
      a->playing = false;
    } else
      warp(a, c, t);
  }
}

void anim_play(Anim *a, int clip) { anim_play_from(a, clip, 0); }
void anim_play_from_frame(Anim *a, int clip, int frame) {
  if (clip >= 0) anim_play_from(a, clip, (frame + 0.001f) / clip_rec(clip)->fps);
}
bool anim_is_playing(const Anim *a, int clip) { return a->playing && a->clip == clip; }

/* the frames from prev (exclusive) to f (inclusive), their events */
static void events(Anim *a, const ClipRec *c, int prev, int f, int dir) {
  const uint16_t *fr = clip_frames(c);
  for (int i = prev + dir; dir > 0 ? i <= f : i >= f; i += dir)
    if (i >= 0 && i < c->n && (fr[i] & 0x8000)) a->events |= ANIM_TRIGGER;
}

static void set_frame(Anim *a, const ClipRec *c, int f) {
  a->frame = (int16_t)f;
  a->sprite = (int16_t)(clip_frames(c)[f] & 0x7FFF);
}

void anim_update(Anim *a, float dt) {
  if (!a->playing || a->paused) return;
  const ClipRec *c = clip_rec(a->clip);
  a->time += dt * a->fps;
  int prev = a->frame, n = c->n, t = (int)a->time;
  switch (c->wrap) {
    case WRAP_LOOP:
    case WRAP_RANDOM_LOOP: {
      int f = t % n;
      set_frame(a, c, f);
      if (f < prev) events(a, c, prev, n - 1, 1), events(a, c, -1, f, 1);
      else events(a, c, prev, f, 1);
      break;
    }
    case WRAP_LOOP_SECTION: {
      int ls = c->loop;
      if (t >= ls) {
        int f = ls + (t - ls) % (n - ls);
        set_frame(a, c, f);
        if (prev < ls) events(a, c, prev, ls - 1, 1), events(a, c, ls - 1, f, 1);
        else if (f < prev) events(a, c, prev, n - 1, 1), events(a, c, ls - 1, f, 1);
        else events(a, c, prev, f, 1);
      } else {
        set_frame(a, c, t);
        events(a, c, prev, t, 1);
      }
      break;
    }
    case WRAP_PING_PONG: {
      int f = n > 1 ? t % (2 * n - 2) : 0, dir = 1;
      if (f >= n) f = 2 * n - 2 - f, dir = -1;
      if (f < prev) dir = -1;
      set_frame(a, c, f);
      events(a, c, prev, f, dir);
      break;
    }
    default:   /* once */
      if (t >= n) {
        set_frame(a, c, n - 1);
        a->playing = false;
        events(a, c, prev, n - 1, 1);
        a->events |= ANIM_DONE;
      } else {
        set_frame(a, c, t);
        events(a, c, prev, t, 1);
      }
      break;
  }
}

/* ---------------------------------------------------------------- half floats */
uint16_t to_f16(float f) {
  uint32_t x;
  memcpy(&x, &f, 4);
  uint32_t s = x >> 16 & 0x8000, e = x >> 23 & 255, m = x & 0x7FFFFF;
  if (e == 255) return (uint16_t)(s | 0x7C00 | (m ? 0x200 : 0));
  int ne = (int)e - 127 + 15;
  if (ne >= 31) return (uint16_t)(s | 0x7C00);
  if (ne <= 0) {
    if (ne < -10) return (uint16_t)s;
    m |= 0x800000;
    uint32_t sh = (uint32_t)(14 - ne), hm = m >> sh;
    if ((m >> (sh - 1)) & 1) hm++;   /* (round half up) */
    return (uint16_t)(s | hm);
  }
  uint32_t h = s | (uint32_t)ne << 10 | m >> 13;
  if (m & 0x1000) h++;   /* round */
  return (uint16_t)h;
}

/* the sprite drawn at (x, y, z), scaled (sx < 0: mirrored), tinted */
void sprite_inst(int sprite, float x, float y, float z, float sx, float sy, uint8_t tint, Inst *out) {
  const SpriteRec *s = sprite_rec(sprite);
  out->ax = (int16_t)lrintf(x * 64 + sx * s->lx);
  out->ay = (int16_t)lrintf(y * 64 + sy * s->ty);
  out->z = (int16_t)lrintf(z * 128);
  out->tex = s->tex;
  out->tint = tint;
  out->flags = 0;
  out->a = to_f16(sx * f16(s->tu));
  out->b = to_f16(-sy * f16(s->tv));
  out->rot = 0;
  out->group = 0;
}

/* turned (degrees, counterclockwise) about its place */
void sprite_inst_rot(int sprite, float x, float y, float z, float sx, float sy, float degrees, uint8_t tint, Inst *out) {
  sprite_inst(sprite, x, y, z, sx, sy, tint, out);
  float r = degrees * (float)M_PI / 180, c = cosf(r), s = sinf(r);
  const SpriteRec *sp = sprite_rec(sprite);
  float lx = sx * sp->lx / 64.0f, ty = sy * sp->ty / 64.0f;
  out->ax = (int16_t)lrintf((x + c * lx - s * ty) * 64);
  out->ay = (int16_t)lrintf((y + s * lx + c * ty) * 64);
  int32_t rot = (int32_t)lrintf(degrees / 360.0f * 65536.0f);
  out->rot = (int16_t)(rot & 0xFFFF);
  if (out->rot) out->flags |= F_ROT;
}
