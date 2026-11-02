/* NumPico's PICO-8: femto8's Lua and API (femto8/) running a cart on the calculator (pico.c), and
 * what the app around it (main.c) sees of it. */
#ifndef NP8_PICO_H
#define NP8_PICO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "carts.h"

typedef struct lua_State lua_State;

/* ---- for femto8's code (femto8/p8_lua.c) */
void *pico_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize);
void pico_hook(lua_State *L);       /* every few thousand Lua instructions */
int pico_panic(lua_State *L);       /* an error outside a pcall */
void pico_update_buttons(void);
_Noreturn void pico_quit_cart(void);  /* extcmd("shutdown") */
_Noreturn void pico_stop_cart(void);  /* stop() */
void pico_breadcrumb(void);           /* extcmd("breadcrumb"): back to the cart that loaded this one */

/* ---- for the app */
/* the picture: 128 x 128 drawn on 240 x 240 (every 8th row and column of PICO-8's once, the others
   twice), from x = PIC_X; the hints are at its left */
#define PIC_X 80
#define PIC_W 240

enum {
  PICO_QUIT,     /* the pause menu's Quit, or the cart ended */
  PICO_HOME,     /* Home: leave the app */
  PICO_ERROR,    /* pico_error says what */
  PICO_MEMORY,   /* the cart needs more memory than the calculator has */
  PICO_LOAD,     /* load(): pico_next is the cart to run, with m_param_string */
};
typedef struct {
  char what[24];   /* "syntax error", "runtime error", "stopped" */
  char msg[120];
  int line;
} pico_error_t;
extern pico_error_t pico_error;
extern int pico_next;
extern uint32_t pico_peak; /* the most memory Lua had, in bytes, the last time a cart ran */

/* more RAM for Lua, on main's stack (its 32 KB are more than the calculator needs: see llimits.h) */
#define PICO_EXTRA 14336
extern uint8_t *pico_extra;

extern p8_cart_t pico_carts[P8_MAX_CARTS];
extern int pico_ncarts;

/* runs a cart until it ends (one of PICO_*) */
int pico_run(int index);
/* the cart's frame as it was last drawn, again (behind a screen of the app's) */
void pico_redraw(void);

/* ---- PICO-8's font, for the app's screens */
/* a P8SCII letter's pixel (x 0..7, y 0..5) */
bool pico_glyph_px(uint8_t c, int x, int y);
/* the width of a P8SCII string in PICO-8's pixels (4 a letter, 8 for those from 128) */
int pico_text_w(const char *s);
extern const uint16_t pico_rgb565[32];
#endif
