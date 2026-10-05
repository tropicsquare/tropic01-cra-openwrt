#!/usr/bin/env python3
"""Check that every relative link in the Markdown files points at something that exists.

Also checks `#anchor` fragments against the headings of the target file (GitHub-style slugs).
External (http/https/mailto) links are not fetched. Exit status is non-zero if anything is broken.

    python3 scripts/check-doc-links.py
"""
import os
import re
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True).stdout.strip()
LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE = re.compile(r"^(```|~~~)")


def slug(heading):
    h = re.sub(r"[`*_]", "", heading.strip().lower())
    h = re.sub(r"[^\w\- ]", "", h)
    return h.replace(" ", "-")


def anchors(path):
    out, fenced = set(), False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if FENCE.match(line):
                fenced = not fenced
            elif not fenced and line.startswith("#"):
                out.add(slug(line.lstrip("#")))
    return out


def main():
    files = subprocess.run(["git", "ls-files", "*.md"], capture_output=True, text=True, cwd=ROOT, check=True).stdout.split()
    bad = 0
    for md in files:
        base = os.path.dirname(os.path.join(ROOT, md))
        fenced = False
        with open(os.path.join(ROOT, md), encoding="utf-8") as f:
            for n, line in enumerate(f, 1):
                if FENCE.match(line):
                    fenced = not fenced
                    continue
                if fenced:
                    continue
                for target in LINK.findall(line):
                    if re.match(r"^(https?:|mailto:)", target):
                        continue
                    path, _, frag = target.partition("#")
                    dest = os.path.normpath(os.path.join(base, path)) if path else os.path.join(ROOT, md)
                    if not os.path.exists(dest):
                        print("%s:%d: missing file: %s" % (md, n, target))
                        bad += 1
                    elif frag and os.path.isfile(dest) and dest.endswith(".md") and frag not in anchors(dest):
                        print("%s:%d: no such heading in %s: #%s" % (md, n, os.path.relpath(dest, ROOT), frag))
                        bad += 1
    print("%d Markdown files checked, %d broken links" % (len(files), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
