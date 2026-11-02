/* Deltarune: the menus. The vessel's name, the C menu (the Dark World's ITEM, EQUIP, POWER, CONFIG; the
 * Light World's ITEM, STAT, CELL), the SAVE menu and the game over. */
#ifndef DR_MENU_H
#define DR_MENU_H
#include <stdbool.h>

void menu_name_start(void);
void menu_name_update(void);
void menu_name_draw(void);
void menu_open(void);          /* C */
void menu_save_open(void);     /* a SAVE point's */
bool menu_save_done(void);
void menu_update(void);
void menu_draw(void);
void menu_gameover_start(void);
void menu_gameover_update(void);
void menu_gameover_draw(void);
#endif
