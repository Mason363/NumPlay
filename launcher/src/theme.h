/* TwiliPlay themes: small flash-resident palettes, no extra framebuffer. */
#ifndef NP_THEME_H
#define NP_THEME_H
#include "np.h"
enum { NP_THEME_STARLIGHT, NP_THEME_LITCLUB, NP_THEME_MIDNIGHT, NP_THEME_COUNT };
typedef struct {
  const char *name;
  uint32_t bg, bg2;
  uint32_t panel, accent, text;
  uint32_t star_a, star_b;
} np_theme_t;
const np_theme_t *np_theme(void);
void np_theme_step(int dir);
#endif
