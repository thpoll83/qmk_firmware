#!/usr/bin/env python3
"""Candidate sheet for NEW or redrawn shortcut-hint icons, at 8x with labels.

    python3 -B variant_sheet.py out.png scratch.py                  # every candidate
    python3 -B variant_sheet.py out.png scratch.py A_snip B_snip    # these candidates
    python3 -B variant_sheet.py out.png scratch.py snip_v1 stage:snip

`scratch.py` is a throwaway file OUTSIDE the repo (e.g. in the scratchpad) that
defines the candidates in a dict named VARIANTS, drawn with the style-D primitives:

    from hint_icons_stage import ICONS as STAGE      # the shipped drawings
    VARIANTS = {}
    def variant(name):
        def deco(fn): VARIANTS[name] = fn; return fn
        return deco

    @variant("A_snip_clip")
    def _(I):
        I.thin(3, 4, 30, 33)
        STAGE["snip"](I)                              # reuse a shipped icon

`stage:<name>` puts a shipped icon on the sheet beside the candidates, for
comparison. Give every variant its own NAME. Both the scratch file and
hint_icons_stage.py are compiled from their source text on every run, never from a
.pyc: Python's bytecode cache is keyed by the source path and (mtime, size), so a
same-size edit in the same second would otherwise render the old drawing (the
reload trap in docs/display/hint-icons.md). When a candidate is picked, copy it into
hint_icons_stage.py and use compare_sheet.py from then on.
"""
import argparse
import os
import sys
import types

from PIL import Image, ImageDraw


def repo_root():
    d = os.path.dirname(os.path.abspath(__file__))
    while d != "/" and not os.path.isdir(os.path.join(d, "keyboards", "polykybd")):
        d = os.path.dirname(d)
    if d == "/":
        sys.exit("cannot find the qmk_firmware root (keyboards/polykybd)")
    return d


sys.dont_write_bytecode = True


def load_source(path, name):
    """Compile `path` from its text, with no bytecode cache read or written."""
    mod = types.ModuleType(name)
    mod.__file__ = path
    sys.modules[name] = mod
    with open(path, encoding="utf-8") as f:
        exec(compile(f.read(), path, "exec"), mod.__dict__)
    return mod


TOOLS = os.path.join(repo_root(), "keyboards", "polykybd", "dev", "tools")
# Registered under its real name, so `from hint_icons_stage import ...` in the
# scratch file gets this fresh copy rather than an import from a .pyc.
stage = load_source(os.path.join(TOOLS, "hint_icons_stage.py"), "hint_icons_stage")


def load_scratch(path):
    mod = load_source(path, "hint_variants")
    if not isinstance(getattr(mod, "VARIANTS", None), dict):
        sys.exit(f"{path} defines no VARIANTS dict")
    return mod.VARIANTS


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("scratch")
    ap.add_argument("names", nargs="*", help="candidates or stage:<name>; default all candidates")
    ap.add_argument("--scale", type=int, default=8)
    a = ap.parse_args()

    variants = load_scratch(a.scratch)
    names = a.names or list(variants)
    S, k, gap, label_h = stage.S, a.scale, 16, 24
    measure = ImageDraw.Draw(Image.new("RGB", (1, 1)))
    # Each cell is as wide as its icon or its label, whichever is wider, and the
    # labels get their own band below the icons, so no scale clips or overlaps them.
    widths = [max(S * k, int(measure.textlength(n)) + 8) for n in names]
    sheet = Image.new("RGB", (sum(widths) + gap * (len(names) - 1), S * k + label_h), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    x = 0
    for n, w in zip(names, widths):
        if n.startswith("stage:"):
            fn = stage.ICONS.get(n[6:])
        else:
            fn = variants.get(n)
        if fn is None:
            sys.exit(f"unknown icon: {n}")
        I = stage.Icon()
        fn(I)
        sheet.paste(I.im.convert("RGB").resize((S * k, S * k), Image.NEAREST), (x, 0))
        dr.text((x + 4, S * k + 6), n, fill=(255, 255, 255))
        x += w + gap
    sheet.save(a.out)
    print(f"{len(names)} icon(s) -> {a.out}")


if __name__ == "__main__":
    main()
