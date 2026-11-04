/* 2 3 4 Players: the screens and their keys (app.c). */
#ifndef FP_APP_H
#define FP_APP_H
#include <stdbool.h>
#include <stdint.h>

void app_init(uint32_t now);
/* the keys held now (plat_keys); false: the game ends (saved) */
bool app_step(uint64_t keys, uint32_t now);
bool app_dirty(void);
void app_draw(void);

#ifdef HOST /* (tests/play.c) */
enum { S_MODE, S_GAMES, S_RULES, S_PLAY, S_SCORE, S_OVER, S_PAUSE, S_ASK };
typedef struct {
  int screen, mode, game, cur, target, round, nsides, winner, matches;
  int score[4];
  bool decided;
  int32_t rt;
  uint16_t wins[16][6];
} app_state_t;
void app_state(app_state_t *s);
uint64_t app_bot_keys(void); /* the keys the computer players hold now (the game's bot) */
uint64_t app_player_key(int p);
void app_test_round(int mode, int game);
void app_test_step(unsigned held);
int app_test_winner(void);
#endif
#endif
