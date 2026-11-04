"""The game's text: its language sheets (EN_*.txt TextAssets, encrypted; the key is found in the game's own assembly),
as {sheet: {key: text}}."""
import base64, functools, html, os, re, struct
import numpy as np
import unity


def _candidates(dll):
    """32-character alphanumeric strings in an assembly's user strings (UTF-16)."""
    data = open(dll, "rb").read()
    for m in re.finditer(rb"(?:[A-Za-z0-9]\x00){32}", data):
        s = m.group(0).decode("utf-16-le")
        yield s


def _decrypt(key, b64):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    raw = base64.b64decode(b64)
    d = Cipher(algorithms.AES(key.encode()), modes.ECB()).decryptor()
    out = d.update(raw) + d.finalize()
    pad = out[-1]
    if not 1 <= pad <= 16 or out[-pad:] != bytes([pad]) * pad:
        raise ValueError("padding")
    return out[:-pad].decode("utf-8")


def _assets():
    f = unity.asset_file("resources.assets")
    out = {}
    for o in f.objects.values():
        if o.type.name == "TextAsset":
            t = o.read()
            if t.m_Name.startswith("EN_"):
                data = t.m_Script
                out[t.m_Name[3:]] = data if isinstance(data, str) else data.decode("utf-8", "replace")
    return out


@functools.lru_cache(maxsize=None)
def _key():
    sample = _assets()["General"]
    dll = os.path.join(unity.DATA, "Managed", "Assembly-CSharp.dll")
    for k in _candidates(dll):
        try:
            _decrypt(k, sample)
            return k
        except Exception:
            continue
    raise RuntimeError("no key for the language sheets")


@functools.lru_cache(maxsize=None)
def sheets():
    """{sheet name (EN_ less): {entry key: text}}"""
    out = {}
    key = _key()
    for name, b64 in _assets().items():
        xml = _decrypt(key, b64)
        out[name] = {m.group(1): m.group(2) for m in re.finditer(r'<entry name="([^"]*)">(.*?)</entry>', xml, re.S)}
    return out


# ---------------------------------------------------------------- the texts the game shows, in its own code page
TEXT_K = 4 / 3   # text a third bigger than the game's (its sizes at this screen's are hard to read), boxes with it
BR, PAGE = 10, 12
PHASES = 4       # glyphs drawn a quarter pixel apart

# the styles: (font, em in pixels, line advance in pixels, ascender, descender in pixels, spacing after each glyph in
# pixels, phases kept: 1 for big text)
#   TextMeshPro: an em is font size / 10 units; a line is (the font's line height + line spacing) / point size ems;
#   character spacing is in hundredths of an em
HUD_PX = 100 / 8.7107        # (the HUD camera's: pixels a unit)
WORLD_PX = 421.94 / 38.1     # (the room's camera, at z = 0)


def _style(font, size, px, line_height, line_spacing, point, asc, desc, k=TEXT_K, spacing=0.0, phases=PHASES, em=None):
    """(em: in pixels, else the game's size, k times; the body text's at sizes that read on the calculator's screen)"""
    em = em or size * 0.1 * px * k
    return (font, em, (line_height + line_spacing) / point * em, asc / point * em, -desc / point * em, spacing / 100 * em,
            phases)


# the body text's sizes (ems in pixels): what is read, bigger than the game's at this screen's size (its titles as
# they are); two pen phases, half a pixel apart, enough at these sizes (and styles of the same font and size share
# their glyphs)
READ_EM, DESC_EM, SMALL_EM, XSMALL_EM = 14.0, 12.5, 11.0, 9.2
PERPETUA = (134.0625, 0, 117, 95.9375, -38.1875)


# (Trajan Pro's metrics: trajan_bold_tmpro's point size, line height, ascender, descender)
TRAJAN = (84, 0, 70, 52.5, -17.5)
STYLES = {
    "DIALOGUE": _style("Perpetua", 7.2, HUD_PX, 134.0625, 24.6, 117, 95.9375, -38.1875, em=READ_EM, phases=2),
    "PROMPT": _style("TrajanPro-Bold", 6.67, WORLD_PX, *TRAJAN, em=11.5, phases=2),
    # the area titles (Area Title: TextMeshPro, size 36): the large one's main line, the small one's, and the lines
    # above and below them (the large title's at their own size; the small one's, a third bigger, the same)
    "TITLE_L": _style("TrajanPro-Bold", 36 * 0.4902, HUD_PX, *TRAJAN, k=1, spacing=4.69, phases=1),
    "TITLE_S": _style("TrajanPro-Bold", 36 * 0.3493, HUD_PX, *TRAJAN, k=1, spacing=4.69, phases=1),
    "TITLE_SUB": _style("TrajanPro-Bold", 36 * 0.1942, HUD_PX, *TRAJAN, k=1, spacing=21.06),
    # the message as an item is taken (UI Msg Get Item: its lines, a third bigger; its item's name)
    "MSG": _style("Perpetua", 3.73 * 1.4407, HUD_PX, *PERPETUA, em=DESC_EM, phases=2),
    "MSG_NAME": _style("TrajanPro-Bold", 9.32 * 1.4407, HUD_PX, *TRAJAN, k=1, phases=1),
    # the notices as a relic or a charm is taken (Relic Get Msg, Charm Get Msg: Perpetua 8), the charm tutorial's title
    # (9) and its lines (6)
    "NOTICE": _style("Perpetua", 8, HUD_PX, *PERPETUA, em=READ_EM, phases=2),
    "TUTE_TITLE": _style("Perpetua", 9, HUD_PX, 134.0625, 0, 117, 95.9375, -38.1875, phases=1),
    "TUTE": _style("Perpetua", 6, HUD_PX, *PERPETUA, em=DESC_EM, phases=2),
    # the menus (Menu_Title's UI canvas, 1920 x 1200 for 320 x 200 pixels; their text bigger: menu.MENU_K): the
    # buttons (Trajan bold, 54 at 0.549 and 45 at 0.7), the screens' titles and slot numbers, the slots' details
    "MENU": _style("TrajanPro-Bold", 31 / 6 * 1.6 * 10, 1, *TRAJAN, em=10.5, phases=2),
    "MENU_TITLE": _style("TrajanPro-Bold", 53 / 6 * 1.35 * 10, 1, *TRAJAN, k=1, phases=1),
    "MENU_SMALL": _style("TrajanPro-Regular", 27 / 6 * 1.6 * 10, 1, *TRAJAN, em=9.5, phases=2),
    # the yes or no box's Yes and No (Text YN's UI List: TextMeshPro 10)
    "YN": _style("Perpetua", 10, HUD_PX, 134.0625, 0, 117, 95.9375, -38.1875),
    # a shop's long descriptions, smaller (still bigger than the game's) where the message's size runs out of the window
    "MSG_S": _style("Perpetua", 3.73 * 1.4407 * 0.78, HUD_PX, *PERPETUA, em=SMALL_EM, phases=2),
    # the longest descriptions (the inventory's relics, some charms), where neither fits their box
    "MSG_XS": _style("Perpetua", 3.73 * 1.4407 * 0.78, HUD_PX, *PERPETUA, em=XSMALL_EM, phases=2),
}
# (the styles a text falls back to where it does not fit: every character of the one it stands in for)
FALLBACKS = {"MSG_S": "MSG", "MSG_XS": "MSG"}

# the items the message shows: (its name in the UI sheet, the Prompts sheet's prefix, tap or press, its two lines, the
# calculator's key for it)
MSGS = {"FIREBALL": ("INV_NAME_SPELL_FIREBALL1", "GET_ITEM_INTRO2", "BUTTON_DESC_TAP", "GET_FIREBALL_1", "GET_FIREBALL_2",
                     "alpha"),
        "DASH": ("INV_NAME_DASH", "GET_ITEM_INTRO1", "BUTTON_DESC_PRESS", "GET_DASH_1", "GET_DASH_2", "shift")}
if os.environ.get("HK_PART") == "2":   # (the Mantis Claw, in the second part)
    MSGS["WALLJUMP"] = ("INV_NAME_WALLJUMP", "GET_ITEM_INTRO1", "BUTTON_DESC_PRESS", "GET_WALLJUMP_1", "GET_WALLJUMP_2", "OK")

# the titles the game shows (the Titles sheet's X_MAIN, X_SUB, X_SUPER): the areas', bosses' and characters'
TITLES = ["KINGSPASS", "DIRTMOUTH", "CROSSROADS", "EGGTEMPLE", "SHAMANTEMPLE", "GREENPATH", "BIGFLY", "FALSE_KNIGHT",
          "HORNET", "ELDERBUG", "SLY", "ISELDA", "CORNIFER", "QUIRREL", "SHAMAN", "STAG", "STAG2", "TISO_C", "TISO_NC",
          "CHARM_SLUG"]
if os.environ.get("HK_PART") == "2":   # (the second part's: Hollow Knight: Fungal Wastes)
    TITLES += ["FUNGUS", "QUEENS_STATION", "MANTIS_VILLAGE", "FUNGUS_CORE", "RUINS", "MAGE_TOWER", "MANTIS_LORDS"]


def clean(s):
    """A sheet's entry as shown: its markup to breaks and pages."""
    s = html.unescape(s)
    s = s.replace("<br>", "\n").replace("<page>", "\f")
    return s.strip(" ")


class Texts:
    def __init__(self):
        self.list, self.index = [], {}

    def id(self, sheet, key, style="DIALOGUE"):
        """A sheet's entry, by its key (shown in a style): its index."""
        return self.add(clean(sheets()[sheet][key]), style)

    def add(self, s, style):
        k = (s, style)
        if k not in self.index:
            self.index[k] = len(self.list)
            self.list.append(k)
        return self.index[k]

    def code_page(self):
        extra = sorted({c for s, _ in self.list for c in s if not (32 <= ord(c) < 127 or c in "\n\f")})
        assert len(extra) <= 128, extra
        cp = {chr(i): i for i in range(32, 127)}
        cp["\n"], cp["\f"] = BR, PAGE
        for i, c in enumerate(extra):
            cp[c] = 128 + i
        return cp

    def blob(self):
        """TEXT: count, offsets, then each text (its code page's bytes, 0 ended)"""
        cp = self.code_page()
        offs, body = [], bytearray()
        for s, _ in self.list:
            offs.append(len(body))
            body += bytes(cp[c] for c in s) + b"\0"
        return struct.pack("<I", len(self.list)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(body)

    def fonts(self):
        """FONT: per style (as STYLES' order) its offset; a style: em, line advance, ascender, descender (floats), the
        first code it has and how many (u8 each, then 2 bytes), PHASES glyph offsets for each of those codes (u16, from the
        style, 0 none: the code's glyph with the pen 0, 1/4... of a pixel right of a whole pixel), then the glyphs: width,
        height, left, top (from the pen's pixel on the baseline, pixels), advance (1/64 pixels, u16), then rows of 4-bit
        alpha (low nibble first)."""
        import font
        cp = self.code_page()
        # (styles of the same font, size, spacing and phases share one set of glyphs: those of all of them; the others'
        # blocks, their metrics and offsets only, go just before the one with the glyphs, their offsets reaching on)
        groups = {}
        for name, (fname, em, line, asc, desc, spacing, phases) in STYLES.items():
            groups.setdefault((fname, em, spacing, phases), []).append(name)
        blocks = {}
        for (fname, em, spacing, phases), names in groups.items():
            names_in = set(names) | {FALLBACKS[n] for n in names if n in FALLBACKS}
            chars = sorted({c for s, st in self.list if st in names_in for c in s if c not in "\n\f"} | {" ", "-"})
            lo = min(cp[c] for c in chars)
            ncodes = max(cp[c] for c in chars) - lo + 1
            heads = 20 + 2 * ncodes * PHASES   # (a block's metrics and offsets)
            body = bytearray()
            at = {}   # (code - lo) * PHASES + pen phase -> its glyph, from the glyphs' start
            for ph in range(phases):
                for ch, g in zip(chars, font.bitmaps(fname, chars, em, ph / phases)):
                    a, left, top, adv = g
                    h, w = a.shape
                    for q in range(PHASES):   # (each pen phase, the nearest drawn below it; big text: one for all)
                        if q * phases // PHASES == ph:
                            at[(cp[ch] - lo) * PHASES + q] = len(body)
                    body += struct.pack("<BBbbH", w, h, left, top, min(65535, round((adv + spacing) * 64)))
                    q = (a.astype(np.uint16) * 15 + 127) // 255
                    for row in q:
                        r = list(row) + [0] * (w & 1)
                        body += bytes(r[i] | r[i + 1] << 4 for i in range(0, len(r), 2))
                    if len(body) & 1:
                        body.append(0)
            while len(body) % 4:
                body.append(0)
            for k, name in enumerate(names):
                _, _, line, asc, desc, _, _ = STYLES[name]
                head = struct.pack("<ffffBBH", em, line, asc, desc, lo, ncodes, 0)
                base = heads * (len(names) - k)   # (the glyphs, from this block's start)
                offs = [0] * (ncodes * PHASES)
                for i, o in at.items():
                    offs[i] = base + o
                assert max(offs) < 65536, name
                blocks[name] = head + struct.pack("<%dH" % len(offs), *offs) + (bytes(body) if k == len(names) - 1 else b"")
        out = bytearray(struct.pack("<I", len(blocks)))
        pos = 4 + 4 * len(blocks)
        order = [n for names in groups.values() for n in names]
        for name in STYLES:
            out += struct.pack("<I", pos + sum(len(blocks[n]) for n in order[:order.index(name)]))
        for name in order:
            out += blocks[name]
        return bytes(out)
