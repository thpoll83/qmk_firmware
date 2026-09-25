#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Preview the tutorial's language-name screens (TUT_LANG_NAME) as the keys draw them.

For each item: the LEFT half's display rows 1-2 (Latin name) and the RIGHT half's
(native name), 7 keys per row, using the firmware's layout rule — up to 7 units on row 2,
more split over rows 1 and 2 with the larger half on top, each row centred. Characters are
drawn with the host preview renderer (PolyKybdHost/tools/oled_preview.py); pre-rendered
names are read straight out of anim/tutorial_names_gen.h, so the sheet shows the bytes the
firmware carries.

⚠️ ITEMS below is a replica of s_tut_preview_all[] in poly_keymap.c — change both.

    python3 tools/tutorial_name_sheet.py --out sheet.png
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KB = os.path.abspath(os.path.join(HERE, ".."))
REPO = os.path.abspath(os.path.join(KB, "..", ".."))
sys.path.insert(0, os.environ.get("POLYHOST_TOOLS",
                                  os.path.join(REPO, "..", "PolyKybdHost", "tools")))
import oled_preview as op  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

W, H, KEYS = 72, 40, 7
TIER = 0xF0000
SCRIPT_BASE = {"TENGWAR": 0xE800, "RUNES": 0xE840, "AUREBESH": 0xE880, "BRAILLE": 0xEA40}

# (Latin name, native units | ("strip", SYM) | ("script", NAME), rtl)
ITEMS = [
    ("Greek",    "ΕΛΛΗΝΙΚΑ", False),
    ("Arabic",   ("strip", "AR"), False),
    ("Hebrew",   "עברית", True),
    ("Hindi",    ("strip", "HI"), False),
    ("Thai",     "ภาษาไทย", False),
    ("Japanese", ("strip", "JA"), False),
    ("Korean",   ("strip", "KO"), False),
    ("Elvish",   ("script", "TENGWAR"), False),
    ("Runes",    ("script", "RUNES"), False),
    ("Aurebesh", ("script", "AUREBESH"), False),
    ("Braille",  ("script", "BRAILLE"), False),
]
# The closing TUT_LANG_MORE screen: (left top, left bottom, right top, right bottom).
# The numbers are NUM_LANG and GLYPH_SCRIPT_COUNT - 1, read out of the headers below.
MORE_WORDS = ("LAYOUTS", "SCRIPTS")


def load_strips():
    src = open(os.path.join(KB, "anim", "tutorial_names_gen.h"), encoding="utf-8").read()
    out = {}
    for sym, body in re.findall(r"TUT_NAME_TILES_(\w+)\[\d+ \* 360\] = \{(.*?)\};", src, re.S):
        data = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", body)]
        out[sym] = [data[i:i + 360] for i in range(0, len(data), 360)]
    return out


def enum_counts():
    lang = open(os.path.join(KB, "lang", "lang_lut.h"), encoding="utf-8").read()
    body = lang[lang.rfind("enum", 0, lang.index("NUM_LANG };")):lang.index("NUM_LANG };")]
    body = re.sub(r"//.*|/\*.*?\*/", "", body, flags=re.S)
    n_lang = len(re.findall(r"\bLANG_[A-Z0-9_]+\b", body))
    st = open(os.path.join(KB, "state.h"), encoding="utf-8").read()
    enum = st[st.index("enum poly_glyph_script"):st.index("GLYPH_SCRIPT_COUNT")]
    n_scripts = len(re.findall(r"\bGLYPH_[A-Z0-9_]+\s*=", enum)) - 1   # minus GLYPH_STD
    return n_lang, n_scripts


def layout(n, side, top=None):
    """(row, position-in-row) per unit, rows 0 = display row 1, 1 = display row 2.
    Centred per row; an odd spare key goes to the OUTER edge so the word leans toward the
    split (tut_name_slot_unit): the right end of the left half, the left of the right."""
    if top is None:
        top = (n + 1) // 2 if n > KEYS else 0
    slots = []
    for r, (first, count) in enumerate(((0, top), (top, n - top))):
        slack = KEYS - count
        start = (slack + 1) // 2 if side == 0 else slack // 2
        slots += [(r, start + k) for k in range(count)]
    return slots


def heavy_tile(heavy, cp):
    """A character in the heavy splash face, centred from its box (tut_draw_heavy)."""
    R = op.Renderer([heavy])
    return char_tile(R, cp)


def char_tile(R, cp):
    im = Image.new("L", (W, H), 0)
    cps = [TIER + cp] if 65 <= cp <= 90 and R._font(TIER + cp) is not None else [cp]
    if R._font(cps[0]) is None:
        ImageDraw.Draw(im).rectangle([0, 0, W - 1, H - 1], outline=160)   # missing glyph
        return im
    ink = set()
    R.draw(lambda x, y: ink.add((x, y)), cps, op.BUFFER_X, 30)
    if ink:
        xs = [x for x, _ in ink]
        ys = [y for _, y in ink]
        dx = (W - (max(xs) - min(xs) + 1)) // 2 - min(xs)
        dy = (H - (max(ys) - min(ys) + 1)) // 2 - min(ys)
        for x, y in ink:
            if 0 <= x + dx < W and 0 <= y + dy < H:
                im.putpixel((x + dx, y + dy), 255)
    return im


def bytes_tile(data):
    im = Image.new("L", (W, H), 0)
    for y in range(H):
        for x in range(W):
            if data[y * 9 + (x >> 3)] & (0x80 >> (x & 7)):
                im.putpixel((x, y), 255)
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tutorial_names.png")
    args = ap.parse_args()
    R = op.load_renderer(os.path.join(KB, "base", "fonts"))
    op.OVERSHOOT = 24
    strips = load_strips()

    G, GAP, LABEL = 6, 36, 90
    bw = LABEL + 2 * KEYS * (W + G) + GAP
    bh = 2 * (H + G) + 14
    sheet = Image.new("L", (bw, (len(ITEMS) + 2) * bh + 10), 30)
    d = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for i, (latin, native, rtl) in enumerate(ITEMS):
        y0 = 10 + i * bh
        d.text((6, y0 + H // 2), latin, fill=255, font=font)
        left = [char_tile(R, ord(c)) for c in latin.upper()]
        if isinstance(native, tuple) and native[0] == "strip":
            right = [bytes_tile(t) for t in strips[native[1]]]
        elif isinstance(native, tuple):
            base = SCRIPT_BASE[native[1]]
            right = [char_tile(R, base + ord(c) - 65) for c in latin.upper()]
        else:
            units = list(native)[::-1] if rtl else list(native)
            right = [char_tile(R, ord(c)) for c in units]
        # The halves swap from item to item (tut_native_on(): native on the RIGHT for an
        # even table row, on the LEFT for an odd one).
        latin_tiles, native_tiles = left, right
        pairs = ((0, latin_tiles), (1, native_tiles)) if i % 2 == 0 else \
                ((0, native_tiles), (1, latin_tiles))
        for side, tiles in pairs:
            x_side = LABEL + side * (KEYS * (W + G) + GAP)
            for (r, pos), t in zip(layout(len(tiles), side), tiles):
                sheet.paste(t, (x_side + pos * (W + G), y0 + r * (H + G)))
    # The two closing screens (TUT_LANG_MORE, TUT_LANG_MORE2): the number on the left
    # half, the word on the right, both on the middle row, in the heavy splash face.
    import gfx_font  # noqa: E402  (the host tool's own GFX header parser)
    heavy = gfx_font.load_ui_font(os.path.join(KB, "base", "fonts"), "FreeSansBold24pt7b.h",
                                  "FreeSansBold24pt7b")
    n_lang, n_scripts = enum_counts()
    for k, (num, word) in enumerate(((n_lang, MORE_WORDS[0]), (n_scripts, MORE_WORDS[1]))):
        y0 = 10 + (len(ITEMS) + k) * bh
        d.text((6, y0 + H // 2), f"(more {k + 1}/2)", fill=255, font=font)
        for side, text in ((0, str(num)), (1, word)):
            tiles = [heavy_tile(heavy, ord(c)) for c in text]
            x_side = LABEL + side * (KEYS * (W + G) + GAP)
            for (r, pos), t in zip(layout(len(text), side, top=0), tiles):
                sheet.paste(t, (x_side + pos * (W + G), y0 + r * (H + G)))
    sheet.save(args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
