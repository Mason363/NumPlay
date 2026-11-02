/* data.bin's LZMA blocks and the RAM they're unpacked into (blk.h). */
#include <string.h>
#include "blk.h"
#include "lzma/LzmaDec.h"
#include "res.h"

#define ARENA (60 * 1024)   /* the cache: a screen of tiles is 20 KB, rarely 55; a battle's monsters 40 */
#define MAX_IN 128          /* blocks in it at once */
#define MAX_BLOCKS 4096     /* (tools/pack.py's) */
#define NONE 0xFF

static uint8_t arena[ARENA] __attribute__((aligned(4)));
typedef struct {
  uint16_t id, used;        /* the block, the frame it was last used in */
  uint32_t off, size;       /* where it is in the arena */
} In;
static In in[MAX_IN];       /* by where they are */
static int nin;
static uint8_t where[MAX_BLOCKS];   /* each block's place in `in`, or NONE */
static uint16_t frame_no = 1;
static bool ready;
uint32_t blk_unpacked, blk_most_in_frame;
static uint32_t unpacked_now;

/* the decoder's tables, in one piece (LzmaDecode asks once a block) */
static uint16_t probs[2800];
static void *probs_alloc(ISzAllocPtr p, size_t size) {
  (void)p;
  return size <= sizeof probs ? probs : NULL;
}
static void probs_free(ISzAllocPtr p, void *a) { (void)p, (void)a; }
static const ISzAlloc alloc = {probs_alloc, probs_free};

static void init(void) {
  memset(where, NONE, sizeof where);
  nin = 0, ready = true;
}

static void drop(int k) {
  where[in[k].id] = NONE;
  memmove(&in[k], &in[k + 1], (size_t)(nin - k - 1) * sizeof in[0]);
  nin--;
  for (int i = k; i < nin; i++) where[in[i].id] = (uint8_t)i;
}

/* room for n bytes: its place in `in` (the new entry, made), or -1 */
static int make_room(uint32_t n) {
  for (;;) {
    if (nin < MAX_IN) {
      uint32_t at = 0;
      for (int i = 0; i <= nin; i++) {
        uint32_t end = i < nin ? in[i].off : ARENA;
        if (end - at >= n) {
          memmove(&in[i + 1], &in[i], (size_t)(nin - i) * sizeof in[0]);
          nin++;
          for (int j = i + 1; j < nin; j++) where[in[j].id] = (uint8_t)j;
          in[i].off = at, in[i].size = n;
          return i;
        }
        if (i < nin) at = in[i].off + in[i].size;
      }
    }
    if (!nin) return -1;
    /* throw out the least recently used (those of this frame last) */
    int old = 0;
    for (int i = 1; i < nin; i++)
      if ((uint16_t)(frame_no - in[i].used) > (uint16_t)(frame_no - in[old].used)) old = i;
    drop(old);
  }
}

const uint8_t *blk_get(int id) {
  if (!ready) init();
  const uint8_t *s = section(SEC_BLOCKS);
  if (id < 0 || id >= (int)rd32(s)) return NULL;
  if (where[id] != NONE) {
    In *b = &in[where[id]];
    b->used = frame_no;
    return arena + b->off;
  }
  const uint8_t *e = s + 12 + 8 * id;
  uint32_t off = rd32(e), size = rd32(e + 4);
  uint32_t next = id + 1 < (int)rd32(s) ? rd32(e + 8) : 0;
  int k = make_room(size);
  if (k < 0) return NULL;
  In *b = &in[k];
  b->id = (uint16_t)id, b->used = frame_no;
  where[id] = (uint8_t)k;
  SizeT out = size, src = next ? next - off : 1u << 30;
  ELzmaStatus st;
  LzmaDecode(arena + b->off, &out, s + off, &src, s + 4, LZMA_PROPS_SIZE, LZMA_FINISH_ANY, &st, &alloc);
  blk_unpacked++, unpacked_now++;
  return arena + b->off;
}

void blk_frame(void) {
  if (!ready) init();
  /* the blocks packed together, so the free room is in one piece */
  uint32_t at = 0;
  for (int i = 0; i < nin; i++) {
    if (in[i].off != at) memmove(arena + at, arena + in[i].off, in[i].size), in[i].off = at;
    at += in[i].size;
  }
  if (unpacked_now > blk_most_in_frame) blk_most_in_frame = unpacked_now;
  unpacked_now = 0;
  frame_no++;
}
