#ifndef WHG_APP_H
#define WHG_APP_H
#include "game.h"
#include "../../../launcher/src/gfx.h"
enum { UI_HOME,UI_GAME,UI_PAUSE,UI_HELP,UI_RECORDS,UI_CREDITS,UI_CONFIRM,UI_SAVE_ERROR,UI_FINISH };
typedef struct {
 WhgProfile profile;
 int screen,selected,return_to,confirm,save_return;
 bool dirty,quit,exit_after_save;
 int16_t enemy_x[WHG_MAX_ENEMIES],enemy_y[WHG_MAX_ENEMIES];
} WhgApp;
void whg_prepare_draw(WhgApp *a);
void whg_draw(void *ctx);
#endif
