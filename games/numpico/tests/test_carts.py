#!/usr/bin/env python3
"""Carts played on this computer (build/host, with the sanitizers) with random keys and the pause menu now and
then, a few seeds each: none may crash. Then the same carts with too little memory: each must end with "out of
memory", not a crash.

usage: test_carts.py HOST [CART|DIR...]"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import p8cart  # noqa: E402


def carts(args):
    for a in args:
        if os.path.isdir(a):
            yield from sorted(os.path.join(a, f) for f in os.listdir(a) if f.lower().endswith((".p8", ".p8.png")))
        elif os.path.exists(a):
            yield a


def run(host, table, *args):
    r = subprocess.run([host, table] + [str(a) for a in args], capture_output=True, text=True, timeout=1800)
    if r.returncode:
        sys.exit(f"test_carts: {table} {' '.join(map(str, args))}: crashed\n{r.stdout}{r.stderr[-2000:]}")
    return r.stdout


def main():
    host = sys.argv[1]
    found = list(carts(sys.argv[2:]))
    if not found:
        print("test_carts: no carts to play (TEST_CARTS)")
        return
    os.makedirs(os.path.join(HERE, "..", "build"), exist_ok=True)
    for c in found:
        table = os.path.join(HERE, "..", "build", "test.bin")
        open(table, "wb").write(p8cart.table([(p8cart.cart_name(c),) + p8cart.read_cart(c)]))
        ends = set()
        for seed in (1, 2):
            out = run(host, table, "--frames", 90, "--seed", seed, "--keys", "menu", "--heap", 4 << 20)
            ends |= {line.split(":")[0] for line in out.splitlines() if line.startswith("end ")}
        small = run(host, table, "--frames", 30, "--heap", 24 << 10)
        assert "end memory" in small or "end error" in small, f"{c}: with too little memory: {small}"
        print(f"test_carts: {os.path.basename(c)}: {', '.join(sorted(ends))}", flush=True)


if __name__ == "__main__":
    main()
