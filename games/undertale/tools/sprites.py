"""The sprites and fonts the pack holds.

A sprite: (name, source or [sources], options). Sources (tools/pack.py load_frames):
  demo:spr_x     the free demo's sprite spr_x, all its frames (the game's size)
  demobg:bg_x    one of the demo's backgrounds
  wiki:File.png  a file of the Undertale Wiki, every frame of a GIF; the wiki shows sprites at twice the size,
                 halved here (wiki1: as it is)
Options: origin (x, y) in the source's pixels, center (its middle), feet (the middle of its bottom),
frames (which of them), keep (no crop to the opaque pixels), every (one frame in n), clear (colours made
see-through), only (all colours but these made see-through), mirror (flipped left to right), box (x, y, w, h: a
part of the source, after halving).
More sources: move:File.gif:x0,y0,x1,y1:frames (what moves in a wiki screenshot GIF), map:Map.png:x,y,w,h (a piece
of an area's map), gen:name (drawn here, GEN)."""
from PIL import Image, ImageDraw


def _img(w, h):
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))


def gen_otile():
    # the XOXO puzzles' O (red), and once the puzzle's solved, the same in green
    out = []
    for c in ((255, 0, 0, 255), (0, 192, 0, 255)):
        im = _img(20, 20)
        d = ImageDraw.Draw(im)
        d.ellipse([3, 3, 16, 16], outline=c, width=2)
        out.append(im)
    return out


def gen_snowball():
    # the Ball Game's snowball, from big to small (it melts as it rolls)
    out = []
    for r in (10, 9, 8, 7, 6, 5, 4, 3):
        im = _img(22, 22)
        d = ImageDraw.Draw(im)
        d.ellipse([11 - r, 11 - r, 11 + r, 11 + r], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))
        d.arc([11 - r + 2, 11 - r + 2, 11 + r - 2, 11 + r - 2], 200, 300, fill=(180, 192, 220, 255))
        out.append(im)
    return out


def gen_bone():
    # a bone 6 pixels wide, its ends' knobs (drawn stretched to any length: gfx_bone)
    im = _img(6, 6)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 5, 5], fill=(255, 255, 255, 255))
    return [im]


def gen_orb():
    im = _img(10, 10)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, 9, 9], fill=(200, 220, 255, 255), outline=(255, 255, 255, 255))
    return [im]


def gen_snowflake():
    im = _img(9, 9)
    d = ImageDraw.Draw(im)
    for a, b in (((4, 0), (4, 8)), ((0, 4), (8, 4)), ((1, 1), (7, 7)), ((1, 7), (7, 1))):
        d.line([a, b], fill=(255, 255, 255, 255))
    return [im]


def gen_iceshard():
    im = _img(7, 11)
    d = ImageDraw.Draw(im)
    d.polygon([(3, 0), (6, 4), (3, 10), (0, 4)], fill=(255, 255, 255, 255))
    return [im]


def gen_present():
    im = _img(10, 10)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 2, 9, 9], fill=(255, 255, 255, 255))
    d.rectangle([4, 2, 5, 9], fill=(0, 0, 0, 255))
    d.rectangle([0, 5, 9, 5], fill=(0, 0, 0, 255))
    d.line([(2, 0), (4, 2)], fill=(255, 255, 255, 255))
    d.line([(7, 0), (5, 2)], fill=(255, 255, 255, 255))
    return [im]


def gen_knife():
    # Doggo's dog-knife: a blade, up
    im = _img(5, 30)
    d = ImageDraw.Draw(im)
    d.polygon([(2, 0), (4, 6), (4, 24), (0, 24), (0, 6)], fill=(255, 255, 255, 255))
    d.rectangle([0, 24, 4, 29], fill=(255, 255, 255, 255))
    return [im]


def gen_dogpaw():
    im = _img(9, 9)
    d = ImageDraw.Draw(im)
    d.ellipse([2, 4, 7, 8], fill=(255, 255, 255, 255))
    for x in (1, 4, 7):
        d.ellipse([x - 1, 0, x + 1, 2], fill=(255, 255, 255, 255))
    return [im]


def gen_heartbullet():
    im = _img(9, 8)
    d = ImageDraw.Draw(im)
    d.polygon([(0, 2), (2, 0), (4, 2), (6, 0), (8, 2), (4, 7)], fill=(255, 255, 255, 255))
    return [im]


def gen_spear():
    # Undyne's spear: a shaft, a head on top
    im = _img(7, 40)
    d = ImageDraw.Draw(im)
    d.polygon([(3, 0), (6, 8), (4, 8), (4, 39), (2, 39), (2, 8), (0, 8)], fill=(255, 255, 255, 255))
    return [im]


def gen_arrow():
    # Undyne's spear in battle, pointing right (turned as it flies)
    im = _img(18, 7)
    d = ImageDraw.Draw(im)
    d.polygon([(17, 3), (11, 0), (11, 2), (0, 2), (0, 4), (11, 4), (11, 6)], fill=(255, 255, 255, 255))
    return [im]


def gen_plane():
    # Tsunderplane's little planes, pointing right
    im = _img(16, 9)
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    d.polygon([(15, 4), (12, 2), (1, 2), (0, 4), (1, 6), (12, 6)], fill=w)
    d.polygon([(6, 2), (9, 2), (5, 0), (3, 0)], fill=w)
    d.polygon([(6, 6), (9, 6), (5, 8), (3, 8)], fill=w)
    d.polygon([(1, 2), (3, 2), (1, 0), (0, 0)], fill=w)
    return [im]


def gen_bomb():
    # a round bomb with its fuse (Tsunderplane's, Pyrope's)
    im = _img(9, 10)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 1, 8, 9], fill=(255, 255, 255, 255))
    d.line([(6, 1), (8, 0)], fill=(255, 255, 255, 255))
    return [im]


def gen_cloud():
    # Vulkin's thundercloud
    im = _img(26, 13)
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    for box in ([0, 4, 10, 12], [6, 0, 17, 10], [14, 3, 25, 12], [4, 6, 22, 12]):
        d.ellipse(box, fill=w)
    return [im]


def gen_bolt():
    # a bolt of Vulkin's thunder, falling
    im = _img(5, 9)
    d = ImageDraw.Draw(im)
    d.line([(3, 0), (1, 4), (3, 4), (1, 8)], fill=(255, 255, 255, 255), width=1)
    return [im]


def gen_note():
    im = _img(9, 11)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 6, 5, 10], fill=(255, 255, 255, 255))
    d.rectangle([4, 0, 5, 8], fill=(255, 255, 255, 255))
    d.line([(5, 0), (8, 3)], fill=(255, 255, 255, 255))
    return [im]


def gen_drop():
    im = _img(7, 10)
    d = ImageDraw.Draw(im)
    d.polygon([(3, 0), (6, 6), (5, 9), (1, 9), (0, 6)], fill=(255, 255, 255, 255))
    return [im]


def gen_arm():
    # a flexing arm: the upper arm along, the forearm up, a fist
    im = _img(16, 14)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 9, 9, 13], fill=(255, 255, 255, 255))
    d.ellipse([3, 6, 9, 12], fill=(255, 255, 255, 255))
    d.rectangle([9, 3, 13, 13], fill=(255, 255, 255, 255))
    d.ellipse([9, 0, 15, 5], fill=(255, 255, 255, 255))
    return [im]


def gen_debris():
    # a rock riding the stream
    im = _img(16, 12)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 1, 15, 11], fill=(90, 84, 128, 255), outline=(20, 16, 40, 255))
    d.line([(4, 4), (8, 3)], fill=(150, 140, 190, 255))
    return [im]


def gen_croissant():
    # Muffet's croissants: a crescent, its horns to the left
    im = _img(14, 11)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, 13, 10], fill=(255, 255, 255, 255))
    d.ellipse([-5, 2, 7, 8], fill=(0, 0, 0, 0))
    return [im]


def gen_donut():
    im = _img(11, 11)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, 10, 10], fill=(255, 255, 255, 255))
    d.ellipse([3, 3, 7, 7], fill=(0, 0, 0, 0))
    return [im]


def gen_mttmini():
    # a small Mettaton for the yellow SOUL to shoot at: his box, its screen's grid, his wheel
    im = _img(16, 22)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 15, 15], fill=(170, 170, 170, 255), outline=(255, 255, 255, 255))
    d.rectangle([3, 3, 12, 10], fill=(255, 220, 40, 255))
    for k in range(4, 12, 2):
        d.line([(k, 3), (k, 10)], fill=(40, 40, 40, 255))
    d.line([(3, 7), (12, 7)], fill=(40, 40, 40, 255))
    d.rectangle([7, 16, 8, 18], fill=(200, 200, 200, 255))
    d.ellipse([4, 17, 11, 21], fill=(200, 30, 30, 255))
    return [im]


def gen_star():
    # the Royal Guards' stars: four points
    im = _img(9, 9)
    d = ImageDraw.Draw(im)
    d.polygon([(4, 0), (5, 3), (8, 4), (5, 5), (4, 8), (3, 5), (0, 4), (3, 3)], fill=(255, 255, 255, 255))
    return [im]


def gen_exblock():
    # Mettaton EX's blocks: a white square, a black ring in it (only the bomb beside it breaks it)
    im = _img(14, 14)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 13, 13], fill=(255, 255, 255, 255))
    d.ellipse([3, 3, 10, 10], fill=(0, 0, 0, 255))
    d.ellipse([5, 5, 8, 8], fill=(255, 255, 255, 255))
    return [im]


def gen_exleg():
    # one of his legs, kicking in from the side: black, long, a pink boot
    im = _img(9, 60)
    d = ImageDraw.Draw(im)
    d.rectangle([1, 0, 7, 46], fill=(40, 40, 40, 255), outline=(255, 255, 255, 255))
    d.polygon([(0, 46), (8, 46), (8, 56), (4, 59), (0, 59)], fill=(255, 90, 170, 255))
    return [im]


def gen_mttheart():
    # his heart, out of the box on his waist: pink, a white rim
    im = _img(16, 14)
    d = ImageDraw.Draw(im)
    d.polygon([(0, 3), (3, 0), (8, 3), (12, 0), (15, 3), (15, 6), (8, 13), (0, 6)], fill=(255, 255, 255, 255))
    d.polygon([(2, 3), (4, 2), (8, 5), (11, 2), (13, 3), (13, 6), (8, 11), (2, 6)], fill=(255, 60, 140, 255))
    return [im]


def gen_disco():
    # the disco ball: a grey ball of little squares
    im = _img(16, 16)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, 15, 15], fill=(190, 190, 200, 255))
    for k in range(3, 15, 4):
        d.line([(k, 1), (k, 14)], fill=(110, 110, 130, 255))
        d.line([(1, k), (14, k)], fill=(110, 110, 130, 255))
    return [im]


def gen_exarm():
    # a piece of his long arms, coming down: a segment, a box on it to shoot
    im = _img(10, 12)
    d = ImageDraw.Draw(im)
    d.rectangle([3, 0, 6, 11], fill=(255, 255, 255, 255))
    d.rectangle([0, 3, 9, 8], fill=(255, 190, 40, 255), outline=(255, 255, 255, 255))
    return [im]


def gen_xshot():
    # Omega Flowey's eyes' bullets: an X
    im = _img(7, 7)
    d = ImageDraw.Draw(im)
    d.line([(0, 0), (6, 6)], fill=(255, 255, 255, 255), width=2)
    d.line([(6, 0), (0, 6)], fill=(255, 255, 255, 255), width=2)
    return [im]


def gen_pan():
    # the green SOUL's frying pan: a disc and its handle
    im = _img(20, 8)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, 12, 7], fill=(255, 255, 255, 255))
    d.rectangle([12, 3, 19, 4], fill=(255, 255, 255, 255))
    return [im]


def gen_shoe():
    # the blue SOUL's ballet shoe
    im = _img(12, 7)
    d = ImageDraw.Draw(im)
    d.polygon([(0, 2), (8, 1), (11, 3), (11, 6), (1, 6)], fill=(255, 255, 255, 255))
    d.line([(3, 0), (6, 3)], fill=(255, 255, 255, 255))
    return [im]


def gen_book():
    # the purple SOUL's book
    im = _img(10, 12)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 9, 11], fill=(255, 255, 255, 255))
    d.line([(2, 0), (2, 11)], fill=(0, 0, 0, 255))
    return [im]


def gen_actbutton():
    # the ACT button, coming down among a SOUL's attack (touch it to call for help)
    im = _img(26, 11)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 25, 10], outline=(255, 160, 0, 255))
    for x, ys in ((3, (2, 8)), (9, (2, 8)), (15, (2, 8)), (21, (2, 8))):
        d.line([(x, ys[0]), (x + 2, ys[1])], fill=(255, 160, 0, 255))
    return [im]


GEN = {"otile": gen_otile, "snowball": gen_snowball, "bone": gen_bone, "orb": gen_orb, "snowflake": gen_snowflake,
       "iceshard": gen_iceshard, "present": gen_present, "knife": gen_knife, "dogpaw": gen_dogpaw,
       "heartbullet": gen_heartbullet, "spear": gen_spear, "note": gen_note, "drop": gen_drop, "arm": gen_arm,
       "debris": gen_debris, "arrow": gen_arrow, "plane": gen_plane, "bomb": gen_bomb, "cloud": gen_cloud,
       "bolt": gen_bolt, "star": gen_star, "croissant": gen_croissant, "donut": gen_donut,
       "mttmini": gen_mttmini, "exblock": gen_exblock, "exleg": gen_exleg, "mttheart": gen_mttheart,
       "disco": gen_disco, "exarm": gen_exarm, "xshot": gen_xshot, "pan": gen_pan, "shoe": gen_shoe, "book": gen_book,
       "actbutton": gen_actbutton}

SNOW_CLEAR = [(133, 163, 216), (114, 129, 177), (139, 163, 186), (180, 192, 201), (195, 207, 216)]
PAP_GIF = "move:Papyrus_screenshot_back_and_forth.gif:0,280,640,450:"

SPRITES = [
    # Frisk walking: down and up 4 frames, left and right 2 (the game's own frames, from the demo)
    ("frisk_d", "demo:spr_maincharad", {"origin": (0, 0), "keep": True}),
    ("frisk_u", "demo:spr_maincharau", {"origin": (0, 0), "keep": True}),
    ("frisk_l", "demo:spr_maincharal", {"origin": (0, 0), "keep": True}),
    ("frisk_r", "demo:spr_maincharar", {"origin": (0, 0), "keep": True}),
    ("toriel_d", "demo:spr_toriel_d", {"origin": (0, 0), "keep": True}),
    ("toriel_u", "demo:spr_toriel_u", {"origin": (0, 0), "keep": True}),
    ("toriel_l", "demo:spr_toriel_l", {"origin": (0, 0), "keep": True}),
    ("toriel_r", "demo:spr_toriel_r", {"origin": (0, 0), "keep": True}),
    ("savepoint", "demo:spr_savepoint", {"origin": (0, 0), "keep": True}),
    ("flowey", "demo:spr_flowey", {"origin": (0, 0), "keep": True}),
    # the SOUL, 8 x 8 as the game's 16 x 16 halved; frame 1: blue
    ("heart", "demo2:spr_heart", {"center": True, "keep": True,
                                  "tints": [(0, 60, 255), (0, 192, 0), (213, 53, 217), (255, 255, 0)]}),   # red, blue, green, purple, yellow
] + [
    # the battle: its buttons (orange, then yellow when chosen), the FIGHT target and its bar, the slash, the HP label
    (n, "demo2:spr_" + n, {"keep": True}) for n in [
        "fightbt", "talkbt", "itembt", "sparebt", "target", "targetchoice", "hpname", "battlebg"]
] + [
    ("strike", "demo2:spr_strike", {"center": True, "keep": True}),
    ("heartbreak", "demo2:spr_heartbreak", {"center": True, "keep": True}),
    ("heartshards", "demo2:spr_heartshards", {"center": True, "keep": True}),
    ("heartgtfo", "demo2:spr_heartgtfo", {"center": True, "keep": True}),
] + [
    # bullets, their middle at their origin
    (n, "demo2:spr_" + n, {"center": True}) for n in [
        "flybullet", "smallfrogbullet", "butterflybllt", "bulletmd", "bulletsm", "circlebulletmd1", "circlebulletsm",
        "vegbullet", "carrotbullet", "roachbullet", "roachdance", "teardrop", "firebullet", "handbullet", "spinbullet",
        "streambullet", "dirbullet", "bulletgenmd"]
] + [
    # the monsters, from the wiki (the game's battle sprites, halved); their origin at their feet
    ("dummy_b", "wiki:Dummy_battle.png", {"feet": True}),
    ("froggit_b", "wiki:Froggit_battle_idle.gif", {"feet": True, "dedupe": True, "every": 2}),
    ("froggit_hurt", "wiki:Froggit_battle_hurt.png", {"feet": True}),
    ("whimsun_b", "wiki:Whimsun_battle_idle.gif", {"feet": True, "dedupe": True, "every": 4}),
    ("whimsun_hurt", "wiki:Whimsun_battle_hurt.png", {"feet": True}),
    ("moldsmal_b", "wiki:Moldsmal_battle.gif", {"feet": True, "dedupe": True, "every": 4}),
    ("loox_b", "wiki:Loox_battle_idle.gif", {"feet": True, "dedupe": True}),
    ("loox_hurt", "wiki:Loox_battle_hurt.png", {"feet": True}),
    ("vegetoid_b", "wiki:Vegetoid_battle_idle.gif", {"feet": True, "dedupe": True}),
    ("vegetoid_hurt", "wiki:Vegetoid_battle_hurt.png", {"feet": True}),
    ("migosp_b", "wiki:Migosp_battle_idle.gif", {"feet": True, "dedupe": True, "every": 2}),
    ("migosp_hurt", "wiki:Migosp_battle_hurt.png", {"feet": True}),
    ("napstablook_b", "wiki:Napstablook_battle_idle.gif", {"feet": True, "dedupe": True}),
    ("napsta_hat", "demo:spr_blookhat", {"feet": True}),
    ("toriel_b", "wiki:Toriel_battle.png", {"feet": True}),
    ("toriel_hurt", "wiki:Toriel_battle_hurt.png", {"feet": True}),
    ("toriel_away", "wiki:Toriel_battle_shocked.png", {"feet": True}),
    ("toriel_fight", "wiki:Toriel_battle_in_fight.png", {"feet": True}),
    ("toriel_dead", "wiki:Toriel_battle_fatal_Neutral.png", {"feet": True}),
] + [
    # the Ruins' things, from the demo (their top-left at their origin, as the game places them)
    (n.replace("spr_", "o_"), "demo:" + n, {"keep": True}) for n in [
        "spr_ruinsdoor1", "spr_switch", "spr_groundswitch1", "spr_spiketile", "spr_rock", "spr_colorswitch",
        "spr_faceswitch", "spr_dummy", "spr_exc", "spr_floweygrow", "spr_floweyshrink", "spr_floweysink",
        "spr_floweylaughoverworld", "spr_toriel_handhold_d", "spr_toriel_handhold_u", "spr_toriel_handhold_l",
        "spr_toriel_handhold_r", "spr_hole", "spr_smallweb", "spr_bigweb", "spr_napstablook_gr", "spr_napstablook_grsm",
        "spr_napstablook_l", "spr_chairempty", "spr_chairiel", "spr_chairielgetup", "spr_candydish", "spr_candydish2",
        "spr_candydish_bad", "spr_cheesetable", "spr_mousehole", "spr_npc_d", "spr_npc_u", "spr_npc_l", "spr_npc_r",
        "spr_pillar", "spr_pieslice", "spr_bigpie", "spr_vegetableoutside", "spr_smallfrog", "spr_ribbon",
        "spr_groundknife", "spr_torielflame", "spr_winkstar", "spr_tornote", "spr_diningtable", "spr_nastytree",
        "spr_toriel_d_phone", "spr_toriel_dt", "spr_toriel_ut", "spr_toriel_lt", "spr_toriel_rt", "spr_toriel_hug",
        "spr_toriel_ruffle"]
] + [
    ("frog_ow", "wiki:Froggit_overworld.png", {"keep": True}),
    ("napsta_ow", "wiki:Napstablook_overworld.png", {"keep": True}),
] + [
    # ---- Snowdin
    # Papyrus walking (from a screenshot of him pacing at home: right; left is the same, mirrored), and facing you
    ("papyrus_r", PAP_GIF + "19,21,23,25", {"feet": True}),
    ("papyrus_l", PAP_GIF + "19,21,23,25", {"feet": True, "mirror": True}),
    ("papyrus_d", "wiki:Papyrus_overworld.png", {"feet": True}),
    ("sans_d", "wiki:Sans_overworld.png", {"feet": True, "walk": (25, 12)}),   # (his steps made from it)
    ("doggo_ow", "wiki:Doggo_overworld.png", {"feet": True}),
    ("dogamy_ow", "wiki:Dogamy_overworld.png", {"feet": True}),
    ("dogaressa_ow", "wiki:Dogaressa_overworld.png", {"feet": True}),
    ("greaterdog_ow", "wiki:Greater_Dog_overworld.png", {"feet": True}),
    ("poff", "wiki:Greater_Dog_overworld_snow_poff.png", {"feet": True}),
    ("dog_ow", "wiki:Annoying_Dog_sprite.gif", {"feet": True}),
    ("dog_bone", "wiki:Annoying_Dog_sprite_bone.gif", {"feet": True, "dedupe": True}),
    # the puzzles' things: an X tile, its O, Snowdin's spikes (all cut from the map: its snow made see-through),
    # the switches, the snowball, the electric orb
    ("xtile", "map:Snowdin_map.png:7460,1240,20,20", {"keep": True, "clear": SNOW_CLEAR}),
    ("otile", "gen:otile", {"keep": True}),
    ("snowspikes", "map:Snowdin_map.png:7680,1220,20,20", {"keep": True, "clear": SNOW_CLEAR}),
    ("snowswitch", "map:Snowdin_map.png:7498,1196,24,14", {"keep": True, "clear": SNOW_CLEAR}),
    ("snowball", "gen:snowball", {"center": True, "keep": True}),
    ("lamp", "map:Snowdin_map.png:2960,365,19,31", {"keep": True, "clear": SNOW_CLEAR + [(44, 46, 63)]}),
    ("papyrus_zap", "wiki:Papyrus_overworld_electrocuted.gif", {"feet": True, "dedupe": True}),
] + [
    # the Ball Game's flags, by the time the ball took
    ("flag_" + c, "wiki:%s_flag_overworld.png" % n, {"feet": True}) for c, n in [
        ("lightblue", "Light_blue"), ("purple", "Purple"), ("green", "Green"), ("blue", "Blue"), ("yellow", "Yellow"),
        ("orange", "Orange"), ("red", "Red")]
] + [
    ("orb", "gen:orb", {"center": True, "keep": True}),
    # the shop's picture (QC behind her counter): the top of the wiki's screenshot
    ("shop_qc", "wiki:QC_screenshot_shop.png", {"keep": True, "box": (0, 0, 320, 120)}),
] + [
    # Snowdin's bullets, their middle at their origin
    (n, "gen:" + n, {"center": True}) for n in ["bone", "snowflake", "iceshard", "present", "knife", "dogpaw",
                                                 "heartbullet"]
] + [
    # Snowdin's monsters
    ("snowdrake_b", "wiki:Snowdrake_battle.gif", {"feet": True, "clear_bg": True, "every": 6}),
    ("snowdrake_hurt", "wiki:Snowdrake_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("icecap_b", "wiki:Ice_Cap_battle_idle.gif", {"feet": True, "clear_bg": True}),
    ("icecap_hurt", "wiki:Ice_Cap_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("icecap_hatless", "wiki:Ice_Cap_battle_hatless.png", {"feet": True, "clear_bg": True}),
    ("gyftrot_b", "wiki:Gyftrot_battle_idle.gif", {"feet": True, "clear_bg": True, "every": 14}),
    ("gyftrot_hurt", "wiki:Gyftrot_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("doggo_b", "wiki:Doggo_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 8}),
    ("doggo_hurt", "wiki:Doggo_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("lesserdog_b", "wiki:Lesser_Dog_battle_idle.gif", {"feet": True, "clear_bg": True}),
    ("lesserdog_hurt", "wiki:Lesser_Dog_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("greaterdog_b", "wiki:Greater_Dog_battle.gif", {"feet": True, "clear_bg": True}),
    ("greaterdog_hurt", "wiki:Greater_Dog_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("dogamy_b", "wiki:Dogamy_battle.png", {"feet": True, "clear_bg": True}),
    ("dogaressa_b", "wiki:Dogaressa_battle.png", {"feet": True, "clear_bg": True}),
    ("jerry_b", "wiki:Jerry_battle.gif", {"feet": True, "clear_bg": True}),
    ("jerry_hurt", "wiki:Jerry_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("chilldrake_b", "wiki:Chilldrake_battle_idle.gif", {"feet": True, "clear_bg": True, "every": 6}),
    ("glyde_b", "wiki:Glyde_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 15}),
    ("glyde_hurt", "wiki:Glyde_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("papyrus_b", "wiki:Papyrus_battle.png", {"feet": True, "clear_bg": True}),
    ("papyrus_hurt", "wiki:Papyrus_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("papyrus_confused", "wiki:Papyrus_battle_confused.gif", {"feet": True, "clear_bg": True, "dedupe": True}),
    # Waterfall: who walks about, the bridge seeds, the bullets
    ("mk_ow", "wiki:Monster_Kid_overworld.png", {"feet": True}),
    ("mk_ow_l", "wiki:Monster_Kid_overworld.png", {"feet": True, "mirror": True}),
    ("undyne_ow", "wiki:Undyne_overworld_armor.png", {"feet": True}),
    ("undyne_throw", "wiki:Undyne_overworld_spear.gif", {"feet": True, "dedupe": True}),
    ("onionsan", "wiki:Onionsan_overworld.png", {"feet": True}),
    ("onionsan_up", "wiki:Onionsan_overworld_surface.png", {"feet": True}),
    ("shyren_ow", "wiki:Shyren_overworld.png", {"feet": True}),
    ("aaron_ow", "wiki:Aaron_overworld.png", {"feet": True}),
    ("woshua_ow", "wiki:Woshua_overworld.gif", {"feet": True}),
    ("seed", "wiki:Bridge_Seed_overworld.png", {"keep": True}),
    ("bloom", "wiki:Bridge_Seed_overworld_flower.png", {"keep": True}),
    # what's about Waterfall (cut from the map, its floor made see-through): the small bird, and to be picked up,
    # the tutu, the artifact on its pedestal
    # past the bridge: Mad Dummy and its dummies, Temmie, the shops' keepers
    ("maddummy_ow", "wiki:Mad_Dummy_overworld.png", {"feet": True}),
    ("maddummy_b", "wiki:Mad_Dummy_battle.gif", {"feet": True, "every": 9}),
    ("dummy_minion", "wiki:Dummy_Minion_battle.png", {"center": True}),
    ("dummy_bot", "wiki:Dummy_Bot_battle.png", {"center": True}),
    ("temmie_ow", "wiki:Temmie_overworld.png", {"feet": True}),
    ("temmie_b", "wiki:Temmie_battle_idle.gif", {"feet": True, "every": 8}),
    ("temmie_hurt", "wiki:Temmie_battle_hurt.png", {"feet": True}),
    ("shop_gerson", "wiki:Gerson_screenshot_shop.png", {"keep": True, "box": (0, 0, 320, 120)}),
    ("shop_tem", "wiki:Tem_Shop_screenshot_Temmie_Armor.png", {"keep": True, "box": (0, 0, 320, 120)}),
    # the MTT-Brand Burger Emporium: its counter, Burgerpants behind it
    ("shop_burger", "wiki:Burgerpants_sprite_shop.png", {"keep": True, "box": (0, 0, 320, 120)}),
    ("burgerpants", "wiki:Burgerpants_face_1.png", {"feet": True}),
    # Bratty and Catty's alley: its trash cans and door, the two of them in front
    ("shop_bratty", "wiki:Bratty_and_Catty_sprite_shop.png", {"keep": True, "box": (0, 0, 320, 120)}),
    ("bratty_catty", "wiki:Bratty_and_Catty_sprite.png", {"feet": True}),
    # the CORE: its monsters (their idle animations thinned out), Mettaton EX (whole, without arms, then just his
    # top: the armless one cut at the waist), his box, a black sausage cut from the map, the shapes of his attacks
    ("ffroggit_b", "wiki:Final_Froggit_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 4}),
    ("ffroggit_hurt", "wiki:Final_Froggit_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("whimsalot_b", "wiki:Whimsalot_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 4}),
    ("whimsalot_hurt", "wiki:Whimsalot_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("astig_b", "wiki:Astigmatism_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 27}),
    ("astig_hurt", "wiki:Astigmatism_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("madjick_b", "wiki:Madjick_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 4}),
    ("madjick_hurt", "wiki:Madjick_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("knight_b", "wiki:Knight_Knight_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 50}),
    ("knight_hurt", "wiki:Knight_Knight_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("mttex_b", "wiki:Mettaton_EX_battle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 3}),
    ("mttex_hurt", "wiki:Mettaton_EX_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("mttex_armless", "wiki:Mettaton_EX_battle_armless.png", {"feet": True, "clear_bg": True}),
    ("mttex_top", "wiki:Mettaton_EX_battle_armless.png", {"feet": True, "clear_bg": True, "box": (0, 0, 106, 70)}),
    ("mttex_ow", "wiki:Mettaton_EX_overworld_limbless.png", {"feet": True}),
    ("sausage", "map:CORE_map.png:117,1653,30,50", {"keep": True, "clear": [(33, 81, 164), (31, 130, 228), (34, 105, 164),
                                                                         (36, 62, 141), (71, 154, 204)]}),
    *[(n, "gen:" + n, {"center": True}) for n in ["exblock", "exleg", "mttheart", "disco", "exarm"]],
    # New Home: Asgore (in the castle, in battle: his idle thinned out, hurt, kneeling), his face; Omega Flowey's
    # screen (his smile, his laugh); the six SOULs; the shapes of their attacks
    ("asgore_ow", "wiki:Asgore_Dreemurr_overworld.png", {"feet": True}),
    ("asgore_b", "wiki:Asgore_Dreemurr_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 24}),
    ("asgore_hurt", "wiki:Asgore_Dreemurr_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("asgore_kneel", "wiki:Asgore_Dreemurr_battle_kneeling.png", {"feet": True, "clear_bg": True}),
    ("face_asgore", "wiki:Asgore_Dreemurr_face.png", {"keep": True}),
    ("omega", "wiki:Omega_Flowey_battle_smile.png", {"center": True, "clear_bg": True}),
    ("omega_laugh", "wiki:Omega_Flowey_battle_laugh.gif", {"center": True, "clear_bg": True, "dedupe": True, "every": 6}),
    *[("soul_" + c.lower(), "wiki:" + c + "_SOUL_sprite.png", {"center": True})
      for c in ["Light_blue", "Orange", "Blue", "Purple", "Green", "Yellow"]],
    *[(n, "gen:" + n, {"center": True}) for n in ["xshot", "pan", "shoe", "book", "actbutton"]],
    ("bird", "map:Waterfall_map.png:8466,578,15,19", {"keep": True, "clear": [(27, 33, 90), (27, 21, 72), (23, 13, 43)]}),
    ("tutu", "map:Waterfall_map.png:1240,1240,22,10", {"keep": True, "clear": [(23, 13, 43), (27, 21, 72), (27, 33, 90)]}),
    ("artifact", "map:Waterfall_map.png:10156,150,12,11",
     {"keep": True, "clear": [(27, 21, 72), (27, 33, 90), (54, 47, 121), (99, 102, 135)]}),
    ("echo_ow", "wiki:Echo_Flower_overworld.png", {"feet": True}),
    ("melody", "map:Waterfall_map.png:10705,554,158,18", {"keep": True, "only": [(255, 255, 255)]}),
    # Hotland: Alphys, Mettaton (his box), the Royal Guards at the elevator (cut from the map), the steam vents'
    # grates each way (cut from the map: the turning ones are drawn over theirs), the burnt pan; its monsters
    ("alphys_ow", "wiki:Alphys_overworld.png", {"feet": True}),
    ("mtt_ow", "wiki:Mettaton_overworld_talking.gif", {"feet": True, "dedupe": True, "every": 4}),
    ("mtt_b", "wiki:Mettaton_battle_box.gif", {"feet": True, "dedupe": True, "every": 10}),
    ("rguards_ow", "map:Hotland_map.png:1012,6146,74,54",
     {"keep": True, "only": [(0, 0, 0), (14, 31, 36), (22, 40, 50), (23, 40, 51), (74, 79, 108), (128, 129, 170)]}),
    ("vent_d", "map:Hotland_map.png:3120,4000,20,20", {"keep": True}),
    ("vent_u", "map:Hotland_map.png:3420,4180,20,20", {"keep": True}),
    ("vent_l", "map:Hotland_map.png:3362,4000,20,20", {"keep": True}),
    ("vent_r", "map:Hotland_map.png:3225,4240,20,20", {"keep": True}),
    ("burntpan", "map:Hotland_map.png:3476,4468,18,18", {"keep": True, "clear": [(181, 85, 21), (178, 83, 19)]}),
    ("vulkin_b", "wiki:Vulkin_battle_idle.gif", {"feet": True, "dedupe": True, "every": 10}),
    ("vulkin_hurt", "wiki:Vulkin_battle_hurt.png", {"feet": True}),
    ("tsunder_b", "wiki:Tsunderplane_battle_idle.gif", {"feet": True, "dedupe": True, "every": 14}),
    ("pyrope_b", "wiki:Pyrope_battle_idle.gif", {"feet": True, "dedupe": True, "every": 6}),
    ("pyrope_hurt", "wiki:Pyrope_battle_hurt.png", {"feet": True}),
    # the second floor: the Royal Guards, Mettaton reporting, the bombs (each a part of the wiki's sheet of them)
    ("rg01_ow", "wiki:RG_01_overworld.png", {"feet": True}),
    ("rg02_ow", "wiki:RG_02_overworld.png", {"feet": True}),
    ("rg01_b", "wiki:RG_01_battle_idle.gif", {"feet": True, "dedupe": True, "every": 6}),
    ("rg02_b", "wiki:RG_02_battle_idle.gif", {"feet": True, "dedupe": True, "every": 6}),
    ("mtt_news", "wiki:Mettaton_overworld_news_reporter.gif", {"feet": True}),
    *[("bomb_" + n, "wiki:Bombs_overworld.gif", {"feet": True, "box": b}) for n, b in (
        ("ball", (0, 0, 33, 32)), ("dog", (33, 0, 33, 32)), ("present", (66, 0, 33, 32)), ("water", (0, 32, 33, 32)),
        ("script", (33, 32, 33, 32)), ("game", (66, 32, 33, 32)))],
    # the news report's script before it's shown to be a bomb, and the Stained Apron (cut from the map)
    ("news_script", "map:Hotland_map.png:4322,1880,16,16", {"keep": True, "feet": True, "clear": [(112, 83, 71), (52, 50, 78), (107, 67, 78)]}),
    ("apron", "map:Hotland_map.png:5836,2618,22,18", {"keep": True, "clear": [(181, 85, 21)]}),
    # the third floor: Muffet (in her lair, in battle, her pet cupcake), her spiders, the Diamond Boys
    ("muffet_ow", "wiki:Muffet_overworld.png", {"feet": True}),
    ("muffet_b", "wiki:Muffet_battle_idle.gif", {"feet": True, "dedupe": True, "every": 4}),
    ("muffet_hurt", "wiki:Muffet_battle_hurt.png", {"feet": True}),
    ("muffet_pet", "wiki:Muffet's_Pet_battle_chasing.gif", {"center": True, "dedupe": True}),
    ("spider_b", "wiki:Bullet_Spider_battle.png", {"center": True}),
    ("diamond1", "wiki:Diamond_Boy_overworld.png", {"feet": True}),
    ("diamond2", "wiki:Tall_Diamond_Boy_overworld.png", {"feet": True}),
    # in battle, the bombs share one look (the dog and the glass of water keep their own)
    ("bomb_b", "wiki:Bomb_battle_idle.gif", {"feet": True, "dedupe": True}),
    ("dogbomb_b", "wiki:Annoying_Dog_battle_bomb.gif", {"feet": True, "dedupe": True}),
    ("water_b", "wiki:Extremely_Agile_Glass_Of_Water_battle_idle.png", {"feet": True}),
    # Undyne fallen in Hotland's heat, on its bridge (cut from the map, the bridge and the rock behind see-through)
    ("undyne_fallen", "map:Hotland_map.png:534,6213,50,27",
     {"keep": True, "clear": [(169, 124, 41), (91, 65, 0), (62, 35, 0), (19, 35, 45), (18, 34, 44), (19, 35, 44),
                              (23, 37, 53), (20, 37, 47), (20, 36, 46), (12, 29, 33), (19, 36, 46), (23, 36, 52)]}),
    ("spear", "gen:spear", {"center": True}),
    ("arrow", "gen:arrow", {"center": True}),
    ("plane", "gen:plane", {"center": True}),
    ("bomb", "gen:bomb", {"center": True}),
    ("cloud", "gen:cloud", {"center": True}),
    ("bolt", "gen:bolt", {"center": True}),
    ("undyne_b", "wiki:Undyne_battle_armor.gif", {"feet": True, "every": 6}),
    ("note", "gen:note", {"center": True}),
    ("drop", "gen:drop", {"center": True}),
    ("arm", "gen:arm", {"center": True}),
    ("debris", "gen:debris", {"center": True}),
    ("star", "gen:star", {"center": True}),
    ("croissant", "gen:croissant", {"center": True}),
    ("donut", "gen:donut", {"center": True}),
    ("mttmini", "gen:mttmini", {"center": True}),
    # Waterfall's monsters
    ("aaron_b", "wiki:Aaron_battle_static.png", {"feet": True, "clear_bg": True}),
    ("aaron_hurt", "wiki:Aaron_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("woshua_b", "wiki:Woshua_battle_idle.gif", {"feet": True, "clear_bg": True, "dedupe": True, "every": 30}),
    ("woshua_hurt", "wiki:Woshua_battle_hurt.png", {"feet": True, "clear_bg": True}),
    ("moldbygg_b", "wiki:Moldbygg_battle_idle.gif", {"feet": True, "clear_bg": True, "every": 6}),
    ("shyren_b", "wiki:Shyren_battle.png", {"feet": True, "clear_bg": True}),
    ("shyren_turned", "wiki:Shyren_battle_turned.png", {"feet": True, "clear_bg": True}),
] + [
    # the intro's pictures, the title, GAME OVER
    ("introimage", "demo:spr_introimage", {"keep": True, "clear_black": True}),
    ("introlast", "demo:spr_introlast", {"keep": True, "clear_black": True}),
    ("undertaletitle", "demo:spr_undertaletitle", {"clear_black": True}),
    ("gameoverbg", "demo2:spr_gameoverbg", {"clear_black": True}),
] + [
    # faces in the dialogue box (the demo's: a head, and Toriel's shoulders under it); 2 frames: talking
    ("face_" + n, "demo:spr_face_" + n, {"keep": True}) for n in [
        "torieltalk", "torieltalkside", "torielhappytalk", "torielwhat", "torielwhatside", "torielmad", "torielcold",
        "torielsurprised", "torielembarrassed", "torielglasses", "torielrevengetalk", "torielscared", "torielblink",
        "torielhappyblink", "torielbody", "sans", "sanschuckle", "sanswink", "papyrus", "papyrusevil",
        "papyruslaugh", "papyrusmad", "papyrusside"]
] + [
    # Alphys's face (the wiki's), for her calls and talks
    ("face_alphys", "wiki:Alphys_face.png", {"keep": True}),
] + [
    # Flowey's faces (his talks and his battle)
    (n, "demo:spr_" + n, {"keep": True}) for n in [
        "floweynice", "floweyniceside", "floweysassy", "floweyevil", "floweygrin", "floweyplain", "floweypissed",
        "floweywink", "floweylaugh", "floweyhurt", "floweytoriel", "floweytoriel2", "floweysideshock"]
]

FONTS = [
    # the dialogue's font: Pixel Operator Mono Bold, 8 pixels a letter
    dict(name="main", ttf="PixelOperatorMono-Bold.ttf", size=16, height=12, advance=8, ascent=13),
    # the menus' text: Pixel Operator Bold
    dict(name="menu", ttf="PixelOperator-Bold.ttf", size=16, height=12, ascent=13),
    # small text (the menu's numbers)
    dict(name="small", ttf="PixelOperator8.ttf", size=8, height=8, ascent=8),
]
