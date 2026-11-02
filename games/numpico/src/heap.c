/* Lua's memory: the RAM the app has after its own (plat_heap), and more on main's stack, without a
 * header on any block.
 *
 * Lua says how big a block is when it frees or resizes it, so a block is just its bytes, rounded up
 * to 8 (a free block's size and link; 16 on a 64-bit computer). The free blocks are in a list kept
 * in address order, merged with their free neighbours. Small blocks (up to 256 bytes: strings,
 * tables, closures, most of Lua's) are taken from the low end of the lowest free block they fit in,
 * big ones (arrays: code, a table's slots, the stack) from the high end of the highest, so the two
 * kinds don't leave holes in each other. A small block freed goes to a list of its size and is used
 * again from there; when a block fits nowhere, those go back to the big list, merged, and the search
 * is made again. */
#include "heap.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct blk {
  uint32_t size;
  struct blk *next;
} blk_t;

#define SMALL_MAX 256
#define GRAIN ((uint32_t)sizeof(blk_t))
#define CLASSES (SMALL_MAX / GRAIN)

static blk_t *small[CLASSES];
static blk_t *big; /* address order */
static uint32_t used, peak, size;

static uint32_t round_up(size_t n) { return n ? (uint32_t)((n + GRAIN - 1) & ~(size_t)(GRAIN - 1)) : GRAIN; }

void heap_init(uint8_t *start, uint8_t *end) {
  memset(small, 0, sizeof small);
  big = NULL;
  used = peak = size = 0;
  heap_add(start, end);
}

uint32_t heap_used(void) { return used; }
uint32_t heap_peak(void) { return peak; }
uint32_t heap_size(void) { return size; }

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

void heap_add(uint8_t *start, uint8_t *end) {
  uint8_t *a = (uint8_t *)(((uintptr_t)start + GRAIN - 1) & ~(uintptr_t)(GRAIN - 1));
  uint8_t *b = (uint8_t *)((uintptr_t)end & ~(uintptr_t)(GRAIN - 1));
  if (b <= a + 4 * GRAIN) return;
  size += (uint32_t)(b - a);
  put_big(a, (uint32_t)(b - a));
}

static void *take_low(uint32_t n) {
  for (blk_t **at = &big; *at; at = &(*at)->next) {
    blk_t *b = *at;
    if (b->size < n) continue;
    if (b->size == n) {
      *at = b->next;
      return b;
    }
    blk_t *rest = (blk_t *)((uint8_t *)b + n);
    rest->size = b->size - n;
    rest->next = b->next;
    *at = rest;
    return b;
  }
  return NULL;
}

static void *take_high(uint32_t n) {
  blk_t **found = NULL;
  for (blk_t **at = &big; *at; at = &(*at)->next)
    if ((*at)->size >= n) found = at;
  if (!found) return NULL;
  blk_t *b = *found;
  if (b->size == n) {
    *found = b->next;
    return b;
  }
  b->size -= n;
  return (uint8_t *)b + b->size;
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
  for (int c = 0; c < (int)CLASSES; c++) {
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
  bool tiny = n <= SMALL_MAX;
  if (tiny && small[n / GRAIN - 1]) {
    blk_t *b = small[n / GRAIN - 1];
    small[n / GRAIN - 1] = b->next;
    p = b;
  } else {
    p = tiny ? take_low(n) : take_high(n);
    if (!p) {
      reclaim();
      p = tiny ? take_low(n) : take_high(n);
    }
  }
  if (p) {
    used += n;
    if (used > peak) peak = used;
  } else { /* (for tests/perf.py) */
    extern uint32_t perf_fail_size, perf_fail_used, perf_fail_largest, perf_fail_map[128];
    perf_fail_size = n, perf_fail_used = used, perf_fail_largest = 0;
    int k = 0;
    for (blk_t *b = big; b; b = b->next) {
      if (b->size > perf_fail_largest) perf_fail_largest = b->size;
      if (k < 128) perf_fail_map[k++] = (uint32_t)(uintptr_t)b, perf_fail_map[k++] = b->size;
    }
    if (k < 128) perf_fail_map[k] = 0;
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
  if (!ptr) return nsize ? alloc(round_up(nsize)) : NULL;
  uint32_t o = round_up(osize);
  if (!nsize) {
    release(ptr, o);
    return NULL;
  }
  uint32_t n = round_up(nsize);
  if (n == o) return ptr;
  /* a big block growing: into the free block just before it or just after it, when one's big enough
     (a table's slots and Lua's stack grow by doubling: moved each time, they'd leave holes) */
  if (n > o && o > SMALL_MAX) {
    uint8_t *p = ptr;
    blk_t **at = &big;
    while (*at && (uint8_t *)*at + (*at)->size < p) at = &(*at)->next;
    blk_t *b = *at;
    if (b && (uint8_t *)b + b->size == p && b->size >= n - o) { /* (before: the data moves down) */
      uint8_t *q = p - (n - o);
      if (b->size == n - o) *at = b->next;
      else b->size -= n - o;
      memmove(q, p, o);
      used += n - o;
      if (used > peak) peak = used;
      return q;
    }
    if (b && (uint8_t *)b + b->size == p) b = b->next;
    if (b && (uint8_t *)b == p + o && b->size >= n - o) { /* (after) */
      blk_t **link = (uint8_t *)*at == p + o ? at : &(*at)->next;
      if (b->size == n - o) {
        *link = b->next;
      } else {
        blk_t *rest = (blk_t *)(p + n);
        rest->size = b->size - (n - o);
        rest->next = b->next;
        *link = rest;
      }
      used += n - o;
      if (used > peak) peak = used;
      return p;
    }
  }
  /* bigger, or a big block becoming small: moved, so it goes where its size belongs */
  if (n > o || (o > SMALL_MAX && n <= SMALL_MAX)) {
    void *q = alloc(n);
    if (q) {
      memcpy(q, ptr, n < o ? n : o);
      release(ptr, o);
      return q;
    }
    if (n > o) return NULL;
  }
  /* smaller: the end of it goes back (that never fails) */
  release((uint8_t *)ptr + n, o - n);
  return ptr;
}
