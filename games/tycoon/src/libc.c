/* Minimal replacements for the few libc routines the compiler may call (copies
 * of structures, zeroed arrays): much smaller than newlib's speed-optimized
 * versions, and available whether the game is linked alone or inside NumPlay. */
#include <stddef.h>

#define EXPORT __attribute__((used, externally_visible))

EXPORT void *memcpy(void *d, const void *s, size_t n) {
  char *o = d;
  const char *i = s;
  while (n--) *o++ = *i++;
  return d;
}

EXPORT void *memmove(void *d, const void *s, size_t n) {
  char *o = d;
  const char *i = s;
  if (o < i) return memcpy(d, s, n);
  while (n--) o[n] = i[n];
  return d;
}

EXPORT void *memset(void *d, int c, size_t n) {
  char *o = d;
  while (n--) *o++ = (char)c;
  return d;
}
