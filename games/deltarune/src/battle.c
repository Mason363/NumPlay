#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* Battles (battle.h): the original's obj_battlecontroller, its menus (scr_charbox), obj_attackpress (FIGHT's
 * bars), obj_tensionbar, the SOUL (obj_heart) in its box (obj_growtangle) and the grazing that builds TP. */
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "game.h"
#include "gfx.h"
#include "menu.h"
#include "res.h"
#include "tables.h"
#include "text.h"
#include "world.h"

Enemy en[MAX_EN];
Bullet bul[MAX_BUL];
int g_tension;
int g_last_encounter;

enum { P_INTRO, P_MENU, P_ACTIONS, P_FIGHT, P_FIGHT_HIT, P_TALK, P_DODGE, P_TURN_END, P_LESSON, P_WIN, P_LOST, P_DONE,
       P_CAMEO };
enum { M_BUTTONS, M_ENEMY, M_ACTS, M_SPELLS, M_ITEMS, M_PARTY };

typedef struct {
  int c;                       /* CH_* */
  int32_t x, y;                /* units (the sprite's origin) */
  int action, target, sub;     /* sub: the ACT, spell or item slot */
  int points;                  /* FIGHT's score */
  int hurt_t, anim_t;
  int heal_show, heal_t;
  int menu_y;                  /* its box rising (mmy) */
} Hero;

static struct {
  int phase, t;
  int enc;
  int nh;
  Hero h[3];
  int turn;                    /* charturn */
  int menu, sel[3][8];         /* bmenuno, bmenucoord by member */
  int enemy_for;               /* M_ENEMY: for FIGHT, ACT, SPARE or a spell */
  int bp;                      /* the panel risen (0..152) */
  int tp_x;                    /* the TP bar sliding in */
  int tp_vx;
  int act_k;                   /* P_ACTIONS: the member acting */
  bool waiting;                /* a text is up */
  Writer w;                    /* the battle box's text */
  bool w_on;
  uint8_t fc, fe;
  int result;
  bool over;
  int gold;
  /* FIGHT's bars */
  int boltx, nbolt;
  int16_t bolt_frame[12];
  int8_t bolt_char[12];
  bool bolt_alive[12];
  bool bolt_hit[12];
  int press_t[3];
  bool attacked[3];
  /* the box and the SOUL */
  int box_t;                   /* growing 0..15, shrinking */
  int32_t hx, hy;              /* the SOUL's top-left, units */
  int inv;                     /* invincible frames */
  int turntimer;
  int graze_t;
  int graze_acc, time_acc;     /* grazing's TP and the turn's time, in 1/20 */
  int pic_t;                   /* a picture bubble's frames */
  uint16_t lesson_after;       /* P_LESSON: the box's text when the turn begins */
  bool talk_wait;              /* P_TALK: the box speaks first */
  bool hide[4];                /* by CH_*: not drawn (a cutscene's marker stands in) */
  int16_t hp0[4];              /* HP at the start */
  bool won_text;
  int talk_i;                  /* whose bubble shows */
  Writer bw;                   /* the bubble's text */
  int saved_mode;
  uint8_t items_snap[NITEMS];
  int defend_tp[3];
  int dmg_show[3], dmg_t[3], dmg_kind[3];
  int shake;
  int enemy_turns;             /* the enemies' turns gone */
  int16_t hp_turn[4];          /* HP as the enemies' turn began */
} B;

/* the cutscene's guest (battle_cameo) */
static struct {
  bool set[2];
  uint16_t spr[2];
  int y[2], fc[2], fe[2];
  const int *ids[2];
  int when, ph, t, x, after;   /* after: the phase it goes back to */
  uint16_t vic_text;
  int vic_fc, vic_fe;
} CM;

#define BOX_CX 320
#define BOX_CY 170
#define BOX_HALF 75          /* spr_battlebg_0: 75 x 75, drawn at 2 */
#define BOX_IN 70            /* its inside's half (the border inside it) */

static const char *char_name(int c) {
  switch (c) {
    case CH_KRIS: return str(scr_gamestart_slash_scr_gamestart_gml_98_0);
    case CH_SUSIE: return str(scr_gamestart_slash_scr_gamestart_gml_99_0);
    case CH_RALSEI: return str(scr_gamestart_slash_scr_gamestart_gml_100_0);
    default: return "";
  }
}
static uint16_t char_color(int c) {
  return c == CH_KRIS ? rgb565(0x00FFFF) : c == CH_SUSIE ? rgb565(0xFF00FF) : rgb565(0x00FF00);
}

/* ---------------------------------------------------------------- helpers for the enemies */
Writer *scene_writer(void) { return &B.bw; }

/* ---------------------------------------------------------------- for a battle's own script */
static int script_ask(int ev, int arg) {
  int r = 0;
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].type && en[i].def && en[i].def->script) r |= en[i].def->script(&en[i], ev, arg);
  return r;
}
static bool scripted(int flag) {
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && en[i].def && (en[i].def->flags & flag)) return true;
  return false;
}
static bool can_move(int c) {
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && en[i].def && en[i].def->script && !en[i].def->script(&en[i], BS_CANMOVE, c)) return false;
  return true;
}
int battle_action(int c, int *sub) {
  for (int k = 0; k < B.nh; k++)
    if (B.h[k].c == c) {
      if (sub) *sub = B.h[k].sub;
      return B.h[k].action;
    }
  return A_NONE;
}
bool battle_text_on(void) { return B.w_on; }
void battle_hide_hero(int c, bool hide) { B.hide[c & 3] = hide; }
void battle_hero_pos(int c, int *x, int *y) {
  for (int k = 0; k < B.nh; k++)
    if (B.h[k].c == c) *x = B.h[k].x, *y = B.h[k].y;
}
int battle_hp_lost(int c) { return B.hp0[c & 3] - sv.hp[c & 3]; }

static Enemy *target_enemy(int i) { return (i >= 0 && i < MAX_EN && en[i].alive) ? &en[i] : NULL; }
int enemies_alive(void) {
  int n = 0;
  for (int i = 0; i < MAX_EN; i++) n += en[i].alive;
  return n;
}
bool party_has(int c) {
  for (int i = 0; i < B.nh; i++)
    if (B.h[i].c == c) return true;
  return false;
}
int party_target(void) {
  int up[3], n = 0;
  for (int i = 0; i < B.nh; i++)
    if (sv.hp[B.h[i].c] > 0) up[n++] = i;
  return n ? up[rnd_n(n)] : 0;
}
int heart_x(void) { return B.hx + 10; }
int heart_y(void) { return B.hy + 10; }
int box_cx(void) { return BOX_CX; }
int box_cy(void) { return BOX_CY; }
int box_half(void) { return BOX_IN; }

void tension_add(int n) {
  g_tension += n;
  if (g_tension > MAXTP) g_tension = MAXTP;
  if (g_tension < 0) g_tension = 0;
}
void mercy_add(Enemy *e, int n) {
  e->mercy = (int16_t)(e->mercy + n);
  if (e->mercy > 100) e->mercy = 100;
}

Bullet *bullet_new(uint16_t spr, int x, int y) {
  for (int i = 0; i < MAX_BUL; i++)
    if (!bul[i].on) {
      Bullet *b = &bul[i];
      memset(b, 0, sizeof *b);
      b->on = true, b->spr = spr, b->x = x * BFX, b->y = y * BFX;
      b->alpha = 255, b->scale = 256, b->r = 8, b->damage = 30, b->gp = 5, b->timep = 5;
      return b;
    }
  static Bullet spare;
  memset(&spare, 0, sizeof spare);
  return &spare;
}
void bullets_clear(void) { memset(bul, 0, sizeof bul); }

static const char *say_buf[MAX_MSG];
static char sayf_buf[2][200];
void battle_face(int fc, int fe) { B.fc = (uint8_t)fc, B.fe = (uint8_t)fe; }
static void writer_box(void) {
  writer_start(&B.w, 15, 188, 33);
  B.w.fc = B.fc, B.w.fe = B.fe;
  if (B.fc) B.w.charline = 26;
  B.fc = B.fe = 0;
  B.w_on = true;
}
void battle_say(const uint16_t *ids, int n) {
  writer_box();
  for (int i = 0; i < n && i < MAX_MSG; i++) say_buf[i] = str(ids[i]), writer_add(&B.w, say_buf[i]);
}
/* a text with ~1 and ~2 filled */
static const char *subst(int slot, uint16_t id, const char *a, const char *b) {
  const char *f = str(id);
  char *o = sayf_buf[slot];
  int n = 0;
  for (int i = 0; f[i] && n < 196; i++) {
    if (f[i] == '~' && (f[i + 1] == '1' || f[i + 1] == '2')) {
      const char *s = f[i + 1] == '1' ? a : b;
      while (s && *s && n < 196) o[n++] = *s++;
      i++;
    } else {
      o[n++] = f[i];
    }
  }
  o[n] = 0;
  return o;
}
void battle_say_ids(int fc, int fe, const int *ids) {
  extern const char *face_msg(int code);
  battle_face(fc, fe);
  writer_box();
  for (int i = 0; ids[i] >= 0 && i < MAX_MSG; i++) writer_add(&B.w, ids[i] & 0x40000 ? face_msg(ids[i]) : str((uint16_t)ids[i]));
}
void battle_say_strs(const char *const *m, int n) {
  writer_box();
  for (int i = 0; i < n && i < MAX_MSG; i++) writer_add(&B.w, m[i]);
}
void battle_box_talk(int fc, int fe, const char *const *m, int n) {
  battle_face(fc, fe);
  battle_say_strs(m, n);
  B.talk_wait = true;
}
void battle_lesson(int fc, int fe, const char *const *m, int n, uint16_t after) {
  battle_face(fc, fe);
  battle_say_strs(m, n);
  B.lesson_after = after;
  B.phase = P_LESSON;
}
void battle_end_attack(void) {
  if (B.turntimer > 1) B.turntimer = 1;
}
void battle_sayf(uint16_t id, const char *a, const char *b) {
  writer_box();
  writer_add(&B.w, subst(0, id, a, b));
}
static void battle_say2(const char *m0, const char *m1) {
  writer_box();
  writer_add(&B.w, m0);
  if (m1) writer_add(&B.w, m1);
}

void enemy_say(Enemy *e, const uint16_t *ids, int n) {
  e->nsay = n > 4 ? 4 : n;
  for (int i = 0; i < e->nsay; i++) e->say[i] = ids[i];
}

void enemy_defeat_run(Enemy *e) {
  if (e->def && (e->def->flags & EF_STAYS)) {
    if (e->hp < 1) e->hp = 1;
    return;
  }
  e->alive = false, e->killed = true, e->fade = 1;
  B.gold += monster_defs[e->type].gold;
}
void enemy_leave(Enemy *e) { e->alive = false, e->fade = 1; }
void enemy_spare(Enemy *e) {
  if (e->def && (e->def->flags & EF_STAYS)) {
    e->def->script(e, BS_SPARED, 0);
    return;
  }
  e->alive = false, e->spared = true, e->fade = 1;
  B.gold += monster_defs[e->type].gold;
}

/* ---------------------------------------------------------------- starting */
void battle_cameo(int when, uint16_t spr, int y, int fc, int fe, const int *ids) {
  when &= 1;
  CM.set[when] = true, CM.spr[when] = spr, CM.y[when] = y, CM.fc[when] = fc, CM.fe[when] = fe, CM.ids[when] = ids;
}
void battle_victory_text(uint16_t id, int fc, int fe) { CM.vic_text = id, CM.vic_fc = fc, CM.vic_fe = fe; }
static void cameo_start(int when, int after) {
  CM.set[when] = false;
  CM.when = when, CM.ph = 0, CM.t = 0, CM.x = 640, CM.after = after;
  B.phase = P_CAMEO;
}
static void cameo_update(void) {
  int w = CM.when;
  CM.t++;
  switch (CM.ph) {
    case 0:   /* in: -20 a frame for 15 frames */
      CM.x -= 20;
      if (CM.t >= 15) CM.ph = 1, CM.t = 0, battle_say_ids(CM.fc[w], CM.fe[w], CM.ids[w]);
      break;
    case 1:
      if (!B.w_on) CM.ph = 2, CM.t = 0;
      break;
    default:  /* out: +20 a frame for 20 frames */
      CM.x += 20;
      if (CM.t >= 20) {
        CM.ph = 3;
        B.phase = CM.after;
        B.t = 0;
      }
      break;
  }
}

void battle_start(int enc) {
  memset(&B, 0, sizeof B);
  memset(en, 0, sizeof en);
  bullets_clear();
  g_last_encounter = enc;
  B.enc = enc;
  g_tension = 0;
  const EncounterDef *d = &encounter_defs[(unsigned)enc < (unsigned)encounter_count ? enc : 0];
  for (int i = 0; i < MAX_EN; i++) {
    const EncMon *m = &d->m[i];
    if (m->type <= 0) continue;
    Enemy *e = &en[i];
    const MonsterDef *md = &monster_defs[m->type];
    e->type = m->type, e->def = enemy_def(m->obj);
    e->alive = true;
    e->hp = e->maxhp = md->hp, e->at = md->at, e->df = md->df;
    e->x = m->x, e->y = m->y;
    /* (0, 0: where the room's own stands, the Dummy: its npc at 900, 160 with the view at 390) */
    if (!m->x && !m->y) e->x = 510, e->y = 160;
    e->comment = " ";
    for (int k = 0; k < 4; k++) e->say[k] = 0xFFFF;
  }
  /* the party, at the left (scr_encountersetup) */
  B.nh = 0;
  for (int k = 0; k < 3; k++)
    if (sv.chr[k]) B.h[B.nh++].c = sv.chr[k];
  for (int k = 0; k < B.nh; k++) {
    B.h[k].x = 80;
    B.h[k].y = B.nh == 1 ? 140 : B.nh == 2 ? 100 + 80 * k : 50 + 80 * k;
  }
  B.tp_x = -40, B.tp_vx = 13;
  for (int c = 0; c < 4; c++) B.hp0[c] = sv.hp[c];
  B.phase = P_INTRO;
  B.saved_mode = g_mode;
  g_mode = MODE_BATTLE;
  uint16_t msg = d->battlemsg != 0xFFFF ? d->battlemsg : monster_defs[d->m[0].type].battlemsg;
  if (msg != 0xFFFF && !scripted(EF_AMBUSH)) battle_say(&msg, 1);
}

bool battle_over(void) { return B.over; }
int battle_result(void) { return B.result; }

int battle_ui(void) {
  switch (B.phase) {
    case P_MENU:
      switch (B.menu) {
        case M_BUTTONS: return BUI_BUTTONS;
        case M_ENEMY: return BUI_TARGET;
        case M_ACTS: return BUI_ACTS;
        case M_SPELLS: return BUI_SPELLS;
        case M_ITEMS: return BUI_ITEMS;
        default: return BUI_PARTY;
      }
    case P_FIGHT: return BUI_BARS;
    case P_DODGE: return BUI_DODGE;
    case P_ACTIONS: case P_TALK: case P_WIN: case P_LESSON: return BUI_TEXT;
    default: return BUI_NONE;
  }
}

/* ---------------------------------------------------------------- the menus */
static int item_count_now(void) {
  int n = 0;
  while (n < NITEMS && sv.item[n]) n++;
  return n;
}
static int spell_of(int c, int i) {
  /* global.spell[]: Kris none (ACT), Susie Rude Buster, Ralsei Pacify and Heal Prayer */
  static const int8_t sp[4][2] = {{0, 0}, {0, 0}, {4, 0}, {3, 2}};
  return (c >= 0 && c < 4 && i < 2) ? sp[c][i] : 0;
}
static int nspells(int c) { return !!spell_of(c, 0) + !!spell_of(c, 1); }
static int act_count(const Enemy *e) {
  int n = 0;
  for (int k = 0; k < 6; k++)
    if (monster_defs[e->type].acts[k].can) n = k + 1;
  return n;
}

static void next_hero(void);
static void begin_actions(void);

static void choose_enemy(int why) {
  B.enemy_for = why;
  B.menu = M_ENEMY;
  int *s = &B.sel[B.turn][M_ENEMY];
  if (!target_enemy(*s))
    for (int i = 0; i < MAX_EN; i++)
      if (en[i].alive) {
        *s = i;
        break;
      }
}

static void bmenu_update(uint32_t pressed) {
  Hero *h = &B.h[B.turn];
  int *s = &B.sel[B.turn][B.menu];
  if (B.menu == M_BUTTONS) {
    if (pressed & K_LEFT) *s = (*s + 4) % 5;
    if (pressed & K_RIGHT) *s = (*s + 1) % 5;
    if (pressed & K_Z) {
      switch (*s) {
        case 0: choose_enemy(A_FIGHT); break;
        case 1:
          if (h->c == CH_KRIS) choose_enemy(A_ACT);
          else if (nspells(h->c)) B.menu = M_SPELLS;
          break;
        case 2:
          if (item_count_now()) B.menu = M_ITEMS;
          break;
        case 3: choose_enemy(A_SPARE); break;
        case 4:
          h->action = A_DEFEND;
          tension_add(40);
          B.defend_tp[B.turn] = 40;
          next_hero();
          break;
      }
    }
    if ((pressed & K_X) && B.turn > 0) {
      /* back to the one before: its choice undone */
      B.turn--;
      Hero *p = &B.h[B.turn];
      if (p->action == A_DEFEND) tension_add(-B.defend_tp[B.turn]);
      if (p->action == A_SPELL) tension_add(spell_defs[p->sub].cost);
      if (p->action == A_ACT) tension_add(monster_defs[en[p->target].type].acts[p->sub].cost);
      if (p->action == A_ITEM) memcpy(sv.item, B.items_snap, sizeof sv.item);
      p->action = A_NONE;
      B.menu = M_BUTTONS;
    }
    return;
  }
  if (B.menu == M_ENEMY) {
    if (!enemies_alive()) {
      B.menu = M_BUTTONS;
      return;
    }
    for (int k = 0; k < 3; k++) {
      if (pressed & K_DOWN) {
        do *s = (*s + 1) % MAX_EN; while (!en[*s].alive);
        break;
      }
      if (pressed & K_UP) {
        do *s = (*s + MAX_EN - 1) % MAX_EN; while (!en[*s].alive);
        break;
      }
    }
    if (pressed & K_X) B.menu = B.enemy_for == A_SPELL ? M_SPELLS : M_BUTTONS;
    if (pressed & K_Z) {
      h->target = *s;
      if (B.enemy_for == A_ACT) {
        B.menu = M_ACTS;
        B.sel[B.turn][M_ACTS] = 0;
      } else {
        h->action = B.enemy_for;
        if (h->action == A_SPELL) tension_add(-spell_defs[h->sub].cost);
        next_hero();
      }
    }
    return;
  }
  if (B.menu == M_ACTS) {
    Enemy *e = &en[h->target];
    int n = act_count(e);
    if (pressed & K_RIGHT && *s % 2 == 0 && *s + 1 < n) (*s)++;
    if (pressed & K_LEFT && *s % 2 == 1) (*s)--;
    if (pressed & K_DOWN && *s + 2 < n) *s += 2;
    if (pressed & K_UP && *s >= 2) *s -= 2;
    if (pressed & K_X) B.menu = M_ENEMY;
    if (pressed & K_Z) {
      const ActDef *a = &monster_defs[e->type].acts[*s];
      bool can = a->can && g_tension >= a->cost;
      if ((a->actor == 2 || a->actor == 4) && !party_has(CH_SUSIE)) can = false;
      if ((a->actor == 3 || a->actor == 4) && !party_has(CH_RALSEI)) can = false;
      if (can) {
        tension_add(-a->cost);
        h->action = A_ACT, h->sub = *s;
        /* the others in the ACT do it with Kris: their turns are taken */
        for (int k = 0; k < B.nh; k++) {
          int c = B.h[k].c;
          if ((c == CH_SUSIE && (a->actor == 2 || a->actor == 4)) || (c == CH_RALSEI && (a->actor == 3 || a->actor == 4)))
            B.h[k].action = A_NONE + 100;   /* (busy acting) */
        }
        next_hero();
      }
    }
    return;
  }
  if (B.menu == M_SPELLS) {
    int n = nspells(h->c);
    if (pressed & K_RIGHT && *s % 2 == 0 && *s + 1 < n) (*s)++;
    if (pressed & K_LEFT && *s % 2 == 1) (*s)--;
    if (pressed & K_X) B.menu = M_BUTTONS;
    if (pressed & K_Z) {
      int sp = spell_of(h->c, *s);
      if (sp && g_tension >= spell_defs[sp].cost) {
        h->sub = sp;
        if (spell_defs[sp].target == 2) choose_enemy(A_SPELL);
        else if (spell_defs[sp].target == 1) B.menu = M_PARTY, B.enemy_for = A_SPELL;
        else {
          h->action = A_SPELL;
          tension_add(-spell_defs[sp].cost);
          next_hero();
        }
      }
    }
    return;
  }
  if (B.menu == M_ITEMS) {
    int n = item_count_now();
    if (*s >= n) *s = n - 1;
    if (pressed & K_RIGHT && *s % 2 == 0 && *s + 1 < n) (*s)++;
    if (pressed & K_LEFT && *s % 2 == 1) (*s)--;
    if (pressed & K_DOWN && *s + 2 < n) *s += 2;
    if (pressed & K_UP && *s >= 2) *s -= 2;
    if (pressed & K_X) B.menu = M_BUTTONS;
    if ((pressed & K_Z) && n) {
      h->sub = sv.item[*s];
      if (item_defs[h->sub].target == 1) {
        B.menu = M_PARTY, B.enemy_for = A_ITEM;
      } else {
        h->action = A_ITEM, h->target = 0;
        /* (taken out now: the next member can't choose it too) */
        memmove(sv.item + *s, sv.item + *s + 1, NITEMS - *s - 1);
        sv.item[NITEMS - 1] = 0;
        next_hero();
      }
    }
    return;
  }
  if (B.menu == M_PARTY) {
    if (pressed & K_DOWN) *s = (*s + 1) % B.nh;
    if (pressed & K_UP) *s = (*s + B.nh - 1) % B.nh;
    if (pressed & K_X) B.menu = B.enemy_for == A_SPELL ? M_SPELLS : M_ITEMS;
    if (pressed & K_Z) {
      h->target = *s;
      h->action = B.enemy_for;
      if (h->action == A_SPELL) tension_add(-spell_defs[h->sub].cost);
      if (h->action == A_ITEM) {
        int slot = B.sel[B.turn][M_ITEMS];
        memmove(sv.item + slot, sv.item + slot + 1, NITEMS - slot - 1);
        sv.item[NITEMS - 1] = 0;
      }
      next_hero();
    }
  }
}

static void next_hero(void) {
  B.menu = M_BUTTONS;
  for (;;) {
    B.turn++;
    if (B.turn >= B.nh) {
      begin_actions();
      return;
    }
    Hero *h = &B.h[B.turn];
    if (h->action == A_NONE + 100) continue;   /* acting with Kris */
    if (sv.hp[h->c] <= 0) continue;            /* down */
    if (!can_move(h->c)) continue;
    memcpy(B.items_snap, sv.item, sizeof sv.item);
    return;
  }
}

static void turn_begin(void) {
  B.phase = P_MENU;
  B.menu = M_BUTTONS;
  B.turn = 0;
  for (int k = 0; k < B.nh; k++) B.h[k].action = A_NONE, B.h[k].points = 0;
  for (int i = 0; i < MAX_EN; i++) en[i].acting = 0;
  /* a downed member gets up a little each turn (an eighth of its HP) */
  for (int k = 0; k < B.nh; k++) {
    int c = B.h[k].c;
    if (sv.hp[c] <= 0) {
      sv.hp[c] += (sv.maxhp[c] + 7) / 8;
      if (sv.hp[c] > sv.maxhp[c]) sv.hp[c] = sv.maxhp[c];
    }
  }
  while (B.turn < B.nh && (sv.hp[B.h[B.turn].c] <= 0 || !can_move(B.h[B.turn].c))) B.turn++;
  if (B.turn >= B.nh) begin_actions();
  memcpy(B.items_snap, sv.item, sizeof sv.item);
}

/* ---------------------------------------------------------------- the actions */
static void win_check(void);

static void heal(int k, int amt) {
  int c = B.h[k].c;
  sv.hp[c] += amt;
  if (sv.hp[c] > sv.maxhp[c]) sv.hp[c] = sv.maxhp[c];
  B.dmg_show[k] = amt, B.dmg_t[k] = 30, B.dmg_kind[k] = 1;
}

static void do_item(int k) {
  Hero *h = &B.h[k];
  static const uint16_t texts[16] = {
    0, scr_spelltext_slash_scr_spelltext_gml_78_0, scr_spelltext_slash_scr_spelltext_gml_82_0,
    scr_spelltext_slash_scr_spelltext_gml_86_0, scr_spelltext_slash_scr_spelltext_gml_122_0,
    scr_spelltext_slash_scr_spelltext_gml_179_0, scr_spelltext_slash_scr_spelltext_gml_183_0,
    scr_spelltext_slash_scr_spelltext_gml_187_0, scr_spelltext_slash_scr_spelltext_gml_191_0,
    scr_spelltext_slash_scr_spelltext_gml_194_0, scr_spelltext_slash_scr_spelltext_gml_197_0,
    scr_spelltext_slash_scr_spelltext_gml_200_0, scr_spelltext_slash_scr_spelltext_gml_203_0,
    scr_spelltext_slash_scr_spelltext_gml_206_0, scr_spelltext_slash_scr_spelltext_gml_209_0,
    scr_spelltext_slash_scr_spelltext_gml_212_0};
  int it = h->sub;
  if (it <= 0 || it >= 16) return;
  battle_sayf(texts[it], char_name(h->c), NULL);
  /* scr_spell's 200 + item: what each heals */
  int tc = B.h[h->target].c;
  switch (it) {
    case 1: heal(h->target, 40); break;
    case 2: heal(h->target, sv.hp[tc] <= 0 ? sv.maxhp[tc] - sv.hp[tc] : (sv.maxhp[tc] + 1) / 2); break;
    case 5: heal(h->target, 20); break;
    case 6: for (int i = 0; i < B.nh; i++) heal(i, 160); break;
    case 7: for (int i = 0; i < B.nh; i++) heal(i, 80); break;
    case 8: heal(h->target, 70); break;
    case 9: heal(h->target, 50); break;
    case 10: heal(h->target, 4); break;
    case 11: for (int i = 0; i < B.nh; i++) heal(i, 30); break;
    case 12: heal(h->target, tc == CH_KRIS ? 10 : tc == CH_SUSIE ? 90 : 60); break;
    case 13: heal(h->target, tc == CH_KRIS ? 80 : 30); break;
    case 14: heal(h->target, 500); break;
    case 15: heal(h->target, 60); break;
    default: break;
  }
}

static void do_spell(int k) {
  Hero *h = &B.h[k];
  int bm = sv.mag[h->c], ba = sv.at[h->c] + weapon_defs[sv.weapon[h->c]].at;
  bm += weapon_defs[sv.weapon[h->c]].mag;
  switch (h->sub) {
    case 2: {   /* Heal Prayer */
      battle_sayf(scr_spelltext_slash_scr_spelltext_gml_18_0, char_name(h->c), NULL);
      heal(h->target, bm * 5);
      break;
    }
    case 3: {   /* Pacify */
      Enemy *e = target_enemy(h->target);
      if (!e) {
        for (int i = 0; i < MAX_EN && !e; i++) e = target_enemy(i);
        if (!e) return;
      }
      if (e->tired) {
        battle_sayf(scr_spelltext_slash_scr_spelltext_gml_23_0, char_name(h->c), NULL);
        if (e->def && (e->def->flags & EF_STAYS)) e->def->script(e, BS_PACIFIED, 0);
        else enemy_spare(e);
      } else if (e->mercy >= 100) {
        battle_sayf(scr_spelltext_slash_scr_spelltext_gml_36_0, char_name(h->c), NULL);
      } else {
        battle_sayf(scr_spelltext_slash_scr_spelltext_gml_33_0, char_name(h->c), NULL);
      }
      break;
    }
    case 4: {   /* Rude Buster */
      Enemy *e = target_enemy(h->target);
      for (int i = 0; i < MAX_EN && !e; i++) e = target_enemy(i);
      battle_sayf(scr_spelltext_slash_scr_spelltext_gml_43_0, char_name(h->c), NULL);
      if (!e) return;
      int dmg = bm * 5 + ba * 11 - e->df * 3;
      if (dmg < 0) dmg = 0;
      e->hp = (int16_t)(e->hp - dmg);
      e->hurt_t = 30, e->shake = 9, e->dmg_show = dmg, e->dmg_t = 40, e->dmg_kind = CH_SUSIE;
      if (e->hp <= 0) enemy_defeat_run(e);
      break;
    }
    default: break;
  }
}

static void do_spare(int k) {
  Hero *h = &B.h[k];
  Enemy *e = target_enemy(h->target);
  for (int i = 0; i < MAX_EN && !e; i++) e = target_enemy(i);
  if (!e) return;
  const char *nm = str(monster_defs[e->type].name);
  if (e->mercy >= 100) {
    battle_sayf(scr_spelltext_slash_scr_spelltext_gml_62_0, char_name(h->c), nm);
    enemy_spare(e);
  } else {
    mercy_add(e, monster_defs[e->type].sparepoint);
    if (e->tired && party_has(CH_RALSEI))
      battle_say2(subst(0, scr_spelltext_slash_scr_spelltext_gml_69_0, char_name(h->c), nm),
                  str(scr_spelltext_slash_scr_spelltext_gml_70_0));
    else
      battle_sayf(scr_spelltext_slash_scr_spelltext_gml_66_0, char_name(h->c), nm);
  }
}

static bool do_act(int k) {
  Hero *h = &B.h[k];
  Enemy *e = target_enemy(h->target);
  if (!e) return false;
  e->acting = h->sub + 1;
  if (e->def && e->def->act && e->def->act(e, h->sub + 1)) return true;
  if (h->sub == 0) {
    /* Check: its name, AT, DF (the original's per-enemy texts are their acts) */
    static char buf[96];
    snprintf(buf, sizeof buf, "* %s - AT %d DF %d/%%", str(monster_defs[e->type].name), e->at, e->df);
    battle_say2(buf, NULL);
    return true;
  }
  return false;
}

static void begin_fight(void);

static void actions_step(void) {
  if (B.w_on || script_ask(BS_STEP, 1)) return;
  while (B.act_k < B.nh) {
    int k = B.act_k++;
    switch (B.h[k].action) {
      case A_ACT: if (do_act(k)) return; break;
      case A_SPELL: do_spell(k); if (B.w_on) return; break;
      case A_ITEM: do_item(k); if (B.w_on) return; break;
      case A_SPARE: do_spare(k); if (B.w_on) return; break;
      default: break;
    }
  }
  win_check();
  if (B.phase != P_ACTIONS) return;
  begin_fight();
}

static void begin_actions(void) {
  B.phase = P_ACTIONS;
  B.act_k = 0;
  B.w_on = false;
  actions_step();
}

/* ---------------------------------------------------------------- FIGHT's bars (obj_attackpress) */
static void begin_talk(void);

static void begin_fight(void) {
  int n = 0, fighters[3], nf = 0;
  for (int k = 0; k < B.nh; k++)
    if (B.h[k].action == A_FIGHT) fighters[nf++] = k;
  if (!nf) {
    begin_talk();
    return;
  }
  /* one bolt each, in a random order, 12 or 18 frames apart (diff 10, +2 in one-button mode) */
  int off = 0, last = -1;   /* (boltxoff, lastbolt) */
  for (int i = 0; i < nf; i++) {
    int j = rnd_n(nf - i) + i, t = fighters[i];
    fighters[i] = fighters[j], fighters[j] = t;
  }
  for (int i = 0; i < nf; i++) {
    off += last;
    B.bolt_frame[n] = (int16_t)(30 + off);
    B.bolt_char[n] = (int8_t)fighters[i];
    B.bolt_alive[n] = true, B.bolt_hit[n] = false;
    n++;
    last = rnd_n(2) ? 12 : 18;
  }
  B.nbolt = n;
  B.boltx = 0;
  for (int k = 0; k < 3; k++) B.attacked[k] = B.h[k].action != A_FIGHT, B.press_t[k] = 0;
  B.phase = P_FIGHT;
  B.t = 0;
}

/* the member hits its target with its points (obj_heroparent's alarm 1) */
static void hero_attack(int k) {
  Hero *h = &B.h[k];
  Enemy *e = target_enemy(h->target);
  for (int i = 0; i < MAX_EN && !e; i++) e = target_enemy(i);
  if (!e) return;
  int at = sv.at[h->c] + weapon_defs[sv.weapon[h->c]].at + armor_defs[sv.armor1[h->c]].at + armor_defs[sv.armor2[h->c]].at;
  int dmg = (at * h->points + 10) / 20 - e->df * 3;
  if (dmg < 0) dmg = 0;
  if (h->points == 0) dmg = -1;   /* a miss */
  h->anim_t = 1;
  e->dmg_show = dmg, e->dmg_t = 40, e->dmg_kind = h->c;
  if (dmg > 0) {
    e->hp = (int16_t)(e->hp - dmg);
    e->hurt_t = 30, e->shake = 9;
    tension_add((h->points + 5) / 10);
    if (e->hp <= 0) enemy_defeat_run(e);
  }
}

static void fight_update(uint32_t pressed) {
  if (pressed & K_Z) {
    /* scr_boltcheck_onebutton: the nearest bolt within its window */
    int best = -1, top = 999;
    for (int i = 0; i < B.nbolt; i++) {
      if (!B.bolt_alive[i]) continue;
      int close = B.bolt_frame[i] - B.boltx;
      if (close < 15 && close > -5 && close < top) top = close, best = i;
    }
    if (best >= 0) {
      int p = top < 0 ? -top : top;
      int k = B.bolt_char[best];
      B.h[k].points += p == 0 ? 150 : p == 1 ? 120 : p == 2 ? 110 : 100 - p * 2;
      B.bolt_alive[best] = false, B.bolt_hit[best] = true;
      B.press_t[k] = 5;
    }
  }
  B.boltx++;
  for (int k = 0; k < 3; k++)
    if (B.press_t[k] > 0) B.press_t[k]--;
  for (int i = 0; i < B.nbolt; i++)
    if (B.bolt_alive[i] && B.bolt_frame[i] - B.boltx < -5) B.bolt_alive[i] = false;
  /* a member whose bolts are all gone attacks */
  for (int k = 0; k < B.nh; k++) {
    if (B.attacked[k]) continue;
    bool left = false;
    for (int i = 0; i < B.nbolt; i++)
      if (B.bolt_char[i] == k && B.bolt_alive[i]) left = true;
    if (!left) B.attacked[k] = true, hero_attack(k);
  }
  bool all = true;
  for (int k = 0; k < B.nh; k++) all &= B.attacked[k];
  if (all) {
    B.phase = P_FIGHT_HIT;
    B.t = 0;
  }
}

/* ---------------------------------------------------------------- the enemies' turn */
static void begin_talk(void) {
  win_check();
  if (B.phase == P_WIN || B.phase == P_DONE) return;
  B.phase = P_TALK;
  B.t = 0;
  B.talk_i = -1;
  for (int i = 0; i < MAX_EN; i++) {
    en[i].nsay = 0, en[i].pic = 0, en[i].rest = false;
    if (en[i].alive && en[i].def && en[i].def->talk) en[i].def->talk(&en[i]);
  }
}

static void begin_dodge(void) {
  bool all_rest = true;
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && !en[i].rest) all_rest = false;
  if (all_rest) {
    B.phase = P_TURN_END;
    B.t = 0, B.box_t = 0;
    return;
  }
  B.phase = P_DODGE;
  B.t = 0;
  B.box_t = 0;
  for (int c = 0; c < 4; c++) B.hp_turn[c] = sv.hp[c];
  B.hx = BOX_CX - 10, B.hy = BOX_CY - 10;
  B.turntimer = 0;
  bullets_clear();
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && en[i].def && en[i].def->turntimer > B.turntimer) B.turntimer = en[i].def->turntimer;
  if (!B.turntimer) B.turntimer = 120;
}

static void talk_update(uint32_t pressed, uint32_t held) {
  if (B.talk_wait) {
    if (B.w_on) return;
    B.talk_wait = false;
  }
  /* each enemy's bubble in turn, Z goes on (scr_blconskip) */
  if (B.talk_i < 0 || B.bw.done) {
    int i = B.talk_i + 1;
    while (i < MAX_EN && !(en[i].alive && (en[i].nsay || en[i].pic))) i++;
    if (i >= MAX_EN) {
      begin_dodge();
      return;
    }
    B.talk_i = i, B.pic_t = 0;
    writer_start(&B.bw, 0, 0, 12);
    B.bw.font = FONT_DOTUMCHE, B.bw.color = C_BLACK, B.bw.hspace = 9, B.bw.vspace = 20;
    for (int k = 0; k < en[i].nsay; k++) {
      static char bufs[MAX_EN][4][120];
      /* (a bubble's message ends on its own: Z goes on) */
      const char *s = str(en[i].say[k]);
      size_t n = strlen(s);
      if (n > 110) n = 110;
      memcpy(bufs[i][k], s, n);
      bufs[i][k][n] = 0;
      if (n < 2 || (s[n - 1] != '/' && s[n - 1] != '%')) strcat(bufs[i][k], "/");
      writer_add(&B.bw, bufs[i][k]);
    }
  }
  if (en[B.talk_i].pic) {
    /* a picture: talkmax (90) frames, Z after 15 */
    B.pic_t++;
    if (B.pic_t >= 90 || ((pressed & K_Z) && B.pic_t > 15)) B.bw.done = true;
    return;
  }
  writer_update(&B.bw, pressed, held);
}

static void soul_update(uint32_t held) {
  int sp = 4;
  int dx = ((held & K_RIGHT) ? sp : 0) - ((held & K_LEFT) ? sp : 0);
  int dy = ((held & K_DOWN) ? sp : 0) - ((held & K_UP) ? sp : 0);
  /* inside the box's border: the SOUL's 20 x 20 box */
  int lo_x = BOX_CX - BOX_IN, hi_x = BOX_CX + BOX_IN - 20, lo_y = BOX_CY - BOX_IN, hi_y = BOX_CY + BOX_IN - 20;
  B.hx = clamp_i(B.hx + dx, lo_x, hi_x);
  B.hy = clamp_i(B.hy + dy, lo_y, hi_y);
}

static void hurt_party(int k, int damage) {
  /* scr_damage: less the member's defense (3 a point), two thirds when defending; down at 0 */
  if (k == 3) {
    for (int i = 0; i < B.nh; i++) hurt_party(i, damage);
    return;
  }
  if (k < 0 || k >= B.nh) k = 0;
  if (sv.hp[B.h[k].c] <= 0) k = party_target();
  Hero *h = &B.h[k];
  int df = sv.df[h->c] + armor_defs[sv.armor1[h->c]].df + armor_defs[sv.armor2[h->c]].df +
           weapon_defs[sv.weapon[h->c]].df;
  int d = damage - df * 3;
  if (h->action == A_DEFEND) d = (2 * d + 2) / 3;
  if (d < 1) d = 1;
  int c = h->c;
  if (sv.hp[c] <= 0) {
    sv.hp[c] = (int16_t)(sv.hp[c] - (d + 2) / 4);
  } else {
    sv.hp[c] = (int16_t)(sv.hp[c] - d);
    if (sv.hp[c] <= 0) sv.hp[c] = (int16_t)(-(sv.maxhp[c] + 1) / 2);
  }
  h->hurt_t = 15;
  B.dmg_show[k] = d, B.dmg_t[k] = 30, B.dmg_kind[k] = 0;
  B.inv = 40;
  B.shake = 8;
  bool up = false;
  for (int i = 0; i < B.nh; i++) up |= sv.hp[B.h[i].c] > 0;
  if (!up) {
    B.phase = P_LOST;
    B.t = 0;
  }
}

static void bullets_update(void) {
  int sx = B.hx + 10, sy = B.hy + 10;
  bool grazing = false;
  for (int i = 0; i < MAX_BUL; i++) {
    Bullet *b = &bul[i];
    if (!b->on) continue;
    b->vx += b->ax, b->vy += b->ay;
    b->x += b->vx, b->y += b->vy;
    b->t++;
    b->img = (int16_t)(b->img + b->ispd);
    if (b->life && b->t >= b->life) {
      b->on = false;
      continue;
    }
    int bx = b->x / BFX, by = b->y / BFX;
    if (!b->keep && (bx < -80 || bx > 720 || by < -80 || by > 560)) {
      b->on = false;
      continue;
    }
    if (b->harmless || b->alpha < 128) continue;
    int dx = bx - sx, dy = by - sy;
    bool hit, graze;
    if (b->w) {
      /* a box turned by angle: the SOUL into its own axes */
      int ca = gfx_cos(b->angle), sa = gfx_sin(b->angle);
      int lx = abs_i((dx * ca + dy * sa) / 1024), ly = abs_i((dy * ca - dx * sa) / 1024);
      hit = lx < b->w + 6 && ly < b->h + 6;
      graze = lx < b->w + 26 && ly < b->h + 26;
    } else {
      int hit_r = b->r + 6, graze_r = b->r + 26, d2 = dx * dx + dy * dy;
      hit = d2 < hit_r * hit_r;
      graze = d2 < graze_r * graze_r;
    }
    if (hit) {
      if (B.inv <= 0) hurt_party(b->target, b->damage);
      if (!b->keep) {
        b->on = false;
        continue;
      }
    }
    if (graze && B.inv <= 0) {
      /* obj_grazebox: the first touch grazepoints and timepoints, then a twentieth of them a frame */
      grazing = true;
      if (!b->grazed) {
        b->grazed = 1;
        tension_add(b->gp);
        B.turntimer -= b->timep;
      } else {
        B.graze_acc += b->gp, B.time_acc += b->timep;
        tension_add(B.graze_acc / 20), B.turntimer -= B.time_acc / 20;
        B.graze_acc %= 20, B.time_acc %= 20;
      }
    }
  }
  if (grazing) B.graze_t = 1;
}

static void dodge_update(uint32_t held) {
  if (B.box_t < 15) {
    B.box_t++;
    return;
  }
  if (B.inv > 0) B.inv--;
  soul_update(held);
  int t = B.t - 16;   /* (battle_update counts B.t; the box grew in the first 15 frames) */
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && en[i].def && en[i].def->attack) en[i].def->attack(&en[i], t);
  bullets_update();
  if (B.phase != P_DODGE) return;
  if (--B.turntimer <= 0) {
    bullets_clear();
    B.phase = P_TURN_END;
    B.t = 0;
    /* (the Hathy fight: Lancer comes, the first turn, if Kris and the next lost nothing) */
    if (CM.set[BC_CLEAN_TURN] && B.enemy_turns == 0 && sv.hp[sv.chr[0]] >= B.hp_turn[sv.chr[0]] &&
        sv.hp[sv.chr[1]] >= B.hp_turn[sv.chr[1]])
      cameo_start(BC_CLEAN_TURN, P_TURN_END);
    B.enemy_turns++;
  }
}

/* ---------------------------------------------------------------- the end */
static void win_check(void) {
  if (enemies_alive()) return;
  if (B.phase == P_WIN || B.phase == P_DONE) return;
  B.phase = P_WIN;
  B.t = 0;
  int gold = B.gold + g_tension / 10;
  sv.gold += gold;
  for (int k = 0; k < B.nh; k++)
    if (sv.hp[B.h[k].c] < 1) sv.hp[B.h[k].c] = (int16_t)((sv.maxhp[B.h[k].c] + 7) / 8);
  char g[12];
  snprintf(g, sizeof g, "%d", gold);
  if (CM.vic_text) {
    battle_face(CM.vic_fc, CM.vic_fe);
    battle_sayf(CM.vic_text, "0", g);
  } else {
    battle_sayf(obj_battlecontroller_slash_Step_0_gml_40_0, "0", g);
  }
  B.result = BR_WON;
}

static void finish(void) {
  memset(&CM, 0, sizeof CM);
  B.over = true;
  B.phase = P_DONE;
  g_mode = B.saved_mode == MODE_BATTLE ? MODE_WORLD : B.saved_mode;
}

void battle_update(uint32_t pressed, uint32_t held) {
  B.t++;
  if (B.tp_vx > 0) B.tp_x += B.tp_vx--, B.tp_x = B.tp_x;
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &en[i];
    e->siner++;
    if (e->hurt_t > 0) e->hurt_t--;
    if (e->dmg_t > 0) e->dmg_t--;
    if (e->fade && e->fade < 20) e->fade++;
    if (e->shake) e->shake = e->shake > 0 ? -(e->shake - 1) : -(e->shake + 1);
    if (!e->tired && e->alive && e->hp <= e->maxhp / 3 && e->hurt_t) e->tired = true, e->comment = "(Tired)";
  }
  for (int k = 0; k < B.nh; k++) {
    if (B.h[k].hurt_t > 0) B.h[k].hurt_t--;
    if (B.dmg_t[k] > 0) B.dmg_t[k]--;
    if (B.h[k].anim_t) B.h[k].anim_t++;
  }
  if (B.shake > 0) B.shake--;
  script_ask(BS_STEP, 0);
  bool text_was = B.w_on;
  if (B.w_on && !writer_update(&B.w, B.phase == P_MENU ? 0 : pressed, B.phase == P_MENU ? 0 : held)) B.w_on = false;
  switch (B.phase) {
    case P_INTRO:
      B.bp = B.bp + ((152 - B.bp) < 40 ? (152 - B.bp + 1) / 2 : 30);
      if (B.bp >= 152) {
        B.bp = 152;
        if (scripted(EF_AMBUSH)) begin_talk();
        else turn_begin();
      }
      break;
    case P_MENU:
      (void)text_was;
      bmenu_update(pressed);
      break;
    case P_ACTIONS:
      if (!B.w_on) actions_step();
      break;
    case P_FIGHT: fight_update(pressed); break;
    case P_FIGHT_HIT:
      if (B.t > 30) begin_talk();
      break;
    case P_TALK:
      if (!B.w_on) talk_update(pressed, held);
      break;
    case P_DODGE: dodge_update(held); break;
    case P_TURN_END:
      if (B.box_t > 0) B.box_t--;
      else {
        win_check();
        if (B.phase == P_TURN_END) {
          /* the flavour text: the first enemy's */
          for (int i = 0; i < MAX_EN; i++)
            if (en[i].alive && en[i].def && en[i].def->flavor) {
              en[i].def->flavor(&en[i]);
              break;
            }
          /* (an enemy that left in its own text, as Lancer out of fuel: the battle ends on it, no spoils) */
          if (!enemies_alive()) {
            if (B.w_on) B.phase = P_WIN, B.t = 0, B.result = BR_WON;
            else win_check();
          } else if (B.phase != P_LESSON) {
            turn_begin();
          }
        }
      }
      break;
    case P_LESSON:
      if (!B.w_on) {
        turn_begin();
        if (B.lesson_after != 0xFFFF) battle_say(&B.lesson_after, 1);
      }
      break;
    case P_WIN:
      if (!B.w_on && CM.set[BC_VICTORY]) {
        cameo_start(BC_VICTORY, P_WIN);
        break;
      }
      if (!B.w_on) {
        B.bp -= 30;
        if (B.bp <= 0) finish();
      }
      break;
    case P_CAMEO: cameo_update(); break;
    case P_LOST:
      if (B.t > 30) {
        B.over = true;
        B.result = BR_LOST;
        B.phase = P_DONE;
        menu_gameover_start();
      }
      break;
    default: break;
  }
}

/* ---------------------------------------------------------------- drawing */
static int bx2(int x) { return x / 2; }

static void draw_background(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 255);
  /* obj_battleback: two grids of squares (bg_battleback1, 50 x 50), drifting */
  int s1 = (B.t / 2) % 100, s2 = B.t % 100;
  for (int y = -100 + s1; y < 480; y += 50)
    for (int x = -100 + s1; x < 640; x += 50) gfx_sprite(bg_battleback1_h, 0, bx2(x), bx2(y), 0, 128);
  for (int y = -210 - s2; y < 480; y += 50)
    for (int x = -200 - s2; x < 640; x += 50) gfx_sprite(bg_battleback1_h, 0, bx2(x), bx2(y), 0, 255);
}

static uint16_t hero_sprite(const Hero *h) {
  int c = h->c;
  bool down = sv.hp[c] <= 0;
  if (B.phase == P_WIN && B.bp < 152)
    return c == CH_KRIS ? spr_krisb_victory : c == CH_SUSIE ? spr_susieb_victory : spr_ralseib_victory;
  if (down) return c == CH_KRIS ? spr_krisb_defeat : c == CH_SUSIE ? spr_susieb_defeat : spr_ralseib_defeat;
  if (h->hurt_t) return c == CH_KRIS ? spr_krisb_hurt : c == CH_SUSIE ? spr_susieb_hurt : spr_ralseib_defeat;
  if (h->anim_t && h->anim_t < 21)
    return c == CH_KRIS ? spr_krisb_attack : c == CH_SUSIE ? spr_susieb_attack : spr_ralseib_attack;
  if (h->action == A_DEFEND && B.phase >= P_ACTIONS && B.phase <= P_TURN_END)
    return c == CH_KRIS ? spr_krisb_defend : c == CH_SUSIE ? spr_susieb_defend : spr_ralseib_defend;
  if (h->action == A_ACT && B.phase == P_ACTIONS)
    return c == CH_KRIS ? spr_krisb_act : c == CH_SUSIE ? spr_susieb_act : spr_ralseib_act;
  if (h->action == A_SPELL && B.phase == P_ACTIONS) return c == CH_SUSIE ? spr_susieb_spell : spr_ralseib_spell;
  if (h->action == A_ITEM && B.phase == P_ACTIONS)
    return c == CH_KRIS ? spr_krisb_item : c == CH_SUSIE ? spr_susieb_item : spr_ralseib_item;
  if (B.phase == P_INTRO) return c == CH_KRIS ? spr_krisb_intro : c == CH_SUSIE ? spr_susieb_idle : spr_ralseib_battleintro;
  return c == CH_KRIS ? spr_krisb_idle : c == CH_SUSIE ? spr_susieb_idle : spr_ralseib_idle;
}

static void draw_number(int n, int x, int y, uint16_t col) {
  char s[8];
  snprintf(s, sizeof s, "%d", n);
  gfx_text(FONT_MAIN, x - gfx_text_width(FONT_MAIN, s, 8) / 2, y, s, col, 255);
}

static void draw_heroes(void) {
  for (int k = 0; k < B.nh; k++) {
    const Hero *h = &B.h[k];
    if (B.hide[h->c & 3]) continue;
    uint16_t spr = hero_sprite(h);
    int fr = (spr == spr_krisb_attack || spr == spr_susieb_attack || spr == spr_ralseib_attack) ? h->anim_t / 3 : B.t / 6;
    if (B.phase == P_INTRO) fr = B.t / 2;
    gfx_sprite(spr, fr, bx2(h->x), bx2(h->y), 0, 255);
    if (B.dmg_t[k]) {
      uint16_t col = B.dmg_kind[k] ? rgb565(0x00FF00) : C_WHITE;
      draw_number(B.dmg_show[k], bx2(h->x) + 20, bx2(h->y) + 10 - (30 - B.dmg_t[k]) / 3, col);
    }
  }
}

static void draw_enemies(void) {
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &en[i];
    if (!e->type || (e->fade >= 20)) continue;
    const EnemyDef *d = e->def;
    int x = bx2(e->x), y = bx2(e->y);
    int a = e->fade ? 255 - e->fade * 12 : 255;
    if (e->killed) x += e->fade * 4;   /* runs off */
    if (d && d->draw) {
      d->draw(e, x, y);
    } else if (d) {
      uint16_t spr = e->hurt_t ? d->spr_hurt : (e->mercy >= 100 && d->spr_spared != 0xFFFF) ? d->spr_spared : d->spr_idle;
      gfx_sprite(spr, e->siner / (d->idle_div ? d->idle_div : 6), x + e->shake / 2, y, 0, (uint8_t)a);
      if (e->spared) gfx_sprite_color(spr, 0, x, y, 0, C_WHITE, (uint8_t)(a / 2));
    }
    if (e->dmg_t) {
      uint16_t col = e->dmg_kind == CH_KRIS ? rgb565(0x00FFFF) : e->dmg_kind == CH_SUSIE ? rgb565(0xFF00FF) : rgb565(0x00FF00);
      if (e->dmg_show < 0) gfx_text(FONT_MAIN, x, y + 10 - (40 - e->dmg_t) / 4, "MISS", rgb565(0xC0C0C0), 255);
      else draw_number(e->dmg_show, x + 20, y + 10 - (40 - e->dmg_t) / 4, col);
    }
  }
}

static void draw_tp(void) {
  int x = bx2(B.tp_x + 51 - 91 + 40), y = 20;
  (void)x;
  x = bx2(B.tp_x);
  if (x < -30) return;
  gfx_sprite(spr_tplogo_h, 0, x - 15, y + 15, 0, 255);
  gfx_sprite(spr_tensionbar_h, 1, x, y, 0, 255);
  int h = spr_h(spr_tensionbar_h);
  int fill = g_tension * (h - 1) / MAXTP;
  uint16_t col = g_tension >= MAXTP ? rgb565(0xFFD040) : rgb565(0xFF9F40);
  if (fill > 0) gfx_rect(x + 2, y + h - 1 - fill, spr_w(spr_tensionbar_h) - 2, fill, col, 255);
  gfx_sprite(spr_tensionbar_h, 0, x, y, 0, 255);
  char s[8];
  int pct = g_tension * 100 / MAXTP;
  if (pct < 100) {
    snprintf(s, sizeof s, "%d", pct);
    gfx_text(FONT_MAIN, x - 15, y + 35, s, C_WHITE, 255);
    gfx_text(FONT_MAIN, x - 12, y + 47, "%", C_WHITE, 255);
  } else {
    gfx_text(FONT_MAIN, x - 14, y + 35, "M", C_YELLOW, 255);
    gfx_text(FONT_MAIN, x - 12, y + 45, "A", C_YELLOW, 255);
    gfx_text(FONT_MAIN, x - 10, y + 55, "X", C_YELLOW, 255);
  }
}

static const uint16_t bt_sprs[6] = {spr_btfight_h, spr_btact_h, spr_btitem_h, spr_btspare_h, spr_btdefend_h, spr_bttech_h};

static void draw_panel(void) {
  int top = bx2(480 - B.bp);
  uint16_t bcol = rgb565(0x332033);
  gfx_rect(0, top, SCREEN_W, SCREEN_H - top, C_BLACK, 255);
  gfx_rect(0, top - 1, SCREEN_W, 1, bcol, 255);
  gfx_rect(0, top + 17, SCREEN_W, 1, bcol, 255);
  /* each member's box (scr_charbox) */
  static const int16_t chunks[4][3] = {{0, 0, 0}, {212, 0, 0}, {106, 326, 0}, {0, 212, 424}};
  for (int k = 0; k < B.nh; k++) {
    const Hero *h = &B.h[k];
    int xc = bx2(chunks[B.nh][k]);
    bool mine = B.phase == P_MENU && B.turn == k;
    int my = mine ? -16 : 0;
    uint16_t cc = char_color(h->c);
    if (mine) {
      gfx_rect(xc, top - 1 + my, 106, 1, cc, 255);
      gfx_rect(xc + 1, top + my, 104, 17, C_BLACK, 255);
      /* the buttons: FIGHT, ACT or MAGIC, ITEM, SPARE, DEFEND */
      for (int b = 0; b < 5; b++) {
        uint16_t s = b == 1 && h->c != CH_KRIS ? bt_sprs[5] : bt_sprs[b];
        bool sel = B.sel[k][M_BUTTONS] == b && B.menu == M_BUTTONS;
        gfx_sprite(s, sel ? 1 : 0, xc + 7 + b * 17, top + 2, 0, 255);
      }
    }
    int by = top + 1 + my;
    static const uint16_t heads[4] = {0, spr_headkris_h, spr_headsusie_h, spr_headralsei_h};
    int face = h->action == A_FIGHT ? 1 : h->action == A_ACT ? 6 : h->action == A_ITEM ? 4 : h->action == A_SPARE ? 5 :
               h->action == A_DEFEND ? 3 : h->action == A_SPELL ? 2 : 0;
    gfx_sprite(heads[h->c], face, xc + 6, by + 1, 0, 255);
    gfx_text(FONT_SMALL, xc + 25, by + 3, h->c == CH_KRIS ? "KRIS" : h->c == CH_SUSIE ? "SUSIE" : "RALSEI", C_WHITE, 255);
    gfx_sprite(spr_hpname_h, 0, xc + 54, by + 6, 0, 255);
    int hp = sv.hp[h->c], mx = sv.maxhp[h->c];
    uint16_t hc = hp <= 0 ? C_RED : hp * 4 <= mx ? C_YELLOW : C_WHITE;
    char s[8];
    snprintf(s, sizeof s, "%d", hp);
    gfx_text(FONT_SMALL, xc + 80 - gfx_text_width(FONT_SMALL, s, 8), by - 1, s, hc, 255);
    gfx_text(FONT_SMALL, xc + 81, by - 1, "/", hc, 255);
    snprintf(s, sizeof s, "%d", mx);
    gfx_text(FONT_SMALL, xc + 102 - gfx_text_width(FONT_SMALL, s, 8), by - 1, s, hc, 255);
    gfx_rect(xc + 64, by + 6, 38, 4, rgb565(0x800000), 255);
    if (hp > 0) gfx_rect(xc + 64, by + 6, (hp * 38 + mx - 1) / mx, 4, cc, 255);
  }
  /* what the box shows below: the text, a list, the bars */
  int ly = top + 19;
  if (B.phase == P_MENU && B.menu != M_BUTTONS) {
    Hero *h = &B.h[B.turn];
    int sel = B.sel[B.turn][B.menu];
    if (B.menu == M_ENEMY) {
      for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        int y = ly + 4 + i * 15;
        uint16_t col = e->mercy >= 100 ? C_YELLOW : e->tired ? rgb565(0x4DB3FF) : C_WHITE;
        const char *nm = str(monster_defs[e->type].name);
        gfx_text(FONT_MAIN, 40, y, nm, col, 255);
        int nw = gfx_text_width(FONT_MAIN, nm, 32);
        if (e->mercy >= 100) gfx_sprite(spr_sparestar_h, 0, 40 + nw + 10, y + 3, 0, 255);
        if (e->tired) gfx_sprite(spr_tiredmark_h, 0, 40 + nw + 20, y + 3, 0, 255);
        gfx_text(FONT_SMALL, 40 + nw + 30, y + 2, e->comment, rgb565(0x808080), 255);
        gfx_rect(255, y + 3, 40, 8, rgb565(0x800000), 255);
        gfx_rect(255, y + 3, e->hp > 0 ? e->hp * 40 / e->maxhp : 0, 8, rgb565(0x00FF00), 255);
        if (sel == i) gfx_sprite(spr_heart_h, 0, 27, y + 2, 0, 255);
      }
      gfx_text(FONT_SMALL, 255, ly - 4, "HP", C_WHITE, 255);
    } else if (B.menu == M_ACTS) {
      Enemy *e = &en[h->target];
      for (int a = 0; a < act_count(e); a++) {
        const ActDef *ad = &monster_defs[e->type].acts[a];
        int x = 15 + (a % 2) * 115, y = ly + 4 + (a / 2) * 15;
        bool ok = ad->can && g_tension >= ad->cost;
        gfx_text(FONT_MAIN, x, y, ad->name != 0xFFFF ? str(ad->name) : "", ok ? C_WHITE : rgb565(0x808080), 255);
        if (sel == a) gfx_sprite(spr_heart_h, 0, x - 12, y + 2, 0, 255);
      }
    } else if (B.menu == M_SPELLS) {
      for (int a = 0; a < nspells(h->c); a++) {
        int sp = spell_of(h->c, a);
        int x = 15 + (a % 2) * 115, y = ly + 4;
        bool ok = g_tension >= spell_defs[sp].cost;
        gfx_text(FONT_MAIN, x, y, str(spell_defs[sp].name), ok ? C_WHITE : rgb565(0x808080), 255);
        if (sel == a) {
          gfx_sprite(spr_heart_h, 0, x - 12, y + 2, 0, 255);
          gfx_text(FONT_SMALL, 250, ly + 4, str(spell_defs[sp].desc), rgb565(0x808080), 255);
          char s[12];
          snprintf(s, sizeof s, "%d%% TP", spell_defs[sp].cost * 100 / MAXTP);
          gfx_text(FONT_MAIN, 250, ly + 34, s, rgb565(0xFF9F40), 255);
        }
      }
    } else if (B.menu == M_ITEMS) {
      int n = item_count_now();
      int page = sel / 6;
      for (int a = page * 6; a < n && a < page * 6 + 6; a++) {
        int x = 15 + (a % 2) * 115, y = ly + 4 + ((a % 6) / 2) * 15;
        gfx_text(FONT_MAIN, x, y, str(item_defs[sv.item[a]].name), C_WHITE, 255);
        if (sel == a) {
          gfx_sprite(spr_heart_h, 0, x - 12, y + 2, 0, 255);
          gfx_text(FONT_SMALL, 250, ly + 4, str(item_defs[sv.item[a]].desc), rgb565(0x808080), 255);
        }
      }
    } else if (B.menu == M_PARTY) {
      for (int k = 0; k < B.nh; k++) {
        int y = ly + 4 + k * 15;
        gfx_text(FONT_MAIN, 40, y, char_name(B.h[k].c), C_WHITE, 255);
        if (sel == k) gfx_sprite(spr_heart_h, 0, 27, y + 2, 0, 255);
      }
    }
    return;
  }
  if (B.phase == P_FIGHT || B.phase == P_FIGHT_HIT) {
    /* obj_attackpress: a row for each member, the bolts sliding to the line */
    for (int k = 0; k < B.nh; k++) {
      if (B.h[k].action != A_FIGHT) continue;
      int y = ly + k * 19;
      uint16_t cc = char_color(B.h[k].c);
      gfx_frame_rect(40, y, 61, 18, 1, B.press_t[k] ? C_WHITE : cc);
      gfx_sprite(spr_pressfront_h, B.h[k].c - 1, 1, y, 0, 255);
      gfx_sprite(spr_pressspot_h, B.h[k].c - 1, 40, y, 0, 255);
    }
    for (int i = 0; i < B.nbolt; i++) {
      if (!B.bolt_alive[i]) continue;
      int k = B.bolt_char[i];
      int x = 40 + (B.bolt_frame[i] - B.boltx) * 4;
      if (x > 318) continue;
      int d = B.bolt_frame[i] - B.boltx;
      uint8_t a = d < 0 ? (uint8_t)(255 + d * 85 > 0 ? 255 + d * 85 : 0) : 255;
      gfx_sprite(spr_attackspot_h, 0, x, ly + k * 19, 0, a);
    }
    return;
  }
  if (B.w_on) writer_draw_face(&B.w), writer_draw(&B.w);
  else if (B.phase == P_MENU && B.menu == M_BUTTONS) writer_draw(&B.w);
}

static void draw_box(void) {
  if (B.phase != P_DODGE && B.phase != P_TURN_END) return;
  int t = B.box_t;
  int half = BOX_HALF * t / 15;
  int cx = bx2(BOX_CX), cy = bx2(BOX_CY), h = half / 2;
  if (h <= 0) return;
  gfx_rect(cx - h, cy - h, 2 * h, 2 * h, C_BLACK, 255);
  gfx_frame_rect(cx - h, cy - h, 2 * h, 2 * h, 2, rgb565(0x00C000));
  if (t < 15 || B.phase != P_DODGE) return;
  gfx_clip(cx - h + 2, cy - h + 2, 2 * h - 4, 2 * h - 4);
  gfx_clip_all();
  for (int i = 0; i < MAX_BUL; i++) {
    const Bullet *b = &bul[i];
    if (!b->on) continue;
    if (b->angle || b->scale != 256)
      gfx_sprite_ex(b->spr, b->img / 16, bx2(b->x / BFX), bx2(b->y / BFX), b->scale, b->scale, b->angle, 0, b->alpha);
    else
      gfx_sprite(b->spr, b->img / 16, bx2(b->x / BFX), bx2(b->y / BFX), 0, b->alpha);
  }
  /* the SOUL, blinking while invincible */
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && en[i].def && en[i].def->draw_front) en[i].def->draw_front(&en[i]);
  if (!(B.inv > 0 && (B.inv / 3) % 2)) gfx_sprite(spr_dodgeheart_h, 0, bx2(B.hx), bx2(B.hy), 0, 255);
  if (B.graze_t) gfx_sprite(spr_grazeappear_h, 0, bx2(B.hx + 10), bx2(B.hy + 10), 0, 96);
  B.graze_t = 0;
}

/* the bubble (obj_battleblcon) where the original puts it, at its own size (the text stays readable): its tail
 * on the halved spot the original's tail is, then kept on the screen and above the panel */
static void draw_bubble(void) {
  if (B.phase != P_TALK || B.talk_i < 0 || B.bw.done) return;
  Enemy *e = &en[B.talk_i];
  const EnemyDef *d = e->def;
  uint16_t spr = d->blspr ? d->blspr : spr_battleblcon_long;
  int bx = e->x + (d->blx || d->bly ? d->blx : -160), by = e->y + d->bly;
  int tipx = spr == spr_battleblcon ? 95 : 148, tipy = 40, w = spr == spr_battleblcon ? 96 : 150, h = spr_h(spr);
  int x = bx2(bx + tipx) - tipx, y = bx2(by + tipy) - tipy;
  int bottom = bx2(480 - B.bp) - 2;
  x = clamp_i(x, 2, SCREEN_W - 2 - w);
  y = clamp_i(y, 2, bottom - h);
  gfx_sprite(spr, 0, x, y, 0, 255);
  if (e->pic) {
    /* obj_heartblcon: the picture bobbing in it */
    int a = e->siner * 256 * 1000 / (8 * 6283);
    int ox = (gfx_sin(a) + (gfx_sin(a) < 0 ? -512 : 512)) / 1024, oy = (gfx_cos(a) + (gfx_cos(a) < 0 ? -512 : 512)) / 1024;
    gfx_sprite(e->pic, B.pic_t / 5, x + 15 + ox, y + 15 + oy, 0, 255);
    return;
  }
  B.bw.x = x + 5, B.bw.y = y + 5;
  writer_draw(&B.bw);
}

void battle_draw(void) {
  draw_background();
  draw_enemies();
  if (B.phase == P_CAMEO && CM.ph < 3) gfx_sprite(CM.spr[CM.when], 0, CM.x / 2, CM.y[CM.when] / 2, 0, 255);
  draw_heroes();
  script_ask(BS_DRAW, 0);
  draw_box();
  draw_bubble();
  draw_tp();
  draw_panel();
}
