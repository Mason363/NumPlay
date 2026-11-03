/* Among Us: drawing the ship around the player (view.c). */
#ifndef AU_VIEW_H
#define AU_VIEW_H
#include "game.h"
#include "res.h"

#define VIEW_MAXPTS 512
typedef struct {
  int cx, cy;          /* the ship's pixel at the screen's top-left */
  float radius;
  bool dark;
  int npts;
  float pts[2 * VIEW_MAXPTS];   /* the light's polygon, on the screen */
} View;
extern View view;

void view_prepare(void);   /* once a frame, before the bands */
const Pic *player_frame(const Player *P, float t);
bool player_visible(int j);
void view_camera(V2 c, float scale, int rx, int ry, int rw, int rh);
#endif
