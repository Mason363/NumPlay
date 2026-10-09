#ifndef CI_SPR_H
#define CI_SPR_H
#include "ci.h"


/* LZMA decoding (one stream at a time) */
void z_open(uint32_t off, uint32_t clen, uint32_t rawlen);
void z_open_stream(const uint8_t *st);          /* a stream from its record (spr_stream), all its strips */
uint32_t z_read(uint32_t n, const uint8_t **at);
extern uint32_t z_epoch;                        /* changes when the decoder is opened again */
bool z_get(uint8_t *dst, uint32_t n);

/* Run-length rows of a sprite, decoded on demand, or NULL:
 * u16 w, u16 h, u16 row offsets[h] (from the start), then each row:
 * nruns, then (skip, len, len palette indices) per run. */
const uint8_t *spr_get(uint16_t sprite);
const uint8_t *spr_peek(uint16_t sprite);    /* only if cached */
const uint8_t *spr_get_soft(uint16_t sprite);   /* spr_get without letting go of what this frame already took */
const uint8_t *spr_stream(uint16_t sprite);   /* 14-byte stream record or NULL */
const uint8_t *spr_mask(uint16_t sprite);     /* its opaque pixels as spans (decodes it if needed), or NULL */
const uint8_t *spr_peek_mask(uint16_t sprite);
uint32_t spr_mask_bytes(uint16_t sprite);     /* the cache its mask takes (a guess until it is made) */
/* the decoder at the sprite's first row, its pixels following (w a row),
 * without the cache: for big scenery the cache has no room for */
bool spr_rows_open(uint16_t sprite, const Sprite *s);
bool spr_rows_at(uint16_t sprite, const Sprite *s, int row);   /* the same from row `row` on */
bool spr_room(uint32_t bytes);                  /* would fit without letting go of what the last frame drew */
void spr_pin(uint16_t sprite, const uint8_t *rle);
bool spr_pinned(uint16_t sprite);                /* kept outside the cache (spr_pin) */
bool spr_opaque_box(uint16_t sprite, int16_t box[4]);   /* its opaque middle: x0, y0, x1, y1 (decodes it once) */
void spr_release(uint16_t sprite);                    /* not needed again this frame */   /* its runs kept elsewhere by the scene (NULL: no more) */
void spr_setup(uint8_t *mem, uint32_t size);   /* where the cache lives (game.c's arena) */
void spr_reset(void);
uint8_t *z_scratch(uint32_t *size);             /* the decoder's ring when it is idle */
void spr_set_ring(uint8_t *mem);   /* LZMA_DICT bytes, on main's stack on the calculator */
void spr_tick(void);
uint32_t spr_cache_used(void);
uint32_t spr_capacity(void);
#endif
