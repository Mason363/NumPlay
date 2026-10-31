/* Bad Time Simulator: the words (the game's own: the Judge's lines, the box's text, the items, the help). */
#include "fight.h"

/* what the Judge says where the scripts speak (tools/attacks.py: "ready?", "here we go.") */
const char *const TALK_SCRIPT[2] = {
  "Court is in session.",
  "That was the opening statement.",
};

/* before each attack of the first half (by the attack's number in the fight) */
const char *const TALK_FIRST[14] = {
  0,
  "Not bad. Most don't get past that.",
  "Watch the blue ones. Stand still and they pass.",
  "You've been busy down here, haven't you?",
  "Every name on that list. I read them all.",
  "Keep your feet under you.",
  "I don't like doing this. You made it a job.",
  "Still standing? Huh.",
  "You know why I wear this hat? Neither do I.",
  "Do you even remember why you started?",
  "It's not too late to stop. Just very, very late.",
  "You're good at this. That's the problem.",
  "My arm's getting tired. My patience isn't.",
  "One more round. Then we'll see.",
};
const char *const TALK_BREAK = "I'm calling a recess. Put it down and we both walk out of here.";
/* the second half */
const char *const TALK_SECOND[10] = {
  "Recess is over. No more warnings.",
  "Each time you come back, you look a little less sorry.",
  "I've seen how this ends. I've seen it a lot.",
  "Hold still. It's easier for both of us.",
  "...",
  "Getting heavy, isn't it? The weight.",
  "I'm not tired. I'm saving my strength.",
  "Alright. Here's my last ruling.",
  "Still here.",
  0,
};
const char *const TALK_FINAL = "Case closed.";
const char *const TALK_WIN[2] = {
  "...can't keep my eyes open.",
  "Fine. You win. Go on.",
};

/* the box, between attacks */
const char *const INFO_DEFAULT = "* The JUDGE taps his gavel.";
const char *const INFO_FIRST = "* You feel this won't go well.";
const char *const INFO_KARMA = "* Something heavy clings to you.";
const char *const INFO_BREAK = "* The JUDGE calls a recess.";
const char *const INFO_15 = "* The real trial begins.";
const char *const INFO_19 = "* Reading this won't help you.";
const char *const INFO_20 = "* The JUDGE looks worn out.";
const char *const INFO_21 = "* The JUDGE is preparing\n  something.";
const char *const INFO_22 = "* The JUDGE is getting ready\n  for his final ruling.";
const char *const CHECK_TEXT = "* JUDGE  ATK 1  DEF 1\n* Never been hit.\n* Every hit takes 1 HP.";
const char *const CHECK_TEXT2 = "* He can't dodge forever.\n* Keep swinging.";

const item_t ITEMS[4] = {
  {"Pie", "Pie", 99},
  {"Noodles", "Noodles", 90},
  {"Steak", "Steak", 60},
  {"Hero", "Hero sandwich", 40},
};

const char *const HELP[3] = {
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
};
