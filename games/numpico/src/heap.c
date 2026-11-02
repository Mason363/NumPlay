/* Lua's memory: all the RAM the app has after its own (plat_heap), without a header on any block.
 * Lua says how big a block is when it frees or resizes it, so a block is just its bytes, rounded up
 * to 8 (a free block's size and link; 16 on a 64-bit computer). Small blocks (up to 256 bytes,
 * most of Lua's) go back to a list of their size and come out first from there; bigger ones go back to a list kept in address order, merged with their
 * free neighbours. When a block doesn't fit anywhere, the small free blocks go back to that list
 * too, merged, and the search is made again. */
#include "heap.h"
#include <stddef.h>
#include <string.h>

#define SMALL_MAX 256
#define GRAIN ((uint32_t)sizeof(blk_t))
#define CLASSES (SMALL_MAX / GRAIN)

typedef struct blk {
  uint32_t size;
  struct blk *next;
} blk_t;

static blk_t *small[CLASSES];
static blk_t *big; /* address order */
static uint8_t *lo, *hi;
static uint32_t used, peak;

static uint32_t r8(size_t n) { return n ? (uint32_t)((n + GRAIN - 1) & ~(size_t)(GRAIN - 1)) : GRAIN; }

void heap_init(uint8_t *start, uint8_t *end) {
  lo = (uint8_t *)(((uintptr_t)start + GRAIN - 1) & ~(uintptr_t)(GRAIN - 1));
  hi = (uint8_t *)((uintptr_t)end & ~(uintptr_t)(GRAIN - 1));
  memset(small, 0, sizeof small);
  big = NULL;
  if (hi > lo + 16) {
    big = (blk_t *)lo;
    big->size = (uint32_t)(hi - lo);
    big->next = NULL;
  }
  used = peak = 0;
}

uint32_t heap_used(void) { return used; }
uint32_t heap_peak(void) { return peak; }
uint32_t heap_size(void) { return hi > lo ? (uint32_t)(hi - lo) : 0; }

/* back into the address-ordered list, merged with the blocks around it */
static void put_big(uint8_t *p, uint32_t n) {
  blk_t **at = &big;
  while (*at && (uint8_t *)*at < p) at = &(*at)->next;
  blk_t *prev = at == &big ? NULL : (blk_t *)((uint8_t *)at - offsetof(blk_t, next));
  blk_t *next = *at;
  if (prev && (uint8_t *)prev + prev->size == p) {
    prev->size += n;
    if (next && (uint8_t *)prev + prev->size == (uint8_t *)next) prev->size += next->size, prev->next = next->next;
    return;
  }
  blk_t *b = (blk_t *)p;
  b->size = n;
  b->next = next;
  if (next && p + n == (uint8_t *)next) b->size += next->size, b->next = next->next;
  *at = b;
}

/* the first free block it fits in: its end */
static void *take_big(uint32_t n) {
  for (blk_t **at = &big; *at; at = &(*at)->next) {
    blk_t *b = *at;
    if (b->size < n) continue;
    if (b->size - n < GRAIN) { /* (it can't be less but 0 or GRAIN or more) */
      *at = b->next;
      return b;
    }
    b->size -= n;
    return (uint8_t *)b + b->size;
  }
  return NULL;
}

/* a sorted list of blocks (merge sort on the list itself) */
static blk_t *sort(blk_t *l, int n) {
  if (n <= 1) {
    if (l) l->next = NULL;
    return l;
  }
  blk_t *mid = l;
  for (int i = 0; i < n / 2 - 1; i++) mid = mid->next;
  blk_t *r = mid->next;
  mid->next = NULL;
  blk_t *a = sort(l, n / 2), *b = sort(r, n - n / 2), *out = NULL, **tail = &out;
  while (a && b) {
    blk_t **m = (uint8_t *)a < (uint8_t *)b ? &a : &b;
    *tail = *m;
    tail = &(*m)->next;
    *m = (*m)->next;
  }
  *tail = a ? a : b;
  return out;
}

/* the small free blocks back into the big list */
static void reclaim(void) {
  blk_t *all = NULL;
  int n = 0;
  for (int c = 0; c < CLASSES; c++) {
    while (small[c]) {
      blk_t *b = small[c];
      small[c] = b->next;
      b->size = (uint32_t)(c + 1) * GRAIN;
      b->next = all;
      all = b;
      n++;
    }
  }
  if (!n) return;
  all = sort(all, n);
  /* (merge the two sorted lists, joining neighbours) */
  blk_t *a = big, *out = NULL, *last = NULL;
  big = NULL;
  while (a || all) {
    blk_t **m = !all || (a && (uint8_t *)a < (uint8_t *)all) ? &a : &all;
    blk_t *b = *m;
    *m = b->next;
    if (last && (uint8_t *)last + last->size == (uint8_t *)b) {
      last->size += b->size;
    } else {
      b->next = NULL;
      if (last) last->next = b;
      else out = b;
      last = b;
    }
  }
  big = out;
}

static void *alloc(uint32_t n) {
  void *p = NULL;
  if (n <= SMALL_MAX && small[n / GRAIN - 1]) {
    blk_t *b = small[n / GRAIN - 1];
    small[n / GRAIN - 1] = b->next;
    p = b;
  } else {
    p = take_big(n);
    if (!p) {
      reclaim();
      p = take_big(n);
    }
  }
  if (p) {
    used += n;
    if (used > peak) peak = used;
  }
  return p;
}

static void release(void *p, uint32_t n) {
  used -= n;
  if (n <= SMALL_MAX) {
    blk_t *b = p;
    b->next = small[n / GRAIN - 1];
    small[n / GRAIN - 1] = b;
  } else {
    put_big(p, n);
  }
}

void *heap_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud;
  if (!ptr) return nsize ? alloc(r8(nsize)) : NULL;
  uint32_t o = r8(osize);
  if (!nsize) {
    release(ptr, o);
    return NULL;
  }
  uint32_t n = r8(nsize);
  if (n == o) return ptr;
  if (n < o) { /* the end of it goes back */
    release((uint8_t *)ptr + n, o - n);
    return ptr;
  }
  void *q = alloc(n);
  if (!q) return NULL;
  memcpy(q, ptr, o);
  release(ptr, o);
  return q;
}
