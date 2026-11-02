/* made by tools/tables.py from the original's scripts: items, weapons, armors, spells, enemies and
 * battles (see tables.h) */
#include "data.h"
#include "tables.h"

const ItemDef item_defs[16] = {
  {0xFFFF, 0xFFFF, 0, 0, 0},
  {scr_iteminfo_slash_scr_iteminfo_gml_14_0, scr_iteminfo_slash_scr_iteminfo_gml_15_0, 1, 25, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_21_0, scr_iteminfo_slash_scr_iteminfo_gml_22_0, 1, 400, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_28_0, scr_iteminfo_slash_scr_iteminfo_gml_29_0, 0, 200, 0},
  {scr_iteminfo_slash_scr_iteminfo_gml_35_0, scr_iteminfo_slash_scr_iteminfo_gml_36_0, 2, 1, 0},
  {scr_iteminfo_slash_scr_iteminfo_gml_42_0, scr_iteminfo_slash_scr_iteminfo_gml_43_0, 1, 5, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_49_0, scr_iteminfo_slash_scr_iteminfo_gml_50_0, 2, 150, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_56_0, scr_iteminfo_slash_scr_iteminfo_gml_57_0, 2, 5, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_63_0, scr_iteminfo_slash_scr_iteminfo_gml_64_0, 1, 70, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_70_0, scr_iteminfo_slash_scr_iteminfo_gml_71_0, 1, 10, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_77_0, scr_iteminfo_slash_scr_iteminfo_gml_78_0, 1, 10, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_84_0, scr_iteminfo_slash_scr_iteminfo_gml_85_0, 2, 70, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_91_0, scr_iteminfo_slash_scr_iteminfo_gml_92_0, 1, 40, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_98_0, scr_iteminfo_slash_scr_iteminfo_gml_99_0, 1, 40, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_105_0, scr_iteminfo_slash_scr_iteminfo_gml_106_0, 1, 10, 1},
  {scr_iteminfo_slash_scr_iteminfo_gml_112_0, scr_iteminfo_slash_scr_iteminfo_gml_113_0, 1, 50, 1},
};
const int item_count = 16;

const EquipDef weapon_defs[11] = {
  {0xFFFF, 0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_29_0, scr_weaponinfo_slash_scr_weaponinfo_gml_30_0, 0, 0, 0, 1, 0, 0, 1, 60},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_54_0, scr_weaponinfo_slash_scr_weaponinfo_gml_55_0, 0, 0, 0, 0, 0, 0, 2, 80},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_75_0, scr_weaponinfo_slash_scr_weaponinfo_gml_76_0, 0, 0, 0, 0, 0, 1, 3, 100},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_97_0, scr_weaponinfo_slash_scr_weaponinfo_gml_98_0, 12, 6, 8, 1, 1, 1, 0, 150},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_118_0, scr_weaponinfo_slash_scr_weaponinfo_gml_119_0, 2, 0, 0, 1, 0, 0, 1, 200},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_139_0, scr_weaponinfo_slash_scr_weaponinfo_gml_140_0, 2, 0, 0, 0, 1, 0, 2, 150},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_160_0, scr_weaponinfo_slash_scr_weaponinfo_gml_161_0, 5, 0, 4, 0, 1, 0, 2, 0},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_181_0, scr_weaponinfo_slash_scr_weaponinfo_gml_182_0, 4, 0, 0, 1, 0, 0, 1, 250},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_202_0, scr_weaponinfo_slash_scr_weaponinfo_gml_203_0, 2, 0, 0, 0, 0, 1, 3, 200},
  {scr_weaponinfo_slash_scr_weaponinfo_gml_223_0, scr_weaponinfo_slash_scr_weaponinfo_gml_224_0, 0, 0, 2, 0, 0, 1, 3, 200},
};
const int weapon_count = 11;

const EquipDef armor_defs[8] = {
  {0xFFFF, 0xFFFF, 0, 0, 0, 1, 0, 1, 0, 0},
  {scr_armorinfo_slash_scr_armorinfo_gml_26_0, scr_armorinfo_slash_scr_armorinfo_gml_27_0, 0, 1, 0, 1, 1, 1, 4, 100},
  {scr_armorinfo_slash_scr_armorinfo_gml_46_0, scr_armorinfo_slash_scr_armorinfo_gml_47_0, 0, 2, 0, 1, 1, 1, 4, 150},
  {scr_armorinfo_slash_scr_armorinfo_gml_66_0, scr_armorinfo_slash_scr_armorinfo_gml_67_0, 0, 1, 0, 1, 0, 1, 4, 100},
  {scr_armorinfo_slash_scr_armorinfo_gml_87_0, scr_armorinfo_slash_scr_armorinfo_gml_88_0, 0, 2, 0, 1, 0, 1, 4, 90},
  {scr_armorinfo_slash_scr_armorinfo_gml_108_0, scr_armorinfo_slash_scr_armorinfo_gml_109_0, 1, 2, 0, 1, 1, 1, 4, 150},
  {scr_armorinfo_slash_scr_armorinfo_gml_128_0, scr_armorinfo_slash_scr_armorinfo_gml_129_0, 0, 0, 2, 1, 1, 1, 4, 120},
  {scr_armorinfo_slash_scr_armorinfo_gml_148_0, scr_armorinfo_slash_scr_armorinfo_gml_149_0, 2, 2, 2, 1, 1, 1, 4, 0},
};
const int armor_count = 8;

const SpellDef spell_defs[8] = {
  {0xFFFF, scr_spellinfo_slash_scr_spellinfo_gml_18_0, 0xFFFF, -1, 0, 0},
  {scr_spellinfo_slash_scr_spellinfo_gml_22_0, scr_spellinfo_slash_scr_spellinfo_gml_23_0, scr_spellinfo_slash_scr_spellinfo_gml_21_0, 125, 2, 0},
  {scr_spellinfo_slash_scr_spellinfo_gml_32_0, scr_spellinfo_slash_scr_spellinfo_gml_33_0, scr_spellinfo_slash_scr_spellinfo_gml_31_0, 80, 1, 1},
  {scr_spellinfo_slash_scr_spellinfo_gml_43_0, scr_spellinfo_slash_scr_spellinfo_gml_44_0, scr_spellinfo_slash_scr_spellinfo_gml_42_0, 40, 2, 0},
  {scr_spellinfo_slash_scr_spellinfo_gml_53_0, scr_spellinfo_slash_scr_spellinfo_gml_54_0, scr_spellinfo_slash_scr_spellinfo_gml_52_0, 100, 2, 0},
  {scr_spellinfo_slash_scr_spellinfo_gml_65_0, scr_spellinfo_slash_scr_spellinfo_gml_66_0, scr_spellinfo_slash_scr_spellinfo_gml_64_0, 0, 2, 0},
  {scr_spellinfo_slash_scr_spellinfo_gml_76_0, scr_spellinfo_slash_scr_spellinfo_gml_77_0, scr_spellinfo_slash_scr_spellinfo_gml_75_0, 0, 0, 0},
  {scr_spellinfo_slash_scr_spellinfo_gml_87_0, scr_spellinfo_slash_scr_spellinfo_gml_88_0, scr_spellinfo_slash_scr_spellinfo_gml_86_0, 0, 0, 0},
};
const int spell_count = 8;

const MonsterDef monster_defs[26] = {
  {0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFFF,
   {{0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_4_0, 130, 7, 0, 0, 0, 10, 100, 0, scr_monstersetup_slash_scr_monstersetup_gml_27_0,
   {{scr_monstersetup_slash_scr_monstersetup_gml_16_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_18_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_20_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_22_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_24_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_26_0, 0xFFFF, 1, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_32_0, 540, 5, 1, 0, 20, 10, 100, 0, scr_monstersetup_slash_scr_monstersetup_gml_51_0,
   {{scr_monstersetup_slash_scr_monstersetup_gml_44_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_47_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_50_0, 0xFFFF, 1, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_59_0, 450, 0, 0, 0, 0, 0, 100, 0, scr_monstersetup_slash_scr_monstersetup_gml_78_0,
   {{scr_monstersetup_slash_scr_monstersetup_gml_71_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_73_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_75_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_83_0, 90, 8, 6, 0, 0, 0, 100, 0, scr_monstersetup_slash_scr_monstersetup_gml_98_0,
   {{scr_monstersetup_slash_scr_monstersetup_gml_95_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_97_0, 0xFFFF, 1, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_103_0, 120, 5, 0, 0, 30, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_115_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_117_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_119_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_123_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_132_0, 150, 6, 0, 0, 28, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_144_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_146_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_148_0, 0xFFFF, 1, 0, 2}, {scr_monstersetup_slash_scr_monstersetup_gml_160_0, 0xFFFF, 1, 0, 2}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_170_0, 270, 8, 1, 0, 43, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_184_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_208_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_210_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_212_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_218_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_226_0, 170, 8, 0, 0, 50, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_238_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_240_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_242_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_251_0, 10, 5, 0, 0, 10, 0, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_263_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_271_0, 0xFFFF, 1, 0, 2}, {scr_monstersetup_slash_scr_monstersetup_gml_274_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_282_0, 1300, 7, 3, 0, 100, 0, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_294_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_296_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_298_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_307_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_315_0, 140, 7, 1, 0, 23, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_327_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_329_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_331_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_337_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_346_0, 2400, 4, -40, 0, 0, 0, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_358_0, 0xFFFF, 1, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_365_0, 120, 8, 1, 0, 38, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_377_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_379_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_381_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_386_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_394_0, 130, 9, 2, 0, 38, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_406_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_408_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_412_0, 0xFFFF, 1, 0, 2}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_420_0, 90, 5, 0, 0, 20, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_432_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_434_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_440_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_449_0, 270, 6, 1, 0, 80, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_463_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_465_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_467_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_470_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_473_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_475_0, 0xFFFF, 1, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_484_0, 700, 6, 0, 0, 0, 0, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_496_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_498_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_501_0, 0xFFFF, 1, 0, 3}, {scr_monstersetup_slash_scr_monstersetup_gml_504_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_506_0, 0xFFFF, 1, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_512_0, 800, 6, 1, 0, 0, 0, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_524_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_527_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_530_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_538_0, 120, 7, 0, 0, 0, 0, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_550_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_553_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_556_0, 0xFFFF, 1, 0, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_562_0, 3500, 10, 5, 0, 0, 0, 999, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_574_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_579_0, scr_monstersetup_slash_scr_monstersetup_gml_580_0, 1, 50, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_585_0, scr_monstersetup_slash_scr_monstersetup_gml_586_0, 1, 125, 4}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_594_0, 1300, 8, 3, 0, 100, 0, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_607_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_609_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_611_0, 0xFFFF, 1, 0, 4}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_619_0, 170, 8, 0, 0, 45, 25, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_631_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_633_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_635_0, 0xFFFF, 1, 0, 2}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_643_0, 190, 8, 0, 0, 40, 10, 100, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_655_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_657_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_659_0, 0xFFFF, 1, 0, 2}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFFF,
   {{0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
  {scr_monstersetup_slash_scr_monstersetup_gml_665_0, 2800, 8, 0, 0, 0, 0, 999, 0, 0xFFFF,
   {{scr_monstersetup_slash_scr_monstersetup_gml_677_0, 0xFFFF, 1, 0, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_704_0, scr_monstersetup_slash_scr_monstersetup_gml_705_0, 1, 62, 1}, {scr_monstersetup_slash_scr_monstersetup_gml_714_0, scr_monstersetup_slash_scr_monstersetup_gml_715_0, 1, 150, 2}, {scr_monstersetup_slash_scr_monstersetup_gml_724_0, scr_monstersetup_slash_scr_monstersetup_gml_725_0, 1, 125, 3}, {0xFFFF, 0xFFFF, 0, 0, 1}, {0xFFFF, 0xFFFF, 0, 0, 1}}},
};
const int monster_count = 26;

const EncounterDef encounter_defs[41] = {
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{1, obj_placeholderenemy, 480, 110}, {1, obj_placeholderenemy, 500, 200}, {0, -1, 0, 0}}, 0xFFFF}, /* obj_placeholderenemy obj_placeholderenemy */
  {{{2, obj_lancerboss, 540, 200}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /* obj_lancerboss */
  {{{3, obj_dummyenemy, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /* obj_dummyenemy */
  {{{5, obj_diamondenemy, 480, 140}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_84_0}, /* obj_diamondenemy */
  {{{5, obj_diamondenemy, 480, 110}, {5, obj_diamondenemy, 500, 200}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_100_0}, /* obj_diamondenemy obj_diamondenemy */
  {{{5, obj_diamondenemy, 480, 110}, {6, obj_heartenemy, 500, 200}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_116_0}, /* obj_diamondenemy obj_heartenemy */
  {{{9, obj_smallcheckers_enemy, 440, 150}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_128_0}, /* obj_smallcheckers_enemy */
  {{{16, obj_clubsenemy, 400, 120}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_139_0}, /* obj_clubsenemy */
  {{{6, obj_heartenemy, 480, 20}, {6, obj_heartenemy, 500, 120}, {6, obj_heartenemy, 460, 220}}, scr_encountersetup_slash_scr_encountersetup_gml_158_0}, /* obj_heartenemy obj_heartenemy obj_heartenemy */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{10, obj_checkers_enemy, 480, 120}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_172_0}, /* obj_checkers_enemy */
  {{{11, obj_ponman_enemy, 480, 110}, {11, obj_ponman_enemy, 500, 200}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_185_0}, /* obj_ponman_enemy obj_ponman_enemy */
  {{{11, obj_ponman_enemy, 480, 20}, {11, obj_ponman_enemy, 500, 120}, {11, obj_ponman_enemy, 460, 220}}, scr_encountersetup_slash_scr_encountersetup_gml_206_0}, /* obj_ponman_enemy obj_ponman_enemy obj_ponman_enemy */
  {{{7, obj_clubsenemy, 400, 30}, {6, obj_heartenemy, 420, 200}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_223_0}, /* obj_clubsenemy obj_heartenemy */
  {{{13, obj_rabbick_enemy, 480, 140}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_234_0}, /* obj_rabbick_enemy */
  {{{13, obj_rabbick_enemy, 480, 60}, {13, obj_rabbick_enemy, 460, 180}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_250_0}, /* obj_rabbick_enemy obj_rabbick_enemy */
  {{{14, obj_bloxer_enemy, 480, 140}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_261_0}, /* obj_bloxer_enemy */
  {{{14, obj_bloxer_enemy, 480, 60}, {14, obj_bloxer_enemy, 460, 180}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_277_0}, /* obj_bloxer_enemy obj_bloxer_enemy */
  {{{12, obj_lancerboss2, 480, 160}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_292_0}, /* obj_lancerboss2 */
  {{{15, obj_jigsawryenemy, 480, 140}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_305_0}, /* obj_jigsawryenemy */
  {{{15, obj_jigsawryenemy, 480, 20}, {15, obj_jigsawryenemy, 500, 120}, {15, obj_jigsawryenemy, 460, 220}}, scr_encountersetup_slash_scr_encountersetup_gml_324_0}, /* obj_jigsawryenemy obj_jigsawryenemy obj_jigsawryenemy */
  {{{15, obj_jigsawryenemy, 480, 20}, {5, obj_diamondenemy, 500, 120}, {6, obj_heartenemy, 460, 220}}, scr_encountersetup_slash_scr_encountersetup_gml_343_0}, /* obj_jigsawryenemy obj_diamondenemy obj_heartenemy */
  {{{13, obj_rabbick_enemy, 480, 60}, {5, obj_diamondenemy, 460, 180}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_358_0}, /* obj_rabbick_enemy obj_diamondenemy */
  {{{20, obj_joker, 500, 160}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_384_0}, /* obj_joker */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{21, obj_checkers_enemy, 480, 120}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_397_0}, /* obj_checkers_enemy */
  {{{22, obj_rudinnranger, 480, 110}, {22, obj_rudinnranger, 500, 200}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_414_0}, /* obj_rudinnranger obj_rudinnranger */
  {{{23, obj_headhathy, 480, 110}, {23, obj_headhathy, 500, 200}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_430_0}, /* obj_headhathy obj_headhathy */
  {{{23, obj_headhathy, 480, 20}, {23, obj_headhathy, 500, 120}, {23, obj_headhathy, 460, 220}}, scr_encountersetup_slash_scr_encountersetup_gml_449_0}, /* obj_headhathy obj_headhathy obj_headhathy */
  {{{19, obj_susieenemy, 520, 80}, {18, obj_lancerboss3, 540, 240}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_463_0}, /* obj_susieenemy obj_lancerboss3 */
  {{{13, obj_rabbick_enemy, 480, 20}, {13, obj_rabbick_enemy, 500, 120}, {13, obj_rabbick_enemy, 460, 220}}, scr_encountersetup_slash_scr_encountersetup_gml_484_0}, /* obj_rabbick_enemy obj_rabbick_enemy obj_rabbick_enemy */
  {{{5, obj_diamondenemy, 480, 20}, {6, obj_heartenemy, 500, 120}, {5, obj_diamondenemy, 460, 220}}, scr_encountersetup_slash_scr_encountersetup_gml_505_0}, /* obj_diamondenemy obj_heartenemy obj_diamondenemy */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{0, -1, 0, 0}, {0, -1, 0, 0}, {0, -1, 0, 0}}, 0xFFFF}, /*  */
  {{{25, obj_king_boss, 460, 70}, {0, -1, 0, 0}, {0, -1, 0, 0}}, scr_encountersetup_slash_scr_encountersetup_gml_517_0}, /* obj_king_boss */
};
const int encounter_count = 41;
