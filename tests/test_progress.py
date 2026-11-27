#!/usr/bin/env python3
"""Checks that progress survives an update of NumPlay.

Installing apps restarts the calculator and the NumWorks installer puts back
the Python scripts only, so NumPlay keeps a copy of every save in
numplay_saves.py. In the ARM emulator:

1. saves of every game (records named by a pattern too, like NumBlocks' regions): NumPlay writes the
   copy, a valid Python script;
2. "update" (every file but the scripts deleted): the saves come back, byte
   for byte, and the other scripts are untouched;
3. Reset asks for OK to be held: a short press resets nothing;
4. holding OK resets the game, and the copy follows: after another update,
   the game starts over while the others keep their progress.

Usage: test_progress.py build/compiled/NumPlay.nwa [--out DIR]
"""
import argparse
import base64
import json
import os
import random
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import emu  # noqa: E402

K = emu.KEYS
COPY = "numplay_saves.py"


def records(buf):
    out, p = [], 0
    while p + 2 <= len(buf):
        n, = struct.unpack_from("<H", buf, p)
        if n == 0:
            return out
        if n < 4 or p + n > len(buf):
            raise ValueError(f"corrupt record at {p}")
        name = buf[p + 2:buf.index(b"\0", p + 2)].decode("latin1")
        out.append((name, bytes(buf[p + 3 + len(name):p + n])))
        p += n
    raise ValueError("no end of the record list")


def write_storage(path, recs):
    buf = bytearray(emu.STORAGE_SIZE)
    p = 0
    for name, content in recs:
        size = 2 + len(name) + 1 + len(content)
        struct.pack_into("<H", buf, p, size)
        buf[p + 2:p + size] = name.encode() + b"\0" + content
        p += size
    open(path, "wb").write(buf)


def presses(*items):
    return [(t, t + (d[0] if d else 90), K[k]) for t, k, *d in items]


def run(nwa, storage, keys, home, out, tag):
    c = emu.Calculator(nwa, storage_file=storage)
    c.enable_checks(reads=True)
    c.keys = keys + [(home, home + 400, K["home"])]
    c.pending_shots = [(t, os.path.join(out, f"{tag}_{t}.png")) for t in (home - 150,)]
    c.run(home + 3000)
    problems = []
    if not c.exited:
        problems.append(f"{tag}: did not quit on Home")
    if c.violations:
        problems += [f"{tag}: {v}" for v in sorted(set(c.violations))[:5]]
    return dict(records(open(storage, "rb").read())), problems


def lz_unpack(data, size):
    """games/common/np_lz.h's unpacker."""
    out, i = bytearray(), 0
    while i < len(data):
        flags = data[i]
        i += 1
        for b in range(8):
            if i >= len(data):
                break
            if flags >> b & 1:
                out.append(data[i])
                i += 1
            else:
                dist = (data[i] | (data[i + 1] & 15) << 8) + 1
                n = (data[i + 1] >> 4) + 3
                i += 2
                for _ in range(n):
                    out.append(out[-dist])
    if len(out) != size:
        raise ValueError(f"unpacked {len(out)} bytes, expected {size}")
    return bytes(out)


def copy_lines(text):
    """The saves in a copy: "#>name:base64" as they are, "#=name:size:base64" packed."""
    copied = {}
    for line in text.split("\n"):
        if line.startswith("#>"):
            name, _, data = line[2:].partition(":")
            copied[name] = base64.b64decode(data)
        elif line.startswith("#="):
            name, _, rest = line[2:].partition(":")
            size, _, data = rest.partition(":")
            copied[name] = lz_unpack(base64.b64decode(data), int(size))
    return copied


def check_copy(recs, expected):
    """The copy is a Python script holding exactly `expected`."""
    problems = []
    c = recs.get(COPY)
    if c is None:
        return [f"{COPY} not written"]
    if c[0] != 0 or c[-1] != 0 or 0 in c[1:-1]:
        problems.append(f"{COPY}: not a script (status byte {c[0]}, zero inside or missing at the end)")
    try:
        text = c[1:-1].decode("ascii")
    except UnicodeDecodeError:
        return problems + [f"{COPY}: not plain text"]
    for line in text.split("\n"):
        if line and not line.startswith("#"):
            problems.append(f"{COPY}: a line that isn't a comment: {line[:40]!r}")
    copied = copy_lines(text)
    if copied != expected:
        problems.append(f"{COPY} holds {sorted(copied)}, expected {sorted(expected)}"
                        + ("" if set(copied) != set(expected) else " (contents differ)"))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nwa")
    ap.add_argument("--out", default="build/test_progress")
    ap.add_argument("--catalog", default=os.path.join(os.path.dirname(__file__), "..", "games", "games.json"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    # (the games in NumPlay.nwa: not those the customizer adds, "default": false)
    games = [g for g in json.load(open(a.catalog, encoding="utf-8"))
             if g.get("default", True)]
    rnd = random.Random(7)
    script = ("pi.py", b"\x01print(3.14159)\n\x00")
    # a save for every file the games write (binary, zeros included), and NumPlay's settings
    saves = {"numplay.set": bytes([ord("N"), 1, 0, 0])}
    # All launcher variants in this fork share the Starlight settings.
    saves["twili_anim.set"] = b"\x01\x00"
    saves["twili.set"] = b"T\x01\x00"
    for g in games:
        # Games with their own recovery script restore on launch (storage.c: own_copy).
        if any(name.endswith(".py") for name in g["records"]):
            continue
        for r in g["records"]:
            # a name ending with "*" stands for records starting with the rest (NumBlocks' regions: "nb1r-1_2.nbe")
            for n in ([r[:-1] + "0_0.nbe", r[:-1] + "-1_2.nbe"] if r.endswith("*") else [r]):
                size = rnd.choice([1, 2, 8, 30, 64, 192, 700])
                if rnd.random() < 0.5:  # random bytes: copied as they are
                    data = bytes(rnd.randrange(256) for _ in range(size))
                else:  # like real saves (zeros, repeats): copied packed
                    data = bytes(rnd.choice([0, 0, 0, i % 13, rnd.randrange(256)]) for i in range(size))
                saves.setdefault(n, data)
    first = games[0]
    reset_files = set(first.get("reset", first["records"]))
    problems = []
    with tempfile.TemporaryDirectory() as d:
        st = os.path.join(d, "storage.bin")
        write_storage(st, [script] + sorted(saves.items()))
        # 1. the copy is written
        recs, p = run(a.nwa, st, [], 2500, a.out, "1_copy")
        problems += p + check_copy(recs, saves)

        def update():  # what the installer leaves: the Python scripts
            write_storage(st, [(n, v) for n, v in records(open(st, "rb").read()) if n.endswith(".py")])

        # 2. after an update, everything comes back
        update()
        recs, p = run(a.nwa, st, [], 2500, a.out, "2_restore")
        problems += p
        for n, v in saves.items():
            if recs.get(n) != v:
                problems.append(f"after an update, {n} " + ("missing" if n not in recs else "differs"))
        if recs.get(script[0]) != script[1]:
            problems.append("the other script changed")

        # to the settings card, open it, Reset on the first game's row, then the red button
        n = len(games)
        lead = [(1200 + 300 * i, "right") for i in range(n)] + [(1500 + 300 * n, "ok")]
        t = 2600 + 300 * n
        # Appearance, Background and System information precede game rows.
        lead += [(t + 300 * i, "down") for i in range(3)]
        t += 900
        lead += [(t, "ok"), (t + 800, "right")]
        t += 1300
        # 3. a short press only shows "Hold OK": nothing is reset
        keys = presses(*lead, (t, "ok"), (t + 700, "back"), (t + 1500, "back"))
        recs, p = run(a.nwa, st, keys, t + 2500, a.out, "3_short")
        problems += p
        if any(recs.get(f) != saves[f] for f in reset_files):
            problems.append("a short press of OK reset the game")
        # 4. held, it resets; the copy follows, so an update doesn't bring it back
        keys = presses(*lead, (t, "ok", 1900), (t + 2600, "ok"), (t + 3300, "back"))
        recs, p = run(a.nwa, st, keys, t + 4300, a.out, "4_hold")
        problems += p
        if any(f in recs for f in reset_files):
            problems.append(f"holding OK did not reset {first['title']}")
        kept = {k: v for k, v in saves.items() if k not in reset_files}
        problems += check_copy(recs, kept)
        update()
        recs, p = run(a.nwa, st, [], 2500, a.out, "5_update_after_reset")
        problems += p
        if any(f in recs for f in reset_files):
            problems.append(f"{first['title']} came back after an update, though it was reset")
        for k, v in kept.items():
            if recs.get(k) != v:
                problems.append(f"after reset and update, {k} " + ("missing" if k not in recs else "differs"))
    for x in problems:
        print("   ", x)
    print("PASS" if not problems else "FAIL")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
