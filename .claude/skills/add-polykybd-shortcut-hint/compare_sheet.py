#!/usr/bin/env python3
"""Before/after sheet for the OS shortcut-hint icons, at 12x with row numbers.

    python3 -B compare_sheet.py out.png                       # HEAD vs working tree, changed icons
    python3 -B compare_sheet.py out.png --base PolyKybd~1     # any git revision as "before"
    python3 -B compare_sheet.py out.png clip_history emoji    # these icons, changed or not

"before" is tools/hint_icons_stage.py at --base (default HEAD); "after" is the file
in the working tree. Each version is loaded from its own temp file under its own
module name. Never render variants by rewriting one module and reloading it: the
bytecode cache is keyed by (mtime, size), so a same-size edit in the same second
re-runs the old code and every variant comes out identical.

Run with -B so no bytecode is written next to the temp copies. Needs Pillow.
"""
import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont


def repo_root():
    d = os.path.dirname(os.path.abspath(__file__))
    while d != "/" and not os.path.isdir(os.path.join(d, "keyboards", "polykybd")):
        d = os.path.dirname(d)
    if d == "/":
        sys.exit("cannot find the qmk_firmware root (keyboards/polykybd)")
    return d


REL = "keyboards/polykybd/tools/hint_icons_stage.py"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def bits(im):
    im = im.convert("1")
    return [[1 if im.getpixel((x, y)) else 0 for x in range(im.width)] for y in range(im.height)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out")
    ap.add_argument("names", nargs="*", help="icons to show (default: every changed icon)")
    ap.add_argument("--base", default="HEAD", help="git revision for 'before' (default HEAD)")
    ap.add_argument("--scale", type=int, default=12)
    a = ap.parse_args()

    root = repo_root()
    src = subprocess.run(["git", "-C", root, "show", f"{a.base}:{REL}"],
                         capture_output=True, text=True, check=True).stdout
    with tempfile.TemporaryDirectory() as tmp:
        old_path = os.path.join(tmp, "stage_before.py")
        with open(old_path, "w", encoding="utf-8") as f:
            f.write(src)
        old = load(old_path, "stage_before")
        new = load(os.path.join(root, REL), "stage_after")

    names = a.names or [n for n in new.ICONS
                        if n not in old.ICONS or bits(old.render(n)) != bits(new.render(n))]
    if not names:
        print(f"no icon differs from {a.base}")
        return 0

    S, SC = new.S, a.scale
    gutter, gap, label = 30, 24, 26
    cell = S * SC
    W = gutter + 2 * cell + gap
    H = len(names) * (cell + label + 10)
    sheet = Image.new("RGB", (W, H), (24, 24, 28))
    dr = ImageDraw.Draw(sheet)
    f = ImageFont.load_default(size=16)
    fs = ImageFont.load_default(size=10)
    for i, n in enumerate(names):
        y0 = i * (cell + label + 10) + 4
        for r in range(S):
            dr.text((4, y0 + r * SC), str(r), fill=(140, 140, 140), font=fs)
        for col, mod in enumerate((old, new)):
            x0 = gutter + col * (cell + gap)
            if n not in mod.ICONS:
                dr.text((x0, y0), "(absent)", fill=(230, 120, 120), font=f)
                continue
            px = bits(mod.render(n))
            for yy in range(S):
                for xx in range(S):
                    if px[yy][xx]:
                        dr.rectangle([x0 + xx * SC, y0 + yy * SC,
                                      x0 + xx * SC + SC - 2, y0 + yy * SC + SC - 2],
                                     fill=(90, 200, 255))
                    else:
                        dr.point((x0 + xx * SC, y0 + yy * SC), fill=(60, 60, 66))
        dr.text((gutter, y0 + cell + 4), f"{n}: {a.base} | working tree",
                fill=(230, 230, 230), font=f)
    sheet.save(a.out)
    print(f"{len(names)} icon(s): {', '.join(names)} -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
