"""The game's areas and rooms, cut from the wiki's maps (tools/fetch.py).

An area is one map: every room of the area, one pixel for one, laid out as in the game. A room is a
rectangle of it (rect: x, y, w, h on the map); its rooms touch where their doors are, so walking out of one
walks into the next (world.c). Walkable: the area's floor colours (and the room's floor_colors), inside the
room's floor_rects if it has some (walls of the floor's colour are left out that way), then its fixes
("walk" or "wall" rectangles). Where a room's rectangle is less than the game's own room, its "view" is the
game's room: the camera keeps to it, so the screen shows what the game's does. Patches change the map first (objects drawn into it, like the rocks, are
taken out: the game draws them).

Rooms are named as the game names them (room_ruins1 is ruins1)."""

# Toriel's home: wood, the rugs, the kitchen's tiles, the hallway's, the bedrooms' floors
HOUSE_FLOOR = [(245, 215, 158), (246, 231, 189), (194, 133, 78), (255, 186, 95), (151, 97, 26), (187, 106, 52),
               (238, 203, 131), (230, 245, 120), (186, 133, 4), (235, 186, 67), (233, 77, 42), (255, 213, 166),
               (166, 189, 255), (74, 151, 201)]
BASEMENT_FLOOR = [(132, 53, 125)]
# the darker floor: cracks, the rocks' tracks
CRACKED = [(132, 53, 125)]
LEAVES = [(128, 17, 17), (232, 35, 19)]
RUINS_PLAIN = [(163, 73, 164), (185, 98, 185)]
RUINS_FLOOR = [
    (163, 73, 164), (185, 98, 185),                      # the floor and its paths
    (128, 17, 17), (232, 35, 19), (233, 77, 42), (123, 24, 19), (121, 18, 0), (64, 0, 0), (157, 32, 0),  # red leaves
    (58, 57, 72), (95, 94, 119), (200, 194, 226), (34, 177, 76),  # the landing: stone, its rings, grass
    (249, 240, 0), (219, 177, 0), (124, 106, 0),  # golden flowers
    (167, 112, 82), (112, 66, 56), (169, 85, 38), (105, 58, 27),  # bridges
]

AREAS = [
    dict(name="ruins", map="Ruins_map.png", floor=RUINS_FLOOR, margin=3, piles=LEAVES, patches=[
        ("mirror", 95, 5988, 27, 60, 178, 5988),     # Toriel tending the flowers (the game's last pages)
        ("fill", 509, 5768, 23, 12, (0, 0, 0)), ("fill", 509, 5780, 23, 11, (34, 177, 76)),   # Flowey
        # what the game draws itself: the switch room's spikes, the Dummy, the pillar Toriel hides behind, the
        # leaf pile's Froggit, the SAVE stars
        ("erase", 1058, 4738, 26, 44, RUINS_PLAIN), ("erase", 1296, 4688, 26, 36, RUINS_PLAIN),
        ("copy", 4521, 4380, 38, 61, 4601, 4380), ("erase", 4598, 4441, 44, 21, RUINS_PLAIN),
        ("erase", 4898, 4441, 28, 23, RUINS_PLAIN),
        ("copy", 531, 5274, 20, 22, 511, 5274), ("copy", 5010, 4480, 22, 20, 5010, 4500),
        ("erase", 7168, 4856, 26, 26, RUINS_PLAIN), ("copy", 8902, 3116, 22, 26, 8942, 3116),
        # the spikes the game raises and lowers (not the blueprint room's: those stay)
    ] + [("erase", x, y, 20, 20, RUINS_PLAIN) for x, y in [
        (5740, 4680), (5740, 4700), (5740, 4720), (5740, 4740), (5740, 4760), (5740, 4780),
        (6880, 4820), (6880, 4840), (6880, 4860), (6900, 4820), (6900, 4840), (6900, 4860),
        (8680, 4620), (8680, 4640), (8760, 4620), (8760, 4640), (8960, 4840), (8980, 4840),
        (8960, 4140), (8980, 4140), (8760, 4340), (8760, 4360), (8660, 4340), (8660, 4360),
        (8440, 4140), (8460, 4140), (8440, 3960), (8460, 3960), (8660, 3740), (8660, 3760)]] + [
        # the rocks, the Froggits, the candy, what lies in the one switch puzzle's rooms, the Toy Knife
        # (a rock starts at the end of its track: the track goes on under it)
        ("copy", 5640, 4735, 20, 20, 5620, 4735), ("copy", 6720, 4795, 20, 20, 6700, 4795),
        ("copy", 6700, 4835, 20, 20, 6680, 4835), ("copy", 6740, 4875, 20, 20, 6720, 4875),
        ("erase", 9400, 3700, 20, 20, RUINS_PLAIN), ("erase", 7660, 4540, 20, 20, RUINS_PLAIN),
        ("erase", 7780, 4540, 20, 20, RUINS_PLAIN), ("erase", 7900, 4540, 20, 20, RUINS_PLAIN),
        ("erase", 4950, 4215, 40, 29, RUINS_PLAIN), ("erase", 8300, 4980, 20, 20, RUINS_PLAIN),
        ("erase", 8660, 5117, 20, 20, RUINS_PLAIN), ("erase", 8260, 5120, 15, 15, RUINS_PLAIN),
        ("erase", 8493, 5115, 35, 19, RUINS_PLAIN), ("erase", 9364, 3566, 15, 15, RUINS_PLAIN),
        # Napstablook on the leaves; Toriel at the door; Flowey in the last room's light
        ("erase", 7524, 4832, 34, 19, LEAVES), ("heal", 8646, 2728, 52, 56), ("mirror", 8712, 2728, 32, 40, 8672, 2728), ("erase", 10003, 2011, 28, 53, BASEMENT_FLOOR + [(97, 44, 97)]),
        ("fill", 10007, 204, 27, 16, (0, 0, 0)), ("fill", 10013, 220, 15, 1, (200, 194, 226)),
        ("fill", 10012, 221, 19, 9, (34, 177, 76)),
    ]),
]

SNOW = [(133, 163, 216), (114, 129, 177)]
ICE = [(139, 163, 186), (180, 192, 201), (195, 207, 216)]
WOOD = [(169, 124, 41), (91, 65, 0), (62, 35, 0)]
# the paths' outlines
SNOW_LINES = [(44, 46, 63)]
INN_FLOOR = [(249, 210, 97)]
PS_FLOOR = [(109, 85, 127), (74, 109, 127)]
# the XOXO puzzles' tiles (drawn by the game, X or O), the spikes it raises and lowers
XOXO1 = [(7460, 1240), (7540, 1240)]
XOXO2 = [(8180, 1200), (8240, 1200), (8280, 1200), (8240, 1220), (8280, 1220), (8200, 1240), (8240, 1240),
         (8260, 1240), (8280, 1240), (8180, 1260), (8240, 1260), (8180, 1280), (8200, 1280), (8220, 1280), (8240, 1280)]
XOXO3 = [(9540, 1160), (9700, 1160), (9540, 1180), (9580, 1180), (9660, 1180), (9620, 1220), (9580, 1240),
         (9620, 1240), (9580, 1260), (9660, 1260), (9540, 1300), (9700, 1300)]
SNOW_SPIKES = [(7120, 940), (7120, 960), (7120, 980), (7680, 1220), (7680, 1240), (7680, 1260)] + \
              [(8360, 1160 + 20 * i) for i in range(8)]
BLUE = [(0, 0, 255)]
SPIKE_COLOURS = [(128, 128, 128), (192, 192, 192), (255, 255, 255), (0, 0, 0)]
AREAS.append(dict(name="snowdin", map="Snowdin_map.png", floor=SNOW + ICE + WOOD, margin=2, lines=SNOW_LINES, ice=ICE,
                  patches=[
    # Sans, Papyrus and the monsters who come out after Papyrus' fight or at the very end; the SAVE stars
    ("erase", 2881, 400, 26, 40, "ring"), ("erase", 3236, 374, 28, 34, "ring"), ("erase", 4668, 380, 37, 37, "ring"),
    ("erase", 5440, 290, 42, 56, "ring"), ("erase", 6108, 280, 54, 58, "ring"), ("erase", 6508, 610, 54, 56, "ring"),
    ("erase", 6743, 695, 24, 27, "ring"), ("erase", 7332, 944, 94, 52, "ring"), ("erase", 7858, 1170, 44, 60, "ring"),
    ("erase", 8008, 1160, 44, 49, "ring"), ("erase", 8374, 1186, 32, 42, "ring"), ("erase", 8816, 1254, 34, 46, "ring"),
    ("erase", 9258, 1300, 24, 27, "ring"), ("erase", 9692, 1488, 36, 44, "ring"), ("erase", 10630, 1270, 38, 50, "ring"),
    ("erase", 11772, 1176, 38, 48, "ring"), ("erase", 10182, 2274, 38, 46, "ring"),
    ("erase", 10328, 2255, 69, 58, "ring"), ("erase", 10532, 2274, 38, 46, "ring"),
    ("erase", 7026, 576, 58, 50, "ring"), ("erase", 5948, 258, 54, 52, "ring"),
    # Doggo in his sentry station (he pops out of it, then he's gone): its dark inside, the sign on its wall
    ("fill", 4338, 357, 21, 25, (47, 32, 26)), ("fill", 4356, 357, 3, 1, (0, 0, 0)), ("fill", 4356, 357, 1, 15, (0, 0, 0)),
    ("fill", 4356, 371, 3, 1, (0, 0, 0)), ("fill", 4357, 358, 2, 13, (103, 65, 35)),
    ("fill", 4359, 372, 2, 10, (47, 32, 26)),
    # Snowdrake and his parents by Papyrus' sentry station: the trees and the path there from beside (the trees
    # repeat every 40 pixels)
    ("copy", 3620, 316, 80, 72, 3700, 316), ("copy", 3620, 316, 80, 72, 3780, 316), ("copy", 3620, 316, 30, 72, 3860, 316),
]))
# (the puzzles' tiles and spikes first: what's beside them is erased after)
AREAS[-1]["patches"][:0] = [("erase", x, y, 20, 20, "ring", BLUE) for x, y in XOXO1 + XOXO2 + XOXO3] + \
    [("erase", x, y, 20, 20, "ring", SPIKE_COLOURS) for x, y in SNOW_SPIKES]
AREAS.append(dict(name="snowtown", map="Snowdin_Town_map.png", floor=SNOW, margin=2, lines=SNOW_LINES, patches=[
    # the SAVE star; Papyrus and Undyne by the house, the dogs at the library, Papyrus in the fog and at home
    # (they come later)
    # (the game's screenshots of those places, where they're in them: no one stands there)
    ("heal", 514, 1072, 30, 30),
    ("shot", "Snowdin_Library_location_exterior.png", 1736, 960, 1856, 1000, 200, 92), ("erase", 2056, 1046, 10, 46, "ring"),
    ("shot", "Papyrus_and_Sans's_House_location.png", 2088, 960, 2128, 1020, 72, 92),
    ("erase", 3383, 1070, 22, 46, [(114, 129, 177)]),
    ("shot", "Papyrus_and_Sans's_House_location_interior.png", 2200, 720, 2206, 846, 44, 62),
    ("shot", "Papyrus_and_Sans's_House_location_Papyrus's_room.png", 2120, 480, 2256, 574, 48, 64),
]))

# Waterfall: dark blue floors whose colours its walls share too, so a room's floor is its rectangles
WF_FLOOR = [(54, 47, 121), (20, 26, 73)]
WF_WATER = [(27, 33, 90), (27, 21, 72)]
WF_CYAN = [(123, 251, 254), (47, 234, 255)]
WF_DUMP = [(17, 11, 49), (18, 24, 71), (5, 6, 33), (15, 9, 47)]   # (the dump's water, waded in)
NAPSTA_FLOOR = [(198, 88, 41), (100, 50, 17), (133, 59, 41), (136, 68, 41)]
AREAS.append(dict(name="waterfall", map="Waterfall_map.png", floor=WF_FLOOR, margin=0, close=True,
                  waves=[((27, 21, 72), [(27, 33, 90)], 12, 26), ((27, 21, 72), [(0, 0, 0)], 2, 9),
                         ((23, 13, 43), [(0, 0, 0)], 2, 9)],
                  patches=[
    # the people the game moves about (they're the game's objects here): Monster Kid, the fish by the first
    # echo flower, Undyne (her black ledges kept black), Woshua, the Goner Kid, Onionsan, Shyren, the grey
    # figure by the piano, Aaron
    ("erase", 716, 1498, 28, 36, "ring"), ("erase", 716, 1404, 34, 50, "ring"),
    ("erase", 1800, 1336, 46, 62, "ring", []), ("copy", 3398, 554, 40, 38, 3438, 554),
    ("copy", 4042, 541, 24, 32, 4002, 541), ("erase", 4354, 370, 42, 60, "ring", []),
    ("copy", 9130, 528, 72, 84, 9232, 528), ("copy", 10100, 630, 34, 40, 10134, 630),
    ("copy", 10130, 682, 32, 58, 10168, 682), ("erase", 11897, 614, 28, 36, "ring"),
    ("copy", 14530, 1636, 42, 70, 14476, 1636), ("copy", 15806, 1714, 40, 66, 15846, 1714),
    # the bridge seeds (the game's objects too) and the bridges they make, drawn solved
    *[("erase", x, y, 20, 20, WF_FLOOR) for x, y in [(2300, 1420), (2280, 1440), (2320, 1440), (2300, 1460),
                                                     (2960, 1340), (3000, 1340), (2960, 1380), (3000, 1380)]],
    # past the bridge: Mad Dummy, the bird (on its side of the gap), Papyrus at Undyne's door, Napstablook (by the
    # houses, at the snail farm, at home), Mettaton, the creature by the pond (all the game's objects)
    ("erase", 18918, 2686, 22, 44, "ring"), ("erase", 18523, 2341, 16, 20, "ring", []), ("erase", 18578, 2058, 26, 48, "ring"),
    ("erase", 18811, 2113, 24, 42, "ring"), ("erase", 18976, 2100, 38, 52, "ring"), ("erase", 19108, 2330, 42, 54, "ring"),
    ("copy", 19276, 2090, 26, 52, 19316, 2090), ("erase", 18969, 1810, 26, 40, "ring"),
    # by the chasm: Undyne on the bridge, Monster Kid hanging off it, the old monster beyond; Undyne on her crag
    ("erase", 22494, 2006, 24, 52, "ring", []), ("erase", 22628, 2050, 24, 34, "ring"), ("erase", 23054, 2000, 32, 36, "ring"),
    # (Undyne on her crag is the game's object here: the sky behind her)
    ("fill", 23441, 1512, 32, 5, (111, 40, 43)), ("fill", 23441, 1517, 32, 51, (133, 18, 18)),
    # the lantern maze, as the map draws it, has no way through for Frisk: two tufts of one wall left out
    ("erase", 21618, 2654, 26, 44, [(0, 0, 0)]),
    # the bird (it waits across the gap), the tutu and the artifact (objects, gone once taken)
    ("erase", 8466, 578, 15, 19, [(27, 33, 90), (27, 21, 72), (23, 13, 43)]),
    ("erase", 1240, 1240, 22, 10, [(23, 13, 43), (27, 21, 72), (27, 33, 90)]),
    ("erase", 10156, 150, 12, 7, [(27, 21, 72), (27, 33, 90), (54, 47, 121)]),
    # the song over the statue in the rain shows only once it has an umbrella (an object, as in the game)
    ("erase", 10705, 554, 158, 18, [(0, 0, 0), (48, 48, 48), (10, 5, 23), (55, 52, 65), (30, 25, 73), (73, 68, 128)]),
    ("fill", 10156, 157, 12, 1, (0, 0, 0)), ("fill", 10156, 158, 12, 3, (99, 102, 135)),
    ("erase", 2380, 1500, 80, 20, "ring"), ("erase", 2850, 1240, 30, 80, "ring"), ("erase", 3040, 1540, 80, 20, "ring"),
]))

# Hotland: its orange rock floors (its black is nothing); the striped bridges, the conveyor belts, the Lab's tiles
HT_FLOOR = [(181, 85, 21), (169, 124, 41), (178, 83, 19), (91, 65, 0)]
HT_STRIPE = [(239, 153, 104), (81, 34, 33), (198, 104, 80), (156, 62, 61), (114, 39, 38)]
HT_BELT = [(112, 83, 71), (52, 50, 78), (107, 67, 78)]
LAB_FLOOR = [(186, 233, 233), (220, 255, 186), (198, 232, 234), (52, 50, 78), (128, 169, 202)]
HT_DOCK = [(54, 47, 121), (27, 33, 90), (27, 21, 72), (23, 13, 43), (45, 49, 96)]
HT_HUB = [(131, 44, 66), (70, 32, 58)]
HT_KITCHEN = []
# the steam vents' grates (their platforms 60 pixels square around them)
HT_VENTS_MAGMA = [(3120, 4000), (3362, 4000), (3503, 4060), (3420, 4180), (3225, 4240), (3325, 4240), (3525, 4240)]
HT_VENTS_HUB = [(4820, 3620), (4820, 3560), (4740, 3620), (4900, 3620)]
HT_VENTS_ISLE = [(4940, 3120), (5060, 3120), (5180, 3120)]
# the third floor: the vent islands (their top-left; the grates' arrows point the way each blows), the grates off
# them (the corridor's end, the walkways north and south, the platform east), the stage's purples, the tiles' colours
# (red left out: its tiles are walls)
HT3_ISLES = [(4720, 700), (4800, 700), (4880, 700), (4640, 780), (4720, 780), (4800, 780), (4880, 780), (4960, 780),
             (4640, 860), (4720, 860), (4800, 860), (4880, 860), (4960, 860), (4640, 940), (4720, 940), (4800, 940),
             (4880, 940), (4960, 940), (4720, 1020), (4800, 1020), (4880, 1020)]
HT3_GRATES = [(4600, 880), (4820, 660), (4820, 1100), (5040, 880)]
HT_STAGE = [(163, 73, 164), (132, 53, 125), (55, 44, 103)]
HT_TILES = [(64, 64, 255), (255, 255, 128), (255, 192, 192), (156, 255, 121), (255, 193, 74), (192, 0, 192)]
# the MTT Resort: its lobby's checkered floor and red carpet; the street's paving and carpet; the restaurant's floor
RES_FLOOR = [(251, 205, 121), (251, 225, 121), (235, 105, 99), (203, 67, 33), (253, 241, 193)]
RES_STREET = [(36, 43, 57), (71, 68, 130), (44, 41, 80), (207, 52, 52), (138, 55, 86), (235, 105, 99)]
RES_DINER = [(114, 73, 95), (90, 83, 119), (90, 74, 132)]
# the vent puzzle's platforms (their top-left)
HT_VENTPUZ = [(4620, 2160), (4520, 2260), (4620, 2260), (4720, 2260), (4520, 2360), (4620, 2360), (4520, 2460),
              (4620, 2460)]
AREAS.append(dict(name="hotland", map="Hotland_map.png", floor=HT_FLOOR, margin=0, close=True, patches=[
    # who the game moves about: the Royal Guards at the elevator (and by the vents, later), the clam by the cooler,
    # Alphys and Mettaton (through the wall) in the Lab, the Vulkin on the belts, the Pyrope by the pan, the pan
    *[("copy", 1006, 6120, 88, 20, 1006, y) for y in (6140, 6160, 6180)], ("erase", 726, 6216, 52, 56, "ring"),
    ("copy", 2100, 6152, 82, 82, 2181, 6152), ("erase", 2198, 6241, 30, 36, "ring"),
    ("erase", 2993, 5170, 30, 32, "ring"), ("erase", 3762, 4424, 34, 42, "ring"), ("erase", 3476, 4468, 18, 18, "ring"),
    ("erase", 4805, 3098, 70, 56, "ring"),
    # the second floor: the two by the invisible path, the Pyrope by the bend, the Royal Guards on it, Mettaton at
    # his news desk, the apron
    ("erase", 5822, 2343, 46, 44, "ring"), ("erase", 5946, 2343, 46, 52, "ring"),
    ("erase", 3202, 1988, 48, 46, [(0, 0, 0)]), ("erase", 3202, 2034, 48, 16, "ring"),
    ("copy", 3495, 2216, 68, 46, 3376, 2216), ("erase", 3376, 2262, 68, 38, "ring"),
    ("erase", 4158, 1716, 56, 66, "ring"), ("erase", 5836, 2618, 22, 18, "ring"),
    # the news report's bombs that move: the script on the belt (the belt repeats every 5 pixels), the basketball, the
    # glass of water (in front of a rock: the same rock, a row up)
    ("copy", 4318, 1850, 24, 26, 4318, 1875), ("erase", 3999, 1565, 21, 20, "ring"), ("copy", 4118, 1576, 26, 24, 4118, 1616),
    # the third floor: the Diamond Boys (there once the north puzzle's done), Muffet in her web (the web repeats every
    # 60 pixels), Mettaton over the tiles, the Royal Guards by the Nice Cream (there if they were spared)
    ("erase", 4736, 414, 30, 40, "ring"), ("erase", 4890, 412, 30, 50, "ring"), ("copy", 6332, 452, 32, 50, 6272, 452),
    ("erase", 6400, 540, 50, 64, [(0, 0, 0)]), ("erase", 7578, 352, 84, 58, "ring"),
    # Undyne, fallen on the bridge in the heat (an object here, there only if she chased Frisk): the planks repeat
    # every 40 pixels
    ("copy", 454, 6219, 50, 21, 534, 6219),
    ("erase", 536, 6208, 42, 11, [(19, 35, 45), (18, 34, 44), (19, 35, 44), (23, 37, 53), (20, 37, 47), (20, 36, 46)]),
]))

AREAS.append(dict(name="resort", map="MTT_Resort_map.png", floor=RES_FLOOR, margin=0, close=True,
                  patches=[("copy", 1289, 818, 26, 36, 1131, 818)]))   # Sans out front (a sprite here)

# the CORE: blue steel, mesh and purple bridges; the floors are cut by their rectangles (its walls are drawn in the
# same blues). The black sausages (only there on the way to the true ending), Mettaton in his room and Alphys in the
# last hallway are the map's: off it (Mettaton and Alphys are objects here)
CORE_FLOOR = [(33, 81, 164), (31, 130, 228), (34, 105, 164), (36, 62, 141), (71, 154, 204), (85, 121, 151),
              (67, 97, 107), (51, 70, 62), (71, 101, 103), (54, 52, 156), (78, 77, 189), (29, 28, 90), (69, 102, 153),
              (32, 29, 112), (30, 74, 218), (15, 62, 216), (22, 101, 221), (85, 116, 138), (131, 44, 66), (70, 32, 58),
              (225, 148, 55), (169, 132, 94), (145, 96, 11), (166, 165, 237), (18, 14, 53), (244, 254, 190),
              (59, 58, 138)]
AREAS.append(dict(name="core", map="CORE_map.png", floor=CORE_FLOOR, margin=0, close=True, patches=[
    ("erase", 117, 1653, 30, 50, "ring"), ("erase", 462, 1759, 32, 50, "ring"), ("erase", 826, 1088, 28, 54, "ring"),
    ("erase", 2096, 832, 28, 50, "ring"), ("erase", 2646, 164, 34, 48, "ring"), ("erase", 3344, 466, 34, 44, "ring", []),
    ("copy", 3624, 112, 36, 52, 3560, 112),   # (Alphys, in the last hallway: the hallway further on, over her)
    # the force field before The End (an object here, drawn while it's up): the bridge 32 pixels on, over it
    ("copy", 2551, 836, 22, 84, 2519, 836),
]))

# New Home: grey streets, Asgore's home, the basement's corridors, the Last Corridor, the throne room, the coffins,
# the barrier. Its floors are cut by their rectangles alone (many colours: carpets, planks, flowers). Asgore (in the
# throne room, at the barrier) is an object here, and the friends of the true ending are off the map
AREAS.append(dict(name="newhome", map="New_Home_map.png", floor=[], margin=0, close=False, patches=[
    ("erase", 5988, 1270, 62, 92, "ring", []), ("erase", 6310, 717, 64, 72, "ring", []),
    ("erase", 5868, 906, 36, 54, "ring", []), ("erase", 5906, 921, 32, 42, "ring", []),
    ("erase", 5944, 889, 36, 64, "ring", []), ("erase", 5976, 886, 38, 70, "ring", []),
    ("erase", 6014, 921, 30, 42, "ring", []), ("erase", 6036, 911, 58, 64, "ring", []),
]))

ROOMS = [
    # the landing: the bed of golden flowers, a hallway, the Delta Rune arch
    dict(name="area1", area="ruins", rect=(0, 5900, 680, 260), title="Ruins", margin=0,
         fix=[("walk", 600, 6040, 36, 22)],
         doors=[((602, 6040, 32, 8), "area1_2", (510, 5858), "up")]),
    # Flowey's patch of grass in the dark (the floor is black: all of it is walked on), the purple arch north
    dict(name="area1_2", area="ruins", rect=(360, 5560, 320, 340), title="Ruins", margin=0,
         floor_colors=[(0, 0, 0)], floor_rects=[(420, 5640, 200, 260), (500, 5560, 40, 80)],
         fix=[("walk", 500, 5560, 40, 80)],   # under the arch
         doors=[((420, 5894, 200, 6), "area1", (608, 6030), "down")]),
    # the entrance: two staircases, the red leaves, the first SAVE Point
    dict(name="ruins1", area="ruins", rect=(360, 5080, 320, 480), title="Ruins - Entrance", margin=0,
         floor_rects=[(380, 5280, 280, 280), (480, 5240, 80, 40), (400, 5200, 240, 22), (400, 5222, 80, 80),
                      (560, 5222, 80, 80)],
         fix=[("walk", 420, 5276, 40, 28), ("walk", 580, 5276, 40, 28)]),
    # the middle road puzzle: six switches on the floor and a lever
    dict(name="ruins2", area="ruins", rect=(360, 4840, 320, 240), title="Ruins",
         fix=[("walk", 482, 4840, 36, 86)]),
    # the switch room, with Toriel's labels; spikes at its end
    dict(name="ruins3", area="ruins", rect=(360, 4600, 720, 240), title="Ruins", fix=[("walk", 482, 4820, 36, 20)]),
    # the training dummy
    dict(name="ruins4", area="ruins", rect=(1080, 4600, 320, 240), title="Ruins"),
    # the blueprint and the spikes over the water
    dict(name="ruins5", area="ruins", rect=(1100, 4340, 1180, 260), title="Ruins",
         fix=[("wall", 1940, 4460, 280, 80), ("walk", 2216, 4460, 12, 40), ("walk", 1932, 4500, 12, 40)] + [
             ("walk", 1940 + 20 * c, 4460 + 20 * r, 20, 20)
             for r, row in enumerate(["..#####...####", "..#####...####", "####.#######..", "####.#######.."])
             for c, v in enumerate(row) if v == "#"]),
    # the long hallway: the independence test
    dict(name="ruins6", area="ruins", rect=(2280, 4340, 2520, 240), title="Ruins"),
    # the leaf pile, its SAVE Point, the first Froggit to talk
    dict(name="ruins7", area="ruins", rect=(4800, 4360, 280, 440), view=(4800, 4360, 320, 440), title="Ruins - Leaf Pile"),
    # the Monster Candy
    dict(name="ruins7a", area="ruins", rect=(4860, 4100, 200, 260), title="Ruins"),
    # the cracked floor and the room under it
    dict(name="ruins8", area="ruins", floor_colors=CRACKED, rect=(5080, 4600, 410, 220), title="Ruins",
         fix=[("walk", 5244, 4704, 72, 56)]),
    dict(name="ruins8b", area="ruins", rect=(5140, 5020, 280, 200), title="Ruins"),
    # the first rock puzzle
    dict(name="ruins9", area="ruins", floor_colors=CRACKED, rect=(5490, 4600, 420, 220), title="Ruins",
         fix=[("walk", 5676, 4736, 28, 28)]),
    # the leaf puzzle: cracked floor above, the way through in leaves below
    dict(name="ruins10", area="ruins", floor_colors=CRACKED, rect=(5910, 4600, 580, 320), title="Ruins",
         fix=[("walk", 6152, 4736, 128, 164), ("walk", 6280, 4778, 188, 122), ("wall", 6280, 4700, 190, 78),
              ("wall", 5910, 4600, 380, 98)]),
    dict(name="ruins10b", area="ruins", rect=(5940, 5020, 540, 300), title="Ruins"),
    # the second rock puzzle: the rock that won't move
    dict(name="ruins11", area="ruins", floor_colors=CRACKED, rect=(6490, 4680, 560, 260), title="Ruins",
         fix=[("walk", 6776, y, 28, 28) for y in (4796, 4836, 4876)]),
    # the mouse hole, its SAVE Point, the cheese
    dict(name="ruins12", area="ruins", rect=(7050, 4720, 380, 200), title="Ruins - Mouse Hole"),
    # Napstablook, pretending to sleep
    dict(name="ruins13", area="ruins", rect=(7430, 4700, 260, 220), title="Ruins"),
    # the Spider Bake Sale
    dict(name="ruins14", area="ruins", rect=(7690, 4700, 340, 220), view=(7690, 4700, 340, 240), title="Ruins"),
    # the Froggits' hall
    dict(name="ruins15", area="ruins", rect=(7520, 4400, 540, 300), title="Ruins"),
    # the one switch puzzle, and its six rooms under it
    dict(name="ruins16", area="ruins", floor_colors=CRACKED, rect=(8060, 4480, 640, 280), title="Ruins",
         fix=[("walk", x, y, 60, 60) for x in (8320, 8440, 8560) for y in (4560, 4680)]),
    dict(name="ruins16a", area="ruins", rect=(8230, 4915, 160, 125), title="Ruins"),
    dict(name="ruins16b", area="ruins", rect=(8410, 4915, 140, 125), title="Ruins"),
    dict(name="ruins16c", area="ruins", rect=(8550, 4915, 140, 125), title="Ruins"),
    dict(name="ruins16d", area="ruins", rect=(8230, 5040, 160, 120), title="Ruins"),
    dict(name="ruins16e", area="ruins", rect=(8410, 5040, 140, 120), title="Ruins"),
    dict(name="ruins16f", area="ruins", rect=(8550, 5040, 140, 120), title="Ruins"),
    # the rotating rooms: the same room, turned; and the one a wrong switch drops Frisk in
    dict(name="ruins17a", area="ruins", rect=(8700, 4500, 360, 400), title="Ruins",
         fix=[("walk", 8752, 4620, 36, 40), ("walk", 8960, 4832, 40, 36)],
         doors=[((8960, 4894, 40, 6), "ruins17b", (8970, 4124), "down")]),
    dict(name="ruins17b", area="ruins", rect=(8720, 4060, 340, 380), title="Ruins",
         fix=[("walk", 8962, 4126, 36, 14), ("walk", 8960, 4132, 40, 36)],
         doors=[((8960, 4126, 40, 6), "ruins17a", (8970, 4858), "up")]),
    dict(name="ruins17c", area="ruins", rect=(8380, 4000, 340, 440), title="Ruins",
         fix=[("walk", 8652, 4340, 36, 40)]),
    dict(name="ruins17d", area="ruins", rect=(8380, 3610, 320, 390), title="Ruins",
         fix=[("walk", 8440, 3952, 40, 36)]),
    dict(name="ruins17x", area="ruins", rect=(9140, 4480, 280, 220), title="Ruins"),
    # the crossroads, the Froggit who saw Toriel, the balcony over Home
    dict(name="ruins18", area="ruins", rect=(8700, 3460, 590, 370), title="Ruins",
         fix=[("walk", 8846, 3720, 48, 80), ("walk", 9166, 3720, 48, 80)]),   # vines on the floor
    dict(name="ruins19", area="ruins", rect=(9290, 3620, 250, 200), title="Ruins"),
    dict(name="ruins20", area="ruins", rect=(9300, 3360, 360, 260), title="Ruins"),
    # Toriel's tree and her door
    dict(name="ruins21", area="ruins", rect=(8860, 2920, 320, 540), title="Ruins - Home",
         fix=[("wall", 9004, 3300, 38, 40)]),   # the tree's trunk (its leaves are walked on)
    # Toriel's home
    dict(name="torhouse1", area="ruins", floor_colors=HOUSE_FLOOR, margin=0, rect=(8860, 2660, 320, 260), title="Home",
         floor_rects=[(8860, 2815, 320, 75), (8900, 2760, 25, 60), (9085, 2740, 80, 80), (8995, 2890, 45, 30)],
         fix=[("walk", 9062, 2742, 24, 16), ("wall", 9100, 2736, 46, 18)], doors=[((9062, 2742, 14, 16), "basement1", (8952, 2316), "right")]),
    dict(name="torhouse2", area="ruins", floor_colors=HOUSE_FLOOR, margin=0, rect=(8540, 2670, 320, 250), title="Home",
         floor_rects=[(8560, 2735, 280, 150), (8585, 2670, 42, 65), (8840, 2800, 20, 85)],
         fix=[("wall", 8655, 2680, 90, 82), ("wall", 8753, 2690, 60, 62), ("wall", 8815, 2705, 22, 47),
              ("wall", 8592, 2822, 108, 50), ("wall", 8578, 2830, 16, 24), ("wall", 8700, 2830, 16, 24)]),
    dict(name="torkitchen", area="ruins", floor_colors=HOUSE_FLOOR, margin=0, rect=(8540, 2500, 200, 170), title="Home",
         floor_rects=[(8562, 2600, 156, 62), (8587, 2660, 40, 12)]),
    dict(name="torhouse3", area="ruins", floor_colors=HOUSE_FLOOR, margin=0, rect=(9180, 2700, 760, 220), title="Home",
         floor_rects=[(9180, 2820, 720, 60)],
         doors=[((9326, 2814, 28, 8), "asrielroom", (9377, 2630), "up"),
                ((9536, 2814, 28, 8), "torielroom", (9607, 2630), "up")]),
    dict(name="asrielroom", area="ruins", floor_colors=HOUSE_FLOOR, margin=0, rect=(9200, 2460, 260, 230), title="Home",
         floor_rects=[(9240, 2540, 180, 120), (9366, 2655, 42, 30)],
         fix=[("wall", 9368, 2536, 40, 46), ("wall", 9405, 2540, 16, 40), ("wall", 9394, 2578, 24, 14)],
         doors=[((9366, 2676, 42, 8), "torhouse3", (9330, 2826), "down")]),
    dict(name="torielroom", area="ruins", floor_colors=HOUSE_FLOOR, margin=0, rect=(9510, 2430, 280, 260), view=(9500, 2440, 320, 240), title="Home",
         floor_rects=[(9530, 2540, 240, 120), (9597, 2655, 42, 35)],
         fix=[("wall", 9530, 2536, 66, 56), ("wall", 9540, 2592, 48, 44), ("wall", 9575, 2580, 26, 30),
              ("wall", 9695, 2605, 28, 36)],
         doors=[((9597, 2676, 41, 8), "torhouse3", (9540, 2826), "down")]),
    # the basement, to the end of the Ruins
    dict(name="basement1", area="ruins", floor_colors=BASEMENT_FLOOR, margin=0, rect=(8860, 2220, 1260, 180), title="Ruins",
         fix=[("walk", 8930, 2320, 20, 40)], doors=[((8930, 2320, 8, 40), "torhouse1", (9084, 2764), "down")]),
    dict(name="basement2", area="ruins", floor_colors=BASEMENT_FLOOR, margin=0, rect=(9930, 1960, 220, 260), title="Ruins",
         fix=[("walk", 10000, 2024, 40, 10)], doors=[((10000, 2026, 40, 6), "basement3", (10010, 1914), "up")]),
    dict(name="basement3", area="ruins", floor_colors=[(97, 44, 97), (132, 53, 125), (0, 0, 0)], margin=0,
         rect=(9940, 300, 180, 1660), floor_rects=[(9980, 300, 80, 1660)], title="Ruins",
         fix=[("walk", 10000, 1950, 40, 10)], doors=[((10000, 1956, 40, 4), "basement2", (10010, 2034), "down")]),
    dict(name="ruinsexit", area="ruins", floor_colors=[(0, 0, 0), (97, 44, 97)], margin=0, rect=(9940, 0, 200, 300),
         floor_rects=[(9980, 70, 80, 230)], title="Ruins - Exit"),

    # ---- Snowdin: the forest, from the Ruins' door to the town
    # the forest's walkway: the path is walked all along (trunks and a branch drawn over it are in front)
    dict(name="snowdin1", area="snowdin", rect=(0, 240, 1070, 240), title="Snowdin Forest",
         fix=[("walk", 40, 404, 1030, 52)]),
    dict(name="snowdin2", area="snowdin", rect=(1100, 240, 670, 240), title="Snowdin Forest", fix=[("walk", 1100, 404, 670, 52)]),
    dict(name="snowdin3", area="snowdin", rect=(1800, 240, 410, 240), title="Snowdin Forest", fix=[("walk", 1800, 404, 410, 52)]),
    dict(name="snowdin4", area="snowdin", rect=(2240, 240, 300, 240), title="Snowdin Forest", fix=[("walk", 2240, 404, 300, 52)]),
    # Papyrus' gate on the bridge: its bars are too wide to stop anyone
    dict(name="snowdin5", area="snowdin", rect=(2570, 240, 590, 240), title="Snowdin Forest",
         fix=[("walk", 2570, 412, 100, 44)]),
    dict(name="boxroad", area="snowdin", rect=(3160, 240, 380, 240), title="Snowdin - Box Road"),
    dict(name="river", area="snowdin", rect=(3220, 0, 320, 240), title="Snowdin Forest"),
    dict(name="papsentry", area="snowdin", rect=(3540, 240, 580, 240), title="Snowdin Forest"),
    dict(name="doggo", area="snowdin", rect=(4120, 240, 460, 240), title="Snowdin Forest"),
    dict(name="icefield", area="snowdin", rect=(4580, 240, 620, 240), ice=True, title="Snowdin Forest"),
    dict(name="snowman", area="snowdin", rect=(4720, 0, 380, 240), title="Snowdin Forest"),
    dict(name="elecmaze", area="snowdin", rect=(5200, 240, 300, 240), title="Snowdin Forest"),
    dict(name="ballgame", area="snowdin", rect=(5500, 240, 800, 580), ice=True, title="Snowdin Forest",
         fix=[("walk", 5716, 318, 68, 22)]),   # the plank bridge over the chasm
    dict(name="dogi", area="snowdin", rect=(6300, 240, 300, 220), title="Snowdin Forest"),
    dict(name="wordsearch", area="snowdin", rect=(6300, 520, 320, 300), view=(6280, 520, 320, 300), title="Snowdin Forest"),
    dict(name="spaghetti", area="snowdin", rect=(6620, 520, 300, 300), view=(6600, 520, 320, 300), title="Snowdin - Spaghetti"),
    dict(name="snowpuzzle", area="snowdin", rect=(6920, 540, 620, 580), title="Snowdin Forest",
         fix=[("walk", 7254, 958, 50, 22)]),   # the plank bridge
    dict(name="xoxo1", area="snowdin", rect=(7260, 1120, 440, 240), title="Snowdin Forest"),
    dict(name="xoxo2", area="snowdin", rect=(7700, 1120, 800, 240), title="Snowdin Forest"),
    dict(name="tilepuzzle", area="snowdin", rect=(8500, 1120, 400, 240), title="Snowdin Forest",
         floor_colors=[(255, 192, 192)], fix=[("walk", 8614, 1238, 52, 22)]),   # the plank bridge
    dict(name="doghouse", area="snowdin", rect=(8900, 1120, 540, 240), title="Snowdin - Dog House"),
    # the ice puzzle: once solved, a bridge to the trees Frisk slides through (an object blocks it until then)
    dict(name="xoxo3", area="snowdin", rect=(9440, 1120, 400, 220), view=(9440, 1120, 400, 240), ice=True, title="Snowdin Forest",
         fix=[("walk", 9780, 1204, 60, 36), ("walk", 9520, 1320, 264, 20)], doors=[((9836, 1205, 4, 40), "poffzone", (10196, 1200), "right")]),
    dict(name="xoxofail", area="snowdin", rect=(9440, 1340, 340, 260), title="Snowdin Forest"),
    dict(name="poffzone", area="snowdin", rect=(10140, 1120, 820, 240), title="Snowdin Forest",
         fix=[("walk", 10190, 1212, 40, 24), ("walk", 10840, 1260, 120, 14)],
         doors=[((10190, 1195, 4, 30), "xoxo3", (9796, 1200), "left"),
                ((10302, 1350, 36, 8), "southcliff", (10590, 2190), "down")]),
    dict(name="dangerbridge", area="snowdin", rect=(10960, 1120, 1040, 240), title="Snowdin Forest",
         fix=[("walk", 11110, 1226, 690, 26)], doors=[((11990, 1236, 10, 50), "snowtown", (4, 1088), "right")]),
    # the mysterious door, up a long corridor from the cliff under the ice bridge
    dict(name="doorroom", area="snowdin", rect=(9780, 1590, 320, 590), title="Snowdin Forest",
         floor_colors=[(22, 67, 88), (27, 89, 109), (30, 106, 122), (6, 14, 22), (13, 37, 51), (19, 56, 75)]),
    dict(name="southcliff", area="snowdin", rect=(9480, 2180, 1200, 240), title="Snowdin Forest",
         fix=[("walk", 9922, 2180, 40, 60)], doors=[((10585, 2180, 30, 6), "poffzone", (10310, 1320), "up")]),

    # ---- Snowdin Town
    dict(name="snowtown", area="snowtown", rect=(0, 960, 3260, 240), title="Snowdin Town",
         fix=[("walk", 0, 1080, 8, 50, "floor")],
         doors=[((0, 1080, 4, 50), "dangerbridge", (11968, 1232), "left"),
                ((624, 1066, 28, 24), "inn1", (628, 918), "up"), ((1312, 1070, 26, 20), "grillbys", (1250, 916), "up"),
                ((1840, 1066, 26, 24), "library", (1950, 668), "up"),
                # the undersnow tunnels: an igloo each end of the town
                ((726, 1066, 24, 24), "snowtown", (2336, 1094), "down"), ((2336, 1084, 26, 16), "snowtown", (728, 1078), "down")]),
    dict(name="townnorth", area="snowtown", rect=(1400, 700, 640, 260), title="Snowdin Town"),
    dict(name="towndock", area="snowtown", rect=(1400, 480, 410, 220), view=(1400, 480, 410, 240), title="Snowdin Town"),
    dict(name="fogroom", area="snowtown", rect=(3260, 960, 360, 240), title="Snowdin Town",
         fix=[("walk", 3440, 1084, 180, 48)],   # the way on: Waterfall's blue ground
         doors=[((3612, 1080, 8, 56), "wf1", (4, 1480), "right")]),
    # the interiors: their floors are many colours, so their floor and furniture are rectangles
    # (an interior's rectangle is the game's room, 320 x 240: the view is where the game has it)
    dict(name="inn1", area="snowtown", rect=(500, 720, 320, 240), title="Snowed Inn", open=True, margin=0,
         floor_rects=[(540, 800, 180, 80), (600, 880, 120, 40), (620, 918, 37, 42), (540, 756, 38, 44)],
         fix=[("wall", 578, 800, 144, 40), ("wall", 700, 846, 22, 74)],
         doors=[((620, 952, 37, 8), "snowtown", (626, 1078), "down"), ((540, 756, 38, 6), "inn2", (512, 676), "right")]),
    dict(name="inn2", area="snowtown", rect=(500, 480, 320, 230), title="Snowed Inn", open=True, margin=0,
         floor_rects=[(540, 600, 100, 62), (660, 626, 122, 36), (504, 684, 312, 22), (566, 660, 32, 26), (706, 660, 32, 26)],
         doors=[((504, 684, 4, 22), "inn1", (548, 790), "down")]),
    dict(name="grillbys", area="snowtown", rect=(1080, 720, 320, 240), title="Grillby's", open=True, margin=0,
         floor_rects=[(1092, 790, 293, 160), (1240, 948, 40, 12)],
         fix=[("wall", 1175, 785, 145, 30), ("wall", 1100, 795, 60, 40), ("wall", 1118, 830, 140, 75),
              ("wall", 1118, 916, 44, 30), ("wall", 1300, 828, 90, 132)],
         doors=[((1240, 954, 40, 6), "snowtown", (1314, 1080), "down")]),
    dict(name="library", area="snowtown", rect=(1800, 480, 320, 240), title="Librarby", open=True, margin=0,
         floor_rects=[(1840, 560, 240, 120), (1940, 680, 40, 30)],
         fix=[("wall", 1840, 600, 80, 80), ("wall", 1968, 588, 94, 72)],   # the counter; the table and the readers
         doors=[((1940, 702, 40, 8), "snowtown", (1842, 1078), "down")]),
    dict(name="pshouse", area="snowtown", rect=(2200, 720, 320, 240), title="Snowdin Town", open=True, margin=0,
         floor_rects=[(2220, 848, 280, 100), (2220, 768, 280, 34), (2222, 800, 56, 52), (2420, 940, 40, 20)],
         fix=[("wall", 2300, 892, 82, 40), ("wall", 2318, 836, 52, 30), ("wall", 2462, 868, 40, 64),
              ("wall", 2228, 904, 30, 28)],
         doors=[((2420, 950, 40, 8), "snowtown", (2240, 1096), "down"), ((2228, 766, 34, 4), "paproom", (2328, 666), "up"), ((2440, 766, 30, 4), "sansroom", (2514, 662), "up")]),
    dict(name="paproom", area="snowtown", rect=(2120, 480, 320, 240), title="Snowdin Town", open=True, margin=0,
         floor_rects=[(2160, 562, 240, 122), (2318, 684, 40, 30)],
         fix=[("wall", 2160, 620, 90, 60), ("wall", 2338, 556, 64, 24), ("wall", 2160, 556, 40, 64),
              ("wall", 2358, 648, 44, 34)],
         doors=[((2318, 706, 40, 8), "pshouse", (2234, 774), "down")]),
    dict(name="sansroom", area="snowtown", rect=(2440, 480, 320, 240), title="Snowdin Town", open=True, margin=0,
         floor_rects=[(2480, 566, 158, 118), (2502, 684, 46, 30)],
         fix=[("wall", 2480, 560, 52, 32), ("wall", 2598, 556, 42, 56)],
         doors=[((2502, 706, 46, 8), "pshouse", (2444, 774), "down")]),
    dict(name="shed", area="snowtown", rect=(2520, 720, 320, 240), title="Snowdin Town", open=True, margin=0,
         floor_rects=[(2580, 770, 180, 120), (2580, 886, 40, 74)], fix=[("wall", 2580, 730, 56, 100)],
         doors=[((2580, 945, 40, 8), "snowtown", (2400, 1096), "down")]),
    # Waterfall, the west: from Snowdin to the bridge Undyne cuts
    dict(name="wf1", area="waterfall", rect=(0, 1330, 1170, 310), title="Waterfall",
         fix=[("walk", 1040, 1505, 20, 135), ("walk", 1150, 1452, 20, 56)],
         doors=[((0, 1450, 4, 96), "fogroom", (3580, 1088), "left")]),
    dict(name="wf_falls", area="waterfall", rect=(1170, 1140, 180, 500), title="Waterfall", floor_colors=WF_WATER,
         floor_rects=[(1170, 1446, 180, 194), (1236, 1326, 48, 124), (1180, 1236, 160, 96)],
         fix=[("walk", 1170, 1452, 14, 56), ("walk", 1336, 1500, 14, 36), ("walk", 1236, 1396, 48, 56)]),
    dict(name="wf_falls_low", area="waterfall", rect=(1030, 1640, 450, 180), title="Waterfall", floor_colors=WF_WATER,
         fix=[("walk", 1040, 1640, 20, 14), ("walk", 1160, 1676, 20, 80), ("walk", 1360, 1676, 20, 80),
              ("wall", 1180, 1640, 160, 14)]),
    dict(name="wf2", area="waterfall", rect=(1350, 1140, 750, 500), title="Waterfall",
         fix=[("walk", 1350, 1500, 16, 36), ("walk", 1756, 1500, 172, 36)]),   # (the tall grass)
    dict(name="wf_seeds1", area="waterfall", rect=(2100, 1300, 520, 340), title="Waterfall"),
    dict(name="wf_seeds2", area="waterfall", rect=(2620, 1120, 500, 520), title="Waterfall",
         fix=[("walk", 2716, 1228, 46, 34), ("walk", 2810, 1494, 100, 28)],   # (the doorway, the planks)
         doors=[((2716, 1232, 46, 24), "wf_wish", (2727, 1080), "up")]),
    # (reached by the secret bridge of seeds, then the planks)
    dict(name="wf_quiche", area="waterfall", rect=(3120, 1340, 260, 300), title="Waterfall", floor_colors=WF_WATER,
         floor_rects=[(3180, 1440, 180, 100)], fix=[("walk", 3120, 1540, 100, 20)]),
    dict(name="wf_wish", area="waterfall", rect=(2640, 780, 740, 340), title="Waterfall", floor_colors=WF_WATER,
         floor_rects=[(2660, 1040, 700, 62), (3098, 816, 64, 226), (2712, 1100, 50, 22)],
         fix=[("walk", 3112, 812, 36, 50)], doors=[((2712, 1118, 50, 4), "wf_seeds2", (2729, 1250), "down")]),
    dict(name="wf_hist", area="waterfall", rect=(2980, 400, 530, 380), title="Waterfall"),
    dict(name="wf_chase1", area="waterfall", rect=(4000, 300, 2520, 400), title="Waterfall"),
    dict(name="wf_grass1", area="waterfall", rect=(6520, 400, 1320, 300), title="Waterfall", floor_colors=WF_WATER,
         floor_rects=[(6520, 540, 360, 72), (6880, 560, 960, 44), (7100, 520, 200, 40)], fix=[("walk", 6520, 544, 360, 64)]),
    dict(name="wf_sans2", area="waterfall", rect=(7840, 0, 360, 620), view=(7840, 0, 360, 660), title="Waterfall",
         floor_colors=WF_WATER, floor_rects=[(7860, 92, 300, 118), (7956, 206, 38, 360), (7840, 540, 360, 80)],
         fix=[("walk", 7956, 420, 38, 140)]),
    # the marsh, where the colours turn: its land is black, its water bright blue (the planks cross it), and around
    # it plain black is nothing (its rectangles); the boardwalk ends at a gap the bird carries Frisk over, a ladder
    # goes down into the marsh
    dict(name="wf_marsh_top", area="waterfall", rect=(8200, 400, 450, 220), view=(8200, 400, 450, 260), title="Waterfall",
         floor_colors=WF_WATER + [(23, 13, 43), (0, 0, 0)], floor_rects=[(8200, 560, 280, 60), (8540, 560, 110, 60)]),
    dict(name="wf_marsh_mid", area="waterfall", rect=(7400, 620, 1250, 180), title="Waterfall",
         floor_colors=[(0, 0, 0)], floor_rects=[(8060, 620, 590, 80), (8060, 700, 110, 100), (7760, 780, 300, 20)],
         fix=[("walk", 8140, 620, 20, 64)]),
    dict(name="wf_marsh", area="waterfall", rect=(7400, 800, 1650, 660), title="Waterfall",
         floor_colors=[(0, 0, 0)], floor_rects=[(7660, 800, 510, 660), (8170, 940, 190, 520), (8360, 1000, 380, 460),
                                               (8740, 800, 55, 660), (8795, 800, 85, 120), (8795, 1000, 265, 460),
                                               (7460, 1150, 200, 190)],
         fix=[("walk", x, y - 2, w, h + 4) for x, y, w, h in [(7821, 825, 58, 55), (7921, 945, 58, 55), (8301, 1005, 58, 55),
              (8741, 1005, 58, 55), (8561, 1145, 58, 55), (7941, 1220, 59, 60), (8501, 1360, 59, 57), (8741, 1440, 56, 17)]]),
    # (up from the marsh, a narrow black path by Onionsan's water)
    dict(name="wf_onion", area="waterfall", rect=(8650, 500, 1250, 300), title="Waterfall", floor_colors=[(0, 0, 0)],
         floor_rects=[(8740, 656, 1160, 48), (8740, 656, 60, 144)], fix=[("walk", 9860, 660, 40, 40)]),
    dict(name="wf_piano", area="waterfall", rect=(9900, 20, 600, 780), title="Waterfall", floor_colors=WF_WATER,
         floor_rects=[(10060, 140, 200, 64), (10136, 200, 48, 104), (10118, 300, 64, 100), (10040, 398, 220, 64),
                      (10118, 460, 64, 140), (10040, 598, 220, 124), (9900, 660, 142, 40), (10258, 660, 242, 40)],
         # (the arch in the wall north of the piano: the way up to the artifact, once the piano has opened it)
         doors=[((10136, 376, 48, 8), "wf_piano", (10150, 258), "up"), ((10136, 294, 48, 6), "wf_piano", (10150, 384), "down")]),
    dict(name="wf_statue", area="waterfall", rect=(10500, 520, 1000, 260), title="Waterfall",
         floor_colors=WF_WATER + [(12, 9, 42), (10, 5, 23), (23, 13, 43), (12, 16, 53), (30, 25, 73), (73, 68, 128), (8, 12, 42)],
         floor_rects=[(10500, 640, 1000, 80)]),   # (the rain, the statue's light)
    # (the rain's puddles are black)
    dict(name="wf_rain1", area="waterfall", rect=(11500, 520, 1360, 300), title="Waterfall",
         floor_colors=WF_WATER + [(0, 0, 0)], floor_rects=[(11500, 660, 1360, 60), (11880, 620, 60, 40), (12800, 720, 60, 100)]),
    dict(name="wf_rain2", area="waterfall", rect=(12700, 820, 300, 980), title="Waterfall", floor_colors=WF_WATER,
         floor_rects=[(12800, 820, 60, 900), (12800, 1660, 200, 60)],
         doors=[((12990, 1660, 10, 60), "wf_castle", (13004, 1726), "right")]),
    dict(name="wf_castle", area="waterfall", rect=(13000, 1560, 1400, 260), title="Waterfall", floor_colors=[],
         fix=[("walk", 13000, 1736, 1400, 40)],
         doors=[((13000, 1736, 6, 40), "wf_rain2", (12966, 1674), "left"),
                ((14394, 1736, 6, 40), "wf_aaron", (14406, 1680), "right")]),
    dict(name="wf_aaron", area="waterfall", rect=(14400, 1560, 960, 260), title="Waterfall", floor_colors=WF_WATER,
         doors=[((14400, 1680, 6, 60), "wf_castle", (14372, 1726), "left")]),
    dict(name="wf_bridge", area="waterfall", rect=(15360, 1540, 2300, 460), title="Waterfall"),
    # under the bridge: the garbage dump's landing (Frisk wades in its water), the Trash Zone, the walkway up to
    # Mad Dummy
    dict(name="wf_dump", area="waterfall", rect=(17580, 3120, 740, 260), title="Waterfall", floor_colors=WF_DUMP,
         fix=[("walk", 17666, 3226, 64, 40)]),   # (the golden flowers Frisk wakes up on)
    dict(name="wf_trash", area="waterfall", rect=(18320, 3120, 500, 240), view=(18320, 3140, 500, 240), title="Waterfall", floor_colors=WF_DUMP),
    dict(name="wf_walkway", area="waterfall", rect=(18820, 2420, 160, 920), title="Waterfall", floor_colors=WF_DUMP),
    # the Quiet Area and its pond, the small bird's side of the gap, Blook Acres (Undyne's, Napstablook's and
    # Mettaton's houses, the snail farm), Napstablook's room, Gerson's crossroads
    dict(name="wf_quiet", area="waterfall", rect=(18660, 2200, 480, 220), view=(18660, 2220, 480, 240), title="Waterfall", floor_colors=WF_WATER,
         fix=[("walk", 18650, 2296, 50, 44)]),
    dict(name="wf_bird2", area="waterfall", rect=(18250, 2200, 410, 240), title="Waterfall",
         floor_colors=WF_WATER + [(23, 13, 43), (0, 0, 0)]),
    dict(name="wf_acres", area="waterfall", rect=(18420, 1960, 1000, 240), view=(18420, 1980, 1000, 240), title="Waterfall", floor_colors=WF_WATER,
         fix=[("walk", 18876, 2088, 24, 30)],   # (Napstablook's doorway)
         doors=[((18876, 2098, 24, 10), "wf_napsta_in", (18826, 1906), "up")]),
    dict(name="wf_napsta_in", area="waterfall", rect=(18780, 1720, 260, 240), view=(18760, 1740, 320, 240), title="Napstablook's house",
         floor_colors=NAPSTA_FLOOR, fix=[("walk", 18820, 1920, 40, 40)],
         doors=[((18820, 1954, 40, 6), "wf_acres", (18877, 2116), "down")]),
    dict(name="wf_gerson", area="waterfall", rect=(19140, 2200, 710, 280), title="Waterfall", floor_colors=WF_WATER),
    # the twin waterfalls (waded through), the mushroom maze (its walls dotted lines of grass: kept clear of), Temmie
    # Village below it
    dict(name="wf_twin", area="waterfall", rect=(19850, 2200, 700, 220), title="Waterfall",
         floor_colors=WF_WATER + WF_DUMP + [(0, 0, 0)], floor_rects=[(19850, 2320, 700, 40)]),
    dict(name="wf_mushroom", area="waterfall", rect=(20550, 2280, 610, 600), title="Waterfall",
         floor_colors=[(0, 0, 0)] + WF_WATER + WF_DUMP, margin=3, floor_rects=[(20550, 2320, 214, 40), (20555, 2350, 605, 530)],
         fix=[("walk", 20550, 2320, 214, 40), ("walk", 20556, 2346, 212, 16), ("walk", 20764, 2320, 24, 40),
              ("walk", 21020, 2372, 140, 26), ("walk", 21056, 2850, 44, 30)]),
    # (the way in, into the maze; out at its top right; the hidden way down to the village)
    dict(name="wf_temmie", area="waterfall", rect=(21020, 2880, 660, 320), title="Temmie Village", floor_colors=WF_WATER,
         floor_rects=[(21040, 3058, 582, 84), (21056, 2880, 44, 180)],
         # (the statue, the mushroom and the Temmies east of the shop: they stand there)
         fix=[("walk", 21056, 2880, 44, 24), ("wall", 21468, 3058, 50, 20), ("wall", 21418, 3078, 22, 12),
              ("wall", 21474, 3122, 22, 12), ("wall", 21582, 3120, 22, 12), ("wall", 21540, 3086, 16, 14)]),
    # the crystal maze of lanterns, the dark dead end where Undyne finds Frisk, the echo flowers' path north, the chasm's
    # bridge (Monster Kid), Undyne's crag, the way north and the gate to Hotland
    dict(name="wf_lantern", area="waterfall", rect=(21160, 2340, 640, 560), title="Waterfall", floor_colors=[(0, 0, 0)],
         margin=3),
    # (the dead end's hidden way north, through the grass, walked once Undyne has gone; the echo flowers' path goes
    # on up into the chasm's room, the rooms meeting)
    dict(name="wf_deadend", area="waterfall", rect=(21800, 2560, 640, 200), title="Waterfall",
         floor_colors=[(0, 0, 0)] + WF_WATER + WF_DUMP, floor_rects=[(21800, 2616, 640, 90), (22160, 2556, 62, 64)],
         fix=[("walk", 22164, 2560, 54, 60)]),
    dict(name="wf_echopath", area="waterfall", rect=(21940, 2080, 420, 480), title="Waterfall",
         floor_colors=[(0, 0, 0)] + WF_WATER + WF_DUMP, floor_rects=[(22160, 2080, 62, 480)]),
    dict(name="wf_chasm", area="waterfall", rect=(22160, 1940, 1090, 160), title="Waterfall",
         floor_colors=WF_WATER + WF_DUMP + [(23, 13, 43)],
         fix=[("walk", 22416, 2040, 238, 20), ("walk", 23096, 2040, 70, 20)]),
    dict(name="wf_arena", area="waterfall", rect=(23250, 1440, 370, 640), view=(23250, 1440, 370, 680), title="Waterfall",
         floor_colors=WF_WATER + WF_DUMP + [(23, 13, 43)], floor_rects=[(23250, 1980, 370, 100), (23418, 1940, 80, 40)],
         doors=[((23418, 1940, 80, 8), "wf_north", (23449, 1340), "up")]),
    # the river under the crag, the neon sign's hall
    dict(name="wf_north", area="waterfall", rect=(23280, 880, 360, 560), title="Waterfall", floor_colors=WF_WATER,
         doors=[((23420, 1372, 78, 8), "wf_arena", (23449, 1950), "down"),
                ((23420, 897, 78, 8), "wf_hotgate", (23512, 776), "right")]),
    dict(name="wf_hotgate", area="waterfall", rect=(23480, 640, 740, 200), title="Waterfall", floor_colors=WF_WATER,
         doors=[((23498, 775, 8, 47), "wf_north", (23449, 910), "down"),
                ((24210, 775, 10, 47), "ht_entrance", (24, 6206), "right")]),
    # Hotland's first room: Sans's station, the bridge over the lava, the water cooler
    dict(name="ht_entrance", area="hotland", rect=(0, 6080, 800, 320), title="Hotland",
         doors=[((0, 6215, 6, 45), "wf_hotgate", (24180, 776), "left")]),
    # the crossroads before the Lab: the elevator up north (the Royal Guards in the way), the River Person's dock
    # south, the Lab east
    dict(name="ht_labfront", area="hotland", rect=(800, 5880, 640, 480), title="Hotland",
         doors=[((1322, 6240, 40, 44), "ht_lab", (1462, 6250), "right")]),
    dict(name="ht_river", area="hotland", rect=(900, 6360, 320, 240), title="Hotland", floor_colors=HT_DOCK),
    # Alphys's Lab, both its floors (the escalators between them)
    dict(name="ht_lab", area="hotland", rect=(1440, 5860, 1100, 460), title="Lab", floor_colors=LAB_FLOOR,
         doors=[((1440, 6200, 6, 100), "ht_labfront", (1300, 6262), "left"),
                ((2534, 6220, 6, 80), "ht_labexit", (2615, 6250), "right")]),
    # out of the Lab: the conveyor belts north, two rooms of them, then the striped bridge up
    dict(name="ht_labexit", area="hotland", rect=(2540, 5990, 440, 350), title="Hotland",
         doors=[((2580, 6236, 34, 44), "ht_lab", (2500, 6255), "left")]),
    dict(name="ht_belt1", area="hotland", rect=(2800, 5240, 320, 800), title="Hotland", floor_colors=HT_STRIPE + HT_BELT),
    dict(name="ht_belt2", area="hotland", rect=(2800, 4380, 320, 860), title="Hotland", floor_colors=HT_STRIPE + HT_BELT,
         fix=[("walk", 2944, 4674, 52, 14)]),
    # the Magma Chamber's islands and steam vents; the burnt pan's room below it; the laser walkway north
    dict(name="ht_magma", area="hotland", rect=(2860, 3940, 860, 440), title="Hotland", floor_colors=HT_STRIPE,
         fix=[("walk", x - 24, y - 24, 68, 68) for x, y in HT_VENTS_MAGMA]),
    dict(name="ht_pan", area="hotland", rect=(3340, 4380, 640, 260), title="Hotland", floor_colors=HT_STRIPE + HT_BELT,
         fix=[("walk", 3600, 4460, 60, 60)]),
    dict(name="ht_lasers", area="hotland", rect=(3040, 3700, 1180, 240), title="Hotland", floor_colors=HT_STRIPE,
         doors=[((4206, 3800, 14, 54), "ht_hub", (4600, 3792), "right")]),
    # the puzzle hub: its turning vent, the shooting puzzles left and right, the door north they open
    dict(name="ht_hub", area="hotland", rect=(4220, 3250, 1220, 690), title="Hotland", floor_colors=HT_STRIPE + HT_HUB,
         fix=[("walk", x - 2, y - 2, 24, 24) for x, y in HT_VENTS_HUB] + [("walk", 4798, 3616, 72, 48), ("walk", 4804, 3396, 52, 86)],
         doors=[((4583, 3795, 8, 50), "ht_lasers", (4176, 3800), "left"),
                ((4800, 3400, 60, 8), "ht_ventisle", (4818, 3236), "up"),
                # (the puzzles' doorways, and back out of them)
                ((5257, 3596, 25, 12), "ht_hub", (5258, 3462), "up"), ((5250, 3500, 40, 4), "ht_hub", (5259, 3608), "down"),
                ((4387, 3596, 25, 12), "ht_hub", (4389, 3462), "up"), ((4380, 3500, 40, 4), "ht_hub", (4389, 3608), "down")]),
    # past the door: the vents over the lava, Mettaton's cooking show, the CORE far off and the elevator
    dict(name="ht_ventisle", area="hotland", rect=(4660, 3000, 640, 300), title="Hotland", floor_colors=HT_STRIPE,
         fix=[("walk", x - 24, y - 24, 68, 68) for x, y in HT_VENTS_ISLE],
         doors=[((4800, 3292, 60, 8), "ht_hub", (4818, 3488), "down")]),
    dict(name="ht_cooking", area="hotland", rect=(5300, 3000, 680, 260), title="Hotland", floor_colors=LAB_FLOOR + HT_KITCHEN),
    dict(name="ht_coreview", area="hotland", rect=(5980, 2900, 1080, 340), title="Hotland", floor_colors=LAB_FLOOR),
    # the second floor: out of the elevator, Sans's hot dog stand; the crossroads (the invisible path north to the Art
    # Club's room, the apron south); the conveyor belt puzzle; the vent puzzle; the Bad Opinion Zone; the bend where
    # the Royal Guards wait; Mettaton's news report; the CORE again, and the elevator up
    dict(name="ht_hotdog", area="hotland", rect=(6040, 2260, 1060, 220), title="Hotland"),
    dict(name="ht_cross2", area="hotland", rect=(5700, 2200, 340, 380), title="Hotland",
         fix=[("walk", 5898, 2276, 22, 116)]),
    dict(name="ht_artclub", area="hotland", rect=(5800, 1990, 240, 210), view=(5740, 2040, 320, 240), title="Hotland"),
    dict(name="ht_apron", area="hotland", rect=(5780, 2580, 260, 160), title="Hotland"),
    dict(name="ht_beltpuz", area="hotland", rect=(5060, 2280, 640, 240), title="Hotland", floor_colors=HT_BELT),
    dict(name="ht_ventpuz", area="hotland", rect=(4400, 2100, 660, 560), title="Hotland", floor_colors=HT_STRIPE + HT_BELT,
         fix=[("walk", x, y, 60, 60) for x, y in HT_VENTPUZ]),
    dict(name="ht_boz", area="hotland", rect=(3740, 2100, 660, 280), title="Hotland", floor_colors=HT_BELT),
    dict(name="ht_guards", area="hotland", rect=(3080, 1940, 660, 440), title="Hotland",
         doors=[((3690, 2036, 10, 40), "ht_news", (3772, 1748), "right")]),
    dict(name="ht_news", area="hotland", rect=(3760, 1540, 880, 500), title="Hotland", floor_colors=HT_STRIPE + HT_BELT,
         doors=[((3760, 1760, 6, 40), "ht_guards", (3670, 2030), "left")]),
    dict(name="ht_coreview2", area="hotland", rect=(3680, 1200, 960, 340), title="Hotland", floor_colors=HT_STRIPE),
    # the third floor: out of the elevator, the spider bake sale; the vent islands (both puzzles open the door east);
    # north to the Diamond Boys and a shooting puzzle; south over the belts, past the laser drones, to the other;
    # through the door, the spider entrance and Muffet's lair; Mettaton's musical, the tile puzzle below its stage;
    # the crossroads by the elevator R3, the MTT Resort north
    dict(name="ht_l3", area="hotland", rect=(3660, 740, 900, 240), view=(3660, 760, 900, 240), title="Hotland", floor_colors=HT_HUB,
         fix=[("walk", 4556, 876, 28, 28)]),
    dict(name="ht_hub3", area="hotland", rect=(4560, 620, 740, 500), title="Hotland", floor_colors=HT_HUB,
         fix=[("walk", x, y, 60, 60) for x, y in HT3_ISLES] + [("walk", x - 4, y - 4, 28, 28) for x, y in HT3_GRATES],
         doors=[((5205, 700, 50, 6), "ht_spiderent", (5220, 525), "up")]),
    dict(name="ht_north3", area="hotland", rect=(4700, 140, 300, 480), title="Hotland", floor_colors=HT_HUB,
         fix=[("walk", 4808, 236, 44, 108)]),
    dict(name="ht_belts3", area="hotland", rect=(4780, 1120, 680, 500), title="Hotland", floor_colors=HT_HUB + HT_BELT),
    dict(name="ht_south3", area="hotland", rect=(5460, 940, 200, 680), view=(5360, 940, 320, 680), title="Hotland", floor_colors=HT_HUB + HT_BELT,
         fix=[("walk", 5518, 1216, 44, 104)]),
    dict(name="ht_spiderent", area="hotland", rect=(5180, 400, 240, 170), view=(5120, 300, 410, 300), title="Hotland", floor_colors=HT_HUB,
         doors=[((5205, 560, 50, 6), "ht_hub3", (5220, 704), "down")]),
    dict(name="ht_lair", area="hotland", rect=(5420, 300, 1220, 240), title="Hotland", open=True,
         floor_rects=[(5420, 461, 1220, 38)]),
    dict(name="ht_stage", area="hotland", rect=(6640, 180, 680, 360), title="Hotland", floor_colors=HT_STAGE,
         fix=[("walk", 6640, 461, 140, 38)]),
    dict(name="ht_tiles", area="hotland", rect=(6340, 540, 1280, 260), title="Hotland",
         floor_colors=HT_FLOOR + HT_BELT + HT_HUB + HT_TILES, fix=[("walk", 7522, 540, 56, 84)]),
    dict(name="ht_cross3", area="hotland", rect=(7320, 280, 820, 260), title="Hotland",
         fix=[("walk", 7522, 290, 56, 124), ("walk", 7522, 470, 56, 70)],
         doors=[((7522, 292, 56, 10), "res_outside", (1210, 976), "up")]),
    # the MTT Resort (its own map): the street outside (Sans by the door), the lobby (the fountain, the Burger
    # Emporium, the elevator, the CORE's door north), the restaurant west, the hotel's hallway east
    dict(name="res_outside", area="resort", rect=(960, 610, 520, 410), title="MTT Resort", floor_colors=RES_STREET,
         # (the building is drawn in the street's colours: the plaza, the door's carpet, the alley up the right)
         floor_rects=[(990, 842, 470, 158), (1190, 1000, 70, 20), (1188, 826, 64, 20), (1384, 648, 34, 200)],
         fix=[("wall", 1084, 932, 112, 22), ("wall", 1244, 932, 112, 22)],   # the velvet ropes
         doors=[((1190, 832, 60, 6), "res_lobby", (1205, 568), "up"), ((1197, 1012, 56, 8), "ht_cross3", (7536, 300), "down")]),
    dict(name="res_lobby", area="resort", rect=(1060, 60, 420, 550), view=(1060, 60, 420, 560), title="MTT Resort",
         floor_rects=[(1080, 278, 380, 162), (1080, 440, 280, 98), (1060, 380, 20, 60), (1460, 380, 20, 60),
                      (1200, 136, 40, 142), (1200, 538, 40, 72)],
         doors=[((1195, 600, 50, 6), "res_outside", (1205, 840), "down"), ((1060, 380, 6, 60), "res_diner", (1030, 396), "left"),
                ((1474, 380, 6, 60), "res_hall", (1488, 396), "right"), ((1200, 132, 40, 8), "core_approach", (310, 2700), "up")]),
    dict(name="res_diner", area="resort", rect=(60, 220, 1000, 300), title="MTT Resort", floor_colors=RES_DINER + RES_FLOOR,
         floor_rects=[(82, 342, 698, 156), (780, 382, 280, 56)],
         doors=[((1054, 380, 6, 60), "res_lobby", (1068, 396), "right")]),
    dict(name="res_hall", area="resort", rect=(1480, 300, 540, 160), title="MTT Resort", floor_rects=[(1480, 380, 520, 60)],
         doors=[((1480, 380, 6, 60), "res_lobby", (1450, 396), "left")]),
    # the hotel's room (door 2), reached from the hall by its door
    dict(name="res_room", area="resort", rect=(1640, 0, 200, 250), title="MTT Resort",
         floor_rects=[(1644, 84, 192, 116), (1720, 200, 40, 50)],
         doors=[((1720, 232, 40, 8), "res_hall", (1735, 364), "down")]),
    # the CORE: its approach up from the Resort, the lobby (its elevator out of order) and the two rooms north of it
    dict(name="core_approach", area="core", rect=(160, 2200, 330, 560), title="CORE",
         floor_rects=[(300, 2290, 40, 470), (270, 2690, 100, 70)],
         fix=[("walk", 300, 2290, 40, 170)],   # (the way in, lit blue)
         doors=[((300, 2752, 40, 8), "res_lobby", (1210, 150), "down"), ((304, 2292, 32, 8), "core_lobby", (310, 2090), "up")]),
    dict(name="core_lobby", area="core", rect=(160, 1930, 330, 210), title="CORE",
         floor_rects=[(177, 2040, 305, 75), (177, 1977, 63, 63), (402, 1977, 59, 63), (298, 2115, 42, 25)],
         doors=[((300, 2134, 40, 6), "core_approach", (310, 2302), "down"), ((180, 1972, 40, 6), "core_doorway", (150, 1860), "up"),
                ((420, 1972, 44, 6), "core_pit", (470, 1860), "up")]),
    dict(name="core_doorway", area="core", rect=(95, 1490, 130, 410), view=(95, 1480, 130, 420), title="CORE",
         floor_rects=[(100, 1582, 120, 238), (140, 1819, 40, 80), (142, 1492, 38, 92)],
         fix=[("walk", 142, 1492, 38, 92)],   # (its doorway, north)
         doors=[((140, 1892, 40, 8), "core_lobby", (190, 1985), "down"), ((142, 1492, 38, 8), "core_west", (150, 1460), "up")]),
    dict(name="core_pit", area="core", rect=(430, 1680, 100, 220), title="CORE",
         floor_rects=[(440, 1761, 79, 75), (461, 1836, 37, 62)],
         doors=[((461, 1892, 37, 8), "core_lobby", (432, 1985), "down")]),
    # the laser ambush (its switch), the crossroads and the room north of it
    dict(name="core_west", area="core", rect=(100, 990, 1140, 540), title="CORE",
         floor_rects=[(119, 1339, 159, 147), (125, 1486, 77, 34), (278, 1356, 265, 32), (543, 1339, 694, 66),
                      (796, 1236, 65, 105), (819, 1196, 53, 42), (766, 1068, 147, 130)],
         doors=[((125, 1512, 77, 8), "core_doorway", (150, 1505), "down"), ((1228, 1360, 12, 40), "core_bridge", (1236, 1362), "right")]),
    # the laser bridge (the power cut), up to the Core Branch
    dict(name="core_bridge", area="core", rect=(1220, 1140, 920, 340), title="CORE",
         floor_rects=[(1220, 1358, 845, 44), (2062, 1145, 58, 270)],
         doors=[((1220, 1360, 6, 40), "core_west", (1200, 1362), "left"), ((2062, 1145, 58, 8), "core_branch", (2080, 1098), "up")]),
    # the Core Branch: three columns, three bridges; west the sage's path (its puzzle), east the force field to The End
    dict(name="core_branch", area="core", rect=(1390, 280, 1220, 870), title="Core Branch",
         floor_rects=[(1740, 357, 60, 785), (2060, 282, 58, 860), (2379, 357, 60, 785), (1710, 357, 120, 83),
                      (2340, 357, 120, 83), (1740, 620, 700, 42), (1400, 862, 1210, 38), (1620, 1102, 820, 40)],
         doors=[((2060, 1142, 58, 8), "core_bridge", (2080, 1160), "down"), ((2060, 282, 58, 8), "core_warrior", (2080, 255), "up"),
                ((2602, 862, 8, 38), "core_end", (2670, 860), "right")]),
    # the warrior's path: its three fights, its switch
    dict(name="core_warrior", area="core", rect=(2050, 60, 700, 240), title="CORE",
         floor_rects=[(2060, 160, 100, 60), (2060, 218, 60, 72), (2160, 160, 460, 40), (2620, 160, 80, 60)],
         doors=[((2060, 290, 60, 8), "core_branch", (2080, 300), "down")]),
    # The End: the bridge, the Core End (a SAVE, the elevator to the lobby, the door to Mettaton)
    dict(name="core_end", area="core", rect=(2650, 750, 880, 260), title="Core End",
         floor_rects=[(2660, 852, 600, 46), (3260, 852, 240, 66)],
         doors=[((2660, 852, 6, 46), "core_branch", (2580, 862), "left"), ((3342, 856, 36, 8), "core_mtt", (3348, 740), "up")]),
    dict(name="core_mtt", area="core", rect=(3250, 230, 280, 550), title="CORE", open=True,
         floor_rects=[(3342, 330, 36, 450), (3282, 460, 156, 120), (3302, 440, 116, 160), (3322, 420, 76, 200)],
         doors=[((3342, 772, 36, 8), "core_end", (3348, 862), "down"), ((3342, 330, 36, 8), "core_hall", (3355, 212), "up")]),
    dict(name="core_hall", area="core", rect=(3320, 60, 1100, 190), title="CORE",
         floor_rects=[(3340, 118, 1022, 42), (3340, 158, 60, 92)],
         doors=[((3340, 242, 60, 8), "core_mtt", (3348, 345), "down")]),
    # New Home: the elevator up from the CORE, the city's street and the way to Asgore's home, its front yard
    dict(name="nh_town", area="newhome", rect=(0, 2930, 1900, 1710), title="New Home", open=True,
         floor_rects=[(40, 4536, 430, 44), (310, 4310, 100, 230), (250, 4310, 1450, 60), (1661, 3410, 34, 960),
                      (1517, 3122, 311, 300)],
         doors=[((1663, 3118, 34, 8), "nh_house", (1670, 2900), "up")]),
    # Asgore's home: the hall (its chained stairs), the living room, the kitchen, the hallway and its two bedrooms
    dict(name="nh_house", area="newhome", rect=(1190, 2440, 1460, 500), title="Asgore's Home", open=True,
         floor_rects=[(1522, 2845, 316, 95), (1197, 2712, 325, 228), (1197, 2560, 200, 140), (1238, 2690, 42, 25),
                      (1838, 2840, 734, 80), (1890, 2575, 200, 120), (2280, 2545, 220, 150)],
         fix=[("wall", 1248, 2830, 90, 40), ("wall", 1290, 2735, 80, 45)],   # the dining table, the fireplace
         doors=[((1663, 2934, 34, 6), "nh_town", (1670, 3125), "down"),
                ((1978, 2836, 26, 6), "nh_house", (2040, 2658), "up"), ((2030, 2690, 42, 6), "nh_house", (1981, 2846), "down"),
                ((2402, 2836, 26, 6), "nh_house", (2410, 2658), "up"), ((2397, 2690, 50, 6), "nh_house", (2405, 2846), "down")]),
    # the basement: two long corridors (the story told on the way), the elevator back up at the end
    dict(name="nh_basement", area="newhome", rect=(1520, 2040, 3460, 420), title="New Home", open=True,
         floor_rects=[(1533, 2293, 1100, 94), (2517, 2143, 216, 250), (2633, 2143, 2200, 67), (4683, 2143, 117, 80)],
         doors=[((4683, 2216, 117, 8), "nh_last", (4720, 2240), "down")]),
    dict(name="nh_last", area="newhome", rect=(4580, 2210, 1400, 250), title="Last Corridor", open=True,
         floor_rects=[(4585, 2335, 1300, 75), (4683, 2222, 117, 113), (5797, 2222, 88, 113)],
         doors=[((4683, 2216, 117, 8), "nh_basement", (4720, 2180), "up"), ((5797, 2216, 88, 8), "nh_thall", (5790, 2180), "up")]),
    # the throne room's entrance (its SAVE), the throne room, the stairs down to the coffins
    dict(name="nh_thall", area="newhome", rect=(5640, 1700, 830, 530), view=(5640, 1680, 830, 550), title="Throne Entrance", open=True,
         floor_rects=[(5760, 1797, 588, 75), (5760, 1872, 88, 350), (6260, 1872, 88, 350), (6005, 1785, 50, 12)],
         doors=[((5760, 2216, 88, 8), "nh_last", (5820, 2240), "down"), ((6005, 1786, 50, 8), "nh_throne", (6010, 1600), "up"),
                ((6260, 2216, 88, 8), "nh_coffins", (6300, 2240), "down")]),
    dict(name="nh_throne", area="newhome", rect=(5880, 1080, 260, 600), title="Throne Room", open=True,
         floor_rects=[(5903, 1238, 215, 381), (5963, 1110, 119, 128), (5940, 1096, 56, 20), (5998, 1619, 48, 60)],
         doors=[((5998, 1662, 48, 8), "nh_thall", (6015, 1805), "down"), ((5940, 1096, 56, 8), "nh_barrier", (5955, 1040), "up")]),
    dict(name="nh_coffins", area="newhome", rect=(6210, 2215, 500, 800), title="New Home", open=True,
         floor_rects=[(6280, 2222, 60, 610), (6230, 2830, 460, 115)],
         doors=[((6280, 2222, 60, 8), "nh_thall", (6290, 2180), "up")]),
    # the room before the barrier ("The End"), and the barrier
    dict(name="nh_barrier", area="newhome", rect=(5820, 850, 700, 240), view=(5820, 840, 700, 240), title="The End", open=True,
         floor_rects=[(5839, 977, 640, 44), (5839, 880, 262, 116), (5939, 1021, 62, 56), (6400, 960, 56, 20)],
         doors=[((5939, 1070, 62, 8), "nh_throne", (5955, 1120), "down"), ((6400, 960, 56, 8), "nh_wall", (6330, 780), "up")]),
    dict(name="nh_wall", area="newhome", rect=(6170, 560, 350, 270), title="The Barrier", open=True,
         floor_rects=[(6190, 600, 320, 215)],
         doors=[((6300, 810, 60, 8), "nh_barrier", (6415, 985), "down")]),
]

# rooms one above the other through a dark doorway (tools/pack.py: apply_links); ("h", left, right, y, h): side
# by side with a gap, a door each way along the rows y .. y + h
LINKS = [
    ("h", "wf_hist", "wf_chase1", 540, 60),
    ("h", "wf_mushroom", "wf_lantern", 2372, 26), ("h", "wf_lantern", "wf_deadend", 2682, 16),
    ("v", "wf_wish", "wf_hist", 3112, 36, 818),
    ("h", "snowdin1", "snowdin2", 404, 52),
    ("h", "snowdin2", "snowdin3", 404, 52),
    ("h", "snowdin3", "snowdin4", 404, 52),
    ("h", "snowdin4", "snowdin5", 404, 52),
    ("v", "ruins1", "ruins2", 506, 28, 5175),
    ("v", "ruins4", "ruins5", 1244, 32, 4640),
    ("v", "ruins7", "ruins7a", 4946, 28, 4400),
    ("v", "ruins13", "ruins15", 7580, 34, 4740),
    ("v", "ruins18", "ruins21", 9006, 30, 3500),
    ("v", "ruins19", "ruins20", 9468, 24, 3670),
    ("v", "ruins21", "torhouse1", 9006, 26, 3070),
    ("v", "ruins17c", "ruins17d", 8444, 32, 4080),
    # the chutes up from the rooms under the cracked floors
    ("up", "ruins8b", 5190, 30, 5070, "ruins8", (5167, 4684)),
    ("up", "ruins8b", 5350, 30, 5070, "ruins8", (5407, 4684)),
    ("up", "ruins10b", 6066, 30, 5070, "ruins10", (6066, 4690)),
    ("up", "ruins16a", 8263, 12, 4945, "ruins16", (8200, 4546)),
    ("up", "ruins16b", 8503, 12, 4945, "ruins16", (8440, 4546)),
    ("up", "ruins16c", 8643, 12, 4945, "ruins16", (8580, 4546)),
    ("up", "ruins16d", 8263, 12, 5065, "ruins16", (8200, 4546)),
    ("up", "ruins16e", 8503, 12, 5065, "ruins16", (8440, 4546)),
    ("up", "ruins16f", 8643, 12, 5065, "ruins16", (8580, 4546)),
    ("up", "ruins17x", 9268, 26, 4510, "ruins17a", (8790, 4606)),
]
