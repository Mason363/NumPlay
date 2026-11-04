/* Deltarune: what the signs, beds and people say, as the original chooses it (gmtexts.c, made by
 * tools/texts.py from its code). A TxtCtx is the instance asking (where it is, how often it was read)
 * and what it gets back: the messages for the dialogue box, a face, a choice. */
#ifndef DR_GMTEXTS_H
#define DR_GMTEXTS_H
#include <stdint.h>
#include "data.h"
#include "res.h"
#include "text.h"

typedef struct TxtCtx {
  int x, y;                  /* the instance's place, the original's units */
  int read, talked;          /* how often it was read */
  int img;                   /* its image_index */
  uint16_t spr;              /* its sprite (an npc's create sets it) */
  uint16_t tspr[4];          /* facing Kris while talking: down, right, up, left (DIR_*) */
  int fc, fe;                /* the face the box starts with */
  int choice, facing, msc, arg, arg1;
  int type;                  /* its own (an npc's create sets it: which one it is) */
  int tempflag[16];
  int gone, hidden;          /* instance_destroy(), visible = 0 (its create) */
  int ispd;                  /* image_speed, 1/16 (-1: as made) */
  const char *msg[MAX_MSG];
  int nmsg;                  /* messages set (the highest + 1) */
  const char *choicemsg[4];
} TxtCtx;

void txt_msg(TxtCtx *c, int i, const char *s);
void txt_face(TxtCtx *c, int i, char who, int fe);
void txt_msg_subst(TxtCtx *c, int i, const char *s, const char *a);   /* ~1 in s filled with a */
#define MSG(i, id) txt_msg(c, i, str(id))
#define CHOICE(i, id) (c->choicemsg[i] = str(id))
#define FACE(i, who, fe) txt_face(c, i, who, fe)
#define NOFACE(i) txt_face(c, i, 0, 0)
void gm_itemget(int id);
void gm_keyitemget(int id);
void gm_weaponget(int id);
void gm_armorget(int id);
void gm_litemget(int id);
int gm_itemcheck(int id);
int gm_keyitemcheck(int id);
int gm_litemcheck(int id);
int gm_weaponcheck(int id);
void gm_itemremove(int id);
void gm_keyitemremove(int id);
void gm_litemremove(int id);

void gm_readable_room1(TxtCtx *c);
void gm_npc_room_create(TxtCtx *c);
void gm_npc_room_talk(TxtCtx *c);
void gm_npc_sign_create(TxtCtx *c);
void gm_npc_sign_talk(TxtCtx *c);
void gm_npc_facing_create(TxtCtx *c);
void gm_npc_facing_talk(TxtCtx *c);
void gm_npc_room_animated_create(TxtCtx *c);
void gm_npc_room_animated_talk(TxtCtx *c);
void gm_scr_text(TxtCtx *c);
extern int g_noroom;
/* "with (obj_x) con = n" in the texts' code: the cutscene object obj_x is told to go on to n (g_signal_*) */
void gm_signal(int obj, int con);
extern int g_signal_obj, g_signal_con;   /* the last scr_itemget found no room (noroom) */
#endif
