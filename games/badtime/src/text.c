/* Bad Time Simulator: the words, as jcw87's Bad Time Simulator has them (Undertale's, by Toby Fox), and the help.
 * Sans says nothing between attacks there (its readme: left out on purpose), only what the attack scripts and
 * the win give him. */
#include "fight.h"

/* what the attack scripts make him say (tools/attacks.py) */
const char *const TALK_SCRIPT[2] = {"ready?", "here we go."};
/* when he falls asleep and you win */
const char *const TALK_WIN[2] = {"huff... puff...", "alright, i guess\nyou win."};

/* the box between attacks: by KARMA, at least 0, 10 and 20 */
const char *const INFO_KR[3] = {
  "* You felt your sins crawling\n  on your back.",
  "* You felt your sins weighing\n  on your neck.",
  "* KARMA coursing through your\n  veins.",
};
/* or, before the first attack and at these hit attempts (13, 15, 19, 20, 21, 22) */
const char *const INFO_FIRST = "* You feel like you're going to\n  have a bad time.";
const char *const INFO_BREAK = "* Sans is taking a break.";
const char *const INFO_15 = "* The REAL battle finally begins.";
const char *const INFO_19 = "* Reading this doesn't seem\n  like the best use of time.";
const char *const INFO_20 = "* Sans is starting to look\n  really tired.";
const char *const INFO_21 = "* Sans is preparing something.";
const char *const INFO_22 = "* Sans is getting ready to\n  use his special attack.";
const char *const CHECK_TEXT = "* SANS 1 ATK 1 DEF\n* The easiest enemy.\n* Can only deal 1 damage.";
const char *const CHECK_TEXT2 = "* Can't keep dodging forever.\n* Keep attacking.";

/* name in the menu, full name, HP it heals */
const item_t ITEMS[4] = {
  {"Pie", "Butterscotch Pie", 99},
  {"I.Noodles", "Instant Noodles", 90},
  {"Steak", "Face Steak", 60},
  {"L. Hero", "Legendary Hero", 40},
};

const char *const HELP[HELP_PAGES] = {
  "Dodge everything. The arrows move your heart.\n"
  "Red heart: it goes where you push it.\n"
  "Blue heart: it falls. Up jumps, higher while held.\n"
  "Hold shift to move slowly.",
  "White bones always hurt. Blue ones hurt only\n"
  "while you move, orange ones only when you don't.\n"
  "A hit takes 1 HP and adds KR, which drains your\n"
  "HP after it, faster the more there is.",
  "Between attacks: FIGHT (he dodges), ACT, ITEM\n"
  "(eat to heal) or MERCY. Survive all 24 attacks.\n"
  "Practice: retry each attack until you pass it.\n"
  "Back pauses. Home saves and quits.",
  "Undertale by Toby Fox: Sans, his attacks, the\n"
  "pictures and words, as in jcw87's Bad Time\n"
  "Simulator. Not affiliated. Pixel Operator font.\n"
  "Made by Mason Chen as part of NumPlay.",
};
