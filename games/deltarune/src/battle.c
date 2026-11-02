/* Battles (battle.h). */
#include "battle.h"
#include "game.h"
#include "gfx.h"

static bool over = true;
static int result;
void battle_start(int encounter) {
  (void)encounter;
  over = true, result = BR_WON;
}
bool battle_over(void) { return over; }
int battle_result(void) { return result; }
void battle_update(uint32_t pressed, uint32_t held) { (void)pressed, (void)held; }
void battle_draw(void) { gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 255); }
int battle_ui(void) { return BUI_NONE; }
