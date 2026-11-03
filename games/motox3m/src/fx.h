/* Moto X3M: the camera and the effects: explosions, exhaust smoke, the wheels' dust, the flaming rider's fire,
 * broken glass (motox3m4.js: RaceCamera, CarExplodeObject, SmokePartClip, GroundPartClip, FirePartClip,
 * GlassBlockPart). */
#ifndef MX_FX_H
#define MX_FX_H
#include <stdbool.h>

typedef struct {
  float x, y, w, h;         /* RaceCamera.x, y, w, h: the world rectangle in view */
  float px, py, scale;      /* the point at the screen's centre, screen pixels a world pixel */
  float frame_h;
  float shake;              /* seconds of shaking left */
  float shx, shy;           /* this frame's shake offset (screen pixels) */
  int path;
  bool tweening;
} Cam;
extern Cam cam;

void cam_reset(float x, float y);
void cam_update(float x, float y, float speed, bool frozen);
bool fx_in_camera(float x, float y, float margin);
void fx_shake(void);
float fx_shake_x(void), fx_shake_y(void);

struct Obj;
void fx_explosion(float x, float y, struct Obj *hide_when_done);
void fx_smoke(float x, float y, float angle);
void fx_ground(float x, float y, float angle, bool fwd);
void fx_fire(float x, float y);
void fx_glass(struct Obj *glass);
void fx_landing(void);
void fx_update(float dt);
void fx_clear(void);
float fx_rand(void);
/* drawn by view.c */
void fx_draw_fire(void);    /* the flaming rider's fire, behind the objects */
void fx_draw_back(void);    /* smoke and dust, over the bike */
void fx_draw_front(void);   /* explosions, fire, glass */
#endif
