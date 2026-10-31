#!/usr/bin/env python3
"""Five Nights at Freddy's 2: gathers the game's pictures tools/pack.py turns into src/data.bin.

    python3 -I tools/fetch.py REF        (then: make data REF=REF; Python + Pillow)

The pictures are the game's own, from its Clickteam HTML5 build as a fan web port keeps it in its repository, at the
commit used: VocalNutria9174's fnaf-2_html5 (github.com/VocalNutria9174/fnaf-2_html5), cloned with git into
REF/html5. Its resources/*.cc1 holds the game's application (a zip of Application.ccj, Clickteam's chunks): the
objects by name with their animations' pictures, the size of each picture and the mosaics the small ones are packed
in (resources/M0001.png...); the others are resources/0001.png... by their number. Five pictures aren't in that build;
two of them, Party Room 4 dark and lit, come from fredtheking's FNaF2-RaylibCs (github.com/fredtheking/FNaF2-RaylibCs),
which keeps the game's pictures by camera.

Every picture pack.py needs is written to REF/pics, as the game draws it (a visitor in the office over the office, a
jumpscare over the office as the screen shows it), and REF/map.txt names them. Below, PICS says where each one comes
from: an animation of the object that shows the office and the cameras ("Active 16", the game's own name), another
object's picture, or a picture by its number."""
import io
import os
import struct
import subprocess
import sys
import urllib.request
import zipfile

from PIL import Image

HTML5 = ("https://github.com/VocalNutria9174/fnaf-2_html5", "9dce599551feb34bd5c2aca199b7a9dfde70f866")
RAYLIB = ("https://raw.githubusercontent.com/fredtheking/FNaF2-RaylibCs/c67435924336ddb46c0ec00c9cce6b6281877625/"
          "FNaF2-RaylibCs/Resources/Game/Main/Camera/")
CC1 = "resources/Five Nights At Freddy's 2 Web Edition.cc1"

VIEW = 109          # "Active 16": the office and the cameras, a picture an animation
SCARES = 113        # "attack animation": the jumpscares, an animation each
OFFICE = 92         # the office, its lights out

# the office's states: an animation of VIEW (the whole office as the game shows it then)
OFFICE_ANIMS = {
    "HALL_LIT": 36, "HALL_TFFAR": 55, "HALL_TF": 56, "HALL_TC": 50, "HALL_MANGLE": 84, "HALL_WF": 73,
    "HALL_WB": 58, "HALL_FOXY": 76, "HALL_GF": 93,
    "LVENT_LIT": 37, "LVENT_TC": 53, "LVENT_BB": 82, "RVENT_LIT": 38, "RVENT_TB": 47, "RVENT_MANGLE": 83,
    "OCC_WF": 74, "OCC_WB": 61, "OCC_WC": 69,
}
# the visitors the game draws over the office: their picture and where it goes (the top left, in the office's
# 1600 x 768; from the objects' places and hot spots in the game's frame: "freddy under table", "Active 21",
# "Active 20"; Toy Bonnie, Toy Chica and Golden Freddy are put in the middle, their hot spots at the screen's)
OVER_OFFICE = {
    "OCC_TF": (512, 814, 2), "OCC_TB": (187, 534, 0), "OCC_TC": (186, 393, 0), "OCC_GF": (520, 548, 268),
    "OFFICE_MANGLE": (121, 700, 0), "OFFICE_BB": (221, 259, 256),
}
# the cameras' states: an animation of VIEW, or ("raylib", file). A state with no picture of its own in the game
# shows the room as it is without them (Mangle on the ceiling, where the camera doesn't see her; the Puppet, who
# never leaves the Prize Corner but for the office), and the Show Stage's and Parts/Service's states the game never
# reaches (they empty in order) show the nearest one.
CAM_ANIMS = {
    "01": {"DARK": 27, "LIT": 28, "TC": 51, "WB": 59, "MANGLE": 28, "PUPPET": 28},
    "02": {"DARK": 33, "DWC": 65, "LIT": 34, "TB": 44, "WC": 66, "MANGLE": 34, "PUPPET": 34},
    "03": {"DARK": 29, "DWF": 71, "LIT": 30, "TB": 43, "WF": 72, "PUPPET": 30},
    "04": {"DARK": ("raylib", "14.png"), "DTB": 90, "LIT": ("raylib", "16.png"), "TB": 91, "WC": 67,
           "PUPPET": ("raylib", "16.png")},
    "05": {"DARK": 13, "LIT": 14, "TC": 52, "BB": 100, "WB": 60},
    "06": {"DARK": 15, "LIT": 16, "TB": 45, "WC": 68, "MANGLE": 81},
    "07": {"DARK": 21, "EMPTY": 22, "TC": 49, "MANGLE": 22, "WF": 70, "WB": 57, "PUPPET": 22},
    # Parts/Service: S + Withered Freddy 1, Bonnie 2, Chica 4 there
    "08": {"DARK": 23, "S0": 64, "S1": 63, "S2": 24, "S3": 24, "S4": 62, "S5": 62, "S6": 24, "S7": 24, "FOXY": 75},
    # the Show Stage: S lit, D dark, + Toy Freddy 1, Bonnie 2, Chica 4 there
    "09": {"S0": 89, "S1": 42, "S2": 12, "S3": 12, "S4": 40, "S5": 40, "S6": 12, "S7": 12,
           "D0": 89, "D1": 41, "D2": 46, "D3": 46, "D4": 39, "D5": 39, "D6": 46, "D7": 46},
    "10": {"DARK": 85, "EMPTY": 87, "TF": 86, "BB": 18, "TFBB": 54, "MANGLE": 87},
    # the Prize Corner: the Puppet in its box, peeking, out of it, gone
    "11": {"DARK": 25, "P0": 26, "P1": 77, "P2": 78, "P3": 79, "EMPTY": 79, "MANGLE": 95},
    "12": {"DARK": 19, "MANGLE": 20, "EMPTY": 80},
}
SCARE_ANIMS = {"WF": 12, "WB": 13, "WC": 14, "FOXY": 15, "TB": 16, "TC": 17, "TF": 18, "MANGLE": 19, "PUPPET": 20,
               "GF": 21}
# pictures by number: the title's (the "Active 2" faces, the words, the star), the help wanted ad, the game over, the
# three endings, the custom night's faces (its "... icon" objects) and the app's icon (Toy Freddy's)
PICTURES = {
    "TITLE": 321, "TITLE_TEXT": 469, "STAR": 593, "NEWSPAPER": 272, "GAMEOVER": 226, "PAYCHECK": 587,
    "OVERTIME": 590, "PINKSLIP": 622, "ICON": 352,
    "FACE0": 551, "FACE1": 339, "FACE2": 338, "FACE3": 341, "FACE4": 344, "FACE5": 352, "FACE6": 346, "FACE7": 576,
    "FACE8": 577, "FACE9": 578,
}


class Game:
    """the game's application (Application.ccj) and its pictures"""

    def __init__(self, root):
        self.root = os.path.join(root, "resources")
        d = zipfile.ZipFile(os.path.join(root, CC1)).read("Application.ccj")
        assert d[:4] == b"PAMU", "not a Clickteam application"
        self.chunks, p = {}, 16
        while p + 8 <= len(d):
            cid, _, size = struct.unpack_from("<HHI", d, p)
            self.chunks.setdefault(cid, []).append(d[p + 8:p + 8 + size])
            p += 8 + size
            if cid == 0x7F7F:
                break
        # the objects: their animations' pictures (an Active's ObjectCommon, its animations at offset 12)
        self.anims, self.names = {}, {}
        b = self.chunks[0x2229][0]
        q = 4
        for _ in range(struct.unpack_from("<I", b, 0)[0]):
            head = name = props = None
            while True:
                sid, _, size = struct.unpack_from("<HHI", b, q)
                body = b[q + 8:q + 8 + size]
                q += 8 + size
                if sid == 0x4444:
                    head = struct.unpack_from("<Hh", body, 0)
                elif sid == 0x4445:
                    name = body.decode("utf-16-le").split("\0")[0]
                elif sid == 0x4446:
                    props = body
                elif sid == 0x7F7F:
                    break
            handle, kind = head
            self.names[handle] = name
            if kind == 2 and props:
                self.anims[handle] = self._animations(props)
        # the pictures' sizes, and where the small ones are in the mosaics (each frame's table)
        b = self.chunks[0x6666][0]
        self.size = {}
        for i in range(struct.unpack_from("<H", b, 2)[0]):
            h, w, hh = struct.unpack_from("<3H", b, 4 + 14 * i)
            self.size[h] = (w, hh)
        self.mosaic = {}
        for frame in self.chunks[0x3333]:
            q = 0
            while q + 8 <= len(frame):
                sid, _, size = struct.unpack_from("<HHI", frame, q)
                if sid == 0x3348:
                    for h in range(size // 6):
                        m, x, y = struct.unpack_from("<3H", frame, q + 8 + 6 * h)
                        if m:
                            self.mosaic.setdefault(h, (m, x, y))
                q += 8 + size
                if sid == 0x7F7F:
                    break
        self.sheets = {}

    @staticmethod
    def _animations(p):
        at = struct.unpack_from("<H", p, 12)[0]
        count = struct.unpack_from("<H", p, at + 2)[0]
        out = {}
        for a, off in enumerate(struct.unpack_from(f"<{count}H", p, at + 4)):
            if not off:
                continue
            dirs = struct.unpack_from("<32H", p, at + off)
            if dirs[0]:
                q = at + off + dirs[0]
                n = struct.unpack_from("<H", p, q + 6)[0]
                out[a] = list(struct.unpack_from(f"<{n}H", p, q + 8))
        return out

    def picture(self, h):
        f = os.path.join(self.root, f"{h:04d}.png")
        if os.path.exists(f):
            return Image.open(f).convert("RGBA")
        if h not in self.mosaic:
            raise SystemExit(f"fetch: picture {h} isn't in the build")
        m, x, y = self.mosaic[h]
        if m not in self.sheets:
            self.sheets[m] = Image.open(os.path.join(self.root, f"M{m:04d}.png")).convert("RGBA")
        w, hh = self.size[h]
        return self.sheets[m].crop((x, y, x + w, y + hh))

    def frames(self, obj, anim):
        return self.anims[obj][anim]


def raylib(ref, name):
    path = os.path.join(ref, "raylib", name)
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with urllib.request.urlopen(RAYLIB + name) as r:
            open(path, "wb").write(r.read())
    return Image.open(path).convert("RGBA")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    ref = sys.argv[1]
    os.makedirs(os.path.join(ref, "pics"), exist_ok=True)
    d = os.path.join(ref, "html5")
    if not os.path.isdir(os.path.join(d, ".git")):
        subprocess.run(["git", "clone", "--quiet", HTML5[0], d], check=True)
    subprocess.run(["git", "-C", d, "checkout", "--quiet", HTML5[1]], check=True)
    game = Game(d)
    assert game.names[VIEW] == "Active 16" and game.names[SCARES] == "attack animation", "not the build expected"
    lines = []

    def save(name, im, k=None):
        rel = f"pics/{name}.png" if k is None else f"pics/{name}_{k:02d}.png"
        im.convert("RGB").save(os.path.join(ref, rel))
        lines.append(f"{name} {rel}")

    def view(anim):
        return game.picture(game.frames(VIEW, anim)[0])

    office = game.picture(OFFICE)
    save("OFFICE", office)
    for name, anim in OFFICE_ANIMS.items():
        save(name, view(anim))
    for name, (h, x, y) in OVER_OFFICE.items():
        im = office.copy()
        im.alpha_composite(game.picture(h), (x, y))
        save(name, im)
    for num, states in CAM_ANIMS.items():
        for state, src in states.items():
            save(f"CAM{num}_{state}", raylib(ref, src[1]) if isinstance(src, tuple) else view(src))
    middle = office.crop((288, 0, 288 + 1024, 768))   # (the office as the screen shows it, looking ahead)
    for who, anim in SCARE_ANIMS.items():
        for k, h in enumerate(game.frames(SCARES, anim)):
            im = middle.copy()
            im.alpha_composite(game.picture(h))
            save(f"SCARE_{who}", im, k)
    for name, h in PICTURES.items():
        im = game.picture(h)
        if name in ("TITLE_TEXT", "STAR"):   # (white or yellow on see-through, as they are)
            rel = f"pics/{name}.png"
            im.save(os.path.join(ref, rel))
            lines.append(f"{name} {rel}")
        else:
            save(name, im)
    with open(os.path.join(ref, "map.txt"), "w") as f:
        f.write("# the game's pictures, as tools/fetch.py made them\n" + "\n".join(lines) + "\n")
    print(f"{len(lines)} pictures in {ref}/pics, {ref}/map.txt written: make data REF={ref}")


if __name__ == "__main__":
    main()
