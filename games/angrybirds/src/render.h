#ifndef ANGRY_RENDER_H
#define ANGRY_RENDER_H
#include "game.h"
typedef void (*DisplayPush)(int,int,int,int,const uint16_t*);
void render_invalidate(void);
void render(const Game*,unsigned,unsigned,bool,DisplayPush);
#endif
