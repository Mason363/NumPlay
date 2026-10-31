/* The screens: the title, the main menu, a level's start (Crazy Dave, the street, the seed chooser, "Ready... Set...
 * PLANT!"), the lawn, pausing, the rewards, the zombies eating your brains, the Almanac and Crazy Dave's shop
 * (screens.h). */
#include <string.h>
#include "draw.h"
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"
#include "save.h"
#include "screens.h"

#define C_DARK 0x0000

bool g_running = true;
int g_state;
uint32_t vis_ticks;

static uint32_t prev_keys, pressed, held;
static int repeat_t[4];
static int state_t;            /* ticks in the state */
static int sel, sel2;          /* the focus in menus */
static int confirm;            /* a question being asked: 0 none */
static int play_level;         /* the level being started or played */

static void go(int st) {
  g_state = st, state_t = 0, sel = 0, sel2 = 0, confirm = 0;
}

/* ---------------------------------------------------------------- keys */

static bool hit(uint32_t k) { return (pressed & k) != 0; }
/* arrows pressed, or held: again after a while, then often */
static uint32_t arrows(void) {
  static const uint32_t a[4] = {K_LEFT, K_RIGHT, K_UP, K_DOWN};
  uint32_t out = 0;
  for (int i = 0; i < 4; i++) {
    if (!(held & a[i])) {
      repeat_t[i] = 0;
      continue;
    }
    int t = repeat_t[i]++;
    if (t == 0 || (t >= 28 && (t - 28) % 9 == 0)) out |= a[i];
  }
  return out;
}

/* ---------------------------------------------------------------- the background */

static int loaded_bg = -1, loaded_x = -9999, loaded_sod = -1;

static void show_back(int bg) {
  if (loaded_bg == bg && loaded_x == 0) return;
  gfx_load_back(bg, 0);
  loaded_bg = bg, loaded_x = 0, loaded_sod = -1;
}

static int lawn_back(void) {
  switch (B.env) {
  case ENV_NIGHT: return BG_NIGHT;
  case ENV_POOL: return BG_POOL;
  case ENV_FOG: return BG_FOG;
  case ENV_ROOF: return BG_ROOF;
  default: return B.level <= 3 ? BG_YARD : BG_LAWN;
  }
}

void show_lawn(int sod) {
  int bg = lawn_back(), sx = 88 + (int)(cam_x * 0.4f + (cam_x < 0 ? -0.5f : 0.5f));
  if (B.level > 3 || B.env != ENV_DAY) sod = 0;
  else if (sod < 0) sod = 1000;
  if (loaded_bg == bg + (B.level == 50) * 100 && loaded_x == sx && loaded_sod == sod) return;
  gfx_load_back(bg, sx);
  if (B.level == 50)
    /* the roof by night: Dr. Zomboss comes in the dark */
    for (int i = 0; i < 256; i++) {
      uint16_t c = gfx_bg_pal[i];
      int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
      gfx_bg_pal[i] = (uint16_t)((r * 3 / 8) << 11 | (g * 3 / 8 + 2) << 5 | (b / 2 + 6 > 31 ? 31 : b / 2 + 6));
    }
  if (sod > 0) {
    int y0 = B.level == 1 ? 108 : 68, y1 = B.level == 1 ? 156 : 196;
    int x1 = 100 + sod - sx;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (x1 > 0) gfx_load_back_rows(BG_LAWN, sx, y0, y1, 0, x1);
  }
  loaded_bg = bg + (B.level == 50) * 100, loaded_x = sx, loaded_sod = sod;
}

/* ---------------------------------------------------------------- Crazy Dave */

typedef struct { int16_t id; const char *text; } Line;
static const Line talks[] = {
  {201, "Evening, neighbor."}, {202, "Those zombies just won't let up, will they?"},
  {203, "You'll notice that fighting zombies at night is different than in the day."},
  {204, "For one, you won't get any sun falling from the sky."}, {205, "You can still get sun from sunflowers, though."},
  {206, "And lucky for you, you got yourself some puff-shrooms."},
  {207, "Plant as many of those as you can and you'll be A-O-GOOD."}, {0, 0},
  {301, "Hey, you found my car key!"}, {302, "You know what that means..."},
  {303, "Crazy Dave's Twiddydinkies is open for business!"}, {304, "Have a look! See if you can't find something you like!"},
  {0, 0},
  {401, "Ever play a game called Whack-a-Zombie?"}, {402, "It's just like huntin' squirbos."},
  {403, "You know, those furry little rodents that dig holes in your lawn?"},
  {404, "Yeah, just like that, but instead of squirbos, it's zombies."},
  {405, "And instead of a shovel, it's a mallet."}, {406, "And instead of ME, it's YOU!"}, {0, 0},
  {501, "Looks like the zombies gave up attacking your front yard."}, {502, "Now they're trying your backyard."},
  {503, "And to top it all off, you can't even use your mushrooms!"}, {504, "'cause they'll fall asleep during the day!"},
  {505, "Well isn't that just dandy!"}, {0, 0},
  {701, "Funny thing about the zombie process. Sometimes it makes 'em come out real little."},
  {702, "Real little and REAL MEAN!"}, {703, "Defend your shins!"}, {0, 0},
  {801, "You know, they used to call me \"Fog Man\""}, {802, "'cause I would hang out in the fog and JUMP OUT AT PEOPLE!"},
  {803, "Ah, those were the days."}, {0, 0},
  {1101, "Why'd it get so dark all of a sudden?"}, {1102, "This can't be good."}, {1103, "Oh well, hope you survive the night!"},
  {0, 0},
  {1201, "Whoa! They even found a way onto your roof!"}, {1202, "Those are some persistent zombies, dude."},
  {1203, "You'll need to use cabbage-pults here due to your roof's angle."}, {1204, "Remember that, and you're good to go!"},
  {0, 0},
  {1304, "I have to warn you..."}, {1305, "You are going to hate this next level."},
  {1306, "Why? Because it's non-stop bungee zombie after bungee zombie."}, {1307, "I hate those confounded bungees!!!"},
  {1308, "I hate them!!"}, {1309, "Hate them with a passion!"}, {1310, "And a vengeance!"},
  {1311, "A-Whoop-a-dee-doo! Here come those idiots now!"}, {0, 0},
  {1401, "Hey, you've collected $%u in coins so far!"}, {1402, "When you collect $750, I'll sell you something really neat-o!"},
  {0, 0},
  {1501, "Hey! Wanna buy an extra seed slot?"},
  {1502, "It'll cost you $750, but you'll be able to choose 7 seeds per level instead of 6!"}, {1503, "How's that sound?"},
  {0, 0},
  {1510, "All right! And hey, when you save up to $5000, I'll sell you the 8-seed slot upgrade!"}, {1511, "Check you later!"},
  {0, 0}, {1520, "Bummer, dude!"}, {0, 0},
  {1551, "Hey! Wanna buy an extra seed slot?"},
  {1552, "It'll cost you $5000, but you'll be able to choose 8 seeds per level instead of 7!"}, {1553, "How's that sound?"},
  {0, 0}, {1560, "All right!"}, {0, 0}, {1570, "Bummer, dude!"}, {0, 0},
  {2300, "Uh oh, he's here."}, {2301, "Better get ready."}, {2302, "It's gonna be one heckuva fight!"},
  {2303, "Listen, though, I know what his weakness is:"}, {2304, "You have to hit him in the pancreas!"},
  {2305, "No, that was a different guy..."}, {2306, "You have to... um... kick him in the..."}, {2307, "Hmm..."},
  {2308, "You have to press up up down down left right..."}, {2309, "er..."}, {2310, "Wait, I remember! It's..."},
  {0, 0},
  {2400, "Greetings, neighbor!"}, {2401, "The name's Crazy Dave."}, {2402, "But you can just call me Crazy Dave."},
  {2403, "Listen, I've got a surprise for you."}, {2404, "But first, I'll need you to clear your lawn."},
  {2405, "Use your shovel and dig up those plants! (the delete key, then OK)"}, {2406, "LET THE DIGGING COMMENCE!"},
  {0, 0},
  {2410, "Ok goody, now for the surprise..."}, {2411, "We're going BOWLING!"}, {2412, "HERE, TAKE THIS WALL-NUT!!"},
  {2413, "Why'd I put a wall-nut in your hand?"}, {2414, "Because I'm CRAAAZY!!!!!"},
  {2415, "NOW GO! BOWL ME A WINNER!"}, {0, 0},
  {2500, "Me and my buddy Harvey Flaxcaster used to break vases when we were bored."},
  {2501, "Well Harvey's outta town, so you're coming with me, Harvey #2!"}, {2502, "BREAK IT LIKE YOU MEAN IT, HARVEY!"},
  {0, 0},
};
#define NTALKS (int)(sizeof talks / sizeof talks[0])

static int talk_at;            /* the line being said (its index), -1 none */
static int talk_after;         /* what follows: TA_* */
/* Wall-nut Bowling's red line: the first time, from "We're going BOWLING!" on */
bool bowling_line(void) {
  if (B.mode != MODE_BOWLING || B.tutorial == 4) return false;
  return g_state != ST_DAVE || talk_at < 0 || talks[talk_at].id < 2400 || talks[talk_at].id > 2410;
}
enum { TA_PAN, TA_SHOVEL, TA_BOWL, TA_SLOT_ASK, TA_SLOT_ASK8, TA_MENU, TA_SHOP };
static int talk_bit(int id) {
  switch (id / 100) {
  case 2: return 0; case 3: return 1; case 4: return 2; case 5: return 3; case 7: return 4; case 8: return 5;
  case 11: return 6; case 12: return 7; case 13: return 8; case 14: return 9; case 23: return 10; case 24: return 11;
  case 25: return 12;
  }
  return 31;
}
static void say(int id, int after) {
  talk_at = -1;
  for (int i = 0; i < NTALKS; i++)
    if (talks[i].id == id) talk_at = i;
  talk_after = after;
  progress.talks |= 1u << talk_bit(id);
  go(ST_DAVE);
}

/* ---------------------------------------------------------------- a level */

static uint8_t street[12];
static int nstreet;
static float pan_from, pan_to;
static int pan_then;           /* ST_CHOOSE or ST_READY */
static int sod_t = -1;
static uint8_t choose_list[N_SEEDS];
static int nchoose;

static bool usable_here(int kind) {
  if ((kind == P_LILYPAD || kind == P_TANGLEKELP || kind == P_SEASHROOM) && !is_pool()) return false;
  if (kind == P_GRAVEBUSTER && B.ngraves == 0) return false;
  if (B.env == ENV_ROOF && (kind == P_SPIKEWEED || kind == P_GRAVEBUSTER)) return false;
  return true;
}
static bool needs_choice(void) { return B.mode == MODE_LAWN && level_defs[B.level - 1].nseeds == 0; }
static int bank_slots(void) {
  int n = 0;
  for (int k = 0; k < N_SEEDS; k++) n += plant_unlocked(k) && usable_here(k);
  return n < progress.slots ? n : progress.slots;
}

static void street_zombies(void) {
  /* the level's kinds, standing about on the street */
  bool kinds[N_ZOMBIES] = {false};
  for (int w = 0; w < B.nwaves; w++)
    for (int i = 0; i < 24 && B.waves[w][i] != 0xFF; i++) kinds[B.waves[w][i]] = true;
  if (B.mode == MODE_WHACK) kinds[Z_NORMAL] = kinds[Z_CONE] = kinds[Z_BUCKET] = true;
  if (B.has_boss) kinds[Z_NORMAL] = true;
  nstreet = 0;
  for (int k = 0; k < N_ZOMBIES && nstreet < 12; k++)
    if (kinds[k] && k != Z_FLAG && k != Z_BACKUP && k != Z_IMP && k != Z_BOBSLED && k != Z_DUCKY) {
      street[nstreet++] = (uint8_t)k;
      if (nstreet < 12 && (k == Z_NORMAL || k == Z_CONE)) street[nstreet++] = (uint8_t)k;
    }
}

static void start_pan(float from, float to, int then) {
  pan_from = from, pan_to = to, pan_then = then;
  go(ST_PAN);
}

static void begin_level(int level) {
  play_level = level;
  board_start(level);
  hud.col = 2, hud.row = B.nrows / 2, hud.selected = -1, hud.shovel = false, hud.issue = 0;
  cam_x = 0, sod_t = -1;
  street_zombies();
  if (B.level <= 3 && B.env == ENV_DAY) sod_t = 0;
  /* Crazy Dave, the first time */
  int talk = 0, after = TA_PAN;
  bool first = !(progress.finished) && level >= progress.level;
  if (first) {
    if (level == 11) talk = 201;
    else if (level == 12 && progress.money < 750) talk = 1401;
    else if (level == 5) talk = 2400, after = TA_SHOVEL;
    else if (level == 15) talk = 401;
    else if (level == 21) talk = 501;
    else if (level == 25) talk = 701;
    else if (level == 31) talk = 801;
    else if (level == 35) talk = 2500;
    else if (level == 40) talk = 1101;
    else if (level == 41) talk = 1201;
    else if (level == 45) talk = 1304;
    else if (level == 50) talk = 2300;
    if (!talk && level >= 13 && level <= 24 && level != 15 && level != 20 && level != 21 && progress.declined < 2) {
      if (progress.slots == 6 && progress.money >= 750) talk = 1501, after = TA_SLOT_ASK;
      else if (level >= 16 && progress.slots == 7 && progress.money >= 5000) talk = 1551, after = TA_SLOT_ASK8;
    }
  } else if (level == 5)
    talk = 2411, after = TA_PAN;
  if (talk == 2400) {
    /* 1-5 the first time: three peashooters on the lawn for Crazy Dave's shovel lesson */
    static const uint8_t at[3][2] = {{5, 1}, {7, 2}, {6, 3}};
    for (int i = 0; i < 3; i++) plant_new(&B.plants[B.nplants++], P_PEASHOOTER, at[i][0], at[i][1]);
  }
  if (talk) say(talk, after);
  else start_pan(0, 380, needs_choice() ? ST_CHOOSE : ST_READY);
}

static void start_play(void) {
  B.phase = PH_PLAY;
  if (B.level == 1 && !progress.finished) B.tutorial = 1;
  if (B.level == 2 && !progress.finished) B.tutorial = 2, B.wave_in = 5000;
  if (B.mode != MODE_WHACK && (progress.rakes > 0) && B.mode != MODE_VASE && !B.has_boss) {
    /* the rake waits in a lane */
    int rows[MAX_ROWS], n = 0;
    for (int r = 0; r < B.nrows; r++)
      if ((B.rows >> r & 1) && !water_row(r)) rows[n++] = r;
    if (n) B.rake.on = 1, B.rake.row = (uint8_t)rows[irand(n)], B.rake.col = 7, B.rake.age = -1, progress.rakes--;
  }
  go(ST_PLAY);
}

/* the shovel lesson of level 1-5: the three peashooters to dig up, the lawn still till then (board_tick) */
static void shovel_lesson(void) {
  progress.items |= ITEM_SHOVEL;
  B.tutorial = 4;
  B.phase = PH_PLAY;
  hud.shovel = true;
  go(ST_PLAY);
}

static void finish_level(void) {
  /* the level won: the next one opens */
  const LevelDef *d = &level_defs[B.level - 1];
  if (B.level >= progress.level) {
    if (d->reward == REWARD_SHOVEL) progress.items |= ITEM_SHOVEL;
    if (d->reward == REWARD_ALMANAC) progress.items |= ITEM_ALMANAC;
    if (d->reward == REWARD_KEYS) progress.items |= ITEM_SHOP;
    if (d->reward == REWARD_MONEY) progress.money += 500;
    if (d->reward == REWARD_TROPHY) progress.items |= ITEM_TROPHY, progress.finished++;
    progress.level = B.level < N_LEVELS ? (uint8_t)(B.level + 1) : 1;
  } else if (d->reward == REWARD_MONEY)
    progress.money += 250;
  if (B.level == N_LEVELS && !(progress.items & ITEM_TROPHY)) progress.items |= ITEM_TROPHY;
  board_forget();
  progress_save();
}

/* ---------------------------------------------------------------- the lawn */

static void play_input(void) {
  uint32_t a = arrows();
  if (a & K_LEFT && hud.col > 0) hud.col--;
  if (a & K_RIGHT && hud.col < COLS - 1) hud.col++;
  if (a & K_UP && hud.row > 0) hud.row--;
  if (a & K_DOWN && hud.row < B.nrows - 1) hud.row++;
  /* sun and coins under the cursor are picked up */
  float cx = cell_x(hud.col), cy = row_y(hud.row, cx) - 30;
  if (board_collect(cx, cy, 52)) {
    if (B.tutorial == 1 && B.level == 1) {}
  }
  for (int n = 1; n <= 10 && B.tutorial != 4; n++) {
    if (!hit(K_DIGIT(n))) continue;
    int slot = n - 1;
    hud.shovel = false;
    if (has_conveyor() ? slot < B.npackets : slot < B.nseeds && B.mode != MODE_VASE) {
      if (hud.selected == slot) hud.selected = -1;
      else if (!has_conveyor() && B.sun < plant_cost(B.seeds[slot])) hud.issue = ISSUE_SUN, hud.issue_t = (int)vis_ticks;
      else if (!has_conveyor() && B.cooldown[slot] > 0) hud.issue = ISSUE_RECHARGE, hud.issue_t = (int)vis_ticks;
      else hud.selected = slot;
    }
  }
  if (hit(K_SHOVEL) && (progress.items & ITEM_SHOVEL) && B.mode != MODE_WHACK && B.mode != MODE_VASE)
    hud.shovel = !hud.shovel, hud.selected = -1;
  if (hit(K_OK)) {
    int col = hud.col, row = hud.row;
    if (hud.shovel) {
      if (board_dig(col, row) && B.tutorial != 4) hud.shovel = false;
    } else if (hud.selected >= 0) {
      int kind = B.mode == MODE_VASE ? B.drops[hud.selected].kind : seed_kind(hud.selected);
      int issue = plant_issue(kind, col, row);
      if (!issue && !has_conveyor() && B.mode != MODE_VASE) {
        if (B.sun < plant_cost(kind)) issue = ISSUE_SUN;
        else if (B.cooldown[hud.selected] > 0) issue = ISSUE_RECHARGE;
      }
      if (issue) hud.issue = issue, hud.issue_t = (int)vis_ticks;
      else if (board_plant(hud.selected, col, row)) hud.selected = -1;
    } else if (B.mode == MODE_WHACK) {
      board_whack(cx, row_y(row, cx) - 40);
    } else if (B.mode == MODE_VASE) {
      for (int i = 0; i < B.ndrops; i++)
        if (B.drops[i].col == col && B.drops[i].row == row) {
          hud.selected = i;
          if (B.drops[i].times == 0 && B.drops[i].left < 300) B.drops[i].left = 300;
          B.drops[i].times++;
          break;
        }
      if (hud.selected < 0) board_break_vase(col, row);
    }
  }
  if (B.mode == MODE_VASE && hud.selected >= B.ndrops) hud.selected = -1;
  if (has_conveyor() && hud.selected >= B.npackets) hud.selected = -1;
}

/* ---------------------------------------------------------------- the frame */

void game_init(void) {
  save_restore_copy();
  progress_load();
  go(ST_TITLE);
}

static void quit_saving(void) {
  if (g_state == ST_PLAY || g_state == ST_PAUSE || (g_state == ST_WON)) {
    if (B.phase == PH_PLAY) board_save();
  }
  progress_save();
  g_running = false;
}

static int menu_items(int *items) {
  int n = 0;
  items[n++] = 0;                                  /* Adventure */
  if (progress.level > 1 || progress.finished) items[n++] = 1;   /* Choose a level */
  if (progress.items & ITEM_ALMANAC) items[n++] = 2;
  if (progress.items & ITEM_SHOP) items[n++] = 3;
  items[n++] = 4;                                  /* How to play */
  items[n++] = 5;                                  /* Quit */
  return n;
}

static int almanac_page;        /* 0 index, 1 plants, 2 zombies */
static int shop_first;

void game_frame(uint32_t keys) {
  pressed = keys & ~prev_keys, held = keys, prev_keys = keys;
  vis_ticks++, state_t++;
  if (keys & K_HOME) {
    quit_saving();
    return;
  }
  switch (g_state) {
  case ST_TITLE:
    /* (the keys, the first time) */
    if (state_t > 30 && hit(K_OK | (K_ANY & ~K_BACK))) go(progress.keys_seen ? ST_MENU : ST_HELP);
    if (hit(K_BACK)) g_running = false;
    break;

  case ST_MENU: {
    int items[8], n = menu_items(items);
    if (confirm) {
      if (hit(K_LEFT | K_RIGHT)) sel2 ^= 1;
      if (hit(K_OK)) {
        if (sel2 == 0) quit_saving();
        confirm = 0;
      }
      if (hit(K_BACK)) confirm = 0;
      break;
    }
    uint32_t a = arrows();
    if (a & K_UP) sel = (sel + n - 1) % n;
    if (a & K_DOWN) sel = (sel + 1) % n;
    if (hit(K_BACK)) confirm = 1, sel2 = 1;
    if (hit(K_ALMANAC) && (progress.items & ITEM_ALMANAC)) almanac_page = 0, go(ST_ALMANAC);
    if (hit(K_OK)) {
      switch (items[sel]) {
      case 0:
        if (board_saved() && board_load()) {
          play_level = B.level;
          hud.col = 2, hud.row = B.nrows / 2, hud.selected = -1, hud.shovel = false;
          cam_x = 0, sod_t = -1, loaded_bg = -1;
          go(ST_PAUSE);   /* (back where it was left, paused) */
        } else
          begin_level(progress.level);
        break;
      case 1: go(ST_LEVELS), sel = progress.level - 1; break;
      case 2: almanac_page = 0, go(ST_ALMANAC); break;
      case 3:
        go(ST_SHOP);
        if (!(progress.talks & (1u << 1))) shop_first = 1, say(301, TA_SHOP);
        break;
      case 4: go(ST_HELP); break;
      case 5: confirm = 1, sel2 = 1; break;
      }
    }
    break;
  }

  case ST_LEVELS: {
    int top = progress.finished ? N_LEVELS : progress.level;
    uint32_t a = arrows();
    if (a & K_LEFT && sel % 10 > 0) sel--;
    if (a & K_RIGHT && sel % 10 < 9 && sel + 1 < top) sel++;
    if (a & K_UP && sel >= 10) sel -= 10;
    if (a & K_DOWN && sel + 10 < top) sel += 10;
    if (hit(K_BACK)) go(ST_MENU);
    if (hit(K_OK)) board_forget(), begin_level(sel + 1);
    break;
  }

  case ST_DAVE:
    if (state_t < 25) break;
    if (talk_after == TA_SLOT_ASK || talk_after == TA_SLOT_ASK8) {
      bool last = talk_at + 1 >= NTALKS || talks[talk_at + 1].id == 0;
      if (last) {
        if (hit(K_LEFT | K_RIGHT)) sel2 ^= 1;
        if (hit(K_OK)) {
          bool eight = talk_after == TA_SLOT_ASK8;
          uint32_t cost = eight ? 5000 : 750;
          if (sel2 == 0 && progress.money >= cost) {
            progress.money -= cost, progress.slots++;
            say(eight ? 1560 : 1510, TA_PAN);
          } else {
            progress.declined++;
            say(eight ? 1570 : 1520, TA_PAN);
          }
          progress_save();
        }
        break;
      }
    }
    if (hit(K_OK | K_BACK)) {
      bool skip = hit(K_BACK);
      if (!skip && talk_at + 1 < NTALKS && talks[talk_at + 1].id != 0) {
        talk_at++, state_t = 10;
        break;
      }
      switch (talk_after) {
      case TA_SHOVEL: show_lawn(-1), shovel_lesson(); break;
      case TA_BOWL: go(ST_READY); break;
      case TA_SHOP: go(ST_SHOP); break;
      case TA_MENU: go(ST_MENU); break;
      case TA_SLOT_ASK: case TA_SLOT_ASK8:
        if (skip) progress.declined++, start_pan(0, 380, needs_choice() ? ST_CHOOSE : ST_READY);
        break;
      default:
        if (B.level == 5 && B.mode == MODE_BOWLING && B.tutorial == 0 && talks[talk_at].id >= 2410) go(ST_READY);
        else start_pan(0, 380, needs_choice() ? ST_CHOOSE : ST_READY);
      }
    }
    break;

  case ST_PAN: {
    float t = state_t / 150.0f;
    if (pan_to > pan_from && state_t > 150 + 100 && pan_then != ST_CHOOSE) {
      /* (shown long enough: back to the lawn) */
      start_pan(pan_to, 0, ST_READY);
      break;
    }
    if (t > 1) t = 1;
    float k = t * t * (3 - 2 * t);
    cam_x = pan_from + (pan_to - pan_from) * k;
    if (pan_to > pan_from && pan_then == ST_CHOOSE && state_t >= 150) {
      int n = 0;
      for (int k2 = 0; k2 < N_SEEDS; k2++)
        if (plant_unlocked(k2)) choose_list[n++] = (uint8_t)k2;
      nchoose = n;
      /* the seeds of last time, as far as they go here */
      uint8_t pick[10];
      int np = 0, cap = bank_slots();
      for (int i = 0; i < progress.nlast && np < cap; i++)
        if (progress.last[i] < N_SEEDS && plant_unlocked(progress.last[i]) && usable_here(progress.last[i]))
          pick[np++] = progress.last[i];
      board_set_seeds(pick, np);
      go(ST_CHOOSE);
      break;
    }
    if (pan_to < pan_from && state_t >= 150) {
      cam_x = 0;
      if (sod_t >= 0) {
        sod_t++;
        if (sod_t < 120) {
          state_t = 150;
          break;
        }
        sod_t = -1;
      }
      go(ST_READY);
    }
    break;
  }

  case ST_CHOOSE: {
    int cap = bank_slots();
    uint32_t a = arrows();
    int rows = (nchoose + 7) / 8;
    /* sel: the packet in the grid, or nchoose: "Let's Rock!" */
    if (a & K_LEFT && sel < nchoose && sel % 8 > 0) sel--;
    if (a & K_RIGHT && sel < nchoose && sel % 8 < 7 && sel + 1 < nchoose) sel++;
    if (a & K_UP) sel = sel == nchoose ? (rows - 1) * 8 : sel >= 8 ? sel - 8 : sel;
    if (a & K_DOWN) sel = sel == nchoose ? sel : sel + 8 < nchoose ? sel + 8 : nchoose;
    if (hit(K_BACK)) {
      board_forget();
      go(ST_MENU);
      break;
    }
    if (hit(K_ALMANAC) && (progress.items & ITEM_ALMANAC)) {}
    if (hit(K_OK)) {
      if (sel == nchoose) {
        if (B.nseeds == cap) {
          progress.nlast = B.nseeds;
          memcpy(progress.last, B.seeds, B.nseeds);
          start_pan(380, 0, ST_READY);
        }
      } else {
        int kind = choose_list[sel], at = -1;
        for (int i = 0; i < B.nseeds; i++)
          if (B.seeds[i] == kind) at = i;
        uint8_t s[10];
        int n = B.nseeds;
        memcpy(s, B.seeds, (size_t)n);
        if (at >= 0) memmove(s + at, s + at + 1, (size_t)(n - at - 1)), n--;
        else if (n < cap && usable_here(kind)) s[n++] = (uint8_t)kind;
        board_set_seeds(s, n);
        if (B.nseeds == cap && at < 0) sel = nchoose;
      }
    }
    break;
  }

  case ST_READY:
    show_lawn(-1);
    if (state_t >= 180) start_play();
    break;

  case ST_PLAY:
    show_lawn(-1);
    if (hit(K_BACK)) {
      if (hud.selected >= 0 || hud.shovel) hud.selected = -1, hud.shovel = false;
      else {
        go(ST_PAUSE);
        break;
      }
    }
    play_input();
    board_tick();
    if (B.tutorial == 4) {
      /* the shovel lesson done: on to bowling */
      bool left = false;
      for (int i = 0; i < B.nplants; i++) left |= B.plants[i].health > 0;
      if (!left) {
        /* (the bowling starts from its first tick: zombies, the belt, the rolling nuts) */
        B.tutorial = 0, B.nplants = 0, B.started = 1, B.ticks = 0, hud.shovel = false;
        say(2410, TA_BOWL);
      }
      break;
    }
    if (B.phase == PH_WON) {
      hud.selected = -1, hud.shovel = false;
      go(ST_WON);
    } else if (B.phase == PH_LOST) {
      board_forget();
      go(ST_LOST);
    }
    break;

  case ST_PAUSE:
    show_lawn(-1);
    if (confirm) {
      if (hit(K_LEFT | K_RIGHT)) sel2 ^= 1;
      if (hit(K_BACK)) confirm = 0;
      if (hit(K_OK)) {
        if (sel2 == 0) {
          if (confirm == 1) quit_saving();
          else if (confirm == 2) begin_level(play_level);
          return;
        }
        confirm = 0;
      }
      break;
    }
    {
      uint32_t a = arrows();
      if (a & K_UP) sel = (sel + 3) % 4;
      if (a & K_DOWN) sel = (sel + 1) % 4;
    }
    if (hit(K_BACK)) go(ST_PLAY);
    if (hit(K_OK)) {
      if (sel == 0) go(ST_PLAY);
      else if (sel == 1) confirm = 2, sel2 = 1;
      else if (sel == 2) {
        if (B.phase == PH_PLAY) board_save();
        progress_save();
        go(ST_MENU);
      } else
        confirm = 1, sel2 = 1;
    }
    break;

  case ST_WON:
    show_lawn(-1);
    /* what is left of the lawn goes on (the dead fall, sun can be picked up) */
    for (int i = 0; i < B.nfx; i++)
      if (++B.fx[i].t >= B.fx[i].life) B.fx[i--] = B.fx[--B.nfx];
    {
      float cx = cell_x(hud.col);
      board_collect(cx, row_y(hud.row, cx) - 30, 52);
    }
    B.ticks++;
    if (state_t > 60 && hit(K_OK)) {
      /* the sun and coins left are picked up too */
      board_collect(400, 300, 2000);
      finish_level();
      go(ST_AWARD);
    }
    break;

  case ST_AWARD:
    if (state_t > 40 && hit(K_OK)) {
      if (B.level == N_LEVELS) go(ST_MENU);
      else begin_level(B.level + 1);
    }
    if (state_t > 40 && hit(K_BACK)) go(ST_MENU);
    break;

  case ST_LOST: {
    if (state_t <= 120) cam_x = -220 * (state_t / 120.0f);
    show_lawn(-1);
    if (state_t > 300) {
      if (hit(K_LEFT | K_RIGHT | K_UP | K_DOWN)) sel ^= 1;
      if (hit(K_OK)) {
        progress_save();
        if (sel == 0) begin_level(play_level);
        else go(ST_MENU);
      }
    }
    break;
  }

  case ST_ALMANAC: {
    if (almanac_page == 0) {
      if (hit(K_LEFT | K_RIGHT | K_UP | K_DOWN)) sel ^= 1;
      if (hit(K_OK)) almanac_page = 1 + sel, sel = 0;
      if (hit(K_BACK)) go(ST_MENU);
      break;
    }
    int n = almanac_page == 1 ? N_SEEDS : N_ZOMBIES, per = almanac_page == 1 ? 8 : 6;
    uint32_t a = arrows();
    if (a & K_LEFT && sel % per > 0) sel--;
    if (a & K_RIGHT && sel % per < per - 1 && sel + 1 < n) sel++;
    if (a & K_UP && sel >= per) sel -= per;
    if (a & K_DOWN && sel + per < n) sel += per;
    if (hit(K_BACK)) almanac_page = 0, sel = 0;
    break;
  }

  case ST_SHOP: {
    uint32_t a = arrows();
    if (a & K_LEFT) sel = (sel + 4) % 5;
    if (a & K_RIGHT) sel = (sel + 1) % 5;
    if (hit(K_BACK)) {
      progress_save();
      go(ST_MENU);
    }
    if (hit(K_OK)) {
      static const uint32_t slot_cost[4] = {750, 5000, 20000, 80000};
      uint32_t cost = 0;
      bool ok = false;
      switch (sel) {
      case 0: if (progress.slots < 10) cost = slot_cost[progress.slots - 6], ok = true; break;
      case 1: if (!(progress.items & ITEM_POOL_CLEANER)) cost = 1000, ok = progress.level > 21 || progress.finished; break;
      case 2: if (!(progress.items & ITEM_ROOF_CLEANER)) cost = 3000, ok = progress.level > 41 || progress.finished; break;
      case 3: if (progress.rakes == 0) cost = 200, ok = true; break;
      case 4: if (!(progress.items & ITEM_FIRST_AID)) cost = 2000, ok = progress.level > 21 || progress.finished; break;
      }
      if (ok && progress.money >= cost) {
        progress.money -= cost;
        if (sel == 0) progress.slots++;
        if (sel == 1) progress.items |= ITEM_POOL_CLEANER;
        if (sel == 2) progress.items |= ITEM_ROOF_CLEANER;
        if (sel == 3) progress.rakes = 3;
        if (sel == 4) progress.items |= ITEM_FIRST_AID;
        progress_save();
      }
    }
    break;
  }

  case ST_HELP:
    if (hit(K_OK | K_BACK)) {
      if (!progress.keys_seen) progress.keys_seen = 1, progress_save();
      go(ST_MENU);
    }
    break;
  }
}

/* ---------------------------------------------------------------- drawing the screens */

static const char *const menu_names[] = {"Adventure", "Choose a Level", "Almanac", "Shop", "How to Play", "Quit"};

static void draw_menu(void) {
  show_back(BG_MENU);
  gfx_anim(I_LOGO, 0, 20, 4, 0, TINT_NONE, 32);
  int items[8], n = menu_items(items);
  /* the tombstone's slabs */
  for (int i = 0; i < n; i++) {
    int y = 64 + i * 27, x = 166;
    bool f = i == sel;
    gfx_anim(I_BUTTON, 0, x, y, 0, f ? TINT_HIT : TINT_DARK, 32);
    const char *label = menu_names[items[i]];
    if (items[i] == 0) {
      gfx_text(F_HEAD, x + 50, y + 15, board_saved() ? "Continue" : "Adventure", f ? rgb565(0xFFF5B0) : rgb565(0xD8D8E8),
               GFX_CENTER | GFX_OUTLINE);
      gfx_text(F_SMALL, x + 50, y + 25,
               progress.finished && progress.level == 1 ? "Play again!" : gfx_fmt("Level %d-%d", level_world(progress.level),
                                                                               level_stage(progress.level)),
               rgb565(0xFFE14A), GFX_CENTER | GFX_OUTLINE);
    } else
      gfx_text(F_HEAD, x + 50, y + 19, label, f ? rgb565(0xFFF5B0) : rgb565(0xD8D8E8), GFX_CENTER | GFX_OUTLINE);
  }
  if (progress.money) draw_money(4, 226, progress.money);
  if (confirm) {
    draw_dialog(60, 80, 200, 80);
    gfx_text(F_HEAD, 160, 104, "Quit the game?", C_WHITE, GFX_CENTER | GFX_OUTLINE);
    draw_button(80, 124, 70, "Quit", sel2 == 0);
    draw_button(170, 124, 70, "Cancel", sel2 == 1);
  }
}

static void draw_levels(void) {
  show_back(BG_MENU);
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0, 14);
  gfx_text(F_BIG, 160, 22, "Choose a Level", rgb565(0xFFE14A), GFX_CENTER | GFX_OUTLINE);
  static const char *const worlds[5] = {"Day", "Night", "Pool", "Fog", "Roof"};
  int top = progress.finished ? N_LEVELS : progress.level;
  for (int w = 0; w < 5; w++) {
    gfx_text(F_HEAD, 6, 54 + w * 34, worlds[w], C_WHITE, GFX_OUTLINE);
    for (int s = 0; s < 10; s++) {
      int l = w * 10 + s, x = 52 + s * 26, y = 40 + w * 34;
      bool open = l < top, f = l == sel;
      gfx_round_rect(x, y, 24, 20, 4, f ? rgb565(0x6CBF3A) : open ? rgb565(0x8A5A2E) : rgb565(0x404040), 32);
      gfx_text(F_SMALL, x + 12, y + 13, gfx_fmt("%d-%d", w + 1, s + 1), open ? C_WHITE : rgb565(0x909090),
               GFX_CENTER | GFX_OUTLINE);
    }
  }
  gfx_text(F_SMALL, 160, 232, "OK: play   Back: menu", C_WHITE, GFX_CENTER | GFX_OUTLINE);
}

static void draw_dave(void) {
  /* the lawn behind him */
  show_lawn(B.level <= 3 ? 0 : -1);
  draw_lawn(false);
  int in = state_t < 25 ? (25 - state_t) * 4 : 0;
  gfx_image(I_DAVE, 4 - in, 66);
  const Line *l = talk_at >= 0 ? &talks[talk_at] : NULL;
  if (!l || state_t < 18) return;
  int x = 84, y = 70, w = 226, h = 74;
  gfx_round_rect(x, y, w, h, 10, C_DARK, 32);
  gfx_round_rect(x + 2, y + 2, w - 4, h - 4, 9, C_WHITE, 32);
  for (int i = 0; i < 8; i++) gfx_rect(x - 6 + i, y + 40 + i / 2, 8 - i, 2, i ? C_WHITE : C_DARK, 32);
  const char *s = l->id == 1401 ? gfx_fmt(l->text, (unsigned)progress.money) : l->text;
  gfx_paragraph(F_BODY, x + w / 2, y + 22, w - 20, s, C_DARK, 0);
  bool ask = (talk_after == TA_SLOT_ASK || talk_after == TA_SLOT_ASK8) &&
             (talk_at + 1 >= NTALKS || talks[talk_at + 1].id == 0);
  if (ask) {
    draw_button(x + 30, y + h + 6, 70, "Yes", sel2 == 0);
    draw_button(x + w - 100, y + h + 6, 70, "No", sel2 == 1);
  } else if ((vis_ticks / 40) % 2)
    gfx_text(F_SMALL, x + w - 8, y + h - 6, "OK", rgb565(0x707070), GFX_RIGHT);
}

static void draw_chooser(void) {
  show_lawn(0);
  draw_street(street, nstreet);
  hud_bank_draw(0, 0, false);
  int cap = bank_slots();
  for (int i = 0; i < cap; i++) {
    int x = 34 + i * 21;
    if (i < B.nseeds) packet_draw(B.seeds[i], x, 3, 0, 0, false, false, i + 1);
    else gfx_rect(x, 3, 20, 28, 0, 10);
  }
  int px = 0, py = 36;
  gfx_image(I_CHOOSER, px, py);
  gfx_text(F_HEAD, px + 93, py + 15, "Choose your plants!", rgb565(0xF2C94C), GFX_CENTER | GFX_OUTLINE);
  for (int i = 0; i < nchoose; i++) {
    int kind = choose_list[i], x = px + 10 + (i % 8) * 21, y = py + 24 + (i / 8) * 30;
    bool chosen = false;
    for (int k = 0; k < B.nseeds; k++) chosen |= B.seeds[k] == kind;
    packet_draw(kind, x, y, 0, 0, chosen || !usable_here(kind), false, 0);
    if (i == sel) gfx_frame_rect(x - 1, y - 1, 22, 30, 1, rgb565(0xFFE14A), 32);
  }
  bool ready = B.nseeds == cap;
  int bx = px + 51, by = py + 178;
  gfx_anim(ready ? I_START : I_START_OFF, 0, bx, by, 0, sel == nchoose ? TINT_HIT : TINT_NONE, 32);
  gfx_text(F_HEAD, bx + 42, by + 15, "Let's Rock!", ready ? rgb565(0xFFE14A) : rgb565(0x9A9A9A), GFX_CENTER | GFX_OUTLINE);
  if (sel == nchoose) gfx_frame_rect(bx - 2, by - 2, 88, 26, 1, rgb565(0xFFE14A), 32);
  /* what the plant does */
  if (sel < nchoose) {
    int kind = choose_list[sel];
    gfx_round_rect(190, 150, 126, 86, 6, C_DARK, 22);
    gfx_text(F_HEAD, 253, 164, plant_defs[kind].name, rgb565(0xFFE14A), GFX_CENTER | GFX_OUTLINE);
    gfx_text(F_SMALL, 253, 175, gfx_fmt("Cost: %d  Recharge: %s", plant_defs[kind].cost,
                                        plant_defs[kind].cooldown >= 5000 ? "very slow" : plant_defs[kind].cooldown >= 3000 ? "slow"
                                                                                                                         : "fast"),
             C_WHITE, GFX_CENTER);
    gfx_paragraph(F_SMALL, 253, 186, 118, plant_blurbs[kind], C_WHITE, 0);
  }
}

static void draw_pause(void) {
  draw_lawn(true);
  hud_draw(false);
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0, 10);
  int x = 70, y = 28, w = 180;
  gfx_anim(I_DIALOG, 0, x + w / 2 - 82, y - 6, 0, TINT_NONE, 32);
  gfx_text(F_BIG, 160, y + 46, "Paused", rgb565(0xE8E8F0), GFX_CENTER | GFX_OUTLINE);
  static const char *const items[4] = {"Back to Game", "Restart Level", "Main Menu", "Quit Game"};
  for (int i = 0; i < 4; i++) draw_button(x + 30, y + 62 + i * 25, w - 60, items[i], i == sel);
  if (confirm) {
    draw_dialog(50, 84, 220, 82);
    gfx_paragraph(F_BODY, 160, 104, 200,
                  confirm == 1 ? "Quit the game? This level is kept for next time." : "Restart this level from the start?",
                  C_WHITE, GFX_OUTLINE);
    draw_button(70, 138, 80, confirm == 1 ? "Quit" : "Restart", sel2 == 0);
    draw_button(170, 138, 80, "Cancel", sel2 == 1);
  }
}

static void draw_reward(int x, int y, bool big) {
  const LevelDef *d = &level_defs[B.level - 1];
  if (d->reward >= 0) {
    if (big) gfx_image(I_BIG_PEASHOOTER + d->reward, x, y + 10);
    else {
      gfx_image(I_PACKET, x - 10, y - 14);
      gfx_image(I_PACKET_PEASHOOTER + d->reward, x, y - 2);
    }
  } else if (d->reward == REWARD_SHOVEL) gfx_anim_scaled(I_SHOVEL, 0, x, y, big ? 512 : 256, big ? 512 : 256, 0, 0, 32);
  else if (d->reward == REWARD_TROPHY) gfx_anim_scaled(I_TROPHY, 0, x - (big ? 31 : 16), y - (big ? 27 : 14), big ? 256 : 128,
                                                        big ? 256 : 128, 0, 0, 32);
  else if (d->reward == REWARD_ALMANAC) {
    gfx_round_rect(x - 12, y - 15, 24, 30, 3, rgb565(0x7A4A20), 32);
    gfx_round_rect(x - 10, y - 13, 20, 26, 2, rgb565(0x3F7F2A), 32);
    gfx_image(I_PACKET_PEASHOOTER, x, y - 2);
  } else if (d->reward == REWARD_KEYS) {
    gfx_ellipse(x - 4, y, 6, 6, rgb565(0xD8C060), 32);
    gfx_ellipse(x - 4, y, 3, 3, 0, 32);
    gfx_rect(x, y - 1, 12, 3, rgb565(0xD8C060), 32);
    gfx_rect(x + 8, y + 2, 2, 4, rgb565(0xD8C060), 32);
  } else {
    gfx_ellipse(x, y + 2, 10, 9, rgb565(0x8A6A3A), 32);
    gfx_ellipse(x, y - 8, 5, 3, rgb565(0x6A4A2A), 32);
    gfx_text(F_HEAD, x, y + 6, "$", rgb565(0xFFE14A), GFX_CENTER | GFX_OUTLINE);
  }
}

static void draw_won(void) {
  draw_lawn(true);
  hud_draw(false);
  int x = screen_x(B.last_x), y = screen_y(B.last_y) - 20;
  if (x < 20) x = 20;
  if (x > 300) x = 300;
  if (y < 40) y = 40;
  int bob = (int)(vis_ticks / 6 % 8);
  bob = bob < 4 ? bob : 8 - bob;
  for (int r = 18; r > 4; r -= 4) gfx_ellipse(x, y - bob, r, r, rgb565(0xFFF5B0), 6);
  draw_reward(x, y - bob, false);
  if (state_t > 60 && (vis_ticks / 40) % 2) draw_message_band("Press OK to pick it up!");
}

static void draw_award(void) {
  show_back(BG_AWARD);
  const LevelDef *d = &level_defs[B.level - 1];
  const char *head, *name, *text;
  static char buf[40];
  if (d->reward >= 0) {
    head = "You got a new plant!", name = plant_defs[d->reward].name, text = plant_blurbs[d->reward];
  } else if (d->reward == REWARD_SHOVEL) {
    head = "You found the shovel!", name = "Shovel", text = "Lets you dig up unwanted plants: the delete key, then OK.";
  } else if (d->reward == REWARD_ALMANAC) {
    head = "You found the Suburban Almanac!", name = "Suburban Almanac",
    text = "Keeps track of all the plants and zombies you encounter. Open it from the menu or with the toolbox key.";
  } else if (d->reward == REWARD_KEYS) {
    head = "You found Crazy Dave's car keys!", name = "Car Keys", text = "Crazy Dave's shop is open: find it in the menu.";
  } else if (d->reward == REWARD_TROPHY) {
    head = "You beat the zombies!", name = "Silver Sunflower Trophy",
    text = "Dr. Zomboss is defeated and your lawn is safe. Play the adventure again, with Crazy Dave picking some of your "
           "seeds!";
  } else {
    memcpy(buf, "You found a money bag!", 23);
    head = buf, name = "Money Bag", text = "$500 for Crazy Dave's shop.";
  }
  gfx_text(F_HEAD, 160, 22, head, rgb565(0xF2C94C), GFX_CENTER | GFX_OUTLINE);
  draw_reward(160, 82, true);
  /* the words in the card's panel, under the name: the body's letters if they fit, else the small ones, in its
   * middle; the longest (the Potato Mine's, the trophy's) a little closer, the name a little higher */
  int top = 154, bottom = 189, width = 114, font = F_BODY, tight = 0;
  int h = gfx_paragraph(font, 160, 0, width, text, 0, GFX_MEASURE) - 1;
  if (h > bottom - top) font = F_SMALL, h = gfx_paragraph(font, 160, 0, width, text, 0, GFX_MEASURE) - 1;
  if (h > bottom - top) {
    top = 152, width = 118, tight = GFX_TIGHT;
    h = gfx_paragraph(font, 160, 0, width, text, 0, GFX_MEASURE | tight) - 1;
  }
  gfx_text(F_HEAD, 160, top - 2, name, rgb565(0x3A2410), GFX_CENTER);
  int y = h < bottom - top ? top + (bottom - top - h) / 2 : top;
  gfx_paragraph(font, 160, y + font_info(font).ascent, width, text, rgb565(0x3A2410), tight);
  draw_button(110, 214, 100, B.level == N_LEVELS ? "Main Menu" : "Next Level", true);
}

static void draw_lost(void) {
  /* the zombie walks into the house */
  draw_lawn(false);
  if (state_t > 120) {
    int k = state_t - 120;
    int dx = (k / 2) % 3 - 1;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0, k < 60 ? k / 3 : 20);
    gfx_text(F_BIG, 160 + dx, 104, "THE ZOMBIES", rgb565(0x7AD030), GFX_CENTER | GFX_OUTLINE);
    gfx_text(F_BIG, 160 - dx, 128, "ATE YOUR BRAINS!", rgb565(0x7AD030), GFX_CENTER | GFX_OUTLINE);
  }
  if (state_t > 300) {
    draw_button(60, 168, 90, "Try Again", sel == 0);
    draw_button(170, 168, 90, "Main Menu", sel == 1);
  }
}

static bool zombie_seen(int k) {
  if (progress.finished) return true;
  if (k == Z_DUCKY) return progress.level > 21;
  if (k == Z_BACKUP) return progress.level > 18;
  if (k == Z_IMP) return progress.level > 48;
  return zombie_defs[k].starting < progress.level || (k == Z_FLAG && progress.level > 2);
}

static void draw_almanac(void) {
  show_back(BG_ALMANAC);
  if (almanac_page == 0) {
    gfx_text(F_BIG, 160, 40, "Suburban Almanac", rgb565(0x5A3A1E), GFX_CENTER);
    gfx_image(I_BIG_PEASHOOTER, 92, 120);
    gfx_image(I_PORTRAIT_NORMAL, 228, 170);
    draw_button(40, 182, 104, "View Plants", sel == 0);
    draw_button(176, 182, 104, "View Zombies", sel == 1);
    gfx_text(F_SMALL, 160, 230, "Back: menu", rgb565(0x5A3A1E), GFX_CENTER);
    return;
  }
  if (almanac_page == 1) {
    gfx_text(F_HEAD, 86, 24, "Plants", rgb565(0x5A3A1E), GFX_CENTER);
    for (int i = 0; i < N_SEEDS; i++) {
      int x = 4 + (i % 8) * 21, y = 32 + (i / 8) * 30;
      if (plant_unlocked(i)) packet_draw(i, x, y, 0, 0, false, false, 0);
      else gfx_round_rect(x, y, 20, 28, 2, rgb565(0x8A7A5A), 32);
      if (i == sel) gfx_frame_rect(x - 1, y - 1, 22, 30, 1, rgb565(0xE83020), 32);
    }
    int k = sel;
    gfx_image(I_CARD, 180, 16);
    if (plant_unlocked(k)) {
      gfx_image(I_BIG_PEASHOOTER + k, 245, 76);
      gfx_text(F_HEAD, 245, 108, plant_defs[k].name, rgb565(0x3A2410), GFX_CENTER);
      int y = gfx_paragraph(F_SMALL, 245, 120, 116, plant_blurbs[k], rgb565(0x3A2410), 0);
      gfx_paragraph(F_SMALL, 245, y + 2, 116, plant_stats[k], rgb565(0x7A2010), 0);
      gfx_text(F_SMALL, 192, 205, gfx_fmt("Cost: %d", plant_defs[k].cost), rgb565(0x3A2410), 0);
      gfx_text(F_SMALL, 298, 205,
               gfx_fmt("Recharge: %s", plant_defs[k].cooldown >= 5000 ? "very slow" : plant_defs[k].cooldown >= 3000 ? "slow" : "fast"),
               rgb565(0x3A2410), GFX_RIGHT);
    } else
      gfx_text(F_HEAD, 245, 110, "???", rgb565(0x3A2410), GFX_CENTER);
  } else {
    gfx_text(F_HEAD, 86, 24, "Zombies", rgb565(0x5A3A1E), GFX_CENTER);
    for (int i = 0; i < N_ZOMBIES; i++) {
      int x = 6 + (i % 6) * 28, y = 30 + (i / 6) * 46;
      gfx_round_rect(x, y, 26, 42, 3, i == sel ? rgb565(0xC88A4A) : rgb565(0x9A7A50), 32);
      if (zombie_seen(i)) gfx_anim_scaled(I_PORTRAIT_NORMAL + i, 0, x + 13, y + 40, 112, 112, 0, TINT_NONE, 32);
      else gfx_text(F_HEAD, x + 13, y + 26, "?", rgb565(0x5A3A1E), GFX_CENTER);
    }
    int k = sel;
    gfx_round_rect(176, 14, 140, 216, 6, rgb565(0x4A4E66), 32);
    gfx_round_rect(180, 18, 132, 208, 5, rgb565(0xF2E2C0), 32);
    if (zombie_seen(k)) {
      gfx_rect(194, 22, 104, 88, rgb565(0x2E3A2A), 32);
      gfx_clip(194, 22, 104, 88);
      gfx_image(I_PORTRAIT_NORMAL + k, 246, 108);
      gfx_clip_all();
      gfx_text(F_HEAD, 246, 124, zombie_defs[k].name, rgb565(0x3A2410), GFX_CENTER);
      int y = gfx_paragraph(F_SMALL, 246, 136, 124, zombie_blurbs[k], rgb565(0x3A2410), 0);
      gfx_paragraph(F_SMALL, 246, y + 2, 124, zombie_stats[k], rgb565(0x7A2010), 0);
    } else
      gfx_text(F_HEAD, 246, 120, "???", rgb565(0x3A2410), GFX_CENTER);
  }
  gfx_text(F_SMALL, 86, 232, "Back: the Almanac's index", rgb565(0x5A3A1E), GFX_CENTER);
}

static void draw_shop(void) {
  show_back(BG_SHOP);
  gfx_image(I_DAVE, 6, 64);
  static const char *const names[5] = {"Extra Seed Slot", "Pool Cleaner", "Roof Cleaner", "Rake", "Wall-nut First Aid"};
  static const char *const what[5] = {
    "Lets you take one more seed into each level.", "Clears a pool lane of zombies, like a lawn mower.",
    "Clears a roof lane of zombies, like a lawn mower.", "Whacks the first zombie that steps on it. Three uses.",
    "Plant wall-nuts, tall-nuts and pumpkins over worn ones to make them whole again."};
  static const uint32_t slot_cost[4] = {750, 5000, 20000, 80000};
  static const uint16_t icons[5] = {I_SHOP_SLOTS, I_POOL_CLEANER, I_SHOP_ROOF, I_SHOP_RAKE, I_SHOP_FIRSTAID};
  gfx_round_rect(84, 100, 232, 70, 6, rgb565(0x6A4424), 32);
  gfx_round_rect(87, 103, 226, 64, 5, rgb565(0x8A5A2E), 32);
  for (int i = 0; i < 5; i++) {
    int x = 92 + i * 45, y = 108;
    bool sold = (i == 0 && progress.slots >= 10) || (i == 1 && (progress.items & ITEM_POOL_CLEANER)) ||
                (i == 2 && (progress.items & ITEM_ROOF_CLEANER)) || (i == 3 && progress.rakes) ||
                (i == 4 && (progress.items & ITEM_FIRST_AID));
    bool avail = i == 0 || i == 3 || (i == 2 ? progress.level > 41 : progress.level > 21) || progress.finished;
    uint32_t cost = i == 0 ? (progress.slots < 10 ? slot_cost[progress.slots - 6] : 0) : i == 1 ? 1000 : i == 2 ? 3000 : i == 3 ? 200 : 2000;
    gfx_round_rect(x, y, 40, 54, 4, i == sel ? rgb565(0xF2C94C) : rgb565(0xE8D8B0), 32);
    if (i == 1) gfx_anim(icons[i], 0, x + 20, y + 34, 0, avail ? TINT_NONE : TINT_GRAY, 32);
    else gfx_anim(icons[i], 0, x + 20 - 13, y + 8, 0, avail ? TINT_NONE : TINT_GRAY, 32);
    gfx_text(F_SMALL, x + 20, y + 49, sold ? "SOLD" : avail ? gfx_fmt("$%u", (unsigned)cost) : "Soon", rgb565(0x3A2410),
             GFX_CENTER);
  }
  gfx_round_rect(84, 176, 232, 50, 8, C_WHITE, 32);
  gfx_text(F_HEAD, 200, 190, names[sel], rgb565(0x3A2410), GFX_CENTER);
  gfx_paragraph(F_SMALL, 200, 202, 220, what[sel], rgb565(0x3A2410), 0);
  draw_money(214, 8, progress.money);
  gfx_text(F_BIG, 150, 30, "Crazy Dave's", rgb565(0xF2C94C), GFX_CENTER | GFX_OUTLINE);
  gfx_text(F_HEAD, 150, 50, "Twiddydinkies", rgb565(0xF2C94C), GFX_CENTER | GFX_OUTLINE);
  gfx_text(F_SMALL, 200, 236, "OK: buy   Back: menu", C_WHITE, GFX_CENTER | GFX_OUTLINE);
}

static void draw_help(void) {
  show_back(BG_ALMANAC);
  gfx_text(F_BIG, 160, 25, "How to Play", rgb565(0x5A3A1E), GFX_CENTER);   /* (in the middle of the stone) */
  static const char *const keys[][2] = {
    {"Arrows", "Move over the lawn"},
    {"1 2 3 ... 0", "Pick a seed packet"},
    {"OK", "Plant it (or whack, or break a vase)"},
    {"Cursor", "Over sun and coins, picks them up"},
    {"Delete", "The shovel: then OK digs up a plant"},
    {"Back", "Put the seed back, or pause"},
    {"Toolbox", "The Almanac (in the menu)"},
    {"Home", "Save and quit"},
  };
  for (int i = 0; i < 8; i++) {
    int y = 62 + i * 19;
    gfx_text(F_HEAD, 116, y, keys[i][0], rgb565(0x7A2010), GFX_RIGHT);
    gfx_text(F_BODY, 126, y, keys[i][1], rgb565(0x3A2410), 0);
  }
  gfx_text(F_SMALL, 160, 226, progress.keys_seen ? "OK: back" : "OK: on to the game", rgb565(0x5A3A1E), GFX_CENTER);
}

void game_draw(void) {
  gfx_begin();
  switch (g_state) {
  case ST_TITLE:
    show_back(BG_MENU);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0, 8);
    gfx_image(I_LOGO, 20, 50);
    if ((vis_ticks / 50) % 2)
      gfx_text(F_HEAD, 160, 200, "Press OK to start!", rgb565(0xFFE14A), GFX_CENTER | GFX_OUTLINE);
    gfx_text(F_SMALL, 160, 232, "A fan version of PopCap's game, for the NumWorks calculator", C_WHITE,
             GFX_CENTER | GFX_OUTLINE);
    break;
  case ST_MENU: draw_menu(); break;
  case ST_LEVELS: draw_levels(); break;
  case ST_DAVE: draw_dave(); break;
  case ST_PAN:
    show_lawn(sod_t >= 0 ? (sod_t * 3 > 320 ? 320 : sod_t * 3) : -1);
    if (cam_x > 0) draw_street(street, nstreet);
    draw_lawn(false);
    if (cam_x > 300 && pan_to > pan_from)
      gfx_text(F_HEAD, 160, 220, gfx_fmt("Level %d-%d", level_world(B.level), level_stage(B.level)), C_WHITE,
               GFX_CENTER | GFX_OUTLINE);
    break;
  case ST_CHOOSE: draw_chooser(); break;
  case ST_READY:
    draw_lawn(true);
    hud_draw(false);
    /* (Wall-nut Bowling starts as Crazy Dave leaves, without "Ready... Set... PLANT!") */
    if (B.mode != MODE_BOWLING) draw_ready(state_t);
    break;
  case ST_PLAY:
    draw_lawn(true);
    hud_draw(true);
    if (B.tutorial == 4) draw_message_band("Dig up the three peashooters: the delete key, then OK on each!");
    break;
  case ST_PAUSE: draw_pause(); break;
  case ST_WON: draw_won(); break;
  case ST_AWARD: draw_award(); break;
  case ST_LOST: draw_lost(); break;
  case ST_ALMANAC: draw_almanac(); break;
  case ST_SHOP: draw_shop(); break;
  case ST_HELP: draw_help(); break;
  }
  gfx_end();
}
