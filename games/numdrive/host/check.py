#!/usr/bin/env python3
"""Checks of NumDrive on a computer (make host test): the host build drives First Gear with the gas held, and Mobile
Home (level 25) to its finish with the host's driver (ND_BOT), which gives gas up to a speed and eases off or brakes
when the house leans back too far, as a player would. Mobile Home must be won with at least one of three such
drivers (a lean of 50 or 55 degrees, braking or not): the house's wobbles are chaotic, and another compiler's
rounding can tip one run either way.

    python3 host/check.py host/nd"""
import os
import re
import subprocess
import sys
import tempfile

ND = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "host/nd")
failures = []


def run(level, frames, **env):
    e = dict(os.environ, ND_LEVEL=str(level), ND_FRAMES=str(frames), ND_NORENDER="1", ND_OUTCOME="1", **env)
    with tempfile.TemporaryDirectory() as d:
        out = subprocess.run([ND], capture_output=True, text=True, env=e, cwd=d, timeout=600).stdout
    m = re.search(r"(WIN|LOSE) at frame (\d+)", out)
    return (m.group(1), int(m.group(2))) if m else (None, frames)


def check(name, ok, detail):
    print(f"{'ok  ' if ok else 'FAIL'} {name}: {detail}")
    if not ok:
        failures.append(name)


r = run(0, 1500, ND_KEYS="RIGHT:0:1500")
check("First Gear, gas held", r[0] == "WIN", f"{r[0] or 'no end'} at frame {r[1]}")

# lean (degrees), dip, top speed (units/s), brake, look ahead (s)
drivers = ["55,25,8,1,0.2", "55,25,8,0,0.2", "50,25,8,1,0.2"]
results = [(d, run(24, 3600, ND_BOT=d)) for d in drivers]
for d, (o, f) in results:
    print(f"     Mobile Home, driver {d}: {o or 'no end'} at frame {f}")
check("Mobile Home finished", any(o == "WIN" for _, (o, _f) in results),
      f"{sum(o == 'WIN' for _, (o, _f) in results)} of {len(drivers)} drivers won")

if failures:
    print(f"{len(failures)} failed")
    sys.exit(1)
print("all passed")
