/* Undertale: items (the game's ids are kept where it has them, for the record). */
#ifndef UT_ITEMS_H
#define UT_ITEMS_H
#include <stdint.h>

enum {
  ITEM_NONE,
  ITEM_MONSTER_CANDY,
  ITEM_STICK,
  ITEM_BANDAGE,
  ITEM_SPIDER_DONUT,
  ITEM_SPIDER_CIDER,
  ITEM_BUTTERSCOTCH_PIE,
  ITEM_SNAIL_PIE,
  ITEM_FADED_RIBBON,
  ITEM_TOY_KNIFE,
  /* Snowdin */
  ITEM_TOUGH_GLOVE,
  ITEM_MANLY_BANDANNA,
  ITEM_SNOWMAN_PIECE,
  ITEM_NICE_CREAM,
  ITEM_BISICLE,
  ITEM_UNISICLE,
  ITEM_CINNAMON_BUNNY,
  /* Waterfall */
  ITEM_OLD_TUTU,
  ITEM_BALLET_SHOES,
  ITEM_QUICHE,
  ITEM_PUNCH_CARD,
  ITEM_ARTIFACT,
  ITEM_ASTRONAUT_FOOD,
  ITEM_CRAB_APPLE,
  ITEM_SEA_TEA,
  ITEM_CLOUDY_GLASSES,
  ITEM_TORN_NOTEBOOK,
  ITEM_TEMMIE_FLAKES,
  /* Hotland */
  ITEM_BURNT_PAN,
  ITEM_INSTANT_NOODLES,
  ITEM_HOT_DOG,
  ITEM_STAINED_APRON,
  /* the MTT Resort */
  ITEM_STARFAIT,
  ITEM_GLAMBURGER,
  ITEM_LEGENDARY_HERO,
  ITEM_STEAK,
  ITEM_JUNK_FOOD,
  ITEM_EMPTY_GUN,
  ITEM_COWBOY_HAT,
  ITEM_MYSTERY_KEY,
  ITEM_HUSH_PUPPY,
  /* New Home */
  ITEM_WORN_DAGGER,
  ITEM_HEART_LOCKET,
  /* the artifact's room, in Waterfall */
  ITEM_ANNOYING_DOG,
  ITEM_DOG_RESIDUE,
  ITEM_DOG_SALAD,
  ITEM_COUNT
};
enum { IT_FOOD, IT_WEAPON, IT_ARMOR, IT_SPECIAL };

typedef struct {
  const char *name;        /* in the menu */
  const char *short_name;  /* in battle, 9 letters at most */
  uint8_t type;
  int16_t heal;            /* HP, 999 all of it */
  uint8_t at, df;
  const char *info;        /* what Info says after its name */
  const char *use;         /* what eating it says (before the HP) */
  uint16_t sell;
} ItemInfo;
const ItemInfo *item_info(int id);
/* uses the item in slot (the inventory's): fills `out` with what to say; in battle or not */
void item_use(int slot, char *out, int cap, int in_battle);
void item_info_text(int id, char *out, int cap);
#endif
