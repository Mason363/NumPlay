"""Select and check the TwiliPlay Foundation package from the repository root."""
import json
import shlex
import subprocess
import sys
import os
from pathlib import Path

GAMES = (
    "numdash crossyroad numblocks numdrive suika doodlejump chess flappybird towerblock coreball minesweeper breakout "
    "badtime rider uttt sudoku invaders sokoban blockblast meowdoku mahjong uno numpico twoplayer paperclips"
).split()


def catalog(target):
    rows = json.loads(Path("games/games.json").read_text(encoding="utf-8"))
    by_id = {game["id"]: game for game in rows}
    if len(by_id) != len(rows) or len(set(GAMES)) != 25:
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
        allocated = {s.name: s.data_size for s in sections if s["sh_flags"] & 2
                     and s["sh_type"] != "SHT_NOBITS" and s.name != ".rodata.eadk_api_level"}
    extra = int((out / "gen/arena_extra").read_text())

    for name, flash in blocks.items():
        info = json.loads((out / "modules" / f"{name}.json").read_text())
        with (out / "kit" / "games" / f"{name}.o").open("rb") as stream:
            shots = sum(s.data_size for s in ELFFile(stream).iter_sections()
                        if s.name.endswith(".shots") and s["sh_flags"] & 2)
        print(f"{name:14} previews {shots:7}  flash with previews {flash:8}  data {info['data']:6}  bss {info['bss']:6}")
    nwlink = shlex.split(os.environ.get("NWLINK", "nwlink"))
    start, end = 0x90180000, 0x903F0000
    ram_start = 0x20000000
    elf = out / "TwiliPlay.elf"
    subprocess.run([*nwlink, "nwa-elf", str(app), str(elf), "--flash-start", hex(start),
                    "--flash-length", str(end - start), "--ram-start", hex(ram_start),
                    "--ram-length", str(148928 - extra)], check=True)
    with elf.open("rb") as stream:
        linked = ELFFile(stream)
        arena = linked.get_section_by_name(".symtab").get_symbol_by_name("np_arena")[0]["st_value"]
        size_symbol = linked.get_section_by_name(".symtab").get_symbol_by_name("np_arena_size")[0]
        section = linked.get_section(size_symbol["st_shndx"])
        offset = size_symbol["st_value"] - section["sh_addr"]
        arena_size = int.from_bytes(section.data()[offset:offset + 4], "little")
    static = arena - ram_start + arena_size
    print(f"RAM prefix {arena - ram_start}; fixed arena {arena_size}; static RAM {static}; "
          f"additional game RAM {extra}; total required {static + extra}")
    for limit in (148928, 153676, 133652):
        binary = out / "TwiliPlay.bin"
        subprocess.run([*nwlink, "nwa-bin", "--flash-start", hex(start),
                        "--flash-length", str(end - start), "--ram-length", str(limit - extra),
                        str(app), str(binary)], check=True)
        installed = binary.stat().st_size
        print(f"RAM limit {limit}: passed; installed {installed}; "
              f"non-game flash {installed - sum(blocks.values())}; free {end-start-installed}")
        print(f"Game table {allocated['.rodata.np_games']}; "
              f"installer overhead including alignment {installed - sum(allocated.values())}")
        if installed > end - start:
            raise ValueError("installed package exceeds N0120 Epsilon 26.4 app region")


if __name__ == "__main__":
    {"catalog": catalog, "check": check}[sys.argv[1]](Path(sys.argv[2]))
