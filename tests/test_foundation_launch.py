"""Launch each Foundation menu item on an emulated N0120, then exit with Home."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import emu
from foundation import GAMES
from test_progress import presses


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
    print(f"PASS {game}: menu launch, Home exit and RAM cleanup", flush=True)


if __name__ == "__main__":
    emu.configure("n0120", "epsilon", 148928, apps_start=0x90180000)
    for index, game in enumerate(GAMES):
        check(sys.argv[1], index, game)
