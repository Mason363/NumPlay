"""Select and check the TwiliPlay Foundation package from the repository root."""
import json
import shlex
import subprocess
import sys
import os
from pathlib import Path

GAMES = (
    "numdash crossyroad numblocks numdrive suika tetris dinorun doodlejump chess "
    "flappybird towerblock coreball pacman snake connectfour g2048 minesweeper breakout"
).split()


def catalog(target):
    rows = json.loads(Path("games/games.json").read_text(encoding="utf-8"))
    by_id = {game["id"]: game for game in rows}
    if len(by_id) != len(rows) or len(set(GAMES)) != 18:
        raise ValueError("expected unique game IDs")
    selected = [dict(by_id[name], default=True) for name in GAMES]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(selected, indent=2) + "\n", encoding="utf-8")


def check(out):
    from elftools.elf.elffile import ELFFile

    app = out / "TwiliPlay.nwa"
    with app.open("rb") as stream:
        sections = list(ELFFile(stream).iter_sections())
        blocks = {s.name.split(".")[-1]: s.data_size for s in sections
                  if s.name.startswith(".rodata.np.")}
        if list(blocks) != GAMES:
            raise ValueError(f"unexpected linked game sections: {list(blocks)}")
        ram = sum(s.data_size for s in sections if s["sh_flags"] & 3 == 3)
    extra = int((out / "gen/arena_extra").read_text())
    print(f"Static writable sections: {ram} bytes; additional arena: {extra} bytes")
    for name, flash in blocks.items():
        info = json.loads((out / "modules" / f"{name}.json").read_text())
        print(f"{name:14} flash with previews {flash:8}  data {info['data']:6}  bss {info['bss']:6}")
    nwlink = shlex.split(os.environ.get("NWLINK", "nwlink"))
    start, end = 0x90180000, 0x903F0000
    for limit in (148928, 153676, 133652):
        binary = out / "TwiliPlay.bin"
        subprocess.run([*nwlink, "nwa-bin", "--flash-start", hex(start),
                        "--flash-length", str(end - start), "--ram-length", str(limit - extra),
                        str(app), str(binary)], check=True)
        installed = binary.stat().st_size
        print(f"RAM limit {limit}: passed; installed {installed}; "
              f"non-game flash {installed - sum(blocks.values())}; free {end-start-installed}")
        if installed > end - start:
            raise ValueError("installed package exceeds N0120 Epsilon 26.4 app region")


if __name__ == "__main__":
    {"catalog": catalog, "check": check}[sys.argv[1]](Path(sys.argv[2]))
