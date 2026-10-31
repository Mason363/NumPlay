#!/usr/bin/env python3
"""Generates the glue between the launcher and the game modules.

From games/games.json and the module sizes written by npmodule.py:
- entry_<game>.c: the game's entry in the table of games the launcher shows. Each entry
  is a section of its own (.rodata.np_games.<n>), and numplay.ld puts the entries of the
  games it links in a row, np_games to np_games_end, so a NumPlay with any set of games is
  linked from the same objects (the customizer website does it in the browser);
- arena.s: the RAM arena, and the symbols that place each game's .data and
  .bss inside it (games never run together), then its heap (np_<game>_heap, the
  game's _heap_start: a big game takes the RAM after its own, its "heap" in
  games.json the least it needs, and its malloc np_sbrk's). It is the last of NumPlay's RAM
  and holds what the launcher needs; a game uses it and the RAM after it, up to
  the end of what the calculator's software gives apps (_heap_end), so NumPlay
  installs with a smaller userland too and a game that doesn't fit there says
  so (launcher/src/games.c). arena_extra: how much more than the arena the
  hungriest game takes, for the Makefile's check of every game's RAM;
- marks_<game>.s: a head and a tail word around the game's block of flash;
- numplay.ld: the script of the final `ld -r`, which keeps each game's code,
  data image, screenshots and marks together as one block; script.json: the
  same script in parts (the start, each game's block, the end) for the website.

With --simulator, gametable.c instead: the whole table as one array, for the simulator.

numplay.ld links the games of games.json but those marked "default": false (they only come from
the website), or with --only "id id ...", those games (NumPlay for Upsilon: those that fit its
RAM); each keeps the index of its place in games.json, which its module's sections are named after.

Usage: gen_games.py games.json MODULE_DIR OUT_DIR [--simulator] [--only "id id ..."]
"""
import argparse
import json
import os
import re

LAUNCHER_RAM = 96 * 1024  # the arena's own size; the launcher's screenshot cache takes what there is
HEAD, TAIL = 0x3147504E, 0x444E4550


def c_str(s):
    return "NULL" if s is None else json.dumps(s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest")
    ap.add_argument("modules")
    ap.add_argument("out")
    ap.add_argument("--simulator", action="store_true")
    ap.add_argument("--only", help="space separated game ids")
    a = ap.parse_args()
    games = manifest = json.load(open(a.manifest))
    index = {g["id"]: n for n, g in enumerate(games, 1)}
    if a.only:
        keep = a.only.split()
        missing = [i for i in keep if i not in index]
        if missing:
            raise SystemExit(f"--only: no such games: {missing}")
        games = [g for g in games if g["id"] in keep]
    else:
        games = [g for g in games if g.get("default", True)]  # the others only come from the website
    np_h = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "launcher", "src", "np.h")).read()
    limit = int(re.search(r"#define NP_MAX_GAMES (\d+)", np_h).group(1))
    if len(manifest) > limit:  # (the website may link them all)
        raise SystemExit(f"{len(manifest)} games, but the launcher holds {limit} (NP_MAX_GAMES in launcher/src/np.h)")
    os.makedirs(a.out, exist_ok=True)
    sizes = {}
    for g in manifest:
        p = os.path.join(a.modules, g["id"] + ".json")
        sizes[g["id"]] = json.load(open(p)) if os.path.exists(p) else {"code": 0, "data": 0, "bss": 0, "ram_align": 8}
    # every game whose module is built gets its entry, marks and place in the arena, for the kit
    kit = [g for g in manifest if g in games or os.path.exists(os.path.join(a.modules, g["id"] + ".json"))]

    # ---- RAM layout
    need = LAUNCHER_RAM
    layout = {}
    for g in kit:
        s = sizes[g["id"]]
        data = s["data"]
        bss_off = (data + 7) & ~7
        heap_off = (bss_off + s["bss"] + 7) & ~7
        layout[g["id"]] = (0, bss_off, heap_off)
        if g in games:
            need = max(need, heap_off + g.get("heap", 0))
    need = (need + 7) & ~7
    # (the simulator's arena, a C array, keeps the hungriest game's size)
    arena = need if a.simulator else LAUNCHER_RAM

    def externs(i):
        return [f"extern const uint32_t np_{i}_begin[], np_{i}_end[];",
                f"extern uint8_t np_{i}_data[], np_{i}_bss[];",
                f"extern const uint8_t np_{i}_init[];",
                f"extern const np_shot_t np_{i}_shots[];",
                f"int np_{i}_main(void);"]

    def lists(g, prefix):
        recs = ", ".join(json.dumps(r) for r in g["records"])
        prog = ", ".join(json.dumps(r) for r in g.get("reset", g["records"]))
        return [f"static const char *const {prefix}records[] = {{{recs + ', ' if recs else ''}NULL}};",
                f"static const char *const {prefix}progress[] = {{{prog + ', ' if prog else ''}NULL}};"]

    def entry(g, prefix, ram):
        i = g["id"]
        s = sizes[i]
        est = s["code"] + s["data"]
        r = (lambda x: x) if ram else (lambda x: "NULL")
        z = (lambda x: x) if ram else (lambda x: 0)
        return [f"  {{{c_str(i)}, {c_str(g['title'])}, {c_str(g['tagline'])}, {c_str(g.get('credit'))},",
                f"   {g['top']}, {g['bottom']}, {g['accent']}, np_{i}_begin, np_{i}_end, np_{i}_main,",
                f"   {r(f'np_{i}_data')}, {r(f'np_{i}_init')}, {z(s['data'])}, {r(f'np_{i}_bss')}, {z(s['bss'])},",
                f"   np_{i}_shots, {len(g['shots'])}, {prefix}records, {prefix}progress, "
                f"{g.get('heap', 0) if ram else est}}}"]  # (the calculator: its heap; the simulator: its size)

    if a.simulator:
        # ---- gametable.c: the simulator has no flash blocks and gives each game its own RAM
        L = ["/* Generated by tools/gen_games.py from games/games.json. */", '#include "np.h"', ""]
        for g in games:
            L += externs(g["id"]) + lists(g, g["id"] + "_") + [""]
        for g in games:
            L.append(f"const uint32_t np_{g['id']}_begin[1], np_{g['id']}_end[1];")
        L.append(f"uint8_t np_arena[{arena}] __attribute__((aligned(8)));")
        L.append(f"const uint32_t np_arena_size = {arena};")
        L.append("")
        L.append("const np_game_t np_games[] = {")
        for g in games:
            e = entry(g, g["id"] + "_", False)
            L += e[:-1] + [e[-1] + ","]
        L.append("};")
        L.append(f"const int np_game_count = {len(games)};")
        open(os.path.join(a.out, "gametable.c"), "w").write("\n".join(L) + "\n")
        print(f"games: {len(games)} (simulator)")
        return

    # ---- entry_<game>.c
    for g in kit:
        i, n = g["id"], index[g["id"]]
        L = [f"/* Generated by tools/gen_games.py from games/games.json: {g['title']} in the launcher's table. */",
             '#include "np.h"', ""] + externs(i) + lists(g, "") + [
             "",
             f'__attribute__((used, section(".rodata.np_games.{n:02d}")))',
             "static const np_game_t entry ="]
        e = entry(g, "", True)
        L += e[:-1] + [e[-1] + ";"]
        open(os.path.join(a.out, f"entry_{i}.c"), "w").write("\n".join(L) + "\n")

    # ---- arena.s
    S = ["@ Generated by tools/gen_games.py: one RAM arena shared by the launcher and the games, the",
         "@ last of NumPlay's RAM (numplay.ld): a game goes on into the RAM after it.",
         '  .section .np_arena,"aw",%nobits', "  .balign 8", "  .global np_arena", "np_arena:",
         f"  .space {arena}",
         '  .section .rodata.np_arena_size,"a"', "  .balign 4", "  .global np_arena_size", "np_arena_size:",
         f"  .word {arena}"]
    for g in kit:
        i = g["id"]
        d, b, h = layout[i]
        S += [f"  .global np_{i}_data", f"  .set np_{i}_data, np_arena + {d}",
              f"  .global np_{i}_bss", f"  .set np_{i}_bss, np_arena + {b}",
              f"  .global np_{i}_heap", f"  .set np_{i}_heap, np_arena + {h}"]
    open(os.path.join(a.out, "arena.s"), "w").write("\n".join(S) + "\n")
    open(os.path.join(a.out, "arena_extra"), "w").write(f"{need - arena}\n")

    # ---- marks_<game>.s
    for g in kit:
        i, n = g["id"], index[g["id"]]
        M = [f"@ Generated by tools/gen_games.py: the first and last words of {g['title']}'s block.",
             f'  .section .rodata.np.{n}.{i}.0head,"a"', "  .balign 4", f"  .word {HEAD:#x}",
             f'  .section .rodata.np.{n}.{i}.zztail,"a"', "  .balign 4", f"  .word {TAIL:#x}"]
        open(os.path.join(a.out, f"marks_{i}.s"), "w").write("\n".join(M) + "\n")

    # ---- numplay.ld, and script.json for the website
    head = ["/* Generated by tools/gen_games.py. Script of the final `ld -r` that makes NumPlay.nwa:",
            "   each game becomes one section, so the calculator's installer keeps its code, data image,",
            "   screenshots and marks in one block of flash that uninstalling can wipe. */",
            "SECTIONS {",
            "  .rodata.eadk_app_name : { KEEP(*(.rodata.eadk_app_name)) }",
            "  .rodata.eadk_api_level : { KEEP(*(.rodata.eadk_api_level)) }",
            "  .rodata.eadk_app_icon : { KEEP(*(.rodata.eadk_app_icon)) }",
            "  .rodata.np_games : {",
            "    np_games = .;",
            "    KEEP(*(SORT_BY_NAME(.rodata.np_games.*)))",
            "    np_games_end = .;",
            "  }"]
    def block(i, n, roms=False):
        p = f".rodata.np.{n}.{i}"
        return [f"  {p} : {{",
                f"    np_{i}_begin = .;",
                f"    KEEP(*({p}.0head))",
                f"    *({p})",
                f"    *({p}.init)"] + (
               ["    *(.rodata.np_roms)"] if roms else []) + [
                f"    KEEP(*({p}.shots))",
                f"    KEEP(*({p}.zztail))",
                f"    np_{i}_end = .;",
                "  }"]
    blocks = {g["id"]: block(g["id"], index[g["id"]], g.get("roms")) for g in manifest}
    tail = ["  .text : { *(.text .text.*) }",
            "  .rodata : { *(.rodata .rodata.*) }",
            "  .data : { *(.data .data.*) }",
            "  .bss : { *(.bss .bss.* COMMON) *(.np_arena) }",
            "  /DISCARD/ : { *(.comment) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }",
            "}"]
    D = head + [x for g in games for x in blocks[g["id"]]] + tail
    open(os.path.join(a.out, "numplay.ld"), "w").write("\n".join(D) + "\n")
    # (for the website's own .nwa apps: an app's block, its id @ID@ and index @N@, and the script that makes a big
    # game's .nwa a module's first step, tools/module.ld)
    module_ld = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "module.ld")).read()
    json.dump({"head": "\n".join(head) + "\n", "tail": "\n".join(tail) + "\n",
               "blocks": {i: "\n".join(b) + "\n" for i, b in blocks.items()},
               "app": "\n".join(block("@ID@", "@N@")) + "\n", "module": module_ld},
              open(os.path.join(a.out, "script.json"), "w"), indent=1)
    print(f"games: {len(games)}, arena {arena} bytes, the hungriest game {need}")


if __name__ == "__main__":
    main()
