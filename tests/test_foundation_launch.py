"""Launch each Foundation menu item on an emulated N0120, then exit with Home."""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import emu
from foundation import GAMES
from test_progress import presses
from test_games import COPIES, play


def check(nwa, index, game):
    calc = emu.Calculator(nwa)
    calc.enable_checks(reads=True)
    begin = calc.symbols[f"np_{game}_begin"]
    end = calc.symbols[f"np_{game}_end"]
    start = 2600 + 300 * index
    navigation = [(1800 + 300 * i, "right") for i in range(index)]
    calc.keys = presses(*navigation, (start, "ok"), (start + 5000, "home", 800))
    calc.samples = []
    calc.run(start + 8500)
    if not any(begin <= pc < end for pc in calc.samples):
        raise AssertionError(f"{game}: no execution observed in selected module")
    if not calc.exited or calc.locks or not calc.app_ram_clean() or calc.violations:
        raise AssertionError(f"{game}: exit={calc.exited} locks={calc.locks} "
                             f"RAM clean={calc.app_ram_clean()} violations={calc.violations[:3]}")
    stack = calc.stack_used()
    if stack > 30 * 1024:
        raise AssertionError(f"{game}: observed stack {stack} exceeds upstream guard")
    print(f"PASS {game}: menu launch, Home exit and RAM cleanup; stack {stack} bytes", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("nwa")
    parser.add_argument("--shard", default="1/1", help="K/N, as in tests/test_games.py")
    args = parser.parse_args()
    part, count = map(int, args.shard.split("/"))
    if not 1 <= part <= count <= len(GAMES):
        parser.error("expected 1 <= K <= N <= 18")
    with Path("games/games.json").open(encoding="utf-8") as manifest:
        records = {g["id"]: g["records"] for g in json.load(manifest)}
    errors = []
    for index, game in enumerate(GAMES):
        if index % count != part - 1:
            continue
        emu.configure("n0120", "epsilon", 148928, apps_start=0x90180000)
        try:
            check(args.nwa, index, game)
            if game in COPIES and COPIES[game] in records[game]:
                out = Path("build/foundation-save-tests") / game
                out.mkdir(parents=True, exist_ok=True)
                problems, stack, _ = play(game, args.nwa, str(out), index, copy=COPIES[game])
                if problems:
                    raise AssertionError(str(problems))
                print(f"PASS {game}: native save script and restore after reinstall; stack {stack} bytes")
        except AssertionError as exc:
            errors.append(f"{game}: {exc}")
    if errors:
        raise SystemExit("\n".join(errors))
