/* data.bin: sections, sprites, tiles, rooms and texts (see res.h). */
#include <stddef.h>
#include "blk.h"
#include "res.h"

const uint8_t *dr_bin;

int spr_frames(uint16_t spr) {
  const uint8_t *s = section(SEC_SPRITES);
  if (spr >= rd32(s)) return 0;
  return rd16(s + 4 + 4 * spr + 2);
}

/* a frame's or tile's bytes: in its block, unpacked (blk.h) */
static const uint8_t *in_block(int sec, int k) {
  const uint8_t *t = section(sec);
  if ((uint32_t)k >= rd32(t)) return NULL;
  t += 4 + 4 * k;
  const uint8_t *b = blk_get(rd16(t));
  return b ? b + rd16(t + 2) : NULL;
}

bool frame_get(uint16_t spr, int i, Frame *f) {
  const uint8_t *s = section(SEC_SPRITES);
  if (spr >= rd32(s)) return false;
  int first = rd16(s + 4 + 4 * spr), n = rd16(s + 4 + 4 * spr + 2);
  if (n <= 0) return false;
  i %= n;
  if (i < 0) i += n;
  const uint8_t *p = in_block(SEC_FRAMES, first + i);
  if (!p) return false;
  f->w = rd16(p), f->h = rd16(p + 2);
  f->ox = rds16(p + 4), f->oy = rds16(p + 6);
  f->bpp = p[8], f->ncol = p[9];
  f->pal = p + 10;
  uint32_t o = 10 + 2u * f->ncol;
  f->rows = p + o;
  f->data = p + o + 2u * f->h;
  return true;
}

int spr_w(uint16_t spr) {
  Frame f;
  return frame_get(spr, 0, &f) ? f.w : 0;
}
int spr_h(uint16_t spr) {
  Frame f;
  return frame_get(spr, 0, &f) ? f.h : 0;
}

void tile_get(uint16_t id, Tile *t) {
  static const uint8_t black[4] = {0, 1, 0, 0};
  const uint8_t *p = in_block(SEC_TILES, id);
  if (!p) p = black;
  t->bpp = p[0] & 0x7F, t->clear = (p[0] & 0x80) != 0, t->ncol = p[1];
  t->pal = p + 2;
  t->bits = p + 2 + 2 * t->ncol;
}

void room_info(int room, RoomInfo *r) {
  const uint8_t *s = section(SEC_ROOMS);
  if ((uint32_t)room >= rd32(s)) room = 0;
  const uint8_t *p = s + 4 + 32 * room;
  r->w = rd16(p), r->h = rd16(p + 2), r->gcols = rd16(p + 4), r->grows = rd16(p + 6);
  const uint8_t *grid = section(SEC_GRID);
  r->grid = grid + 2 * rd32(p + 8);
  uint32_t f = rd32(p + 12);
  r->fgrid = f == 0xFFFFFFFFu ? NULL : grid + 2 * f;
  r->solids = section(SEC_SOLIDS) + 10 * rd32(p + 16);
  r->nsolid = rd16(p + 20);
  r->insts = section(SEC_INSTS) + 12 * rd32(p + 22);
  r->ninst = rd16(p + 26);
  r->flags = p[28];
  r->bgcol = rd16(p + 30);
}

void room_inst(const RoomInfo *r, int i, Inst *o) {
  const uint8_t *p = r->insts + 12 * i;
  o->obj = rd16(p), o->x = rds16(p + 2), o->y = rds16(p + 4), o->sx = rds16(p + 6), o->sy = rds16(p + 8);
  o->n = rd16(p + 10);
}

const char *str(uint16_t id) {
  const uint8_t *s = section(SEC_STRINGS);
  if (id >= rd32(s)) return "";
  return (const char *)s + rd32(s + 4 + 4 * id);
}
