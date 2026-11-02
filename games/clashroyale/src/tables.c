/* made by tools/pack.py from Clash Royale's assets/csv_logic: see tables.h */
#include "tables.h"

const SetDef SETS[NSETS] = {
  {{0, 6, 5, 255, 26}, {30, 6, 5, 4, 35}, {60, 1, 5, 255, 1}}, /* Knight */
  {{65, 6, 5, 255, 16}, {95, 6, 5, 3, 24}, {125, 1, 5, 255, 1}}, /* Archer */
  {{130, 6, 5, 255, 18}, {160, 6, 5, 4, 19}, {190, 1, 5, 255, 1}}, /* Goblin */
  {{195, 6, 5, 255, 30}, {225, 6, 5, 3, 28}, {255, 1, 5, 255, 1}}, /* Giant */
  {{260, 6, 5, 255, 30}, {290, 6, 5, 3, 21}, {320, 1, 5, 255, 2}}, /* Pekka */
  {{325, 6, 5, 255, 16}, {355, 6, 5, 4, 14}, {385, 1, 5, 255, 16}}, /* Minion */
  {{390, 6, 1, 255, 60}, {396, 6, 1, 5, 39}, {402, 1, 1, 255, 1}}, /* Balloon */
  {{403, 6, 5, 255, 24}, {433, 6, 5, 3, 17}, {463, 1, 5, 255, 1}}, /* Witch */
  {{468, 6, 5, 255, 16}, {498, 6, 5, 3, 22}, {528, 1, 5, 255, 2}}, /* Barbarian */
  {{533, 6, 5, 255, 36}, {563, 6, 5, 3, 18}, {593, 1, 5, 255, 2}}, /* Golem */
  {{598, 6, 5, 255, 16}, {628, 6, 5, 2, 14}, {658, 1, 5, 255, 2}}, /* Skeleton */
  {{663, 6, 5, 255, 16}, {693, 6, 5, 2, 27}, {723, 1, 5, 255, 1}}, /* Valkyrie */
  {{728, 6, 5, 255, 20}, {758, 6, 5, 3, 33}, {788, 1, 5, 255, 35}}, /* Bomber */
  {{793, 6, 5, 255, 28}, {823, 6, 5, 3, 30}, {853, 1, 5, 255, 1}}, /* Musketeer */
  {{858, 6, 5, 255, 18}, {888, 6, 5, 4, 22}, {918, 1, 5, 255, 18}}, /* BabyDragon */
  {{923, 6, 5, 255, 20}, {953, 6, 5, 3, 27}, {983, 1, 5, 255, 1}}, /* Prince */
  {{988, 6, 5, 255, 16}, {1018, 6, 5, 3, 30}, {1048, 1, 2, 255, 1}}, /* Wizard */
  {{1050, 6, 5, 255, 16}, {1080, 6, 5, 3, 23}, {1110, 1, 5, 255, 1}}, /* MiniPekka */
  {{1115, 6, 5, 255, 22}, {1145, 6, 5, 4, 23}, {1175, 1, 5, 255, 1}}, /* SpearGoblin */
  {{1180, 6, 5, 255, 30}, {1210, 6, 5, 3, 22}, {1240, 1, 5, 255, 1}}, /* GiantSkeleton */
  {{1245, 6, 5, 255, 17}, {1275, 6, 5, 4, 20}, {1305, 1, 5, 255, 1}}, /* HogRider */
  {{1310, 1, 5, 255, 1}, {1315, 6, 5, 3, 24}, {1345, 1, 5, 255, 1}}, /* PrincessB */
  {{1350, 6, 5, 255, 36}, {1380, 6, 5, 3, 18}, {1410, 1, 5, 255, 2}}, /* Golemite */
  {{1415, 1, 1, 255, 1}, {1415, 1, 1, 255, 1}, {1415, 1, 1, 255, 1}}, /* KingTower */
  {{1416, 1, 1, 255, 1}, {1416, 1, 1, 255, 1}, {1416, 1, 1, 255, 1}}, /* PrincessTower */
  {{1417, 1, 1, 255, 1}, {1417, 1, 1, 255, 1}, {1417, 1, 1, 255, 1}}, /* PrincessTowerTop */
  {{1418, 1, 5, 255, 1}, {1418, 1, 5, 255, 1}, {1418, 1, 5, 255, 1}}, /* Cannon */
  {{1423, 1, 1, 255, 1}, {1423, 1, 1, 255, 1}, {1423, 1, 1, 255, 1}}, /* GoblinHut */
  {{1424, 1, 5, 0, 3}, {1429, 3, 5, 1, 3}, {1444, 1, 5, 255, 3}}, /* Mortar */
  {{1449, 1, 1, 255, 1}, {1449, 1, 1, 255, 1}, {1449, 1, 1, 255, 1}}, /* InfernoTower */
  {{1450, 1, 1, 255, 1}, {1450, 1, 1, 255, 1}, {1450, 1, 1, 255, 1}}, /* BombTower */
  {{1451, 1, 1, 255, 1}, {1451, 1, 1, 255, 1}, {1451, 1, 1, 255, 1}}, /* BombTowerTop */
  {{1452, 1, 1, 255, 1}, {1452, 1, 1, 255, 1}, {1452, 1, 1, 255, 1}}, /* BarbarianHut */
  {{1453, 3, 1, 255, 3}, {1453, 3, 1, 255, 3}, {1453, 3, 1, 255, 3}}, /* Tesla */
  {{1456, 1, 1, 255, 1}, {1456, 1, 1, 255, 1}, {1456, 1, 1, 255, 1}}, /* ElixirCollector */
  {{1457, 1, 5, 255, 1}, {1457, 1, 5, 255, 1}, {1457, 1, 5, 255, 1}}, /* Xbow */
  {{1462, 1, 1, 255, 1}, {1462, 1, 1, 255, 1}, {1462, 1, 1, 255, 1}}, /* Tombstone */
  {{1463, 3, 1, 255, 3}, {1463, 3, 1, 255, 3}, {1463, 3, 1, 255, 3}}, /* BalloonBomb */
  {{1466, 3, 1, 255, 3}, {1466, 3, 1, 255, 3}, {1466, 3, 1, 255, 3}}, /* GiantSkeletonBomb */
};

const UnitDef UNITS[NUNITS] = {
  {"Wizard", 2400, 0, 1000, 500, 8000, 0, 8000, 0, 0, 1400, 0, 0, 0, 0, U_GROUND | U_AIR | U_NOPUSH | U_BUILDING | U_TOWER | U_KING,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23, 102, 60, -6, 2, 255, 2400, -1}, /* KingTower */
  {"Archer Tower", 1400, 0, 800, 0, 8500, 0, 8500, 0, 0, 1000, 0, 1, 0, 0, U_GROUND | U_AIR | U_NOPUSH | U_BUILDING | U_TOWER,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 102, 70, -5, 5, 33, 2200, 25}, /* PrincessTower */
  {"Knight", 600, 75, 1100, 700, 1500, 0, 6000, 60, 1000, 500, 6, 255, 0, 0, U_GROUND,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 85, 85, 0, -10, 255, 0, -1}, /* Knight */
  {"Archer", 125, 0, 1200, 1000, 5500, 0, 6000, 60, 1000, 500, 3, 2, 0, 0, U_GROUND | U_AIR,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 0, 0, 0, 1, 75, 75, 0, -15, 255, 0, -1}, /* Archer */
  {"Goblin", 80, 50, 1100, 900, 1000, 0, 6000, 120, 1000, 500, 2, 255, 0, 0, U_GROUND,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 0, 0, 0, 2, 50, 50, -5, 5, 255, 0, -1}, /* Goblin */
  {"Giant", 2000, 120, 1500, 1000, 2000, 0, 8000, 45, 1000, 750, 18, 255, 0, 0, U_GROUND | U_BUILDINGS | U_NOPUSH,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 85, 80, 0, 0, 255, 0, -1}, /* Giant */
  {"P.E.K.K.A.", 2600, 450, 1800, 1200, 1500, 0, 6000, 45, 3000, 750, 18, 255, 0, 0, U_GROUND | U_NOPUSH,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 80, 80, 0, 0, 255, 0, -1}, /* Pekka */
  {"Minion", 90, 0, 1000, 500, 2500, 0, 6000, 90, 1000, 500, 2, 3, 0, 0, U_GROUND | U_AIR | U_FLY,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 1500, 0, 0, 5, 85, -60, 0, 20, 255, 0, -1}, /* Minion */
  {"Balloon", 1000, 600, 3000, 2800, 400, 0, 8000, 60, 1000, 300, 6, 255, 0, 0, U_GROUND | U_BUILDINGS | U_FLY | U_NOPUSH,
   0, 0, 0, 34, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3000, 0, 0, 6, 85, 50, 0, -10, 255, 0, -1}, /* Balloon */
  {"Witch", 500, 0, 700, 200, 5500, 0, 6000, 60, 1000, 500, 4, 4, 0, 0, U_GROUND | U_AIR,
   0, 0, 0, 255, 0, 12, 3, 5, 300, 6900, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 80, 80, 0, 0, 255, 0, -1}, /* Witch */
  {"Barbarian", 300, 75, 1500, 1000, 1200, 0, 6000, 60, 1000, 500, 4, 255, 0, 0, U_GROUND,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 0, 0, 0, 8, 85, 85, 0, -10, 255, 0, -1}, /* Barbarian */
  {"Golem", 3150, 186, 2500, 1500, 1500, 0, 8000, 45, 3000, 750, 20, 255, 0, 0, U_GROUND | U_BUILDINGS | U_NOPUSH,
   186, 2000, 1800, 35, 2, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, -85, 80, 0, 0, 255, 0, -1}, /* Golem */
  {"Skeleton", 27, 27, 1000, 500, 750, 0, 6000, 90, 1000, 500, 1, 255, 0, 0, U_GROUND,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 0, 0, 0, 10, 50, 50, -5, 5, 255, 0, -1}, /* Skeleton */
  {"Valkyrie", 800, 110, 1500, 1300, 1500, 0, 6000, 60, 1000, 500, 5, 255, 2000, 0, U_GROUND | U_NOPUSH | U_SELFAOE,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, -85, 80, 0, 0, 255, 0, -1}, /* Valkyrie */
  {"Bomber", 150, 0, 1900, 1700, 5000, 0, 6000, 60, 1000, 500, 4, 5, 0, 0, U_GROUND,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 50, 50, -5, 5, 255, 0, -1}, /* Bomber */
  {"Musketeer", 340, 0, 1100, 600, 6500, 0, 6500, 60, 1000, 750, 5, 6, 0, 0, U_GROUND | U_AIR,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 80, 80, 0, -10, 255, 0, -1}, /* Musketeer */
  {"Baby Dragon", 800, 0, 1800, 1300, 3500, 0, 6000, 90, 1000, 500, 5, 7, 0, 0, U_GROUND | U_AIR | U_FLY | U_NOPUSH,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3500, 0, 0, 14, 85, -60, 0, 20, 255, 0, -1}, /* BabyDragon */
  {"Prince", 1100, 220, 1500, 1000, 2500, 0, 6000, 60, 1000, 650, 6, 255, 0, 0, U_GROUND | U_NOPUSH,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 250, 440, 230, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 80, 80, 0, 0, 255, 0, -1}, /* Prince */
  {"Wizard", 340, 0, 1700, 1500, 5500, 0, 6000, 60, 1000, 700, 5, 8, 0, 0, U_GROUND | U_AIR,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 80, 80, 0, 0, 255, 0, -1}, /* Wizard */
  {"Mini P.E.K.K.A.", 600, 325, 1800, 1300, 1500, 0, 6000, 90, 1000, 450, 4, 255, 0, 0, U_GROUND | U_NOPUSH,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 80, 80, 0, 0, 255, 0, -1}, /* MiniPekka */
  {"Spear Goblin", 52, 0, 1300, 1100, 5500, 0, 6000, 120, 1000, 500, 1, 9, 0, 0, U_GROUND | U_AIR,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 0, 0, 0, 18, 75, 75, 0, -15, 255, 0, -1}, /* SpearGoblin */
  {"Giant Skeleton", 1800, 100, 1500, 1000, 2000, 0, 6000, 60, 1000, 1200, 15, 255, 0, 0, U_GROUND | U_NOPUSH,
   0, 0, 0, 36, 1, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 19, 50, 50, -5, 5, 255, 0, -1}, /* GiantSkeleton */
  {"Hog Rider", 800, 160, 1500, 1000, 1400, 0, 10000, 120, 1000, 600, 4, 255, 0, 0, U_GROUND | U_BUILDINGS | U_JUMP,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 0, 0, 0, 20, 85, 85, 0, -10, 255, 0, -1}, /* HogRider */
  {"Cannon", 450, 0, 800, 0, 6000, 0, 6000, 0, 1000, 600, 0, 10, 0, 40000, U_GROUND | U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 26, 90, -90, -7, 10, 255, 0, -1}, /* Cannon */
  {"Goblin Hut", 700, 0, 10000, 0, 0, 0, 0, 0, 1000, 1000, 0, 255, 0, 60000, U_GROUND | U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 20, 1, 2, 0, 4950, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 27, 90, 60, -8, 0, 255, 0, -1}, /* GoblinHut */
  {"Mortar", 600, 0, 5000, 4000, 12000, 5000, 12000, 0, 3000, 600, 0, 11, 0, 30000, U_GROUND | U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28, 80, 80, 0, 0, 255, 0, -1}, /* Mortar */
  {"Inferno Tower", 850, 20, 400, 1600, 6500, 0, 6500, 0, 1000, 600, 0, 255, 0, 45000, U_GROUND | U_AIR | U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 75, 2000, 400, 2000, 0, 0, 0, 0, 0, 0, 29, 90, 70, -5, 15, 255, 0, -1}, /* InfernoTower */
  {"Bomb Tower", 900, 0, 1600, 1100, 6500, 0, 6500, 0, 1000, 600, 0, 12, 0, 60000, U_GROUND | U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 100, 85, -8, 0, 14, 3200, 31}, /* BombTower */
  {"Barbarian Hut", 1100, 0, 10000, 0, 0, 0, 0, 0, 1000, 1000, 0, 255, 0, 60000, U_GROUND | U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 10, 2, 2, 500, 13500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 90, 60, -8, 0, 255, 0, -1}, /* BarbarianHut */
  {"Tesla", 420, 64, 700, 0, 6000, 0, 6000, 0, 1000, 500, 0, 255, 0, 60000, U_GROUND | U_AIR | U_NOPUSH | U_BUILDING | U_HIDES,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 800, 800, 33, 80, 80, 0, 0, 255, 0, -1}, /* Tesla */
  {"Elixir Collector", 800, 0, 0, 0, 0, 0, 0, 0, 1000, 1000, 0, 255, 0, 70000, U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9800, 1, 0, 0, 0, 0, 34, 55, 50, -15, 0, 255, 0, -1}, /* ElixirCollector */
  {"X-Bow", 850, 0, 300, 0, 13000, 0, 13000, 0, 5000, 600, 0, 13, 0, 40000, U_GROUND | U_NOPUSH | U_BUILDING,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 35, 80, 80, 0, 0, 255, 0, -1}, /* Xbow */
  {"Tombstone", 200, 0, 10000, 0, 0, 0, 0, 0, 1000, 1000, 0, 255, 0, 60000, U_GROUND | U_NOPUSH | U_BUILDING,
   0, 0, 0, 12, 4, 12, 1, 2, 0, 2950, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 36, 90, 60, -8, 0, 255, 0, -1}, /* Tombstone */
  {"Archer", 125, 0, 1200, 1000, 5500, 0, 6000, 60, 1000, 500, 3, 1, 0, 0, U_GROUND | U_AIR,
   0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 400, 0, 0, 0, 21, 75, 75, 0, -15, 255, 0, -1}, /* PrincessB */
  {"Bomb", 0, 0, 0, 0, 0, 0, 0, 0, 1000, 450, 0, 255, 0, 0, U_GROUND | U_AIR | U_NOPUSH | U_BUILDING | U_BOMB,
   100, 2000, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 37, 0, 0, 0, 0, 255, 0, -1}, /* BalloonBomb */
  {"Golemite", 630, 37, 2500, 1500, 750, 0, 8000, 45, 1000, 500, 6, 255, 0, 0, U_GROUND | U_BUILDINGS,
   37, 2000, 1800, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 22, 50, 50, -5, 5, 255, 0, -1}, /* Golemite */
  {"Bomb", 0, 0, 0, 0, 0, 0, 0, 0, 1000, 450, 0, 255, 0, 0, U_GROUND | U_AIR | U_NOPUSH | U_BUILDING | U_BOMB,
   720, 3000, 1800, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 38, 0, 0, 0, 0, 255, 0, -1}, /* GiantSkeletonBomb */
};

const ProjDef PROJS[NPROJS] = {
  {850, 50, 0, 0, P_HOMING, 255, 0, 0, 0, 0, 1471}, /* KingProjectile */
  {400, 50, 0, 0, P_HOMING | P_ARROW, 255, 0, 0, 0, 60, -1}, /* PrincessProjectile */
  {400, 40, 0, 0, P_HOMING | P_ARROW, 255, 0, 0, 0, 60, -1}, /* ArcherArrow */
  {1000, 40, 0, 0, P_HOMING, 255, 0, 0, 0, 0, 1471}, /* MinionSpit */
  {600, 35, 1000, 0, P_HOMING | P_AOE_GROUND | P_AOE_AIR, 255, 0, 0, 0, 0, 1472}, /* WitchProjectile */
  {300, 100, 1500, 0, P_AOE_GROUND, 255, 0, 0, 0, 200, 1473}, /* BombSkeletonProjectile */
  {1000, 90, 0, 0, P_HOMING, 255, 0, 0, 0, 0, 1471}, /* MusketeerProjectile */
  {500, 100, 1200, 0, P_HOMING | P_AOE_GROUND | P_AOE_AIR, 255, 0, 0, 0, 250, 1474}, /* BabyDragonProjectile */
  {600, 130, 1200, 0, P_HOMING | P_AOE_GROUND | P_AOE_AIR, 255, 0, 0, 0, 0, 1475}, /* chr_wizardProjectile */
  {350, 24, 0, 0, P_HOMING | P_SPEAR, 255, 0, 0, 0, 150, -1}, /* SpearGoblinProjectile */
  {1000, 60, 0, 0, P_HOMING, 255, 0, 0, 0, 0, 1476}, /* TowerCannonball */
  {300, 120, 1800, 0, P_AOE_GROUND, 255, 0, 0, 0, 60, 1477}, /* MortarProjectile */
  {300, 100, 1500, 0, P_AOE_GROUND, 255, 0, 0, 0, 200, 1473}, /* BombTowerProjectile */
  {1400, 20, 0, 0, P_HOMING | P_ARROW, 255, 0, 0, 0, 0, -1}, /* xbow_projectile */
  {600, 325, 2500, 1800, P_AOE_GROUND | P_AOE_AIR | P_REDUCED, 255, 0, 0, 0, 50, 1478}, /* FireballSpell */
  {600, 0, 0, 0, P_HOMING | P_AOE_GROUND | P_AOE_AIR | P_ARROW, 255, 0, 0, 0, 50, -1}, /* ArrowsSpellDeco */
  {350, 700, 2000, 1800, P_AOE_GROUND | P_AOE_AIR | P_REDUCED, 255, 0, 0, 0, 50, 1479}, /* RocketSpell */
  {400, 50, 1500, 0, P_AOE_GROUND | P_REDUCED, 4, 3, 5, 1200, 40, 1480}, /* GoblinBarrelSpell */
  {500, 650, 0, 0, P_REDUCED, 255, 0, 0, 0, 0, -1}, /* LighningSpell */
};

const AreaDef AREAS[NAREAS] = {
  {5000, 10000, 600, 300, 0, A_OWN | A_GROUND | A_AIR, 500, 0, 255, 135, 135, 135}, /* Rage */
  {3000, 5000, 300, 0, 0, A_ENEMIES | A_GROUND | A_AIR, 5000, 300, 255, -100, -100, -100}, /* Freeze */
  {3500, 1500, 0, 460, 0, A_ENEMIES | A_BIGGEST | A_GROUND | A_AIR, 0, 0, 18, 0, 0, 0}, /* Lightning */
  {2500, 1, 0, 0, 85, A_ENEMIES | A_REDUCED | A_GROUND | A_AIR, 0, 0, 255, 0, 0, 0}, /* Zap */
};

const CardDef CARDS[NCARDS] = {
  {"Knight", "A tough melee fighter. The Barbarian´s handsome, cultured cousin. Rumor has it that he was knighted based on the sheer awesomeness of his mustache alone.", 0, 3, 0, 1, 2, 255, 255, 0, 0, 1501}, /* Knight */
  {"Archers", "A pair of unarmored ranged attackers. They´ll help you with ground and air unit attacks, but you´re on your own with coloring your hair.", 0, 3, 0, 2, 3, 255, 255, 0, 0, 1503}, /* Archer */
  {"Goblins", "Three fast, unarmored melee attackers. Small, fast, green and mean!", 0, 2, 0, 3, 4, 255, 255, 0, 0, 1505}, /* Goblins */
  {"Giant", "Slow but durable, only attacks buildings. A real one-man wrecking crew!", 1, 5, 0, 1, 5, 255, 255, 0, 0, 1507}, /* Giant */
  {"P.E.K.K.A.", "A heavily armored, slow melee fighter. Swings from the hip but packs a huge punch.", 2, 7, 0, 1, 6, 255, 255, 0, 0, 1509}, /* Pekka */
  {"Minions", "Three fast, unarmored flying attackers. Roses are red, minions are blue, they can fly, and will crush you!", 0, 3, 0, 3, 7, 255, 255, 0, 0, 1511}, /* Minions */
  {"Balloon", "As pretty as they are, you won´t want a parade of THESE balloons showing up on the horizon. Drops powerful bombs and when shot down, crashes dealing area damage.", 2, 5, 0, 1, 8, 255, 255, 0, 0, 1513}, /* Balloon */
  {"Witch", "Summons skeletons, shoots destructo beams, has glowing pink eyes that unfortunately don´t shoot lasers.", 2, 5, 0, 1, 9, 255, 255, 0, 0, 1515}, /* Witch */
  {"Barbarians", "A horde of melee attackers with mean mustaches and even meaner tempers.", 0, 5, 0, 4, 10, 255, 255, 0, 0, 1517}, /* Barbarians */
  {"Golem", "Slow but durable, only attacks buildings. When destroyed, explosively splits into two Golemites and deals area damage!", 2, 8, 0, 1, 11, 255, 255, 0, 0, 1519}, /* Golem */
  {"Skeletons", "Four fast, very weak melee fighters. Swarm your enemies with this pile of bones!", 0, 1, 0, 4, 12, 255, 255, 0, 0, 1521}, /* Skeletons */
  {"Valkyrie", "Tough melee fighter, deals area damage around her. Swarm or horde, no problem! She can take them all out with a few spins.", 1, 4, 0, 1, 13, 255, 255, 0, 0, 1523}, /* Valkyrie */
  {"Skeleton Army", "Summons an army of skeletons. Meet Larry and his friends Harry, Gerry, Terry, Mary, etc.", 2, 4, 0, 20, 12, 255, 255, 0, 0, 1525}, /* SkeletonArmy */
  {"Bomber", "Small, lightly protected skeleton that throws bombs. Deals area damage that can wipe out a swarm of enemies.", 0, 3, 0, 1, 14, 255, 255, 0, 0, 1527}, /* Bomber */
  {"Musketeer", "Don´t be fooled by her delicately coiffed hair, the Musketeer is a mean shot with her trusty boomstick.", 1, 4, 0, 1, 15, 255, 255, 0, 0, 1529}, /* Musketeer */
  {"Baby Dragon", "Flying troop that deals area damage. Baby dragons hatch cute, hungry and ready for a barbeque.", 2, 4, 0, 1, 16, 255, 255, 0, 0, 1531}, /* BabyDragon */
  {"Prince", "Don´t let the little pony fool you. Once the Prince gets a running start, you WILL be trampled. Does 2x damage once he gets charging.", 2, 5, 0, 1, 17, 255, 255, 0, 0, 1533}, /* Prince */
  {"Wizard", "The most awesome man to ever set foot in the arena, the Wizard will blow you away with his handsomeness... and/or fireballs.", 1, 5, 0, 1, 18, 255, 255, 0, 0, 1535}, /* Wizard */
  {"Mini P.E.K.K.A.", "The arena is a certified butterfly-free zone. No distractions for P.E.K.K.A, only destruction.", 1, 4, 0, 1, 19, 255, 255, 0, 0, 1537}, /* MiniPekka */
  {"Spear Goblins", "Three unarmored ranged attackers. Who the heck taught these guys to throw spears!?! Who thought that was a good idea?!", 0, 2, 0, 3, 20, 255, 255, 0, 0, 1539}, /* SpearGoblins */
  {"Giant Skeleton", "The bigger the skeleton, the bigger the bomb. Carries a bomb that blows up when the Giant Skeleton dies.", 2, 6, 0, 1, 21, 255, 255, 0, 0, 1541}, /* GiantSkeleton */
  {"Hog Rider", "Fast melee troop that targets buildings and can jump over the river. He followed the echoing call of 'Hog Riderrrrr' all the way through the arena doors.", 1, 4, 0, 1, 22, 255, 255, 0, 0, 1543}, /* HogRider */
  {"Minion Horde", "Six fast, unarmored flying attackers. Three's a crowd, six is a horde!", 0, 5, 0, 6, 7, 255, 255, 0, 0, 1545}, /* MinionHorde */
  {"Cannon", "Defensive building. Shoots cannonballs with deadly effect, but cannot target flying troops.", 0, 3, 1, 1, 23, 255, 255, 0, 0, 1547}, /* Cannon */
  {"Goblin Hut", "Building that spawns Spear Goblins. But don´t look inside. You don´t want to see how they are made.", 1, 5, 1, 1, 24, 255, 255, 0, 0, 1549}, /* GoblinHut */
  {"Mortar", "Defensive building with a long range. Shoots exploding shells that deal area damage. Cannot shoot at targets that get very close!", 0, 4, 1, 1, 25, 255, 255, 0, 0, 1551}, /* Mortar */
  {"Inferno Tower", "Defensive building, roasts targets for damage that increases over time. Burns through even the biggest and toughest enemies!", 1, 5, 1, 1, 26, 255, 255, 0, 0, 1553}, /* InfernoTower */
  {"Bomb Tower", "Defensive building that houses a Bomber. Deals area damage to anything dumb enough to stand near it.", 1, 5, 1, 1, 27, 255, 255, 0, 0, 1555}, /* BombTower */
  {"Barbarian Hut", "Troop building that periodically deploys Barbarians to fight the enemy. Time to make the Barbarians.", 1, 7, 1, 1, 28, 255, 255, 0, 0, 1557}, /* BarbarianHut */
  {"Tesla", "Defensive building. Whenever it's not zapping the enemy, the power of Electrickery is best kept grounded.", 0, 4, 1, 1, 29, 255, 255, 0, 0, 1559}, /* Tesla */
  {"Elixir Collector", "You gotta spend Elixir to make Elixir.", 1, 5, 1, 1, 30, 255, 255, 0, 0, 1561}, /* Elixir Collector */
  {"X-Bow", "Nice tower you got there. Would be a shame if this X-Bow whittled it down from this side of the arena...", 2, 6, 1, 1, 31, 255, 255, 0, 0, 1563}, /* Xbow */
  {"Tombstone", "Troop building that periodically deploys Skeletons to fight the enemy. When destroyed, spawns 4 Skeletons. Creepy!", 1, 3, 1, 1, 32, 255, 255, 0, 0, 1565}, /* Tombstone */
  {"Fireball", "Annnnnd... Fireball. Incinerates a small area, dealing high damage. Reduced damage to Crown Towers.", 1, 4, 2, 1, 255, 14, 255, 2500, 0, 1567}, /* Fireball */
  {"Arrows", "Arrows pepper a large area, damaging everyone hit. Reduced damage to Crown Towers.", 0, 3, 2, 1, 255, 15, 255, 4000, 15, 1569}, /* Arrows */
  {"Rage", "Increases troop movement and attack speed by 35%. Troop buildings and summoners deploy troops faster. Chaaaarge!", 2, 3, 2, 1, 255, 255, 0, 5000, 0, 1571}, /* Rage */
  {"Rocket", "Deals high damage to a small area. Looks really awesome doing it. Reduced damage to Crown Towers.", 1, 6, 2, 1, 255, 16, 255, 2000, 0, 1573}, /* Rocket */
  {"Goblin Barrel", "Spawns three Goblins anywhere on the Arena. It's going to be a thrilling ride, boys!", 2, 4, 2, 1, 255, 17, 255, 1500, 0, 1575}, /* GoblinBarrel */
  {"Freeze", "Freezes troops and buildings, making them unable to move or attack. Everybody chill.", 2, 4, 2, 1, 255, 255, 1, 3000, 0, 1577}, /* Freeze */
  {"Lightning", "Bolts of lightning hit up to 3 enemy troops or buildings with the most hitpoints in the target area. Reduced damage to Crown Towers.", 2, 6, 2, 1, 255, 255, 2, 3500, 0, 1579}, /* Lightning */
  {"Zap", "Lightning zaps enemies, dealing damage inside a small radius. Reduced damage to Crown Towers.", 0, 2, 2, 1, 255, 255, 3, 2500, 0, 1581}, /* Zap */
};

const uint8_t BOT_DECKS[NBOTDECKS][8] = {
  {CA_KNIGHT, CA_GOBLINS, CA_SKELETONS, CA_ARCHER, CA_BOMBER, CA_ARROWS, CA_FIREBALL, CA_GIANT}, /* BasicDeck7 */
  {CA_KNIGHT, CA_ARCHER, CA_GOBLINS, CA_SPEARGOBLINS, CA_GIANT, CA_GOBLINHUT, CA_FIREBALL, CA_BABYDRAGON}, /* BasicDeck8 */
  {CA_KNIGHT, CA_ARCHER, CA_GOBLINS, CA_SPEARGOBLINS, CA_GIANT, CA_GOBLINHUT, CA_ARROWS, CA_SKELETONARMY}, /* BasicDeck9 */
  {CA_KNIGHT, CA_ARCHER, CA_GOBLINS, CA_SKELETONS, CA_PRINCE, CA_SPEARGOBLINS, CA_ARROWS, CA_FIREBALL}, /* BasicDeck10 */
  {CA_VALKYRIE, CA_ARCHER, CA_WITCH, CA_SKELETONS, CA_TOMBSTONE, CA_GIANTSKELETON, CA_KNIGHT, CA_FIREBALL}, /* BasicDeck11 */
  {CA_KNIGHT, CA_ARCHER, CA_GOBLINS, CA_SKELETONS, CA_BOMBTOWER, CA_MINIONS, CA_TOMBSTONE, CA_BALLOON}, /* BasicDeck12 */
  {CA_BARBARIANS, CA_BARBARIANHUT, CA_SKELETONARMY, CA_SKELETONS, CA_WITCH, CA_FIREBALL, CA_CANNON, CA_ARROWS}, /* BasicDeck13 */
  {CA_MUSKETEER, CA_BOMBER, CA_BARBARIANHUT, CA_GOBLINS, CA_GIANT, CA_BALLOON, CA_ARCHER, CA_FIREBALL}, /* BasicDeck14 */
  {CA_MUSKETEER, CA_BOMBER, CA_MINIPEKKA, CA_GOBLINS, CA_PRINCE, CA_BALLOON, CA_ARCHER, CA_FIREBALL}, /* BasicDeck15 */
  {CA_KNIGHT, CA_ARCHER, CA_SKELETONARMY, CA_GOBLINS, CA_FIREBALL, CA_GIANT, CA_VALKYRIE, CA_MINIPEKKA}, /* BasicDeck16 */
  {CA_HOGRIDER, CA_INFERNOTOWER, CA_CANNON, CA_KNIGHT, CA_SPEARGOBLINS, CA_PRINCE, CA_ARCHER, CA_FIREBALL}, /* BasicDeck17 */
  {CA_WITCH, CA_PEKKA, CA_GOBLINS, CA_SPEARGOBLINS, CA_ZAP, CA_WIZARD, CA_ARCHER, CA_ARROWS}, /* BasicDeck18 */
  {CA_MUSKETEER, CA_MINIONHORDE, CA_ROCKET, CA_GOBLINS, CA_GIANT, CA_WIZARD, CA_ARCHER, CA_ARROWS}, /* BasicDeck19 */
  {CA_BARBARIANHUT, CA_TOMBSTONE, CA_BOMBTOWER, CA_GOBLINS, CA_GIANT, CA_SPEARGOBLINS, CA_PRINCE, CA_FIREBALL}, /* BasicDeck20 */
};

const int32_t GLOBALS[NGLOBALS] = {
  10, /* MAX_MANA */
  28000, /* MANA_REGEN_MS */
  14000, /* MANA_REGEN_MS_END */
  14000, /* MANA_REGEN_MS_OVERTIME */
  60, /* MANA_SPEED_UP_WHEN_REMAINING_SECONDS */
  5, /* START_MANA */
  2000, /* NEXT_SPELL_COOLDOWN_MILLISECONDS */
  1000, /* NEXT_SPELL_COOLDOWN_MILLISECONDS_BOOST */
  1000, /* NEXT_SPELL_COOLDOWN_MILLISECONDS_OVERTIME */
  10, /* DAMAGE_INCREASE_PERCENT_PER_SPELL_LEVEL */
  10, /* HITPOINT_INCREASE_PERCENT_PER_SPELL_LEVEL */
  7, /* DAMAGE_INCREASE_PERCENT_PER_KING_LEVEL */
  7, /* HITPOINT_INCREASE_PERCENT_PER_KING_LEVEL */
  8, /* DAMAGE_INCREASE_PERCENT_PER_TOWER_LEVEL */
  8, /* HITPOINT_INCREASE_PERCENT_PER_TOWER_LEVEL */
  2000, /* MELEE_RANGE_LIMIT */
  50, /* REDUCED_TOWER_DAMAGE_PERCENT */
  3300, /* KING_ACTIVATE_TIME_MS */
  8, /* TOURNAMENT_MAX_EXP_LEVEL */
  8, /* TOURNAMENT_MAX_COMMON_LEVEL */
  6, /* TOURNAMENT_MAX_RARE_LEVEL */
  3, /* TOURNAMENT_MAX_EPIC_LEVEL */
  180, /* TOURNAMENT_MATCH_LENGTH_SECONDS */
  180, /* MATCH_LENGTH */
  60, /* OVERTIME_SECONDS */
};
