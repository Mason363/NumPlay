"""The game's path pieces: the prefabs its PathElement lays (GetPrefabNameForType, and AddObstacle's trees), in the
order of the port's mesh table (data.h: M_<prefab>) and floor table (floor.c). Pieces are laid as they are,
unturned: "Vert" ones run along z (north +z), "Horz" ones along x (east +x)."""

TEMPLE = (["templeStraightVert", "templeStraightVert2", "templeStraightVert3", "templeStraightVert4", "templeStraightVert5",
           "templeStraightTowerVert", "templeStraightHorz", "templeStraightHorz2", "templeStraightHorz3",
           "templeStraightHorz4", "templeStraightHorz5", "templeStraightTowerHorz"]
          + [f"templeTurn{c}" for c in "ABCD"] + [f"templeT{s}" for s in ("Left", "Right", "Up", "Down")]
          + ["templeStraightHoleVert", "templeStraightHoleLongAVert", "templeStraightHoleLongBVert", "templeStraightHoleHorz",
             "templeStraightHoleLongAHorz", "templeStraightHoleLongBHorz",
             "templeStraightNarrowSmallLeftVert", "templeStraightNarrowSmallRightVert", "templeStraightNarrowSmallDownHorz"]
          + [f"templeStraightNarrowLong{s}{ab}Vert" for s in ("Left", "Right") for ab in ("", "A", "B")]
          + [f"templeStraightNarrowLong{s}{ab}Horz" for s in ("Up", "Down") for ab in ("", "A", "B")]
          + ["templeStraightFlameTowerHighVert", "templeStraightFlameTowerHighHorz"]
          + [f"templeToPlankCap{ab}{o}" for ab in "AB" for o in ("Vert", "Horz")]
          + ["templeStraightTreeTripLeftVert", "templeStraightTreeTripRightVert", "templeStraightTreeTripUpHorz",
             "templeStraightTreeTripDownHorz", "templeStraightTreeJumpLeftVert", "templeStraightTreeJumpRightVert",
             "templeStraightTreeJumpUpHorz", "templeStraightTreeJumpDownHorz", "templeStraightTreeSlideVert",
             "templeStraightTreeSlideHorz", "temple"])
PLANK = (["plankStraightVert", "plankStraightHorz"] + [f"plankTurn{c}" for c in "ABCD"]
         + [f"plankT{s}" for s in ("Left", "Right", "Up", "Down")]
         + ["plankStraightHoleVert", "plankStraightHoleHorz", "plankStraightTowerVert", "plankStraightTowerHorz"])
CLIFF = (["cliffStraightVert", "cliffStraightHorz"] + [f"cliffTurn{c}" for c in "ABCD"]
         + [f"cliffT{s}" for s in ("Left", "Right", "Up", "Down")]
         + [f"cliffToTempleCap{ab}{o}" for ab in "AB" for o in ("Vert", "Horz")]
         + ["cliffStraightTreeVert", "cliffStraightTreeHorz"])
PREFABS = TEMPLE + PLANK + CLIFF
FLOOR_PREFABS = PREFABS
