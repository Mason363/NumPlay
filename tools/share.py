"""Lays a game's pictures (or any byte strings read from an offset) out in one
blob, as short as this finds, each string still read the same:

- a string seen before is not stored again;
- one found inside another is read from there;
- one that starts with another's end overlaps it (greedily, the longest
  overlap first).

Only for strings whose readers stay inside them. glue: (a, b) pairs where b
must come right after a's bytes (a reader looks just before b). words: the
strings are of 16-bit words (lists of ints), the blob and offsets in words.

    blob, offsets = share(strings, glue)
"""


def share(spans, glue=(), words=False):
    if words:
        blob, offs = share([''.join(chr(0x10000 + w) for w in s) for s in spans], glue, None)
        return [ord(c) - 0x10000 for c in blob], offs
    spans = [s if words is None else bytes(s) for s in spans]
    after = dict((b, a) for a, b in glue)
    units, where, seen = [], [None] * len(spans), {}   # where: string -> (unit, offset in it)
    for i, s in enumerate(spans):
        if i in after:
            u = where[after[i]][0]
            assert where[after[i]][1] + len(spans[after[i]]) == len(units[u]), 'glue: a must end its unit'
            where[i] = (u, len(units[u]))
            units[u] += s
        elif s in seen:
            where[i] = seen[s]
        else:
            where[i] = seen[s] = (len(units), 0)
            units.append(s)
    # a unit inside a longer one is read from there
    held = {}   # unit -> (unit holding it, offset there)
    order = sorted(range(len(units)), key=lambda u: -len(units[u]))
    for k, u in enumerate(order):
        for v in order[:k]:
            if v not in held and len(units[v]) > len(units[u]):
                at = units[v].find(units[u])
                if at >= 0:
                    held[u] = (v, at)
                    break
    live = [u for u in range(len(units)) if u not in held]

    def lap(a, b):
        for k in range(min(len(a), len(b)) - 1, 0, -1):
            if a.endswith(b[:k]):
                return k
        return 0
    laps = {(a, b): lap(units[a], units[b]) for a in live for b in live if a != b}
    moved = {u: (u, 0) for u in live}
    while laps:
        (a, b), k = max(laps.items(), key=lambda e: (e[1], -e[0][0], -e[0][1]))
        if k < 1:
            break
        base = len(units[a]) - k
        units[a] += units[b][k:]
        for u, (h, o) in moved.items():
            if h == b:
                moved[u] = (a, base + o)
        live.remove(b)
        laps = {p: v for p, v in laps.items() if b not in p}
        for o in live:
            if o != a:
                laps[(a, o)] = lap(units[a], units[o])
                laps[(o, a)] = lap(units[o], units[a])
    blob, start = '' if words is None else b'', {}
    for u in live:
        start[u] = len(blob)
        blob += units[u]

    def place(u):
        if u in held:
            v, at = held[u]
            return place(v) + at
        h, o = moved[u]
        return start[h] + o
    offsets = []
    for i, s in enumerate(spans):
        u, o = where[i]
        offsets.append(place(u) + o)
        assert blob[offsets[-1]:offsets[-1] + len(s)] == s
    for a, b in glue:
        assert offsets[b] == offsets[a] + len(spans[a])
    return blob, offsets
