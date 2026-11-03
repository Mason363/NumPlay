/* Among Us: the map (MapBehaviour): where you are and your tasks; for impostors the sabotage map; the admin table's
 * counts of who is in each room. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "view.h"
#include "world.h"

void hud_keys(const char *s);

void map_tick(uint32_t pressed, int mode) {
  (void)mode;
  if (pressed & (K_BACK | K_TOOL)) g.overlay = O_NONE;
}

void map_draw(int mode) {
  view_draw();
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, rgb565(0x1B3D6B), 200);
  gfx_text(FONT_BIG, 160, 110, mode == O_SABMAP ? "Sabotage" : mode == O_ADMIN ? "Admin" : "Map", 0xFFFF, 0, 0, 1);
  hud_keys("Back close");
}
