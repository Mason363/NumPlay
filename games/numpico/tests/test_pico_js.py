#!/usr/bin/env python3
"""NumPlay Studio's carts (customize/pico.js) are tools/p8cart.py's, byte for byte: the table of the same carts.

usage: test_pico_js.py CART..."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import p8cart  # noqa: E402

JS = os.path.join(HERE, "..", "..", "..", "customize", "pico.js")
NODE = """
const fs = require('fs'), P = require(process.argv[1]);
(async () => {
  const carts = [];
  for (const f of process.argv.slice(3)) carts.push(await P.readCart(new Uint8Array(fs.readFileSync(f)), f));
  fs.writeFileSync(process.argv[2], P.cartTable(carts));
})().catch((e) => { console.error(e); process.exit(1); });
"""


def main():
    carts = sys.argv[1:]
    out = os.path.join(HERE, "..", "build", "pico_js.bin")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    subprocess.run(["node", "-e", NODE, os.path.abspath(JS), out] + carts, check=True)
    js = open(out, "rb").read()
    py = p8cart.table([(p8cart.cart_name(c),) + p8cart.read_cart(c) for c in carts])
    assert js == py, f"the tables differ at {next((i for i in range(min(len(js), len(py))) if js[i] != py[i]), min(len(js), len(py)))}" \
        f" ({len(js)} and {len(py)} bytes)"
    print(f"test_pico_js: {len(carts)} carts, {len(js)} bytes, the same from customize/pico.js and tools/p8cart.py")


if __name__ == "__main__":
    main()
