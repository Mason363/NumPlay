/* Temple Run: the world drawn (scene.c). */
#ifndef TR_SCENE_H
#define TR_SCENE_H
#include <stdbool.h>
#include "r3d.h"

void scene_init(void);
void scene_reset(void);    /* the camera straight behind the runner (a new run) */
void scene_follow(void);   /* a 60th of a second: the camera after the runner */
/* the frame: the camera after the runner (on the title: in front of it), the world, then overlay over it */
void scene_draw(r_band_fn overlay, bool on_title);
/* one mesh (data.h) at a point of the world, turned by yaw (0: its front toward +z); alpha < 255 see-through */
void scene_mesh(int m, float x, float y, float z, float yaw, int alpha);
float r_sin_(float a);
#endif
