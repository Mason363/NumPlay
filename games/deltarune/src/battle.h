/* Deltarune: battles (the original's obj_battlecontroller and its enemies).
 *
 * The original's battle is 640 x 480, halved here as the Dark World is: positions stay the original's
 * units (1/16 of them for what moves), drawn at half. A battle is an encounter (scr_encountersetup's
 * number: who is fought and where); the party (up to three) each picks FIGHT, ACT (Kris) or MAGIC, ITEM,
 * SPARE or DEFEND; then the enemies talk and attack the SOUL in the box, grazing their bullets builds TP. */
#ifndef DR_BATTLE_H
#define DR_BATTLE_H
#include <stdbool.h>
#include <stdint.h>

enum { BR_WON, BR_SPARED, BR_FLED, BR_LOST, BR_SCRIPTED };

void battle_start(int encounter);
bool battle_over(void);
int battle_result(void);
void battle_update(uint32_t pressed, uint32_t held);
void battle_draw(void);
/* (for the tests) what the battle waits for */
enum { BUI_NONE, BUI_BUTTONS, BUI_TARGET, BUI_ACTS, BUI_ITEMS, BUI_SPELLS, BUI_PARTY, BUI_BARS, BUI_DODGE, BUI_TEXT };
int battle_ui(void);
#endif
