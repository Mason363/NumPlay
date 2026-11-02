/* FNAF World: its numbers, from its events (gamesrc spec/fnafworld_spec.md and fnafworld_spec2.md). */
#include "game.h"

const char *const CHAR_NAME[NCHAR + 1] = {
  "", "Freddy", "Bonnie", "Chica", "Foxy", "Toy Bonnie", "Toy Chica", "Toy Freddy", "Mangle", "Balloon Boy", "JJ",
  "Phantom Freddy", "Phantom Chica", "Phantom BB", "Phantom Foxy", "Phantom Mangle", "Withered Bonnie",
  "Withered Chica", "Withered Freddy", "Withered Foxy", "Shadow Freddy", "Marionette", "Phantom Marionette",
  "Golden Freddy", "Paperpals", "Nightmare Freddy", "Nightmare Bonnie", "Nightmare Chica", "Nightmare Foxy",
  "Endo 01", "Endo 02", "Plushtrap", "Endoplush", "Springtrap", "RXQ", "Crying Child", "Funtime Foxy",
  "Nightmare Fredbear", "Nightmare", "Fredbear", "Spring Bonnie", "Jack-O-Bonnie", "Jack-O-Chica", "Animdude",
  "Mr. Chipper", "Nightmare BB", "Nightmarionne", "Coffee", "Purpleguy"};

/* the events 156-249 (and the character select's 153-200) */
const uint8_t CHAR_ATTACKS[NCHAR + 1][3] = {
  {0, 0, 0}, {1, 6, 26}, {2, 7, 12}, {11, 13, 15}, {3, 8, 31}, {7, 32, 36}, {11, 26, 44}, {1, 13, 29}, {3, 34, 36},
  {9, 31, 32}, {9, 34, 41}, {22, 23, 24}, {16, 23, 41}, {17, 21, 24}, {8, 16, 41}, {16, 38, 43}, {4, 6, 42},
  {2, 11, 37}, {1, 22, 40}, {3, 24, 35}, {41, 40, 44}, {37, 38, 40}, {8, 35, 39}, {8, 25, 46}, {37, 39, 49},
  {5, 23, 33}, {5, 25, 43}, {5, 20, 44}, {5, 42, 35}, {27, 28, 47}, {29, 47, 48}, {16, 20, 23}, {48, 4, 45},
  {5, 10, 24}, {46, 49, 50}, {27, 28, 50}, {14, 30, 50}, {19, 33, 51}, {19, 25, 51}, {51, 15, 49}, {10, 14, 30},
  {46, 54, 52}, {46, 54, 57}, {48, 58, 53}, {49, 57, 59}, {51, 55, 56}, {37, 55, 53}, {50, 58, 60}, {29, 52, 59}};

const char *const ATTACK_NAME[61] = {
  "", "Mic Toss", "Bite", "Hook", "Eye Beam", "Bite 2", "Pizza Wheel", "Bash Jam", "Jumpscare", "Balloons",
  "Springlocks", "Cupcake", "Happy Jam", "Party Favors", "Happy Jam 2", "Regen Song", "Toxic Bite",
  "Toxic Balloon", "Toxic Song", "Toxic Bite 2", "Bad Pizza", "Gloom Balloon", "Gloom Song", "Sludge", "Rainy Day",
  "Rainy Day 2", "Birthday", "Power Song", "Armor Song", "Speed Song", "Cosmic Song", "Hot Cheese", "Munchies",
  "Freddles", "Poppers", "Hot Cheese 2", "Prize Ball", "Prize Ball 2", "Mystery Box", "Mystery Box 2", "Esc Key",
  "Unscrew", "Unscrew 2", "Pizza Wheel 2", "Waterhose", "Waterhose 2", "Haunting", "Endo Army", "Neon Wall",
  "Mimic Ball", "Gift Boxes", "Mega Bite", "Slasher", "4th Wall", "Jack-O-Bomb", "Bubble Breath", "Balloons 2",
  "Buzzsaw", "Mega Virus", "Hocus Pocus", "Neon Wall 2"};

/* the colour of each button's end (the game's button pictures): 0 orange, 1 red, 2 pink, 3 green, 4 purple, 5 white,
 * 6 yellow, 7 black */
const uint8_t ATTACK_COLOUR[61] = {
  0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 1, 1, 1, 1, 1, 6, 6,
  6, 6, 6, 7, 7, 1, 7, 7, 6, 6, 6, 6, 6, 1, 4, 4, 0, 2, 0, 0, 1, 6, 1};

const char *const ENEMY_NAME[54] = {
  "", "Bouncepot", "Mechrab", "Gearrat", "Totemole", "Chop 'N Roll", "Boxbyte", "Chillax", "Flan", "Seaweed",
  "Crabapple", "Colossal", "Quarry", "Metalman", "Goldmine", "Graveweed", "Beartrap", "Prototype", "Tombstack", "Rot",
  "Blacktrap", "Meringue", "Dogfight", "Ballboy", ">>>>>>", "%__^^&(", "_!2222", "Tangle", "Redbear", "White Rabbit",
  "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "Neon", "Jangle", "P. Goon"};

const char *const BOSS_NAME[20] = {
  "", "Auto Chipper", "Seagoon", "Eyesore", "Mad Endo", "Browboy", "Gold Endo", "Bubba", "Porkpatch", "Bouncer", "",
  "Snowcone", "Supergoon", "Overclock", "Security", "Chipper's Revenge", "", "", "", "PurpleGeist"};

const char *const CHIP_NAME[NCHIP + 1] = {
  "", "Headstart: Defense", "Headstart: Strength", "Headstart: Speed", "Evercomet: Weak", "Quickstart: Party",
  "Block: Jumpscare", "Run: Luck", "Endless: Defense", "Endless: Strength", "Endless: Speed", "Evercomet: Strong",
  "Auto: Giftboxes", "Auto: Regen", "Find: Characters", "Curse: Status", "Freddle: Fury", "Auto: Shield",
  "Auto: Mimic", "Counter: Bite", "Pizza: Fury", "Block: Unscrew"};

const char *const BYTE_NAME[NBYTE + 1] = {
  "", "Gnat", "Neon Bee", "Neon Wasp", "Medpod 1", "Medpod 2", "Mega-Med", "Mini-Reaper", "Reaper", "X-Reaper",
  "Mini-FO", "UFO", "X-FO", "Block5", "Block20", "Block50", "Pop-Pop", "BOOM", "KABOOM", "BossDrain01",
  "BossDrain02", "BossDrain-X"};

/* the shop's events 69-110 */
const uint16_t BYTE_PRICE[NBYTE + 1] = {0, 25, 75, 150, 25, 100, 500, 100, 500, 2000, 200, 500, 1500, 50, 500, 2000,
                                        99, 999, 4999, 200, 1500, 5000};

/* 1017: "life p N" = level * 5 + 100 + character * 5 */
int32_t life_max(int c, int lv) { return lv * 5 + 100 + c * 5; }

/* 1100: a level up when "next" > (level^2 + 1) * 10 + character * 5 */
int32_t exp_needed(int c, int lv) { return (lv * lv + 1) * 10 + c * 5; }

/* 1003 */
int32_t enemy_life(int type, int diff) { return (10 + type * type * 5) * diff; }

/* 1004: ((c * 50) + 100) + ((c / 20) * 2000) * diff */
int32_t challenger_life(int c, int diff) { return c * 50 + 100 + c / 20 * 2000 * diff; }

/* 1619: the challenger: 9 + Random(area + 3) + (Random(3) / 2) * (Random(3) + 1) */
int challenger_pick(int area) {
  int a = rnd_n(area + 3);
  int b = rnd_n(3) / 2;
  int c = rnd_n(3) + 1;
  return 9 + a + b * c;
}

/* 1077-1084 */
int32_t victory_exp(int count, int type, int boss, int area, int diff) {
  if (boss) {
    if (boss == 6) return 0;
    if (boss == 14) return 5000 * diff;
    if (boss == 10 || boss == 15 || boss == 19) return 1;
    return (area * 50 + 200) * diff;
  }
  return type == 14 ? 0 : count * type * 5 * diff;
}

int32_t victory_tokens(int count, int type, int boss, int area, int diff) {
  if (boss) {
    if (boss == 6) return 500 * diff;
    if (boss == 14) return 250 * diff;
    if (boss == 10 || boss == 15 || boss == 19) return 1;
    return 0;
  }
  if (type == 14) return count * 25;
  int a = rnd_n(11) / 10 * 10;
  return count * area + a + rnd_n(6);
}

/* 1086-1093: how much exp one tick (50 ms) counts; the checks follow each other */
int exp_tick(int32_t exp) {
  int k = 0;
  static const int32_t over[8] = {0, 10, 25, 50, 100, 200, 300, 500};
  static const int take[8] = {1, 2, 2, 2, 2, 2, 10, 20};
  for (int i = 0; i < 8; i++)
    if (exp > over[i]) exp -= take[i], k += take[i];
  return k;
}

/* the hits' divisor: "attack arrow 2" D (2 with a defense boost, else 1) + "shield" B (2) + "pwr down icon" D (2) */
int divisor(bool def_boost, bool shield, bool pwr_down) { return (def_boost ? 2 : 1) + (shield ? 2 : 0) + (pwr_down ? 2 : 0); }

/* 1061 */
int32_t enemy_strength(int type, int diff, int div, int block, int armor) {
  return (type * 4 + rnd_n(5)) * diff / div - block - armor * 10;
}

/* 1577-1597: a boss's life, speed, strength (rolled), its special and its animation (in "character 2") */
int boss_info(int boss, int diff, int32_t *life, int32_t *speed, int32_t *strength, int *special, int *an) {
  static const struct { int32_t life; uint8_t times_diff; int16_t speed, str, str_r; uint8_t special, an, effect; } B[20] = {
    {0, 0, 100, 0, 1, 0, 0, 0},
    {300, 1, 100, 40, 20, 7, 52, 4},        /* Auto Chipper */
    {2000, 1, 100, 90, 20, 8, 53, 6},       /* Seagoon */
    {1000, 1, 100, 40, 20, 3, 54, 7},       /* Eyesore */
    {4000, 1, 100, 200, 10, 2, 55, 8},      /* Mad Endo */
    {5000, 1, 100, 250, 100, 1, 51, 9},     /* Browboy */
    {20000, 1, 100, 250, 100, 4, 56, 10},   /* Gold Endo */
    {15000, 1, 100, 250, 100, 7, 57, 11},   /* Bubba */
    {10000, 1, 100, 450, 100, 6, 58, 2},    /* Porkpatch */
    {1000, 1, 100, 60, 20, 9, 59, 5},       /* Bouncer */
    {100000, 0, 100, 1000, 500, 10, 60, 6}, /* the last */
    {15000, 1, 100, 200, 300, 0, 59, 7},    /* Snowcone (specials 5 to 9) */
    {10000, 1, 25, 400, 300, 0, 53, 8},     /* Supergoon (3 to 5) */
    {15000, 1, 10, 200, 100, 3, 55, 9},     /* Overclock */
    {50000, 1, 200, 999, 999, 11, 61, 10},  /* Security */
    {200000, 0, 75, 3000, 100, 0, 62, 11},  /* Chipper's Revenge (5 to 10) */
    {0, 0, 100, 0, 1, 0, 0, 0}, {0, 0, 100, 0, 1, 0, 0, 0}, {0, 0, 100, 0, 1, 0, 0, 0},
    {500000, 0, 1000, 999, 999, 0, 63, 2},  /* PurpleGeist */
  };
  if (boss < 1 || boss > 19 || !B[boss].an) boss = 1;
  if (life) *life = B[boss].life * (B[boss].times_diff ? diff : 1);
  if (speed) *speed = B[boss].speed;
  if (strength) *strength = B[boss].str + rnd_n(B[boss].str_r);
  if (special) *special = B[boss].special;
  if (an) *an = B[boss].an;
  return B[boss].effect;
}

/* 966-1000: each enemy's special ("enemy attack" A) and the picture its hits make ("what attack effect") */
int enemy_special(int type) {
  switch (type) {
    case 3: return 6;
    case 6: case 9: case 15: case 19: return 2;
    case 8: case 23: return 1;
    case 10: case 17: return 4;
    case 11: case 22: return 8;
    case 18: return 3;
    case 20: case 27: return 5;
    case 21: case 24: return 9;
    case 25: return 10;
    case 26: return 11;
    case 28: case 29: return 7;
    default: return 0;
  }
}

int enemy_hit_effect(int type) {
  static const uint8_t E[30] = {0, 9, 1, 2, 5, 8, 3, 10, 10, 5, 8, 10, 6, 4, 5, 7, 3, 4, 2, 11, 3, 5, 6, 2, 3, 4, 5, 6, 7, 8};
  if (type >= 51) return type == 51 ? 8 : type == 52 ? 9 : 10;
  return type >= 1 && type <= 29 ? E[type] : 1;
}
