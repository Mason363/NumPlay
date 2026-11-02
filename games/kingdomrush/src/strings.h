/* Kingdom Rush: the game's own words (strings.c, made by tools/pack.py). */
#ifndef KR_STRINGS_H
#define KR_STRINGS_H
const char *note_title(int n);
const char *note_text(int n);         /* 0 when there is nothing to show */
int note_pic(int n, int *arg);        /* 1 an enemy, 2 a spell, 3 a tower (TW_*), 0 none */
const char *enemy_desc(int type);
const char *level_title(int i);
const char *level_story(int i);
const char *upgrade_name(int line, int n);   /* n: 0 to 4 */
const char *upgrade_desc(int line, int n);
const char *tower_text(const char *key);     /* TOWER_*, POWER_* */
const char *tip(int i);
#endif
