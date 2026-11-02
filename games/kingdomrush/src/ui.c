/* The battle's pictures: towers, what flies, the tower menu (TowerMenu.as), the counts and spells (Hud,
 * MenuBottom), the pause, notification and end screens. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"
#include "game.h"
#include "gfx.h"
#include "settings.h"
#include "special.h"
#include "strings.h"
#include "ui.h"

#define C_PANEL RGB(0x2A1C10)
#define C_EDGE RGB(0x8A6A3A)
#define C_GOLD RGB(0xFFD32A)
#define C_TEXT RGB(0xF4E6C8)
#define C_DIM RGB(0x9A8A70)

/* TowerMenuItem's icons (its frames, from 1) */
enum {
  IC_SELL = 2, IC_RALLY = 4, IC_ARCHER, IC_SOLDIER, IC_MAGE, IC_ENGINEER, IC_UP, IC_ARCANE, IC_SORCERER, IC_PALADIN,
  IC_BARBARIAN, IC_RIFLEMEN, IC_SHARPSHOOTER, IC_BFG, IC_TESLA, IC_LOCKED
};

/* TowerMenu.§_-NM§: the places around a spot (positions 1 to 11), at 0.4 */
static const int8_t place[12][2] = {{0, 23}, {-16, -13}, {16, -13}, {-16, 17}, {16, 17}, {-20, -2}, {20, -2},
                                    {0, -18}, {0, 24}, {16, 16}, {-18, -8}, {18, -8}};

const char *tower_name(int kind, int level) {
  switch (kind) {
  case TW_ARCHER: return level == 1 ? "Archer Tower" : level == 2 ? "Marksmen Tower" : "Sharpshooter Tower";
  case TW_BARRACKS: return level == 1 ? "Militia Barracks" : level == 2 ? "Footmen Barracks" : "Knights Barracks";
  case TW_MAGE: return level == 1 ? "Mage Tower" : level == 2 ? "Adept Tower" : "Wizard Tower";
  case TW_ENGINEER: return level == 1 ? "Dwarven Bombard" : level == 2 ? "Dwarven Artillery" : "Dwarven Howitzer";
  case TW_RANGER: return tower_text("TOWER_RANGERS_NAME");
  case TW_MUSKETEER: return tower_text("TOWER_MUSKETEERS_NAME");
  case TW_PALADIN: return tower_text("TOWER_PALADINS_NAME");
  case TW_BARBARIAN: return tower_text("TOWER_BARBARIANS_NAME");
  case TW_ARCANE: return tower_text("TOWER_ARCANE_NAME");
  case TW_SORCERER: return tower_text("TOWER_SORCERER_NAME");
  case TW_BFG: return tower_text("TOWER_BFG_NAME");
  case TW_TESLA: return tower_text("TOWER_TESLA_NAME");
  }
  return "";
}

static bool barracks(int k) { return k == TW_BARRACKS || k == TW_PALADIN || k == TW_BARBARIAN; }

/* ------------------------------------------------------------------ the menu */
static void item(RingItem *it, int action, int icon, int pos, int cost, bool locked, const char *name, int range) {
  memset(it, 0, sizeof *it);
  it->action = (uint8_t)action, it->icon = (uint8_t)icon, it->cost = (int16_t)cost, it->locked = locked;
  it->x = place[pos][0], it->y = place[pos][1], it->range = (int16_t)range;
  snprintf(it->name, sizeof it->name, "%s", name);
}

int ring_items(int s, RingItem *out) {
  Spot *t = &B.spot[s];
  int n = 0;
  if (t->kind == TW_NONE) {
    static const uint8_t acts[4] = {RA_ARCHER, RA_BARRACKS, RA_MAGE, RA_ENGINEER};
    static const uint8_t icons[4] = {IC_ARCHER, IC_SOLDIER, IC_MAGE, IC_ENGINEER};
    static const uint8_t kinds[4] = {TW_ARCHER, TW_BARRACKS, TW_MAGE, TW_ENGINEER};
    for (int k = 0; k < 4; k++) {
      int c = spot_cost(s, k + 1);
      item(&out[n++], acts[k], c == -2 ? IC_LOCKED : icons[k], k + 1, c < 0 ? 0 : c, c == -2, tower_name(kinds[k], 1),
           c == -2 ? 0 : tower_range(kinds[k], 1));
    }
    return n;
  }
  if (t->level < 3) {
    int c = spot_cost(s, 0);
    item(&out[n++], RA_UPGRADE, c == -2 ? IC_LOCKED : IC_UP, 7, c < 0 ? 0 : c, c == -2, tower_name(t->kind, t->level + 1),
         c == -2 ? 0 : tower_range(t->kind, t->level + 1));
  } else if (t->level == 3) {
    static const uint8_t ic[5][2] = {{0, 0}, {IC_RIFLEMEN, IC_SHARPSHOOTER}, {IC_PALADIN, IC_BARBARIAN},
                                     {IC_ARCANE, IC_SORCERER}, {IC_TESLA, IC_BFG}};
    for (int k = 1; k <= 2; k++) {
      int c = spot_cost(s, k), sk = special_kind(t->kind, k);
      item(&out[n++], k == 1 ? RA_SPECIAL1 : RA_SPECIAL2, c == -2 ? IC_LOCKED : ic[t->kind][k - 1], k, c < 0 ? 0 : c, c == -2,
           tower_name(sk, 4), c == -2 ? 0 : tower_range(sk, 4));
    }
  } else {
    Ability a[3];
    int na = special_abilities(s, a);
    for (int k = 0; k < na; k++) {
      char nm[40];
      if (a[k].level >= a[k].max) snprintf(nm, sizeof nm, "%s (top)", tower_text(a[k].key));
      else snprintf(nm, sizeof nm, "%s %d/%d", tower_text(a[k].key), a[k].level + 1, a[k].max);
      item(&out[n], RA_ABILITY0 + k, a[k].icon, a[k].pos, a[k].level >= a[k].max ? 0 : a[k].cost, false, nm, 0);
      out[n].level = a[k].level, out[n].max = a[k].max;
      n++;
    }
  }
  if (barracks(t->kind) || t->kind == TW_SORCERER)
    item(&out[n++], RA_RALLY, IC_RALLY, 9, 0, false, "Rally point", 0);
  if (!t->premade || 1) {
    char nm[40];
    snprintf(nm, sizeof nm, "Sell for %d", spot_sell_value(s));
    item(&out[n++], RA_SELL, IC_SELL, 0, 0, false, nm, 0);
  }
  return n;
}

void draw_ring(const RingItem *it, int n, int sel, int cx, int cy, int cash) {
  if (!gfx_rows(cy - 40, 80)) return;
  gfx_ellipse(cx, cy + 2, 25, 23, RGB(0x6A5030), 20);
  for (int i = 0; i < n; i++) {
    int x = cx + it[i].x, y = cy + it[i].y;
    int slot = anim_slot(AN_UI_ICONS, it[i].icon - 1, false);
    bool afford = it[i].action == RA_SELL || it[i].action == RA_RALLY || it[i].cost <= cash;
    bool done = it[i].max && it[i].level >= it[i].max;
    if (it[i].locked || !afford || done) spr_frame_tint(slot, x, y, false, RGB(0x302820), 18);
    else spr_frame(slot, x, y, false);
    if (i == sel) {   /* the chosen one: a bright ring */
      gfx_ellipse(x, y, 11, 11, RGB(0xFFE14D), 32);
      gfx_ellipse(x, y, 12, 12, RGB(0xFFE14D), 32);
      gfx_ellipse(x, y, 13, 13, RGB(0x4A3000), 32);
    }
    if (it[i].cost && !it[i].locked && !done) {
      char s[8];
      snprintf(s, sizeof s, "%d", it[i].cost);
      gfx_text_oc(FONT_SMALL, x, y + 7, s, afford ? C_GOLD : RGB(0xB0A090));
    }
  }
}

/* ------------------------------------------------------------------ towers */
typedef struct {
  int idle, shoot;
  int shooter;      /* the archers' set (AN_S_*_IDLEDOWN), or the mage's */
  int8_t sx[2], sy[2];   /* the shooters' places (at full size) */
} TowerLook;

static TowerLook look(const Spot *t) {
  switch (t->kind) {
  case TW_ARCHER:
    if (t->level == 1) return (TowerLook){AN_T_ARCHER1_ALL, -1, AN_S_ARCHER1_IDLEDOWN, {-7, 7}, {-50, -50}};
    if (t->level == 2) return (TowerLook){AN_T_ARCHER2_ALL, -1, AN_S_ARCHER2_IDLEDOWN, {-7, 6}, {-52, -52}};
    return (TowerLook){AN_T_ARCHER3_ALL, -1, AN_S_ARCHER3_IDLEDOWN, {-8, 6}, {-54, -54}};
  case TW_RANGER: return (TowerLook){AN_T_RANGER_ALL, -1, AN_S_RANGER_IDLEDOWN, {-6, 6}, {-59, -59}};
  case TW_MUSKETEER: return (TowerLook){AN_T_MUSKETEER_ALL, -1, AN_S_MUSKETEER_IDLEDOWN, {-10, 6}, {-54, -54}};
  case TW_MAGE:
    if (t->level == 1) return (TowerLook){AN_T_MAGE1_IDLE, AN_T_MAGE1_SHOOT, AN_S_MAGE_IDLEDOWN, {-8, 0}, {-49, 0}};
    if (t->level == 2) return (TowerLook){AN_T_MAGE2_IDLE, AN_T_MAGE2_SHOOT, AN_S_MAGE_IDLEDOWN, {-8, 0}, {-48, 0}};
    return (TowerLook){AN_T_MAGE3_IDLE, AN_T_MAGE3_SHOOT, AN_S_MAGE_IDLEDOWN, {-8, 0}, {-53, 0}};
  case TW_ARCANE: return (TowerLook){AN_T_ARCANE_IDLE, AN_T_ARCANE_SHOOT, AN_S_ARCANE_IDLEDOWN, {-4, 0}, {-51, 0}};
  case TW_SORCERER: return (TowerLook){AN_T_SORCERER_IDLE, AN_T_SORCERER_SHOOT, AN_S_SORCERER_IDLEDOWN, {-6, 0}, {-50, 0}};
  case TW_ENGINEER:
    if (t->level == 1) return (TowerLook){AN_T_ENG1_IDLE, AN_T_ENG1_SHOOT, -1, {0}, {0}};
    if (t->level == 2) return (TowerLook){AN_T_ENG2_IDLE, AN_T_ENG2_SHOOT, -1, {0}, {0}};
    return (TowerLook){AN_T_ENG3_IDLE, AN_T_ENG3_SHOOT, -1, {0}, {0}};
  case TW_BFG: return (TowerLook){AN_T_BFG_IDLE, AN_T_BFG_SHOOT, -1, {0}, {0}};
  case TW_TESLA: return (TowerLook){AN_T_TESLA_IDLE, AN_T_TESLA_SHOOTING, -1, {0}, {0}};
  case TW_BARRACKS:
    if (t->level == 1) return (TowerLook){AN_T_BAR1_IDLE, AN_T_BAR1_DOOR_OPEN, -1, {0}, {0}};
    if (t->level == 2) return (TowerLook){AN_T_BAR2_IDLE, AN_T_BAR2_OPEN_DOOR, -1, {0}, {0}};
    return (TowerLook){AN_T_BAR3_IDLE, AN_T_BAR3_OPEN_DOOR, -1, {0}, {0}};
  case TW_PALADIN: return (TowerLook){AN_T_PALADIN_IDLE, AN_T_PALADIN_OPEN_DOOR, -1, {0}, {0}};
  case TW_BARBARIAN: return (TowerLook){AN_T_BARBARIAN_IDLE, AN_T_BARBARIAN_OPEN_DOOR, -1, {0}, {0}};
  }
  return (TowerLook){-1, -1, -1, {0}, {0}};
}

static int base_of(int k) {
  switch (k) {
  case TW_ARCHER: case TW_RANGER: case TW_MUSKETEER: return TW_ARCHER;
  case TW_MAGE: case TW_ARCANE: case TW_SORCERER: return TW_MAGE;
  case TW_ENGINEER: case TW_BFG: case TW_TESLA: return TW_ENGINEER;
  default: return TW_BARRACKS;
  }
}

void draw_base(const Spot *t, int x, int y, int terrain) {
  if (t->kind == TW_NONE || t->kind == 0xFF || t->building) return;
  int b = base_of(t->kind);
  int an = b == TW_ARCHER ? AN_T_BASE_ARCHER_FOREST : b == TW_MAGE ? AN_T_BASE_MAGE_FOREST : AN_T_BASE_BAR_FOREST;
  spr_anim(an + terrain, 0, false, x, y, false);
}

void draw_tower(const Spot *t, int x, int y) {
  if (t->building) {
    int b = base_of(t->kind);
    spr_anim(b == TW_ARCHER ? AN_T_BUILD_ARCHER : b == TW_MAGE ? AN_T_BUILD_MAGE : AN_T_BUILD_BAR, 0, false, x, y, false);
    return;
  }
  TowerLook L = look(t);
  int b = base_of(t->kind);
  if (b == TW_BARRACKS) {
    if (t->door) spr_anim(L.shoot, t->door_t, false, x, y, false);
    else spr_anim(L.idle, 0, false, x, y, false);
    return;
  }
  bool shooting = L.shoot >= 0 && t->anim_t < anim_ticks(L.shoot) && (t->charging || t->anim_t < anim_ticks(L.shoot));
  if (L.shoot >= 0 && (t->charging || shooting) && t->shots) spr_anim(L.shoot, t->anim_t, false, x, y, false);
  else spr_anim(L.idle, 0, false, x, y, false);
  if (L.shooter < 0) return;
  /* the shooters: [idle down, idle up, shoot down, shoot up] in that order in data.bin */
  int nsh = b == TW_ARCHER ? 2 : 1;
  for (int k = 0; k < nsh; k++) {
    int sx = x + (int)lroundf(L.sx[k] * 0.4f), sy = y + (int)lroundf(L.sy[k] * 0.4f);
    int up = t->shooter_up[k], an = L.shooter + up;
    int shoot_an = L.shooter + 2 + up;
    int tt = t->shooter_t[k];
    if (t->shots && tt < anim_ticks(shoot_an) && (b != TW_ARCHER || ((t->arrows + (t->charging ? 1 : 0)) & 1) == k))
      spr_anim(shoot_an, tt, false, sx, sy, t->shooter_flip[k]);
    else
      spr_anim(an, 0, false, sx, sy, t->shooter_flip[k]);
  }
}

/* ------------------------------------------------------------------ what flies */
static void line_dir(int x, int y, float rot, int len, uint16_t shaft, uint16_t tip) {
  float c = cosf(rot), s = sinf(rot);
  int x0 = x - (int)lroundf(c * len / 2), y0 = y - (int)lroundf(s * len / 2);
  int x1 = x + (int)lroundf(c * len / 2), y1 = y + (int)lroundf(s * len / 2);
  gfx_line(x0, y0, x1, y1, shaft);
  gfx_fill(x1, y1, 1, 1, tip);
}

void draw_bullet(const Bullet *b, int x, int y) {
  switch (b->kind) {
  case BU_ARROW: case BU_ARROW_ENEMY:
    if (b->active) line_dir(x, y, b->rot, 7, b->kind == BU_ARROW ? RGB(0x8A5A2A) : RGB(0x3A2A4A), RGB(0xE0E0E0));
    break;
  case BU_AXE_ENEMY:
    spr_anim(b->active ? AN_B_AXEENEMY_IDLE : AN_B_AXEENEMY_HIT, b->anim_t, b->active, x, y, b->xs < 0);
    break;
  case BU_BOLT:
    spr_anim(b->active ? AN_B_BOLT_TRAVEL : AN_B_BOLT_HIT, b->anim_t, b->active, x, y, false);
    break;
  case BU_BOLT_SORCERER:
    spr_anim(b->active ? AN_B_BOLTSORCERER_TRAVEL : AN_B_BOLTSORCERER_HIT, b->anim_t, b->active, x, y, false);
    break;
  case BU_BOLT_NECRO:
    spr_anim(b->active ? AN_B_BOLTNECROMANCER_TRAVEL : AN_B_BOLTNECROMANCER_HIT, b->anim_t, b->active, x, y, false);
    break;
  case BU_BOMB: {
    static const int16_t idle[4] = {AN_B_BOMBCOMMON_IDLE, AN_B_BOMBCOMMON_IDLE, AN_B_BOMBDYNAMITE_IDLE, AN_B_BOMBBLACK_IDLE};
    static const int16_t hit[4] = {AN_B_BOMBCOMMON_HIT, AN_B_BOMBCOMMON_HIT, AN_B_BOMBDYNAMITE_HIT, AN_B_BOMBBLACK_HIT};
    int l = b->level > 3 ? 3 : b->level;
    int ia = b->bombs == 9 ? AN_B_BOMBBFG_IDLE : idle[l], ha = b->bombs == 9 ? AN_B_BOMBBFG_HIT : hit[l];
    if (b->active) spr_anim(ia, 0, false, x, y, false);
    else spr_anim(ha, b->anim_t, false, x, y, false);
    break;
  }
  case BU_FIREBALL:
    spr_anim(b->active ? AN_B_FIREBALL_TRAVEL : AN_B_FIREBALL_HIT, b->anim_t, b->active, x, y, false);
    break;
  case BU_MISSILE_ENEMY:
    if (b->active) spr_anim(AN_B_MISSILEENEMY_START, b->anim_t, true, x, y, b->xs < 0);
    break;
  default:
    special_draw_bullet(b, x, y);
  }
}

void draw_wave_flag(int x, int y, bool air, uint32_t frame, bool selected) {
  if (!gfx_rows(y - 14, 28)) return;
  int left = battle_wave_seconds_left();
  /* the countdown: a pie filling (WaveFlag's orange arc) */
  if (selected) gfx_ellipse_fill(x, y, 11, 11, RGB(0xFFE14D), 32);
  spr_anim(AN_UI_WAVEFLAG, 0, false, x, y, false);
  spr_anim(air ? AN_UI_WAVEICO_AIR : AN_UI_WAVEICO_GROUND, 0, false, x, y, false);
  int pulse = (frame / 4) % 2;
  gfx_ellipse(x, y, 9 + pulse, 9 + pulse, selected ? RGB(0x6A4A00) : RGB(0xFF7A1A), 28);
  (void)left;
}

/* ------------------------------------------------------------------ the left side: lives, gold, wave, spells */
static void counter(int an, int y, const char *s) {
  spr_anim(an, 0, false, 4, y, false);
  gfx_text_o(FONT_HUD, 21, y - 1, s, C_TEXT);
}

static void spell(int y, int an_idle, int an_open, int cd, int reload, char key, bool aiming) {
  int x = 8;
  if (cd < 0) {   /* locked: the stone doors */
    spr_anim(an_open, 0, false, x + 11, y + 13, false);
  } else if (cd > 0 && cd <= 32 && reload == 0) {
    spr_anim(an_open, 32 - cd, false, x + 11, y + 13, false);
  } else {
    spr_anim(an_idle, 0, false, x + 11, y + 13, false);
    if (cd > 0) {   /* recharging: dark from the top, the seconds left */
      int h = 26 * cd / (reload ? reload : 1);
      if (h > 26) h = 26;
      gfx_blend(x, y, 22, h, BLACK, 20);
      char s[8];
      snprintf(s, sizeof s, "%d", (cd + 29) / 30);
      gfx_text_oc(FONT_SMALL, x + 11, y + 7, s, WHITE);
    }
  }
  if (aiming) gfx_frame_rect(x - 2, y - 2, 26, 30, RGB(0xFFE14D));
  char k[2] = {key, 0};
  gfx_panel(x + 17, y + 18, 9, 10, RGB(0x1A0F08), C_EDGE);
  gfx_text(FONT_SMALL, x + 19, y + 18, k, C_TEXT);
}

void draw_sidebar(int aiming) {
  if (gfx_y0 >= 240) return;
  gfx_fill(0, gfx_y0, 40, BAND_H, C_PANEL);
  gfx_fill(39, gfx_y0, 1, BAND_H, C_EDGE);
  char s[16];
  snprintf(s, sizeof s, "%d", B.lives);
  counter(AN_UI_HEART, 4, s);
  snprintf(s, sizeof s, "%d", B.cash);
  counter(AN_UI_COIN, 22, s);
  snprintf(s, sizeof s, "%d/%d", B.current_wave, B.max_waves);
  spr_anim(AN_UI_SKULL, 0, false, 5, 41, false);
  gfx_text_o(FONT_SMALL, 21, 41, s, C_TEXT);
  int fc = power_fire_cooldown(), rc = power_reinf_cooldown();
  spell(60, AN_UI_PFIRE_IDLE, AN_UI_PFIRE_OPEN, fc, B.fire_open_t ? 0 : B.fire_reload, '1', aiming == 1);
  spell(94, AN_UI_PREINF_IDLE, AN_UI_PREINF_OPEN, rc, B.reinf_open_t ? 0 : B.reinf_reload, '2', aiming == 2);
  /* the next wave: ready to call, and how soon it comes */
  int left = battle_wave_seconds_left();
  if (left >= 0 && B.state == LS_NORMAL) {
    int y = 130;
    spr_anim(AN_UI_WAVEFLAG, 0, false, 20, y + 10, false);
    gfx_panel(3, y + 20, 34, 11, RGB(0x1A0F08), C_EDGE);
    gfx_text_c(FONT_SMALL, 20, y + 20, B.waiting_first ? "start" : "call", C_GOLD);
  }
}

static void info_box(const char *title, int y) {
  gfx_fill(1, y, 38, 240 - y, RGB(0x1E140B));
  gfx_fill(1, y, 38, 1, C_EDGE);
  gfx_text_box(FONT_SMALL, 3, y + 2, 35, title, C_TEXT);
}

static const char *armor_word(int a) {
  if (a == 0) return "none";
  if (a <= 30) return "low";
  if (a <= 60) return "med";
  if (a < 90) return "high";
  return "great";
}

void draw_enemy_info(const Enemy *e) {
  if (gfx_y0 + BAND_H < 166) return;
  info_box(enemy_name(e->type), 166);
  char s[24];
  snprintf(s, sizeof s, "%d", (int)e->health);
  int y = 166 + 2 + gfx_line_h(FONT_SMALL) * (gfx_text_w(FONT_SMALL, enemy_name(e->type)) > 35 ? 2 : 1);
  spr_anim(AN_UI_HEART, 0, false, 3, y + 1, false);
  gfx_text(FONT_SMALL, 18, y, s, C_TEXT);
  snprintf(s, sizeof s, "A %s", armor_word(e->armor));
  gfx_text(FONT_SMALL, 3, y + 14, s, C_DIM);
  snprintf(s, sizeof s, "M %s", armor_word(e->magic_armor));
  gfx_text(FONT_SMALL, 3, y + 25, s, C_DIM);
}

void draw_spot_info(const Spot *t) {
  if (t->kind == TW_NONE || t->kind == 0xFF || gfx_y0 + BAND_H < 166) return;
  info_box(tower_name(t->kind, t->level), 166);
}

void draw_key_line(int x0, const char *text, const RingItem *it, int cash) {
  int y = 228;
  if (!gfx_rows(y, 12)) return;
  gfx_blend(x0, y, 320 - x0, 12, BLACK, 22);
  if (it) {
    char s[64];
    int x = x0 + 4;
    x += gfx_text(FONT_SMALL, x, y + 1, it->name, it->locked ? C_DIM : WHITE) + 6;
    if (it->cost && !(it->max && it->level >= it->max)) {
      spr_anim(AN_UI_COIN, 0, false, x, y + 1, false);
      snprintf(s, sizeof s, "%d", it->cost);
      x += 18;
      x += gfx_text(FONT_SMALL, x, y + 1, s, it->cost <= cash ? C_GOLD : RGB(0xFF6040)) + 6;
    }
    gfx_text_r(FONT_SMALL, 316, y + 1, "OK buy  Back close", C_DIM);
    return;
  }
  gfx_text(FONT_SMALL, x0 + 4, y + 1, text, C_TEXT);
}

/* ------------------------------------------------------------------ pause, quit, notes, the end */
static void window(int x, int y, int w, int h) {
  gfx_panel(x, y, w, h, RGB(0x3A2614), RGB(0xC09A5A));
  gfx_frame_rect(x + 2, y + 2, w - 4, h - 4, RGB(0x6A4A26));
}

static void button(int cx, int y, const char *s, bool sel) {
  int w = gfx_text_w(FONT_HUD, s) + 16;
  gfx_panel(cx - w / 2, y, w, 18, sel ? RGB(0xE0A020) : RGB(0x5A3E20), sel ? WHITE : RGB(0xC09A5A));
  gfx_text_c(FONT_HUD, cx, y + 2, s, sel ? RGB(0x2A1A0C) : C_TEXT);
}

void draw_pause(int sel) {
  if (!gfx_rows(50, 140)) return;
  window(90, 50, 180, 140);
  gfx_text_oc(FONT_BIG, 180, 56, "Paused", C_GOLD);
  static const char *const it[4] = {"Resume", "Restart", "How to play", "Quit to map"};
  for (int i = 0; i < 4; i++) button(180, 88 + 24 * i, it[i], sel == i);
}

void draw_quit(int sel) {
  if (!gfx_rows(80, 80)) return;
  window(70, 80, 220, 76);
  gfx_text_c(FONT_TEXT, 180, 90, "Leave this battle?", C_TEXT);
  button(140, 120, "Quit", sel == 0);
  button(220, 120, "Stay", sel == 1);
}

void draw_note(int n) {
  if (!gfx_rows(40, 160)) return;
  window(60, 40, 240, 160);
  gfx_text_oc(FONT_HUD, 180, 48, note_title(n), C_GOLD);
  int h = gfx_text_box(FONT_TEXT, 72, 72, 216, note_text(n), C_TEXT);
  /* its picture under the words: the enemy walking, the spell, the tower */
  int arg = 0, kind = note_pic(n, &arg), y = 72 + h + (184 - 72 - h) * 3 / 4;
  if (kind == 1) {
    int an = enemy_anim[arg][0];
    spr_anim(an, (int)(g.ticks % (uint32_t)anim_ticks(an)), false, 180, y, false);
  } else if (kind == 2) {
    spr_anim(arg == 1 ? AN_UI_PFIRE_IDLE : AN_UI_PREINF_IDLE, 0, false, 180, y - 4, false);
  } else if (kind == 3) {
    Spot t = {0};
    t.kind = (uint8_t)arg, t.level = 4;
    draw_base(&t, 180, y, 0);
    draw_tower(&t, 180, y);
  }
  gfx_text_c(FONT_SMALL, 180, 184, "OK continue", C_DIM);
}

void draw_result(bool won, int stars, int mode, int sel, int t) {
  if (t < 10 || !gfx_rows(40, 160)) return;
  window(70, 40, 220, 160);
  gfx_text_oc(FONT_BIG, 180, 48, won ? "Victory!" : "Defeat", won ? C_GOLD : RGB(0xFF6040));
  if (won) {
    if (mode == MODE_CAMPAIGN) {
      for (int k = 0; k < 3; k++) {
        int x = 150 + 30 * k, y = 100;
        bool on = k < stars && t > 20 + 10 * k;
        if (on) spr_anim(AN_UI_STAR, 0, false, x - 7, y - 7, false);
        else spr_frame_tint(anim_slot(AN_UI_STAR, 0, false), x - 7, y - 7, false, RGB(0x302820), 24);
        if (on) gfx_ellipse(x, y, 9, 9, C_GOLD, 12);
      }
    } else
      gfx_text_c(FONT_TEXT, 180, 100, mode == MODE_HEROIC ? "Heroic Challenge won: 1 star" : "Iron Challenge won: 1 star", C_TEXT);
  } else
    gfx_text_box(FONT_TEXT, 84, 88, 192, tip((int)(B.frame / 7)), C_TEXT);
  button(140, 166, won ? "Continue" : "Restart", sel == 0);
  button(225, 166, won ? "Restart" : "Map", sel == 1);
}
