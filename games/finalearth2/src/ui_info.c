/* The Final Earth 2: the city's information windows (the original's
 * gui.infoWindows.MaterialInfoWindow, gui.PopulationInfoWindow,
 * gui.HappinessWindow, gui.HouseInformationWindow,
 * gui.WorkerDistributionWindow, gui.BirthControlWindow,
 * gui.EntertainmentInformationWindow, gui.infoWindows.FindBuildingsOfType,
 * gui.GlobalUpgradeWindow and gui.infoWindows.InvitePeopleWindow) and the
 * city panel's tooltips (gui.CityGUI). */
#include <math.h>
#include "uii.h"

static int count_class(int c) {
  int n = 0;
  for (int i = 1; i < MAX_PERMS; i++)
    if ((perms[i].flags & PF_USED) && perms[i].cls == c) n++;
  return n;
}

/* a building type's picture (gui.BuildingTypeImage): its button back and its first 20x20 */
void type_image(int x, int y, int cls) {
  const ClsText *ct = &cls_text[cls];
  uint16_t spr = pcls[cls].sprite;
  if (ct->back == BACK_OWN && spr_w(spr) >= 64) ui_spr_ex(spr, 44, 0, 20, 20, x, y, 0, 255, GFX_NO_TINT);
  else if (ct->back && ct->back != BACK_OWN) ui_spr_ex(ct->back, 0, 0, 20, 20, x, y, 0, 255, GFX_NO_TINT);
  ui_spr_ex(spr, 0, 0, 20, 20, x, y, 0, 255, GFX_NO_TINT);
}

/* ---------------------------------------------------------------- materials */
/* getProductionInfo: what the citizens made and (factories, buildings) used yesterday and today */
static void production_info(char *buf, int size, int m) {
  char t[200];
  const char *name = mat_lname[m];
  long used1 = (long)floor(stats.materialUsed[m][1]), used0 = (long)floor(stats.materialUsed[m][0]);
  str_cat(buf, size, "\n");
  str_cat(buf, size, lo_3(t, sizeof t, L_citizen_production_cost, LN(floor(stats.materialProduction[m][1])), LS(name),
                          LN(floor(stats.materialProduction[m][0]))));
  if ((used1 || used0) && m != M_FOOD) {
    str_cat(buf, size, "\n");
    str_cat(buf, size, lo_3(t, sizeof t, m == M_KNOWLEDGE ? L_operating_cost : L_factory_operating_cost, LN(used1), LS(name), LN(used0)));
  }
}

/* Eating.getFoodText */
static void food_text(char *buf, int size) {
  char t[96];
  str_cat(buf, size, L_food_description_1);
  str_cat(buf, size, "\n");
  str_cat(buf, size, L_food_description_2);
  str_cat(buf, size, " ");
  if (eating.foodRationing) str_cat(str_cat(buf, size, L_rationing_halves_this), size, " ");
  if (eating.currentFoodSaveByHerbsPct >= 0.0001f)
    str_cat(str_cat(buf, size, lo_2(t, sizeof t, L_food_description_3, LN(round(eating.currentFoodSaveByHerbsPct * 0.2 * 100)), LN(20))), size, " ");
  if (eating.totalConsumedFoodPerDay != -1) str_cat(buf, size, lo_1(t, sizeof t, L_food_description_4, LN((int)eating.totalConsumedFoodPerDay)));
}

static const char *const mat_use[M_COUNT] = {
  [M_WOOD] = L_wood_use, [M_STONE] = L_stone_use, [M_KNOWLEDGE] = L_knowledge_use, [M_MACHINEPARTS] = L_machine_parts_use,
  [M_REFINEDMETAL] = L_refined_metals_use, [M_COMPUTERCHIPS] = L_computer_chips_use, [M_GRAPHENE] = L_graphene_use,
  [M_ROCKETFUEL] = L_rocketFuel_use};

/* the tooltip of a material in the panel */
static void material_tip(int m, char *buf, int size) {
  buf[0] = 0;
  if (m == M_FOOD) {
    food_text(buf, size);
    production_info(buf, size, m);
    if (eating.foodShortage > 0.1f) {
      char t[160];
      str_cat(buf, size, "\n");
      str_cat(buf, size, lo_1(t, sizeof t, L_not_enough_food, LN(ceil(eating.foodShortage))));
    }
  } else {
    str_cat(buf, size, mat_use[m]);
    production_info(buf, size, m);
  }
}

/* MaterialInfoWindow: the production (blue) and use (red) of the last seven days */
void info_material(WinOut *w) {
  int m = w->win->arg;
  char t[64];
  win_title_icon(w, mat_title(m, t, sizeof t), mat_icon[m]);
  if (m == M_STONE) {
    /* no rock anyone can reach, or far more rock teleporters than mines */
    bool group_cit[MAX_WORLDS] = {0}, group_mine[MAX_WORLDS] = {0}, group_rock[MAX_WORLDS] = {0};
    for (int i = 1; i < MAX_CITS; i++)
      if (cits[i].flags & CF_ALIVE) group_cit[worlds[cits[i].onWorld].group % MAX_WORLDS] = true;
    int mines = 0, tps = 0;
    for (int i = 1; i < MAX_PERMS; i++) {
      Perm *p = &perms[i];
      if (!(p->flags & PF_USED)) continue;
      int g = worlds[p->world].group % MAX_WORLDS;
      if (p->cls == CLS_StoneMine) group_mine[g] = true, mines++;
      if (is_a(p, B_ROCK) || p->cls == CLS_StoneTeleporter) group_rock[g] = true;
      if (p->cls == CLS_StoneTeleporter) tps++;
    }
    bool warn = true, any_mine = false;
    for (int g = 0; g < MAX_WORLDS; g++) {
      if (group_cit[g] && group_rock[g]) warn = false;
      any_mine |= group_mine[g];
    }
    if (warn && any_mine && ncits > 0) win_para(w, FONT_STANDARD, L_stone_warning, 250, C_BLACK);
    if (tps >= 12 && (mines <= 5 || (float)mines / tps < 0.3f)) win_para(w, FONT_STANDARD, L_mines_warning, 250, C_BLACK);
  }
  const int top = 5, bar_h = 100, bar_w = 9, spacing = 4, days = 7;
  const int gw = 34 + bar_w * 2 * days + spacing * days;
  double maxv = 9;
  bool any_used = false;
  for (int d = 0; d < days; d++) {
    if (stats.materialProduction[m][d] > maxv) maxv = stats.materialProduction[m][d];
    if (stats.materialUsed[m][d] > maxv) maxv = stats.materialUsed[m][d];
    if (stats.materialUsed[m][d] > 0) any_used = true;
  }
  int zeroes = (int)floor(log(maxv) / log(10));
  int dividers = (int)floor(pow(10, zeroes));
  int q = (int)(ceil(maxv) / dividers);
  if (q < 2) dividers /= 5;
  else if (q < 4) dividers /= 2;
  if (dividers < 1) dividers = 1;
  double graph_top = floor(maxv / dividers) * dividers + dividers;
  double scaling = bar_h / graph_top;
  int naxis = (int)(graph_top / dividers) + 1;
  /* the day labels under the bars (turned), then the legend: the graph's height */
  int max_y = 0;
  for (int d = 0; d < days; d++) {
    char lab[24];
    const char *s = d == 0 ? L_today : d == 1 ? L_yesterday : lo_1(lab, sizeof lab, L_n_days_ago, LN(d));
    int yy = top + bar_h + 4 + ui_text_w(FONT_10, s);
    if (yy > max_y) max_y = yy;
  }
  int legend_y = max_y + 5;
  int height = legend_y + 10 + (any_used ? 13 : 0) + 3;
  int gx, gy;
  if (win_area(w, gw, height, &gx, &gy)) {
    int xx = gw - bar_w;
    for (int d = 0; d < days; d++) {
      int hp = (int)(scaling * stats.materialProduction[m][d]), hc = (int)(scaling * stats.materialUsed[m][d]);
      if (any_used) {
        ui_rect(gx + xx, gy + top + bar_h - hc, bar_w, hc, C_RED, 255);
        xx -= bar_w;
        ui_rect(gx + xx, gy + top + bar_h - hp, bar_w, hp, 0x001F, 255);
        xx -= bar_w + spacing;
      } else {
        xx -= bar_w;
        ui_rect(gx + xx, gy + top + bar_h - hp, bar_w * 2, hp, 0x001F, 255);
        xx -= bar_w + spacing;
      }
    }
    int guide = xx + bar_w + spacing - 2;
    ui_rect(gx + guide - 1, gy, 1, bar_h + top, C_BLACK, 255);
    ui_rect(gx + guide - 1, gy + bar_h + top, gw - guide + 3, 1, C_BLACK, 255);
    int interval = (int)floor(graph_top / (naxis - 1));
    for (int i = 0; i < naxis; i++) {
      char num[16];
      fmt_large(num, sizeof num, (double)interval * i, true);
      int yp = top + bar_h - (int)(scaling * interval * i);
      ui_rect(gx + guide - 3, gy + yp, 2, 1, C_BLACK, 255);
      /* anchor (1, 0.5): right-aligned, centred on the tick */
      ui_text(FONT_10, gx + guide - 5 - ui_text_w(FONT_10, num), gy + yp - 4 - 3, num, C_BLACK);
    }
    xx = gw;
    for (int d = 0; d < days; d++) {
      char lab[24];
      const char *s = d == 0 ? L_today : d == 1 ? L_yesterday : lo_1(lab, sizeof lab, L_n_days_ago, LN(d));
      /* turned a quarter: anchor (0, 0.5) at the bar pair's middle */
      ui_text_rot(FONT_10, gx + xx - bar_w + 4 + 3, gy + top + bar_h + 4, s, C_BLACK);
      xx -= bar_w * 2 + spacing;
    }
    ui_rect(gx, gy + legend_y, 10, 10, 0x001F, 255);
    ui_text(FONT_10, gx + 13, gy + legend_y - 3, L_production, C_BLACK);
    if (any_used) {
      ui_rect(gx, gy + legend_y + 13, 10, 10, C_RED, 255);
      ui_text(FONT_10, gx + 13, gy + legend_y + 13 - 3, L_usage, C_BLACK);
    }
  }
  if (m == M_FOOD) {
    char buf[300];
    buf[0] = 0;
    food_text(buf, sizeof buf);
    win_para(w, FONT_STANDARD, buf, gw - 10, C_BLACK);
  }
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

/* ---------------------------------------------------------------- population */
void info_population(WinOut *w) {
  char t[128];
  win_title_icon(w, L_pop_by_age, SPR_POPULATION);
  int per_age[121] = {0}, children = 0, oldest = 0;
  bool exceeding = false;
  for (int i = 1; i < MAX_CITS; i++) {
    if (!(cits[i].flags & CF_ALIVE)) continue;
    int age = (int)cit_age(&cits[i]);
    if (age > 120) per_age[120]++, exceeding = true;
    else if (age >= 0) per_age[age]++;
    if (age < 16) children++;
    if (age > oldest) oldest = age;
  }
  win_para(w, FONT_STANDARD, lo_2(t, sizeof t, L_citizens_and_children, LN(ncits), LN(children)), 250, C_BLACK);
  win_para(w, FONT_STANDARD, L_citizens_by_age_explain, 250, C_BLACK);
  int gx, gy;
  if (win_area(w, 40 + 210, 121 + 3, &gx, &gy)) {
    int maxc = 0;
    for (int a = 0; a <= 120; a++)
      if (per_age[a] > maxc) maxc = per_age[a];
    ui_rect(gx, gy, 1, 120, C_BLACK, 255);
    ui_rect(gx + 39, gy, 1, 120, C_BLACK, 255);
    ui_rect(gx, gy + 120, 40, 1, C_BLACK, 255);
    for (int a = 0; a <= 120; a++) {
      if (maxc) ui_rect(gx + 40, gy + a, (int)((float)per_age[a] / maxc * 210), 1, C_BLACK, 255);
      if (a % 10 == 0) ui_rect(gx, gy + a, 40, 1, C_BLACK, 255);
    }
    for (int i = 0; i < 12; i++) {
      char lab[16], n2[8];
      fmt_int(lab, sizeof lab, i * 10);
      if (i == 11 && exceeding) {
        str_cat(lab, sizeof lab, "+");
      } else {
        str_cat(lab, sizeof lab, "-");
        fmt_int(n2, sizeof n2, i * 10 + 10);
        str_cat(lab, sizeof lab, n2);
      }
      ui_text(FONT_10, gx + 2, gy + i * 10, lab, C_BLACK);
    }
  }
  if (ncits >= 1950) {
    /* the population limit (NumberSelectControl) */
    char num[16];
    fmt_int(num, sizeof num, babies.softPopLimit);
    win_row_begin(w);
    int tw = ui_text_w(FONT_STANDARD, L_pop_limit);
    int x0 = w->rx;
    (void)x0;
    w->rx += tw + 4 + 4;
    win_text_button(w, num, 10, BX_BUTTON, false, false);
    win_text_button(w, "+", 11, BX_BUTTON, false, false);
    win_text_button(w, "-", 12, BX_BUTTON, false, false);
    win_row_end(w);
    int lx, ly;
    win_area(w, 0, 0, &lx, &ly);
    ui_text(FONT_STANDARD, lx, ly - 15 + 1, L_pop_limit, C_BLACK);
    win_space(w, 6);
    win_para(w, FONT_STANDARD, L_pop_limit_hint, 250, C_BLACK);
  }
  const char *label = L_follow_citizen;
  int a = 1;
  win_bottom(w, &label, &a, 1, L_close, ACT_CLOSE);
}

void info_population_action(Win *wn, int a) {
  (void)wn;
  extern bool follow_mode;
  if (a == 1) {
    win_close_all();
    follow_mode = true;
    cu.zone = Z_CITY;
    notify_top(L_tap_near_citizen_to_follow, NULL, 240);
  } else if (a == 10) {
    babies.softPopLimit = 3000;
  } else if (a == 11 && babies.softPopLimit + 25 <= 10000) {
    babies.softPopLimit += 25;
  } else if (a == 12 && babies.softPopLimit - 25 >= 2000) {
    babies.softPopLimit -= 25;
  }
}

/* ---------------------------------------------------------------- happiness */
static const char *ent_name(int t) {
  static const char *const names[] = {L_night_clubs, L_pubs_restaurants, L_art, L_nature, L_gaming, L_libraries};
  return t >= 0 && t < 6 ? names[t] : "";
}

/* HappinessBoost's texts (HB_*) */
static const char *boost_text(int t) {
  static const char *const texts[] = {L_music_festival_boost, L_citizen_gratefulness_bonus, L_malfunctioned_haunted_labs};
  return t >= 0 && t < 3 ? texts[t] : "";
}

/* HappinessWindow: two columns, the texts (some indented) and their numbers, laid out first so that the numbers'
 * column starts after the widest text (addTexts: each text, then 5 pixels) */
typedef struct { char text[44], num[12]; int8_t pad_left, pad_top; } HappyRow;
#define HAPPY_ROWS 24

static void happy_add(HappyRow *r, int *n, const char *text, const char *colon, const char *num, int pad_left, int pad_top) {
  if (*n >= HAPPY_ROWS) return;
  HappyRow *h = &r[(*n)++];
  h->text[0] = 0;
  str_cat(str_cat(h->text, sizeof h->text, text), sizeof h->text, colon);
  h->num[0] = 0;
  str_cat(h->num, sizeof h->num, num);
  h->pad_left = (int8_t)pad_left, h->pad_top = (int8_t)pad_top;
}

/* a whole number as "[sign]n" ((x | 0) as the original shows it) */
static const char *happy_num(char *b, int size, const char *sign, float v) {
  b[0] = 0;
  str_cat(b, size, sign);
  fmt_int(b + strlen(b), size - (int)strlen(b), (long)v);
  return b;
}

void info_happiness(WinOut *w) {
  char n[16];
  win_title_icon(w, L_happiness, SPR_HAPPINESS);
  HappinessState *h = &happiness;
  HappyRow rows[HAPPY_ROWS];
  int nrows = 0, ent_row;
  happy_add(rows, &nrows, L_home_happiness, "", happy_num(n, sizeof n, "", h->homeHappiness), 0, 0);
  happy_add(rows, &nrows, L_sense_of_purpose, "", happy_num(n, sizeof n, "", h->purposeHappiness), 0, 0);
  ent_row = nrows;
  happy_add(rows, &nrows, L_entertainment_happiness, "", happy_num(n, sizeof n, "", h->entertainmentHappiness), 0, 0);
  static const char *const ent_names[] = {L_night_clubs, L_pubs_restaurants, L_art, L_nature, L_gaming};
  for (int i = 0; i < 5; i++) happy_add(rows, &nrows, ent_names[i], ":", happy_num(n, sizeof n, "", h->perEntertainmentType[i]), 10, 0);
  if (story_is("cityofthekey")) {
    /* the Glory of the Key and its three parts */
    const GloryOfTheKey *g = glory_of_the_key();
    happy_add(rows, &nrows, L_glory_of_the_key, ":", happy_num(n, sizeof n, "", g->gloryOfTheKeyHappiness), 0, 0);
    happy_add(rows, &nrows, L_sculptures, ":", happy_num(n, sizeof n, "", g->sculpturesHappiness), 10, 0);
    happy_add(rows, &nrows, L_luxury_housing, ":", happy_num(n, sizeof n, "", g->highEndHomeHappiness), 10, 0);
    happy_add(rows, &nrows, L_high_end_restaurants, ":", happy_num(n, sizeof n, "", g->expensiveRestaurantHappiness), 10, 0);
  }
  happy_add(rows, &nrows, L_education_happiness, "", happy_num(n, sizeof n, "", h->schoolHappiness), 0, 0);
  happy_add(rows, &nrows, L_medical_happiness, "", happy_num(n, sizeof n, "", h->medicalHappiness), 0, 0);
  if (h->foodShortageUnhappiness > 0)
    happy_add(rows, &nrows, L_food_shortage_unhappiness, "", happy_num(n, sizeof n, "-", h->foodShortageUnhappiness), 0, 0);
  if (h->foodRationingUnhappiness > 0)
    happy_add(rows, &nrows, L_food_rationing_unhappiness, "", happy_num(n, sizeof n, "-", h->foodRationingUnhappiness), 0, 0);
  if (h->overtimeUnhappinessShown > 0)
    happy_add(rows, &nrows, L_mandatory_overtime_unhappiness, "", happy_num(n, sizeof n, "-", h->overtimeUnhappinessShown), 0, 0);
  if (h->enthusiasmHappiness != 0)
    happy_add(rows, &nrows, L_new_city_enthousiasm, "", happy_num(n, sizeof n, "+", h->enthusiasmHappiness), 0, 0);
  /* the boosts running: "text:" and "+n" (or "-n"), one a line, 4 below the rest */
  for (int b = 0, nb = happiness_boosts(); b < nb; b++) {
    uint8_t t;
    float v = happiness_boost(b, &t);
    happy_add(rows, &nrows, boost_text(t), ":", happy_num(n, sizeof n, v > 0 ? "+" : "", v), 0, b == 0 ? 4 : 0);
  }
  happy_add(rows, &nrows, L_total_happiness, "", happy_num(n, sizeof n, "", h->happiness), 0, 4);
  ui_float_fmt(n, sizeof n, h->actionSpeedModifier, 2);
  happy_add(rows, &nrows, L_work_speed_modifier, "", n, 0, 0);
  /* the numbers' column: right of the widest text (the entertainment row: its text, 3, and its "..." button) */
  int dots_w = 2 + 3 + ui_text_w(FONT_STANDARD, "...") + 3 + 3;
  int col = 0;
  for (int i = 0; i < nrows; i++) {
    int tw = rows[i].pad_left + ui_text_w(FONT_STANDARD, rows[i].text) + (i == ent_row ? 3 + dots_w : 5);
    if (tw > col) col = tw;
  }
  for (int i = 0; i < nrows; i++) {
    HappyRow *r = &rows[i];
    w->y += r->pad_top;
    int x, y;
    if (i == ent_row) {
      /* the entertainment row's "..." button (a ContainerButton) to its information window */
      int tw = ui_text_w(FONT_STANDARD, r->text), bx = tw + 3;
      bool vis = win_area(w, 0, 0, &x, &y);
      bool f = win_small_button(w, bx, dots_w, 12, 1);
      if (vis) {
        ui_text(FONT_STANDARD, x, y - 2, r->text, C_BLACK);
        ui_box(x + bx, y, dots_w, 12, f ? (key_held(K_OK) ? BX_BUTTON_DOWN : BX_BUTTON_HOVER) : BX_BUTTON);
        ui_text(FONT_STANDARD, x + bx + 2 + 3, y - 3, "...", C_BLACK);
        ui_text(FONT_STANDARD, x + col, y - 2, r->num, C_BLACK);
      }
      w->y += 12;
      continue;
    }
    if (win_area(w, col + ui_text_w(FONT_STANDARD, r->num), 12, &x, &y)) {
      ui_text(FONT_STANDARD, x + r->pad_left, y - 2, r->text, C_BLACK);
      ui_text(FONT_STANDARD, x + col, y - 2, r->num, C_BLACK);
    }
  }
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

void info_happiness_action(Win *wn, int a) {
  (void)wn;
  if (a == 1) win_open(WK_ENTERTAINMENT, 0, 0);
}

/* ---------------------------------------------------------------- houses */
static const char *housing_help(char *buf, int size) {
  if (stats.peopleWithHome >= stats.people) {
    if (stats.houseCapacity == stats.peopleWithHome) return L_houses_exactly_enough;
    return lo_1(buf, size, L_house_n_cap_remaining, LN(stats.houseCapacity - stats.peopleWithHome));
  }
  int homeless = stats.people - stats.peopleWithHome;
  return homeless == 1 ? L_homeless_one : lo_1(buf, size, L_homeless_n, LN(homeless));
}

/* "x<number>: ", as wide as BitmapText measures it (a line's last space adds nothing): the "x1:3/4" of the original */
static int count_w(int number) {
  char t[16] = "x";
  fmt_int(t + 1, sizeof t - 1, number);
  str_cat(t, sizeof t, ":");
  return ui_text_w(FONT_STANDARD, t);
}

/* a table of building types: picture, "x<number>: " and a text (HouseInformationWindow, WorkerDistributionWindow) */
static int type_row(WinOut *w, int cls, int number, int num_col, const char *text) {
  char t[24] = "x";
  fmt_int(t + 1, sizeof t - 1, number);
  str_cat(t, sizeof t, ": ");
  int x, y;
  if (win_area(w, num_col + ui_text_w(FONT_STANDARD, text) + 2, 20, &x, &y)) {
    type_image(x, y, cls);
    ui_text(FONT_STANDARD, x + 20 + 2, y + 5 - 2, t, C_BLACK);
    ui_text(FONT_STANDARD, x + num_col + 2, y + 5 - 2, text, C_BLACK);
  }
  return 0;
}

void info_houses(WinOut *w) {
  char t[96];
  win_title_icon(w, L_housing_information, SPR_HOUSING);
  win_para(w, FONT_STANDARD, housing_help(t, sizeof t), 250, C_BLACK);
  if (stats.peopleWithHome < stats.people) {
    win_button(w, 0, L_find_homeless, 1, true);
    win_space(w, 6 - 4);
  }
  int num_col = 0, any = 0;
  for (int i = 0; i < nbuild_order; i++) {
    int c = build_order[i];
    if (!(pcls[c].base & B_HOUSE)) continue;
    int n = count_class(c);
    if (!n) continue;
    int wd = 20 + 2 + count_w(n);
    if (wd > num_col) num_col = wd;
  }
  for (int i = 0; i < nbuild_order; i++) {
    int c = build_order[i];
    if (!(pcls[c].base & B_HOUSE)) continue;
    int n = 0, filled = 0, cap = 0;
    for (int k = 1; k < MAX_PERMS; k++) {
      Perm *p = &perms[k];
      if (!(p->flags & PF_USED) || p->cls != c) continue;
      n++, filled += p->nresidents, cap += bld_residents(p);
    }
    if (!n) continue;
    any++;
    type_row(w, c, n, num_col, lo_2(t, sizeof t, L_residents, LN(filled), LN(cap)));
  }
  if (!any) win_para(w, FONT_STANDARD, L_there_are_no_houses, 250, C_BLACK);
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

void info_houses_action(Win *wn, int a) {
  (void)wn;
  if (a != 1) return;
  int n = 0;
  for (int i = 1; i < MAX_CITS; i++)
    if ((cits[i].flags & CF_ALIVE) && !cits[i].home) n++;
  if (!n) return;
  int pick = rnd_int(n);
  for (int i = 1; i < MAX_CITS; i++)
    if ((cits[i].flags & CF_ALIVE) && !cits[i].home && pick-- == 0) {
      cu.following = (uint16_t)i;
      win_open(WK_CITIZEN, (uint16_t)i, 0);
      break;
    }
}

/* ---------------------------------------------------------------- workers */
extern int priority_jobs[CLS_COUNT];

void info_workers(WinOut *w) {
  char t[96];
  win_title_icon(w, L_assign_workers, SPR_WORK);
  int num_col = 0, text_col = 0, any = 0;
  for (int i = 0; i < nbuild_order; i++) {
    int c = build_order[i];
    if (!(pcls[c].base & B_WORK)) continue;
    int n = 0, filled = 0, jobs = 0;
    for (int k = 1; k < MAX_PERMS; k++) {
      Perm *p = &perms[k];
      if (!(p->flags & PF_USED) || p->cls != c) continue;
      n++, filled += p->nworkers, jobs += bld_jobs(p);
    }
    if (!n) continue;
    int wd = 20 + 2 + count_w(n);
    if (wd > num_col) num_col = wd;
    lo_2(t, sizeof t, L_workers, LN(filled), LN(jobs));
    str_cat(t, sizeof t, ",");
    int tw = ui_text_w(FONT_STANDARD, t) + 2;
    if (tw > text_col) text_col = tw;
  }
  for (int i = 0; i < nbuild_order; i++) {
    int c = build_order[i];
    if (!(pcls[c].base & B_WORK)) continue;
    if (story_is("cityofthekey") && c == CLS_SecretSocietyHouse) continue;
    int n = 0, filled = 0, jobs = 0;
    for (int k = 1; k < MAX_PERMS; k++) {
      Perm *p = &perms[k];
      if (!(p->flags & PF_USED) || p->cls != c) continue;
      n++, filled += p->nworkers, jobs += bld_jobs(p);
    }
    if (!n) continue;
    any++;
    lo_2(t, sizeof t, L_workers, LN(filled), LN(jobs));
    str_cat(t, sizeof t, ",");
    int y0 = w->y;
    type_row(w, c, n, num_col, t);
    /* NumberSelectControl: the priority jobs (a button: the current workers), then up and down (spr_minibutton) */
    int prio = c < CLS_COUNT ? priority_jobs[c] : 0;
    char num[16];
    if (prio == 1000000) num[0] = 0, str_cat(num, sizeof num, L_max);
    else fmt_int(num, sizeof num, prio);
    int px = num_col + 2 + text_col + 4, nw = ui_text_w(FONT_STANDARD, num);
    nw = (nw > 20 ? nw : 20) + 6;
    w->y = y0 + 2;
    bool f0 = win_small_button(w, px, nw, 14, 100 + c);
    bool f1 = win_small_button(w, px + nw, 9, 7, 200 + c);
    w->y = y0 + 2 + 7;
    bool f2 = win_small_button(w, px + nw, 9, 7, 300 + c);
    w->y = y0;
    int bx, by;
    if (win_area(w, 0, 0, &bx, &by)) {
      ui_box(bx + px, by + 2, nw, 14, f0 ? BX_BUTTON_HOVER : BX_BUTTON);
      ui_text(FONT_STANDARD, bx + px + 3, by + 2 + 3 - 2, num, C_BLACK);
      ui_frame(SPR_MINIBUTTON, 9, f1 ? 1 : 0, bx + px + nw, by + 2, 0, 255);
      ui_spr(SPR_ARROWUP, bx + px + nw + 1, by + 2 + 1);
      ui_frame(SPR_MINIBUTTON, 9, f2 ? 1 : 0, bx + px + nw, by + 2 + 7, 0, 255);
      ui_spr(SPR_ARROWDOWN, bx + px + nw + 1, by + 2 + 7 + 1);
      ui_text(FONT_STANDARD, bx + px + nw + 9, by + 2 + 1, L_priority_text, C_BLACK);
    }
    int right = px + nw + 9 + ui_text_w(FONT_STANDARD, L_priority_text);
    if (w->maxw < right) w->maxw = right;
    w->y = y0 + 20;
  }
  if (!any) win_para(w, FONT_STANDARD, L_no_work_buildings, 250, C_BLACK);
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

void info_workers_action(Win *wn, int a) {
  (void)wn;
  int c = a % 100, op = a / 100;
  if (c >= CLS_COUNT) return;
  int jobs = 0, filled = 0;
  for (int k = 1; k < MAX_PERMS; k++) {
    Perm *p = &perms[k];
    if ((p->flags & PF_USED) && p->cls == c) jobs += bld_jobs(p), filled += p->nworkers;
  }
  int n = priority_jobs[c];
  if (op == 1) n = filled;
  else if (op == 2) n = n == 1000000 ? n : n + 1 <= jobs ? n + 1 : 1000000;
  else if (op == 3) n = n == 1000000 ? jobs : n > 0 ? n - 1 : 0;
  priority_jobs[c] = n;
  sim_flags |= SIM_JOBS_CHANGED;
  tut_flags |= TF_PRIO;
}

/* ---------------------------------------------------------------- birth control */
void info_birth(WinOut *w) {
  win_title(w, L_birth_control);
  int mode = babies.mode;
  win_activatable(w, mode == 1, 1, L_greatly_discourage_children, L_pop_stabilize, NULL, NULL);
  win_activatable(w, mode == 2, 2, L_encourage_contraception, L_slow_pop_growth, NULL, NULL);
  win_activatable(w, mode == 0, 3, L_no_special_policy, L_babies_born_normal, NULL, NULL);
  win_activatable(w, mode == 3, 4, L_encourage_parenthood, L_more_babies_born, NULL, NULL);
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

void info_birth_action(Win *wn, int a) {
  (void)wn;
  if (a == 1) babies.mode = 1, babies.targetPopAmount = ncits;
  else if (a == 2) babies.mode = 2;
  else if (a == 3) babies.mode = 0;
  else if (a == 4) babies.mode = 3;
}

/* ---------------------------------------------------------------- entertainment */
void info_entertainment(WinOut *w) {
  win_title_icon(w, cat_text[3].name, SPR_HAPPINESS);
  int shard = -1;
  bool failed = false;
  for (int i = 1; i < MAX_PERMS && !failed; i++) {
    Perm *p = &perms[i];
    if (!(p->flags & PF_USED) || !is_building(p) || !(p->flags & PF_IS_ENTERTAINMENT)) continue;
    if (shard < 0) shard = p->shard;
    else if (p->shard != shard) failed = true;
  }
  if (failed) win_para(w, FONT_STANDARD, L_entertainment_information_multiple_wg_with_citizens, 150, C_BLACK);
  /* (the original's line for citizens in more than one group of worlds is left out: its text is missing from the
   * original, which shows Localize's "!!! missing text !!!") */
  for (int t = 0; t < 5; t++) {
    win_para(w, FONT_15, ent_name(t), 250, C_BLACK);
    float total = 0;
    int shown = 0;
    for (int i = 0; i < nbuild_order; i++) {
      int c = build_order[i];
      int n = 0;
      float cap = 0;
      for (int k = 1; k < MAX_PERMS; k++) {
        Perm *p = &perms[k];
        if (!(p->flags & PF_USED) || p->cls != c || !is_building(p) || !(p->flags & PF_IS_ENTERTAINMENT)) continue;
        if (ent_type(p) != t && !(bld_secondary_entertainment_types(p) >> t & 1)) continue;
        n++, cap += bld_entertainment_capacity(p);
      }
      if (!n) continue;
      shown++;
      total += cap;
      char x[16] = "x", num[24];
      fmt_int(x + 1, sizeof x - 1, n);
      str_cat(x, sizeof x, ": ");
      fmt_num(num, sizeof num, cap);
      int bx, by;
      if (win_area(w, 20 + 2 + count_w(n) + 20 + ui_text_w(FONT_STANDARD, num), 20, &bx, &by)) {
        type_image(bx, by, c);
        ui_text(FONT_STANDARD, bx + 22, by + 3, x, C_BLACK);
        ui_text(FONT_STANDARD, bx + w->cw - ui_text_w(FONT_STANDARD, num), by + 3, num, C_BLACK);
      }
    }
    if (!shown) {
      win_para(w, FONT_STANDARD, "--", 250, C_BLACK);
    } else {
      char tl[24] = "", num[24];
      str_cat(str_cat(tl, sizeof tl, L_total), sizeof tl, ":");
      fmt_num(num, sizeof num, total);
      int bx, by;
      if (win_area(w, ui_text_w(FONT_STANDARD, tl) + 20 + ui_text_w(FONT_STANDARD, num), 20, &bx, &by)) {
        ui_text(FONT_STANDARD, bx, by + 3, tl, C_BLACK);
        ui_text(FONT_STANDARD, bx + w->cw - ui_text_w(FONT_STANDARD, num), by + 3, num, C_BLACK);
      }
    }
    win_space(w, 4);
  }
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

/* ---------------------------------------------------------------- highlight buildings */
void info_find(WinOut *w) {
  Win *wn = w->win;
  char t[96];
  if (wn->arg2) {
    /* FindBuildingsOfTypeActive: highlighting one type */
    int c = wn->arg;
    cu.highlight_cls = (uint16_t)(c + 1);
    win_title(w, lo_1(t, sizeof t, L_highlighting, LS(cls_text[c].name)));
    const char *label = L_select_any;
    int a = 1;
    win_bottom(w, &label, &a, 1, L_close, ACT_CLOSE);
    return;
  }
  cu.highlight_cls = 0;
  win_title(w, L_highlight_buildings);
  bool first = true;
  for (int cat = 0; cat < NCATEGORIES; cat++) {
    int n = 0;
    for (int i = 0; i < nbuild_order; i++) {
      int c = build_order[i];
      if (pcls[c].category != cat || cls_text[c].sub) continue;
      int st = unlock_state(c);
      bool needs_unlock = cls_text[c].hint && st == UNLOCK_UNLOCKED;
      if (st == UNLOCK_LOCKED || needs_unlock) continue;
      if (!n) {
        if (!first) win_space(w, 6);
        win_para(w, FONT_15, cat_text[cat].name, 250, C_BLACK);
        first = false;
        win_row_begin(w);
      }
      if (n && n % 9 == 0) {
        win_row_end(w);
        win_row_begin(w);
      }
      /* an ImageButton (spr_buildingbutton) with the type's picture */
      int x = w->rx;
      int bx, by;
      bool f = win_small_button(w, x, 22, 22, 10 + c);
      w->rx = x + 22;
      if (w->rh < 22) w->rh = 22;
      if (w->maxw < w->rx) w->maxw = w->rx;
      int save = w->y;
      if (win_area(w, 0, 0, &bx, &by)) {
        ui_frame(SPR_BUILDINGBUTTON, 22, f ? (key_held(K_OK) ? 2 : 1) : 0, bx + x, by, 0, 255);
        type_image(bx + x + 1, by + 1, c);
      }
      w->y = save;
      n++;
    }
    if (n) win_row_end(w);
  }
  win_space(w, 6);
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

void info_find_action(Win *wn, int a) {
  if (wn->arg2) {
    if (a == 1) {
      /* cityActions_CityHighlightBuildingsOfType.findLeastUpgraded */
      int c = wn->arg, best = 0, best_n = 1 << 30;
      for (int i = 1; i < MAX_PERMS; i++) {
        Perm *p = &perms[i];
        if (!(p->flags & PF_USED) || p->cls != c) continue;
        int n = __builtin_popcount(p->upgrades);
        if (n < best_n) best_n = n, best = i;
      }
      if (best) {
        cu.selected = (uint16_t)best;
        win_open(WK_BUILDING, (uint16_t)best, 0);
        Perm *p = P(best);
        camera_follow((float)perm_city_x(p) + 10, (float)perm_city_y(p) + 10);
      }
    }
    return;
  }
  if (a >= 10 && a < 10 + CLS_COUNT) win_open(WK_FIND, (uint16_t)(a - 10), 1);
}

/* ---------------------------------------------------------------- upgrades and policies */
static bool only_new;

void info_upgrades(WinOut *w) {
  win_title(w, L_upgrades_and_policies);
  win_checkbox(w, only_new, 1, L_only_show_unupgraded, "", NULL);
  win_space(w, 6);
  int any = 0;
  for (int i = 0; i < nbuild_order; i++) {
    int c = build_order[i];
    /* the first building of the type with anything to buy */
    Perm *p = NULL;
    for (int k = 1; k < MAX_PERMS && !p; k++)
      if ((perms[k].flags & PF_USED) && perms[k].cls == c) p = &perms[k];
    if (!p) continue;
    UpgradeOption opts[24];
    int n = upgrade_options(p, opts, 24);
    if (!n) continue;
    if (only_new) {
      bool keep = false;
      for (int k = 0; k < n && !keep; k++) {
        if (opts[k].kind == UO_POLICY) keep = true;
        else if (opts[k].kind == UO_UPGRADE) {
          float total[M_COUNT];
          bool ca;
          keep = upgrade_all_count(p, opts[k].upgrade, total, &ca) > 0;
        } else if (opts[k].kind == UO_CITY) keep = !opts[k].owned;
      }
      if (!keep) continue;
    }
    any++;
    /* the type's picture and name (Arial16) */
    int x, y;
    if (win_area(w, 23 + ui_text_w(FONT_16, cls_text[c].name), 20, &x, &y)) {
      type_image(x, y, c);
      ui_text(FONT_16, x + 23, y + 3 - 2, cls_text[c].name, C_BLACK);
    }
    win_space(w, 4);
    extern void upgrades_for_type(WinOut *w, Perm *p, int base);
    upgrades_for_type(w, p, 100 + 30 * any);
  }
  if (!any) win_para(w, FONT_STANDARD, L_no_upgrades, 250, C_BLACK);
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

void info_upgrades_action(Win *wn, int a) {
  (void)wn;
  if (a == 1) {
    only_new = !only_new;
    return;
  }
  extern void upgrades_for_type_action(int a);
  upgrades_for_type_action(a);
}

/* ---------------------------------------------------------------- invite citizens */
static const char *invite_result;

void info_invite(WinOut *w) {
  char t[128];
  win_title(w, L_invite_people);
  int limit = (stories[story.index].flags & ST_FREEPLAY) ? babies.softPopLimit : 1900;
  bool full_window = ncits + spawners_pending() + 10 < limit;
  if (full_window) {
    win_para(w, FONT_STANDARD, lo_1(t, sizeof t, L_invite_people_description, LN(10)), 250, C_BLACK);
    float cost[M_COUNT];
    invite_price(cost);
    /* displayCityAmounts: what the city has / the price */
    int x, y;
    if (win_area(w, 120, 12, &x, &y)) {
      int xx = 0;
      for (int m = 0; m < M_COUNT; m++) {
        if (cost[m] <= 0) continue;
        if (m == M_FOOD) xx -= 2;
        ui_spr(mat_icon[m], x + xx, y);
        xx += 11 + (m == M_FOOD ? -2 : 0);
        char a[24], b[16];
        fmt_large(a, sizeof a, floor(city.materials.m[m]), false);
        str_cat(a, sizeof a, "/");
        fmt_num(b, sizeof b, cost[m]);
        str_cat(a, sizeof a, b);
        xx += ui_text(FONT_STANDARD, x + xx, y - 1, a, city.materials.m[m] < cost[m] ? C_RED : C_BLACK) + 3;
      }
    }
    win_space(w, 2);
    win_button(w, 0, L_invite_people, 1, true);
    win_space(w, 0);
    win_para(w, FONT_STANDARD, invite_result ? invite_result : " ", 250, C_BLACK);
    win_space(w, 6);
  } else {
    win_para(w, FONT_STANDARD, L_invite_people_done, 250, C_BLACK);
  }
  win_bottom(w, NULL, NULL, 0, L_close, ACT_CLOSE);
}

void info_invite_action(Win *wn, int a) {
  (void)wn;
  if (a != 1) return;
  static char result[96];
  int p = invite_problem();
  if (p == 3) invite_result = L_cant_afford;
  else if (p == 2) invite_result = L_invite_not_enough_houses;
  else if (invite_citizens()) invite_result = lo_1(result, sizeof result, L_invite_people_success, LN(10));
}

/* ---------------------------------------------------------------- the panel's tooltips */
void panel_tooltip(int which, int x, int y) {
  Tip t = {0};
  char text[600], head[48];
  text[0] = 0;
  switch (which) {
  case PR_HELP: {
    extern int goal_help(char *buf, int size, const char **header);
    goal_help(text, sizeof text, &t.header);
    t.text = text;
    break;
  }
  case PR_PAUSE:
    str_cat(str_cat(str_cat(text, sizeof text, L_ok_or_alpha), sizeof text, " "), sizeof text, city.paused ? L_to_unpause : L_to_pause);
    t.header = city.paused ? L_game_paused : L_game_running;
    t.text = text;
    break;
  case PR_SPEED: {
    static const char *const names[] = {L_normal_speed, L_onepointfive_speed, L_double_speed, L_half_speed, L_threequarter_speed};
    t.header = names[cu.speed];
    t.text = L_change_game_speed_pc;
    break;
  }
  case PR_MENU: t.header = L_game_menu; break;
  case PR_HIDE: t.text = cu.panel_hidden ? L_show_extra_info : L_hide_extra_info; break;
  case PR_POP: {
    char a[64];
    int children = 0;
    for (int i = 1; i < MAX_CITS; i++)
      if ((cits[i].flags & CF_ALIVE) && cit_age(&cits[i]) <= 16) children++;
    str_cat(text, sizeof text, L_total_amount_of_citizens);
    str_cat(text, sizeof text, "\n");
    str_cat(text, sizeof text, children == 1 ? L_is_one_child : lo_1(a, sizeof a, L_are_n_children, LN(children)));
    str_cat(text, sizeof text, "\n");
    str_cat(text, sizeof text, ncits >= 1950 ? L_tap_for_age_distribution_or_pop_limit : L_tap_for_age_distribution);
    t.header = L_population, t.text = text;
    break;
  }
  case PR_HAPPY: {
    /* HappinessWindow's tooltip */
    char n[32];
    HappinessState *h = &happiness;
#define LINE(label, v) do { str_cat(text, sizeof text, "\n"); str_cat(text, sizeof text, label); str_cat(text, sizeof text, " "); fmt_int(n, sizeof n, (long)(v)); str_cat(text, sizeof text, n); } while (0)
    str_cat(text, sizeof text, L_happiness_explainer);
    LINE(L_home_happiness, h->homeHappiness);
    LINE(L_sense_of_purpose, h->purposeHappiness);
    LINE(L_entertainment_happiness, h->entertainmentHappiness);
    LINE(L_education_happiness, h->schoolHappiness);
    LINE(L_medical_happiness, h->medicalHappiness);
    str_cat(text, sizeof text, "\n");
    if (h->foodShortageUnhappiness > 0) {
      str_cat(text, sizeof text, L_food_shortage_unhappiness), str_cat(text, sizeof text, " ");
      fmt_int(n, sizeof n, (long)h->foodShortageUnhappiness), str_cat(text, sizeof text, n), str_cat(text, sizeof text, "\n");
    }
    if (h->foodRationingUnhappiness > 0) {
      str_cat(text, sizeof text, L_food_rationing_unhappiness), str_cat(text, sizeof text, " ");
      fmt_int(n, sizeof n, (long)ceil(h->foodRationingUnhappiness)), str_cat(text, sizeof text, n), str_cat(text, sizeof text, "\n");
    }
    if (h->overtimeUnhappinessShown > 0) {
      str_cat(text, sizeof text, L_mandatory_overtime_unhappiness), str_cat(text, sizeof text, " ");
      fmt_int(n, sizeof n, (long)ceil(h->overtimeUnhappinessShown)), str_cat(text, sizeof text, n), str_cat(text, sizeof text, "\n");
    }
    for (int b = 0, nb = happiness_boosts(); b < nb; b++) {
      /* the boosts: "text: n" */
      uint8_t bt;
      float v = happiness_boost(b, &bt);
      str_cat(str_cat(text, sizeof text, boost_text(bt)), sizeof text, ": ");
      fmt_int(n, sizeof n, (long)v), str_cat(text, sizeof text, n), str_cat(text, sizeof text, "\n");
    }
    if (h->enthusiasmHappiness != 0) {
      str_cat(text, sizeof text, L_new_city_enthousiasm), str_cat(text, sizeof text, " ");
      fmt_int(n, sizeof n, (long)h->enthusiasmHappiness), str_cat(text, sizeof text, n), str_cat(text, sizeof text, "\n");
    }
    char sp[16], line[96];
    ui_float_fmt(sp, sizeof sp, h->actionSpeedModifier, 2);
    str_cat(text, sizeof text, "\n");
    str_cat(text, sizeof text, lo_1(line, sizeof line, L_citizens_work_at_speed, LS(sp)));
#undef LINE
    t.header = L_happiness, t.text = text;
    break;
  }
  case PR_HOUSES: t.header = L_housing, t.text = housing_help(text, sizeof text); break;
  case PR_WORK: t.header = L_work, t.text = lo_1(text, sizeof text, L_work_tooltip, LN(stats.jobs)); break;
  default:
    if (which >= PR_MAT0) {
      int m = which - PR_MAT0;
      material_tip(m, text, sizeof text);
      t.header = mat_title(m, head, sizeof head);
      t.text = text;
    }
  }
  ui_tooltip(&t, x, y, false);
}
