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
comparison. Give every variant its own NAME and render with -B: the module is loaded
under a unique name each run, so a same-size edit in the same second cannot re-run
old bytecode (the reload trap in docs/display/hint-icons.md). When a candidate is
picked, copy it into hint_icons_stage.py and use compare_sheet.py from then on.
"""
import argparse
import importlib.util
import os
import sys
import time

from PIL import Image, ImageDraw


def repo_root():
    d = os.path.dirname(os.path.abspath(__file__))
    while d != "/" and not os.path.isdir(os.path.join(d, "keyboards", "polykybd")):
        d = os.path.dirname(d)
    if d == "/":
        sys.exit("cannot find the qmk_firmware root (keyboards/polykybd)")
    return d


TOOLS = os.path.join(repo_root(), "keyboards", "polykybd", "dev", "tools")
sys.path.insert(0, TOOLS)
import hint_icons_stage as stage  # noqa: E402


def load_scratch(path):
    name = "hint_variants_%d" % time.time_ns()
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
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
    S, k, pad = stage.S, a.scale, 4
    sheet = Image.new("RGB", (len(names) * (S + pad) * k, (S + 6) * k), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, n in enumerate(names):
        if n.startswith("stage:"):
            fn = stage.ICONS.get(n[6:])
        else:
            fn = variants.get(n)
        if fn is None:
            sys.exit(f"unknown icon: {n}")
        I = stage.Icon()
        fn(I)
        x = i * (S + pad) * k
        sheet.paste(I.im.convert("RGB").resize((S * k, S * k), Image.NEAREST), (x, 0))
        dr.text((x + 4, S * k + 8), n, fill=(255, 255, 255))
    sheet.save(a.out)
    print(f"{len(names)} icon(s) -> {a.out}")


if __name__ == "__main__":
    main()
