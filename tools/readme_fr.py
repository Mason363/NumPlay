#!/usr/bin/env python3
"""Keeps README.fr.md, the README in French, the translation of README.md as it is now.

README.fr.md's first line names the README.md it translates (its git blob hash). This fails when README.md has
changed since (CI runs it: .github/workflows/readme.yml), and checks the two still hold the same things: the same
pictures in the same order, the same links, the same numbers, as many headings of each level, and links within the
page that lead to one of its headings.

  python3 tools/readme_fr.py            check
  python3 tools/readme_fr.py --diff     what README.md changed since the translation, to translate it
  python3 tools/readme_fr.py --stamp    once README.fr.md says it all again: record the README.md it translates

Python 3 alone; --diff needs git."""
import argparse
import collections
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
EN, FR = os.path.join(ROOT, "README.md"), os.path.join(ROOT, "README.fr.md")
STAMP = re.compile(r"^<!-- README\.md translated: ([0-9a-f]{40}) -->\n")
# (each other's name, the switch between them)
SWITCH = {"README.md", "README.fr.md"}


def blob(path):
    """git's hash of the file (git hash-object), so --diff can find it in the history"""
    d = open(path, "rb").read()
    return hashlib.sha1(b"blob %d\0" % len(d) + d).hexdigest()


def slug(heading):
    """the anchor GitHub gives a heading"""
    h = re.sub(r"[^\w\- ]", "", heading.strip().lower())
    return h.replace(" ", "-")


def parts(text):
    pictures = re.findall(r'<img[^>]*\bsrc="(docs/[^"]+)"', text) + re.findall(r'!\[[^\]]*\]\((docs/[^)]+)\)', text)
    links = re.findall(r'href="([^"#][^"]*)"', text) + re.findall(r'\]\(([^)#][^)]*)\)', text)
    links = [u for u in links if u not in SWITCH and not u.startswith("docs/media/")]
    anchors = re.findall(r'href="#([^"]+)"', text) + re.findall(r'\]\(#([^)]+)\)', text)
    headings = re.findall(r"^(#{1,6}) (.+)$", text, re.M)
    words = re.sub(r'(?:href|src)="[^"]*"|\]\([^)]*\)|https?://\S+|img\.shields\.io/\S+', " ", text)
    numbers = re.findall(r"\d+", words)
    return pictures, links, anchors, headings, numbers


def diff_lists(name, a, b):
    if a == b:
        return []
    ca, cb = collections.Counter(a), collections.Counter(b)
    only_en, only_fr = sorted((ca - cb).elements()), sorted((cb - ca).elements())
    out = [f"{name} differ"]
    if only_en:
        out.append(f"  only in README.md: {', '.join(only_en[:12])}")
    if only_fr:
        out.append(f"  only in README.fr.md: {', '.join(only_fr[:12])}")
    if not only_en and not only_fr:
        out.append("  (the same ones, in another order)")
    return out


def check():
    en, fr = open(EN, encoding="utf-8").read(), open(FR, encoding="utf-8").read()
    errors = []
    m = STAMP.match(fr)
    if not m:
        errors.append("README.fr.md doesn't start with the README.md it translates: run python3 tools/readme_fr.py --stamp")
    elif m.group(1) != blob(EN):
        errors.append("README.md changed since README.fr.md was translated from it: translate what changed "
                      "(python3 tools/readme_fr.py --diff), then run python3 tools/readme_fr.py --stamp")
    pe, le, ae, he, ne = parts(en)
    pf, lf, af, hf, nf = parts(STAMP.sub("", fr))
    errors += diff_lists("Pictures", pe, pf)
    errors += diff_lists("Links", sorted(le), sorted(lf))
    errors += diff_lists("Numbers", sorted(ne), sorted(nf))
    if [h for h, _ in he] != [h for h, _ in hf]:
        errors.append(f"Headings differ: README.md has {len(he)}, README.fr.md {len(hf)} (or not of the same levels)")
    for name, anchors, heads in (("README.md", ae, he), ("README.fr.md", af, hf)):
        lost = sorted(set(anchors) - {slug(t) for _, t in heads})
        if lost:
            errors.append(f"{name}: links to #{', #'.join(lost)}, which none of its headings is")
    if errors:
        print("\n".join(errors))
        return 1
    print("README.fr.md translates README.md as it is now")
    return 0


def stamp():
    fr = open(FR, encoding="utf-8").read()
    fr = f"<!-- README.md translated: {blob(EN)} -->\n" + STAMP.sub("", fr)
    open(FR, "w", encoding="utf-8").write(fr)
    print("README.fr.md: stamped with README.md as it is now")
    return check()


def diff():
    m = STAMP.match(open(FR, encoding="utf-8").read())
    if not m:
        sys.exit("README.fr.md has no stamp: it can't tell what changed")
    old = subprocess.run(["git", "cat-file", "-p", m.group(1)], cwd=ROOT, capture_output=True)
    if old.returncode:
        sys.exit("The README.md it was translated from isn't in this repository's history (fetch it, or translate again)")
    with tempfile.NamedTemporaryFile("wb", suffix=".md", delete=False) as t:
        t.write(old.stdout)
    try:
        return subprocess.run(["git", "--no-pager", "diff", "--no-index", "--", t.name, EN], cwd=ROOT).returncode and 0
    finally:
        os.unlink(t.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--stamp", action="store_true", help="record the README.md README.fr.md now translates")
    g.add_argument("--diff", action="store_true", help="what README.md changed since README.fr.md's translation")
    a = ap.parse_args()
    return stamp() if a.stamp else diff() if a.diff else check()


if __name__ == "__main__":
    sys.exit(main())
