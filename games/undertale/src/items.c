/* Items: what they are and what using one does (items.h). Their texts are the game's. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "items.h"

static const ItemInfo items[ITEM_COUNT] = {
  [ITEM_NONE] = {"", "", IT_SPECIAL, 0, 0, 0, "", "", 0},
  [ITEM_MONSTER_CANDY] = {"Monster Candy", "MnstrCndy", IT_FOOD, 10, 0, 0, "Has a distinct,\n  non-licorice flavor.",
                          "* You ate the Monster Candy.\n* Very un-licorice-like.", 25},
  [ITEM_STICK] = {"Stick", "Stick", IT_WEAPON, 0, 0, 0, "Its bark is worse than\n  its bite.",
                  "* You threw the stick away.\n* Then picked it back up.", 150},
  [ITEM_BANDAGE] = {"Bandage", "Bandage", IT_ARMOR, 10, 0, 0, "It has already been used\n  several times.",
                    "* You re-applied the bandage.\n* Still kind of gooey.", 150},
  [ITEM_SPIDER_DONUT] = {"Spider Donut", "SpidrDont", IT_FOOD, 12, 0, 0, "A donut made with Spider\n  Cider in the batter.",
                         "* You ate the Spider Donut.", 30},
  [ITEM_SPIDER_CIDER] = {"Spider Cider", "SpidrCidr", IT_FOOD, 24, 0, 0, "Made with whole spiders,\n  not just the juice.",
                         "* You drank the Spider Cider.", 60},
  [ITEM_BUTTERSCOTCH_PIE] = {"Butterscotch Pie", "ButtsPie", IT_FOOD, 999, 0, 0, "Butterscotch-cinnamon\n  pie, one slice.",
                             "* You ate the Butterscotch\n  Pie.", 180},
  [ITEM_SNAIL_PIE] = {"Snail Pie", "SnailPie", IT_FOOD, 998, 0, 0, "An acquired taste.",
                      "* You ate the Snail Pie.\n* Your HP was maxed. Almost.", 350},
  [ITEM_FADED_RIBBON] = {"Faded Ribbon", "Ribbon", IT_ARMOR, 0, 0, 3, "If you're cuter, monsters\n  won't hit you as hard.", "", 0},
  [ITEM_TOY_KNIFE] = {"Toy Knife", "Toy Knife", IT_WEAPON, 0, 3, 0, "Made of plastic.\n* A rarity nowadays.", "", 100},
  [ITEM_TOUGH_GLOVE] = {"Tough Glove", "TuffGlove", IT_WEAPON, 0, 5, 0, "A worn pink leather glove.\n* For five-fingered folk.", "", 50},
  [ITEM_MANLY_BANDANNA] = {"Manly Bandanna", "Mandanna", IT_ARMOR, 0, 0, 7, "It has seen some wear.\n* It has abs drawn on it.", "", 50},
  [ITEM_SNOWMAN_PIECE] = {"Snowman Piece", "SnowPiece", IT_FOOD, 45, 0, 0, "Please take this to the\n  ends of the earth.",
                          "* You ate the Snowman Piece.", 40},
  [ITEM_NICE_CREAM] = {"Nice Cream", "NiceCream", IT_FOOD, 15, 0, 0, "Instead of a joke, the\n  wrapper says something nice.",
                       "* You unwrapped the Nice Cream.", 2},
  [ITEM_BISICLE] = {"Bisicle", "Bisicle", IT_FOOD, 11, 0, 0, "It's a two-pronged popsicle,\n  so you can eat it twice.",
                    "* You eat one half of the\n  Bisicle.", 5},
  [ITEM_UNISICLE] = {"Unisicle", "Unisicle", IT_FOOD, 11, 0, 0, "It's a SINGLE-pronged popsicle.\n* Wait, that's just normal...",
                     "* You ate the Unisicle.", 2},
  [ITEM_CINNAMON_BUNNY] = {"Cinnamon Bunny", "CinnaBun", IT_FOOD, 22, 0, 0, "A cinnamon roll in the\n  shape of a bunny.",
                           "* You eat the Cinnamon Bunny.", 8},
  [ITEM_OLD_TUTU] = {"Old Tutu", "Old Tutu", IT_ARMOR, 0, 0, 10, "A dusty frilly skirt.\n* Still holds up well.", "", 80},
  [ITEM_BALLET_SHOES] = {"Ballet Shoes", "BallShoes", IT_WEAPON, 0, 7, 0, "Worn dancing shoes.\n* Too big for you.", "", 80},
  [ITEM_QUICHE] = {"Abandoned Quiche", "Quiche", IT_FOOD, 34, 0, 0, "A quiche someone left\n  under a bench.",
                   "* You eat the quiche.", 76},
  [ITEM_PUNCH_CARD] = {"Punch Card", "PunchCard", IT_SPECIAL, 0, 0, 0, "A card for a game nobody\n  plays anymore.",
                       "* You look at the punch card.\n* It makes you want to punch\n  something.", 15},
  [ITEM_ARTIFACT] = {"Legendary Artifact", "Artifact", IT_SPECIAL, 0, 0, 0, "A mysterious orb.\n* It's a little heavy.",
                     "* You hold the orb up.\n* Nothing happens.", 0},
  [ITEM_ASTRONAUT_FOOD] = {"Astronaut Food", "AstroFood", IT_FOOD, 21, 0, 0, "Food for a trip to space,\n  found in a cooler.",
                           "* You eat the Astronaut Food.", 25},
  [ITEM_CRAB_APPLE] = {"Crab Apple", "CrabApple", IT_FOOD, 18, 0, 0, "A fruit with little claws.",
                       "* You eat the Crab Apple.", 5},
  [ITEM_SEA_TEA] = {"Sea Tea", "SeaTea", IT_FOOD, 10, 0, 0, "Tea made from sea grass.",
                    "* You drink the Sea Tea.\n* Your feet feel lighter.", 5},
  [ITEM_CLOUDY_GLASSES] = {"Cloudy Glasses", "Glasses", IT_ARMOR, 0, 0, 5, "Glasses you can barely\n  see through.", "", 30},
  [ITEM_TORN_NOTEBOOK] = {"Torn Notebook", "TornNotbo", IT_WEAPON, 0, 2, 0, "An old notebook, torn up.\n* It doesn't hit very hard.", "", 50},
  [ITEM_TEMMIE_FLAKES] = {"Temmie Flakes", "TemFlakes", IT_FOOD, 2, 0, 0, "Bits of paper, sold as\n  food.",
                          "* You eat the Temmie Flakes.", 2},
  [ITEM_BURNT_PAN] = {"Burnt Pan", "BurntPan", IT_WEAPON, 0, 10, 0,
                      "A pan, burnt black. Hits\n  hard, every time.\n* Food heals 4 HP more with it.", "", 50},
  [ITEM_INSTANT_NOODLES] = {"Instant Noodles", "InstaNood", IT_FOOD, 15, 0, 0, "A cup of noodles, quick to\n  make.",
                            "* You eat the Instant Noodles.\n* They're a little crunchy.", 2},
  [ITEM_HOT_DOG] = {"Hot Dog...?", "HotDog", IT_FOOD, 20, 0, 0, "A hot dog from Sans's stand.\n* Better not ask what's in it.",
                    "* You eat the Hot Dog.\n* (Bark!)", 10},
  [ITEM_STAINED_APRON] = {"Stained Apron", "StainApro", IT_ARMOR, 0, 0, 11, "An apron with old stains.\n* Heals 1 HP every other turn.",
                          "", 50},
  [ITEM_STARFAIT] = {"Starfait", "Starfait", IT_FOOD, 14, 0, 0, "A tall, sparkly dessert.\n* Very popular.",
                     "* You eat the Starfait.\n* It's sweet and fizzy.", 5},
  [ITEM_GLAMBURGER] = {"Glamburger", "GlamBurg", IT_FOOD, 27, 0, 0, "A burger that glitters all\n  over. Very popular.",
                       "* You eat the Glamburger.\n* It crunches. It's the glitter.", 15},
  [ITEM_LEGENDARY_HERO] = {"Legendary Hero", "L. Hero", IT_FOOD, 40, 0, 0, "A sandwich shaped like a\n  sword.",
                           "* You eat the Legendary Hero.\n* You feel brave!", 40},
  [ITEM_STEAK] = {"Face Steak", "FaceSteak", IT_FOOD, 60, 0, 0, "A steak in the shape of\n  Mettaton's face. Don't ask.",
                  "* You eat the steak.\n* ...It looks back at you.", 20},
  [ITEM_JUNK_FOOD] = {"Junk Food", "JunkFood", IT_FOOD, 17, 0, 0, "Food someone threw away,\n  probably.",
                      "* You eat the Junk Food.\n* Someone took a bite first.", 1},
  [ITEM_EMPTY_GUN] = {"Empty Gun", "EmptyGun", IT_WEAPON, 0, 12, 0, "An old revolver with no\n  bullets in it.", "", 100},
  [ITEM_COWBOY_HAT] = {"Cowboy Hat", "CowboyHat", IT_ARMOR, 0, 5, 12, "A worn hat. Makes you want\n  a beard. (+5 AT)", "", 50},
  [ITEM_MYSTERY_KEY] = {"Mystery Key", "MystryKey", IT_SPECIAL, 0, 0, 0, "A key, too bent to go on\n  your keychain.", "", 500},
  [ITEM_HUSH_PUPPY] = {"Hush Puppy", "HushPupy", IT_FOOD, 65, 0, 0, "A spell that keeps a dog\n  from casting magic.",
                       "* You eat the Hush Puppy.\n* Dog-magic is neutralized.", 150},
  [ITEM_WORN_DAGGER] = {"Worn Dagger", "WornDG", IT_WEAPON, 0, 15, 0, "Good for cutting plants and\n  vines.", "", 250},
  [ITEM_HEART_LOCKET] = {"Heart Locket", "H.Locket", IT_ARMOR, 0, 0, 15, "It says \"Best Friends\n  Forever.\"", "", 250},
  [ITEM_ANNOYING_DOG] = {"Annoying Dog", "Annoy Dog", IT_SPECIAL, 0, 0, 0, "A little white dog.\n* It's fast asleep...",
                         "* You deployed the dog.", 999},
  [ITEM_DOG_RESIDUE] = {"Dog Residue", "DogResidu", IT_SPECIAL, 0, 0, 0, "Shiny trail left behind by\n  a dog.",
                        "* You used the Dog Residue.", 1},
  [ITEM_DOG_SALAD] = {"Dog Salad", "Dog Salad", IT_FOOD, 0, 0, 0, "Recovers HP. (Hit Poodles.)", "* You ate the Dog Salad.", 8},
};

/* the Dog Residue's other looks (the game's six kinds of it) */
static const char *const residue[] = {"Shiny trail left behind by\n  a dog.", "Dog-shaped husk shed from\n  a dog's carapace.",
                                      "Dirty dishes left unwashed\n  by a dog.", "Glowing crystals secreted\n  by a dog.",
                                      "Jigsaw puzzle left\n  unfinished by a dog.", "Web spun by a dog to\n  ensnare prey."};

const ItemInfo *item_info(int id) { return &items[id >= 0 && id < ITEM_COUNT ? id : 0]; }

void item_info_text(int id, char *out, int cap) {
  const ItemInfo *it = item_info(id);
  if (id == ITEM_DOG_RESIDUE) {
    snprintf(out, (size_t)cap, "* \"%s\"\n* %s", it->name, residue[rnd_n(6)]);
    return;
  }
  if (id == ITEM_DOG_SALAD) {
    snprintf(out, (size_t)cap, "* \"%s\" - Heals ?? HP\n* %s", it->name, it->info);
    return;
  }
  switch (it->type) {
    case IT_WEAPON: snprintf(out, (size_t)cap, "* \"%s\" - Weapon AT %d\n* %s", it->name, it->at, it->info); break;
    case IT_ARMOR:
      if (id == ITEM_BANDAGE) snprintf(out, (size_t)cap, "* \"%s\" - Heals %d HP\n* %s", it->name, it->heal, it->info);
      else snprintf(out, (size_t)cap, "* \"%s\" - Armor DF %d\n* %s", it->name, it->df, it->info);
      break;
    case IT_SPECIAL: snprintf(out, (size_t)cap, "* \"%s\"\n* %s", it->name, it->info); break;
    default:
      if (it->heal >= 998) snprintf(out, (size_t)cap, "* \"%s\" - Heals ALL HP\n* %s", it->name, it->info);
      else snprintf(out, (size_t)cap, "* \"%s\" - Heals %d HP\n* %s", it->name, it->heal, it->info);
      break;
  }
}

/* heals and says so: "You recovered N HP!" or "Your HP was maxed out." */
static int heal(int n, char *out, int cap) {
  int before = sv.hp;
  sv.hp = (int16_t)min_i(sv.maxhp, sv.hp + n);
  if (sv.hp >= sv.maxhp) return snprintf(out, (size_t)cap, "\n* Your HP was maxed out.");
  return snprintf(out, (size_t)cap, "\n* You recovered %d HP!", sv.hp - before);
}

void item_use(int slot, char *out, int cap, int in_battle) {
  int id = sv.items[slot];
  const ItemInfo *it = item_info(id);
  out[0] = 0;
  switch (it->type) {
    case IT_WEAPON: {
      if (id == ITEM_STICK) {
        snprintf(out, (size_t)cap, "%s", it->use);
        return;
      }
      int old = sv.weapon;
      sv.weapon = (uint8_t)id;
      sv.items[slot] = (uint8_t)(old == ITEM_STICK ? 0 : old);
      if (!sv.items[slot]) item_remove_at(slot);
      snprintf(out, (size_t)cap, "* You equipped the %s.", it->name);
      return;
    }
    case IT_ARMOR:
      if (id == ITEM_BANDAGE && sv.armor != ITEM_BANDAGE) {
        int n = snprintf(out, (size_t)cap, "%s", it->use);
        item_remove_at(slot);
        heal(it->heal, out + n, cap - n);
        return;
      } else {
        int old = sv.armor;
        sv.armor = (uint8_t)id;
        sv.items[slot] = (uint8_t)old;
        if (!old) item_remove_at(slot);
        snprintf(out, (size_t)cap, "* You equipped the %s.", it->name);
        return;
      }
    case IT_SPECIAL:   /* looked at, kept */
      if (id == ITEM_DOG_RESIDUE) {
        /* the rest of the inventory fills up with it (now and then a Dog Salad) */
        int n = 0;
        for (int i = 0; i < NUM_ITEMS; i++)
          if (!sv.items[i]) sv.items[i] = (uint8_t)(rnd_n(7) ? ITEM_DOG_RESIDUE : ITEM_DOG_SALAD), n++;
        if (n) snprintf(out, (size_t)cap, "%s\n* The rest of your inventory\n  filled up with Dog Residue.", it->use);
        else snprintf(out, (size_t)cap, "%s|* ...|* You finished using it.\n* An uneasy atmosphere fills\n  the room.", it->use);
        return;
      }
      snprintf(out, (size_t)cap, "%s", it->use);
      return;
    default: {
      int n = snprintf(out, (size_t)cap, "%s", it->use);
      if (id == ITEM_NICE_CREAM) {   /* its wrapper's kind words */
        static const char *const nice[] = {"You're just great!", "You look nice today!", "Are those claws natural?",
                                           "You're super spiffy!", "Have a wonderful day!", "Is this as sweet as you?"};
        n += snprintf(out + n, (size_t)(cap - n), "\n* \"%s\"", nice[rnd_n(6)]);
      }
      /* half a Bisicle is a Unisicle */
      if (id == ITEM_BISICLE) sv.items[slot] = ITEM_UNISICLE;
      else item_remove_at(slot);
      if (id == ITEM_DOG_SALAD) {   /* what it turns out to be, and so what it heals */
        static const char *const what[] = {"Oh. There are bones...", "Oh. Fried tennis ball...", "Oh. Tastes yappy...",
                                           "It's literally garbage???"};
        static const int16_t heals[] = {2, 10, 30, 999};
        int k = rnd_n(4);
        n += snprintf(out + n, (size_t)(cap - n), "\n* %s", what[k]);
        if (heals[k] >= 999) {
          sv.hp = sv.maxhp;
          snprintf(out + n, (size_t)(cap - n), "\n* Your HP was maxed out.");
        } else {
          heal(heals[k] + (sv.weapon == ITEM_BURNT_PAN ? 4 : 0), out + n, cap - n);
        }
        return;
      }
      if (it->heal >= 999) {
        sv.hp = sv.maxhp;
        snprintf(out + n, (size_t)(cap - n), "\n* Your HP was maxed out.");
      } else if (it->heal == 998) {
        if (sv.hp < sv.maxhp - 1) sv.hp = (int16_t)(sv.maxhp - 1);
      } else {
        heal(it->heal + (sv.weapon == ITEM_BURNT_PAN ? 4 : 0), out + n, cap - n);
      }
      (void)in_battle;
      return;
    }
  }
}
