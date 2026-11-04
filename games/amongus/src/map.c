/* Among Us: the map (MapBehaviour): where you are and your tasks; for impostors the sabotage map, its buttons chosen
 * with the arrows; the admin table's count of who is in each room (MapCountOverlay). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "view.h"
#include "world.h"

void hud_keys(const char *s);

/* the map's picture: 6.96 px a unit, its top-left at the ship's (-23.86, 7.23) */
#define MAP_X0 8
#define MAP_Y0 31
#define MAP_K 6.96f
static inline float mx(float x) { return MAP_X0 + (x + 23.86f) * MAP_K; }
static inline float my(float y) { return MAP_Y0 + (7.23f - y) * MAP_K; }

/* each room's middle (its areas' points) */
static float room_x[N_ROOMS], room_y[N_ROOMS];
static void rooms_init(void) {
  if (room_x[R_CAFETERIA] != 0) return;
  int n[N_ROOMS] = {0};
  for (int a = 0; a < N_AREAS; a++)
    for (int k = 0; k < AREAS[a].n; k++) {
      const V2 *v = &AREA_PTS[AREAS[a].first + k];
      room_x[AREAS[a].room] += v->x, room_y[AREAS[a].room] += v->y, n[AREAS[a].room]++;
    }
  for (int r = 0; r < N_ROOMS; r++)
    if (n[r]) room_x[r] /= n[r], room_y[r] /= n[r];
  room_y[R_CAFETERIA] += 1.2f;   /* (above the table) */
}

/* ---- the sabotage map's buttons ---- */

enum { SB_REACTOR, SB_O2, SB_LIGHTS, SB_COMMS, SB_DOORS };
typedef struct {
  uint8_t kind, room;
  int8_t dx, dy;   /* from the room's middle, in map pixels */
} SabButton;
static const SabButton SAB_BUTTONS[] = {
    {SB_REACTOR, R_REACTOR, 0, 0},   {SB_O2, R_O2, 0, 0},        {SB_LIGHTS, R_ELECTRICAL, -10, 0},
    {SB_COMMS, R_COMMS, 0, -3},      {SB_DOORS, R_ELECTRICAL, 12, 0}, {SB_DOORS, R_CAFETERIA, 0, 0},
    {SB_DOORS, R_STORAGE, 0, 0},     {SB_DOORS, R_MEDBAY, 0, 0}, {SB_DOORS, R_SECURITY, 0, 0},
    {SB_DOORS, R_UPPER_ENGINE, 0, 0}, {SB_DOORS, R_LOWER_ENGINE, 0, 0},
};
#define N_SAB_BUTTONS (int)(sizeof SAB_BUTTONS / sizeof SAB_BUTTONS[0])

static void button_xy(int b, float *x, float *y) {
  *x = mx(room_x[SAB_BUTTONS[b].room]) + SAB_BUTTONS[b].dx;
  *y = my(room_y[SAB_BUTTONS[b].room]) + SAB_BUTTONS[b].dy;
}

/* 1 ready, 0 not now; *cd the cooldown left, 0-1 */
static int button_ready(int b, float *cd) {
  const SabButton *B = &SAB_BUTTONS[b];
  *cd = 0;
  if (B->kind == SB_DOORS) {
    if (g.door_cd[B->room] > 0) *cd = g.door_cd[B->room] / 30;
    return g.door_cd[B->room] <= 0;
  }
  if (g.sab == SAB_NONE && g.sab_cd > 0) *cd = g.sab_cd / 30;
  return sabotage_ready();
}

static void press(int b) {
  float cd;
  if (!button_ready(b, &cd)) return;
  const SabButton *B = &SAB_BUTTONS[b];
  switch (B->kind) {
  case SB_REACTOR: sabotage(SAB_REACTOR); break;
  case SB_O2: sabotage(SAB_O2); break;
  case SB_LIGHTS: sabotage(SAB_LIGHTS); break;
  case SB_COMMS: sabotage(SAB_COMMS); break;
  default: close_doors(B->room); break;
  }
}

/* the button nearest the arrow's way from the selected one */
static int step(int from, float kx, float ky) {
  float fx, fy;
  button_xy(from, &fx, &fy);
  int best = from;
  float bs = 1e9f;
  for (int b = 0; b < N_SAB_BUTTONS; b++) {
    if (b == from) continue;
    float x, y;
    button_xy(b, &x, &y);
    float dx = x - fx, dy = y - fy, along = dx * kx + dy * ky, across = fabsf(dx * ky - dy * kx);
    if (along <= 2) continue;
    float s = along + across * 2.5f;
    if (s < bs) bs = s, best = b;
  }
  return best;
}

void map_open(int mode) {
  rooms_init();
  g.overlay = (uint8_t)mode;
  g.sel = 0;
  if (mode == O_SABMAP) {
    /* the button nearest the player */
    const Player *P = &g.p[g.me];
    float px = mx(P->pos.x), py = my(P->pos.y), bd = 1e9f;
    for (int b = 0; b < N_SAB_BUTTONS; b++) {
      float x, y;
      button_xy(b, &x, &y);
      float d = (x - px) * (x - px) + (y - py) * (y - py);
      if (d < bd) bd = d, g.sel = (uint8_t)b;
    }
  }
}

void map_tick(uint32_t pressed, int mode) {
  if (pressed & K_BACK) {
    g.overlay = O_NONE;
    return;
  }
  if ((pressed & K_TOOL) && mode != O_ADMIN) {
    g.overlay = O_NONE;
    return;
  }
  if (mode != O_SABMAP) return;
  if (g.sel >= N_SAB_BUTTONS) g.sel = 0;
  if (pressed & K_LEFT) g.sel = (uint8_t)step(g.sel, -1, 0);
  if (pressed & K_RIGHT) g.sel = (uint8_t)step(g.sel, 1, 0);
  if (pressed & K_UP) g.sel = (uint8_t)step(g.sel, 0, -1);
  if (pressed & K_DOWN) g.sel = (uint8_t)step(g.sel, 0, 1);
  if (pressed & (K_OK | K_EXE)) press(g.sel);
}

/* ---- drawing ---- */

/* the map's picture in a colour: its rooms the colour, its halls lighter (the picture is red, halls pink) */
static void draw_map_pic(uint32_t base, uint32_t light) {
  const Pic *p = pic(PIC_UI_MAP);
  const uint16_t *own = pic_pal(p);
  uint16_t pal[16];
  int n = pic_ncolors(p);
  for (int i = 0; i < n && i < 16; i++) {
    int gch = (own[i] >> 5) & 63;   /* pink halls have green in them, rooms none */
    pal[i] = blend(rgb565(base), rgb565(light), gch * 256 / 63);
  }
  gfx_spr(p, MAP_X0 + p->ox, MAP_Y0 + p->oy, false, pal, 256);
}

static void draw_names(void) {
  for (int r = 1; r < N_ROOMS; r++) {
    const char *s = ROOM_NAMES[r];
    if (r == R_COMMS) s = "Comms";
    int x = (int)mx(room_x[r]), y = (int)my(room_y[r]), w = gfx_text_width(FONT_SMALL, s);
    if (x + w / 2 > SCREEN_W - 3) x = SCREEN_W - 3 - w / 2;
    gfx_text(FONT_SMALL, x, y + 4, s, 0xFFFF, 0x0000, 1, 1);
  }
}

static uint16_t white_pal[256];
static void white_init(void) {
  if (white_pal[1]) return;
  gfx_mix_palette(white_pal, 0xD6E0F0, 0x8394BF, 0x95CADC);
}

/* the player's own little crewmate (the map's HerePoint) */
static void here_point(void) {
  const Player *P = &g.p[g.me];
  int x = (int)mx(P->pos.x), y = (int)my(P->pos.y + FEET_Y);
  gfx_spr(pic(PIC_VOTE_ICON), x, y, P->flip, color_pal[P->color], 256);
}

/* the yellow marks of the player's tasks, the red ones of a sabotage (MapTaskOverlay) */
static void task_marks(void) {
  const Player *P = &g.p[g.me];
  bool blink = fmodf(g.t, 0.8f) < 0.5f;
  if (g.sab && blink) {
    static const uint8_t FIX[] = {0, T_RESET_REACTOR, T_RESTORE_O2, T_FIX_LIGHTS, T_FIX_COMMS};
    for (int c = 0; c < N_CONSOLES; c++)
      if (!CONSOLES[c].kind && (CONSOLES[c].types >> FIX[g.sab] & 1))
        gfx_text(FONT_BIG, (int)mx(CONSOLES[c].x), (int)my(CONSOLES[c].y) + 6, "!", 0xF800, 0xFFFF, 1, 1);
  }
  if (P->imp || g.sab == SAB_COMMS) return;
  for (int k = 0; k < P->ntasks; k++) {
    const Task *T = &P->tasks[k];
    if (task_done(T) || T->timer > 0) continue;
    for (int c = 0; c < N_CONSOLES; c++)
      if (console_serves(c, T)) {
        gfx_text(FONT_BIG, (int)mx(CONSOLES[c].x), (int)my(CONSOLES[c].y) + 6, "!", 0xFFE0, 0x0000, 1, 1);
        if (TASK_DEFS[T->def].type != T_UPLOAD_DATA || T->step) break;
      }
  }
}

static const int SAB_ICON[] = {PIC_UI_SABOTAGE_REACTOR, PIC_UI_SABOTAGE_O2, PIC_UI_SABOTAGE_ELECTRICITY,
                               PIC_UI_SABOTAGE_COMMS, PIC_UI_SABOTAGE_DOORS};

static void sab_buttons(void) {
  for (int b = 0; b < N_SAB_BUTTONS; b++) {
    float x, y, cd;
    button_xy(b, &x, &y);
    int ready = button_ready(b, &cd);
    const Pic *p = pic(SAB_ICON[SAB_BUTTONS[b].kind]);
    if (b == g.sel) gfx_disc(x, y, 16, 0xFFE0, 256);
    gfx_spr(p, (int)x, (int)y, false, NULL, ready ? 256 : 110);
    if (cd > 0) {
      /* the cooldown: the button red from the bottom, going down */
      int h = (int)(p->h * (cd > 1 ? 1 : cd));
      int y1 = (int)y - p->oy + p->h, y0 = y1 - h;
      if (gfx_rows(y0, y1)) {
        uint16_t red[256];
        const uint16_t *own = pic_pal(p);
        for (int i = 0; i < pic_ncolors(p); i++) red[i] = blend(own[i], 0xF800, 170);
        gfx_spr_clip(p, (int)x, (int)y, false, red, 200, (int)x - p->ox, y0, (int)x - p->ox + p->w, y1);
      }
    }
  }
}

static void sab_name(char *b, int n) {
  float cd;
  const SabButton *B = &SAB_BUTTONS[g.sel < N_SAB_BUTTONS ? g.sel : 0];
  int ready = button_ready(g.sel, &cd);
  static const char *const NAMES[] = {"Reactor Meltdown", "Oxygen", "Lights", "Comms", "Doors"};
  if (B->kind == SB_DOORS) snprintf(b, (size_t)n, "%s doors", ROOM_NAMES[B->room]);
  else snprintf(b, (size_t)n, "%s", NAMES[B->kind]);
  if (!ready && cd > 0)
    snprintf(b + strlen(b), (size_t)n - strlen(b), " %ds", (int)ceilf(cd * 30));
}

/* admin: a little white crewmate for each body and living player in a room */
static void admin_counts(void) {
  if (g.sab == SAB_COMMS) {
    if (fmodf(g.t, 1.2f) < 0.8f) gfx_text(FONT_BIG, 160, 122, "Comms Sabotaged", 0xF800, 0x0000, 1, 1);
    return;
  }
  int n[N_ROOMS] = {0};
  for (int i = 0; i < g.nplayers; i++) {
    const Player *P = &g.p[i];
    if (P->alive && P->vent == NONE) n[P->room]++;
  }
  for (int b = 0; b < MAXP; b++)
    if (g.bodies[b].on) n[ship_room_at(g.bodies[b].pos)]++;
  white_init();
  const Pic *p = pic(PIC_VOTE_ICON);
  for (int r = 1; r < N_ROOMS; r++) {
    if (!n[r]) continue;
    int per = n[r] > 5 ? 5 : n[r], rows = (n[r] + 4) / 5;
    for (int k = 0; k < n[r]; k++) {
      int row = k / 5, col = k % 5, inrow = row == rows - 1 ? n[r] - row * 5 : per;
      int x = (int)mx(room_x[r]) + (col * 2 - inrow + 1) * 6, y = (int)my(room_y[r]) - 6 + (row * 2 - rows + 1) * 6;
      gfx_spr(p, x, y, false, white_pal, 256);
    }
  }
}

void map_draw(int mode) {
  rooms_init();
  view_draw();
  gfx_fill(0, gfx_y0, SCREEN_W, BAND, 0x0000, 170);
  if (mode == O_SABMAP) draw_map_pic(0xC81E1E, 0xF2A3A3);
  else draw_map_pic(0x2A62C9, 0x9CC2F2);
  if (mode != O_SABMAP) draw_names();
  if (mode == O_ADMIN) {
    admin_counts();
    hud_keys("Back close");
    return;
  }
  task_marks();
  if (mode == O_SABMAP) sab_buttons();
  here_point();
  if (g.sab == SAB_COMMS && mode == O_MAP && fmodf(g.t, 1.2f) < 0.8f)
    gfx_text(FONT_BIG, 160, 22, "Comms Sabotaged", 0xF800, 0x0000, 1, 1);
  if (mode == O_SABMAP) {
    char b[64], k[96];
    sab_name(b, sizeof b);
    gfx_text(FONT_BIG, 160, 22, b, 0xFFFF, 0x0000, 1, 1);
    snprintf(k, sizeof k, "arrows choose   OK sabotage   toolbox or Back close");
    hud_keys(k);
  } else
    hud_keys("toolbox or Back close");
}
