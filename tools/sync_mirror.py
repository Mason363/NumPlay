#!/usr/bin/env python3
"""A game of NumPlay as a repository of its own: Champion Island and NumBlocks.

The games live in NumPlay, next to the others. Each one's own repository is a
mirror: this script builds the tree that goes there, and
.github/workflows/mirror-games.yml runs it on every push to main, so the mirrors
follow NumPlay and are never edited by hand.

A mirror's root is the game's folder. What the game shares with NumPlay comes
along (the two Epsilon headers, the license, a font's license, a README's
animation) and the few paths that point into NumPlay are rewritten for the new
layout. Each rewrite has to match exactly once, so a change in NumPlay that
breaks one fails the sync instead of shipping a broken mirror.

A mirror that also has a release file (NumBlocks.nwa) gets NumPlay's releases:
its tree carries .github/workflows/release.yml (from tools/mirror/release.yml),
and --tag puts a NumPlay release's tag on the mirror's commit for it, which
makes that workflow copy the file from the NumPlay release into one of the
mirror's own.

Usage: sync_mirror.py GAME DEST [--rev REV] [--commit | --tag TAG]
  GAME      championisland or numblocks
  DEST      the mirror's checkout; everything in it but .git is replaced
  --rev     the NumPlay commit to build from (default HEAD): the game's files
            come from that commit, not from the working tree
  --commit  commit the result in DEST if it changed, as the author of the last
            NumPlay commit that touched the game
  --tag     instead of changing DEST, put TAG on its commit that holds exactly
            what REV gives (it must be there already: pushed by a sync)
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
REPO = "https://github.com/Mason363/NumPlay"
RELEASE_TEMPLATE = os.path.join(ROOT, "tools", "mirror", "release.yml")

# what NumPlay's own .gitignore covers for a game that the game's doesn't
GITIGNORE = ["output/", "build/", "__pycache__/", "*.pyc", ".DS_Store"]
EPSILON_REWRITES = [
    ("src/plat_eadk.c", '"../../common/epsilon_app.h"', '"../common/epsilon_app.h"'),
    ("src/plat_eadk.c", '"../../common/epsilon_files.h"', '"../common/epsilon_files.h"'),
]
EPSILON_HEADERS = {
    "games/common/epsilon_app.h": "common/epsilon_app.h",
    "games/common/epsilon_files.h": "common/epsilon_files.h",
}

MIRRORS = {
    "championisland": {
        "repo": "Mason363/NumWorks-Champion-Island",
        "game": "games/championisland",
        # NumPlay's files the game needs, and where they go in the mirror
        "shared": dict(EPSILON_HEADERS, **{
            "LICENSE": "LICENSE",
            "LICENSES/PixelMplus.txt": "LICENSES/PixelMplus.txt",
            "docs/media/championisland.gif": "docs/championisland.gif",
        }),
        # paths that point into NumPlay: (file, old, new), each found exactly once
        "rewrites": EPSILON_REWRITES + [
            ("Makefile", "../common/epsilon_app.h ../common/epsilon_files.h", "common/epsilon_app.h common/epsilon_files.h"),
            ("README.md", "](../../README.md)", "](%s#readme)" % REPO),
            ("README.md", 'src="../../docs/media/championisland.gif"', 'src="docs/championisland.gif"'),
            ("README.md", "](../../LICENSES/PixelMplus.txt)", "](LICENSES/PixelMplus.txt)"),
        ],
        "gitignore": GITIGNORE,
        "release": None,
    },
    "numblocks": {
        "repo": "Mason363/NumBlocks",
        "game": "games/numblocks",
        "shared": dict(EPSILON_HEADERS, LICENSE="LICENSE"),
        "rewrites": EPSILON_REWRITES + [
            ("Makefile", "../common/epsilon_app.h ../common/epsilon_files.h", "common/epsilon_app.h common/epsilon_files.h"),
            # its releases are NumPlay's: the same file, in the mirror's own
            ("README.md", "](%s/releases/latest)" % REPO, "](https://github.com/Mason363/NumBlocks/releases/latest)"),
        ],
        "gitignore": ["*.pyc", ".DS_Store"],
        "release": "NumBlocks.nwa",
    },
}


def git(*args, cwd=ROOT, env=None, check=True, **kw):
    return subprocess.run(["git", "-C", cwd, *args], check=check, env=env, **kw)


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def inputs(m):
    """NumPlay's paths the mirror is made of."""
    return [m["game"], *m["shared"]]


def note(m):
    text = ("> This repository mirrors the `%s` folder of NumPlay and is updated automatically: "
            "changes go to NumPlay, and edits made here are overwritten." % m["game"])
    if m["release"]:
        text += " Its releases are NumPlay's: the same `%s`, from the NumPlay release of that version." % m["release"]
    return text


def export(m, rev, dest):
    """The game and what it shares, as of rev, laid out as in NumPlay."""
    try:
        tar = git("archive", "--format=tar", rev, "--", *inputs(m), stdout=subprocess.PIPE).stdout
    except subprocess.CalledProcessError:
        sys.exit("sync_mirror: %s is not all in NumPlay at %s" % (", ".join(inputs(m)), rev))
    subprocess.run(["tar", "-x", "-C", dest], input=tar, check=True)


def build(m, rev, out):
    """The mirror's tree, as of rev, in the empty folder out."""
    with tempfile.TemporaryDirectory() as tmp:
        export(m, rev, tmp)
        shutil.copytree(os.path.join(tmp, m["game"]), out, dirs_exist_ok=True)
        for src, dst in m["shared"].items():
            os.makedirs(os.path.dirname(os.path.join(out, dst)), exist_ok=True)
            shutil.copy2(os.path.join(tmp, src), os.path.join(out, dst))

    for name, old, new in m["rewrites"]:
        path = os.path.join(out, name)
        text = read(path)
        if text.count(old) != 1:
            sys.exit("sync_mirror: %s has %d matches of %r, expected 1: update REWRITES" % (name, text.count(old), old))
        write(path, text.replace(old, new))

    # the note goes under the README's first line: a quote, or the title
    path = os.path.join(out, "README.md")
    first, sep, rest = read(path).partition("\n")
    if first.startswith("> "):
        write(path, first + "\n>\n" + note(m) + "\n" + rest)
    elif first.startswith("# "):
        write(path, first + "\n\n" + note(m) + "\n" + rest)
    else:
        sys.exit("sync_mirror: README.md starts with neither a quote nor a title: update note()")

    path = os.path.join(out, ".gitignore")
    have = [line for line in (read(path).split("\n") if os.path.exists(path) else []) if line]
    write(path, "\n".join(have + [line for line in m["gitignore"] if line not in have]) + "\n")

    if m["release"]:
        os.makedirs(os.path.join(out, ".github", "workflows"))
        write(os.path.join(out, ".github", "workflows", "release.yml"),
              read(RELEASE_TEMPLATE).replace("{{ASSET}}", m["release"]).replace("{{NAME}}", m["release"].split(".")[0]))

    check_includes(out)


def check_includes(root):
    """Every quoted #include in the mirror finds its file (the ARM build isn't run here)."""
    missing = []
    for base, dirs, files in os.walk(root):
        for name in files:
            if not name.endswith((".c", ".h")):
                continue
            for inc in re.findall(r'^\s*#\s*include\s+"([^"]+)"', read(os.path.join(base, name)), re.M):
                if not any(os.path.exists(os.path.join(where, inc)) for where in (base, os.path.join(root, "src"))):
                    missing.append("%s: %s" % (os.path.relpath(os.path.join(base, name), root), inc))
    if missing:
        sys.exit("sync_mirror: includes that no longer resolve in the mirror:\n  " + "\n  ".join(missing))


def replace(dest, out):
    """Make dest hold exactly out, keeping its .git."""
    os.makedirs(dest, exist_ok=True)
    for name in os.listdir(dest):
        if name != ".git":
            path = os.path.join(dest, name)
            shutil.rmtree(path) if os.path.isdir(path) and not os.path.islink(path) else os.remove(path)
    shutil.copytree(out, dest, dirs_exist_ok=True)


def need_checkout(dest):
    # without its own .git, git would climb to the repository around dest (NumPlay's checkout, in CI)
    if not os.path.isdir(os.path.join(dest, ".git")):
        sys.exit("sync_mirror: %s is not a git checkout of the mirror" % dest)


def commit(m, dest, rev):
    """Commit dest if it changed: the last NumPlay commit that touched the game gives the author and the title."""
    need_checkout(dest)
    git("add", "-A", cwd=dest)
    if git("diff", "--cached", "--quiet", cwd=dest, check=False).returncode == 0:
        print("sync_mirror: the mirror is already up to date")
        return
    paths = inputs(m) + ([os.path.relpath(RELEASE_TEMPLATE, ROOT)] if m["release"] else [])
    name, email, date, subject = git(
        "log", "-1", "--format=%an%n%ae%n%aI%n%s", rev, "--", *paths,
        stdout=subprocess.PIPE, text=True).stdout.strip().split("\n", 3)
    sha = git("rev-parse", rev, stdout=subprocess.PIPE, text=True).stdout.strip()
    env = dict(os.environ, GIT_AUTHOR_NAME=name, GIT_AUTHOR_EMAIL=email, GIT_AUTHOR_DATE=date)
    git("commit", "-q", "-m", "%s\n\nSynced from %s/commit/%s" % (subject, REPO, sha), cwd=dest, env=env)
    print("sync_mirror: committed %s" % subject)


def tag(dest, out, name):
    """Put the tag on the mirror's newest commit whose tree is out's: what the mirror was at that NumPlay version."""
    need_checkout(dest)
    gitdir = os.path.join(dest, ".git")
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, GIT_INDEX_FILE=os.path.join(tmp, "index"))
        subprocess.run(["git", "--git-dir", gitdir, "--work-tree", out, "add", "-A"], check=True, env=env)
        tree = subprocess.run(["git", "--git-dir", gitdir, "write-tree"], check=True, env=env,
                              stdout=subprocess.PIPE, text=True).stdout.strip()
    log = git("log", "--format=%H %T", "origin/main", cwd=dest, stdout=subprocess.PIPE, text=True).stdout.split("\n")
    found = [line.split()[0] for line in log if line.endswith(" " + tree)]
    if not found:
        sys.exit("sync_mirror: no commit of the mirror holds the tree of this NumPlay version: "
                 "has its push to main been synced?")
    git("tag", name, found[0], cwd=dest)
    print("sync_mirror: %s is %s" % (name, found[0][:7]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game", choices=sorted(MIRRORS))
    ap.add_argument("dest")
    ap.add_argument("--rev", default="HEAD")
    what = ap.add_mutually_exclusive_group()
    what.add_argument("--commit", action="store_true")
    what.add_argument("--tag")
    args = ap.parse_args()
    m = MIRRORS[args.game]
    if args.tag and not m["release"]:
        sys.exit("sync_mirror: %s has no release file: nothing to tag" % args.game)

    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "mirror")
        build(m, args.rev, out)
        if args.tag:
            tag(args.dest, out, args.tag)
        else:
            replace(args.dest, out)
    if args.commit:
        commit(m, args.dest, args.rev)


if __name__ == "__main__":
    main()
