#!/usr/bin/env python3
"""Draw the OS shortcut-hint icon set and write it as a font-pack font.

One family for every hint in hints/os_hints.c: a 34x34 grid, 2 px strokes,
outline = the object, solid = the part that acts, and a solid disc in the
bottom-right corner for a modifier (+ - x arrows i refresh clock record).
Icons are AUTHORED on a 28 grid and drawn at 34 (ScaledDraw): coordinates
scale, strokes and thin bars keep their authored 2 px.

The glyphs form their own GFXfont, PolyHintIcons, in base/fonts/hint_icons.h at
U+100026.., the slots right after the resident IconsFont (which ends at
U+100025). It is NOT compiled into the firmware: fonts.yaml lists it under
index.pack_extra_fonts and in the `symbol` bundle, so it ships in symbol.plyf and
costs no firmware flash. It is the last font in the global order and a pack_extra
font, so its gidx is pinned and adding it moves no other font. With no pack
flashed the firmware shows no hint at all (keycode_to_disp_overlay checks the
glyph). After a change here, reship the symbol bundle with the
reship-fontpack-bundle skill. Each glyph carries the keycap placement in its own
metrics (xOffset 36, top at panel row 3) and the font copies IconsFont's yAdvance
40, so a hint string is the bare glyph with no leading spaces.

    python3 tools/hint_icons.py            # rewrite hint_icons.h
    python3 tools/hint_icons.py --check    # exit 1 if a header is stale
    python3 tools/hint_icons.py --sheet out.png   # contact sheet, 4x
    python3 tools/hint_icons.py --style d  # switch IconsFont to another style

Five styles draw the same actions on the same grid: a (this file, outline),
b (hint_icons_solid.py), c (hint_icons_bold.py), d (hint_icons_stage.py,
"stage and actor") and e (hint_icons_retro.py, 80s home computer). Slots and ICON_HINT_* names always follow this file's
ICONS order, so a style switch changes pixels only. The header records which
style it carries, and a plain run or --check keeps that style.

RETIRED icons stay drawn in every style but are not written to the font. They
are the app-shortcut hints (copy, save, undo, ...) and F2/F5 (rename, refresh),
which the app overlays draw now; os_hints.c has no caller for them. Retiring an
icon renumbers every slot after it. Nothing outside the firmware uses these
codepoints, but the ICON_HINT_* block in lang/named_glyphs.h must be re-pasted
from this tool's order (--macros prints it).

Needs Pillow. Run from anywhere; paths are derived from this file.
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageDraw

DESIGN = 28          # icons are authored on a 28 grid ...
S = 34               # ... and drawn on a 34 grid: coordinates scale, strokes stay 2 px
K = S / DESIGN


def _s(v):
    return int(round(v * K))


class ScaledDraw:
    """ImageDraw proxy: scales every coordinate by K, keeps stroke widths, and keeps
    a filled bar <= 3 px thick at its authored thickness so 2 px lines stay 2 px."""

    def __init__(self, d):
        self.d = d

    @staticmethod
    def _pts(xy):
        return [(_s(x), _s(y)) for (x, y) in (xy if isinstance(xy[0], tuple) else zip(xy[0::2], xy[1::2]))]

    def rectangle(self, b, fill=None, outline=None, width=1):
        x0, y0, x1, y1 = b
        X0, Y0 = _s(x0), _s(y0)
        X1 = X0 + (x1 - x0) if x1 - x0 <= 2 else _s(x1)
        Y1 = Y0 + (y1 - y0) if y1 - y0 <= 2 else _s(y1)
        self.d.rectangle([X0, Y0, X1, Y1], fill=fill, outline=outline, width=width)

    def line(self, xy, fill=None, width=1):
        self.d.line(self._pts(xy), fill=fill, width=width)

    def polygon(self, xy, fill=None, outline=None):
        self.d.polygon(self._pts(xy), fill=fill, outline=outline)

    def _box(self, b):
        x0, y0, x1, y1 = b
        return [_s(x0), _s(y0), _s(x1), _s(y1)]

    def ellipse(self, b, fill=None, outline=None, width=1):
        self.d.ellipse(self._box(b), fill=fill, outline=outline, width=width)

    def arc(self, b, a0, a1, fill=None, width=1):
        self.d.arc(self._box(b), a0, a1, fill=fill, width=width)

    def pieslice(self, b, a0, a1, fill=None):
        self.d.pieslice(self._box(b), a0, a1, fill=fill)

    def rounded_rectangle(self, b, radius=0, fill=None, outline=None, width=1):
        self.d.rounded_rectangle(self._box(b), radius=_s(radius), fill=fill, outline=outline, width=width)


class Icon:
    def __init__(self):
        self.im = Image.new("1", (S, S), 0)
        self.d = ScaledDraw(ImageDraw.Draw(self.im))
        self.n = ImageDraw.Draw(self.im)   # NATIVE 34-grid drawing, no scaling

    # ---- native 34-grid helpers -------------------------------------------------
    # Corner brackets, dashed frames and the badge are drawn on the real grid: scaled
    # from 28, their 2 px pieces land on uneven pixels and the corners come out
    # lopsided (fullscreen, snip) and the badge symbols lose their shape.
    def NF(self, x0, y0, x1, y1, c=1):
        self.n.rectangle([x0, y0, x1, y1], fill=c)

    def brackets(self, arm):
        """Four 2 px corner brackets on rows/cols 1..32, `arm` px long, mirror-exact."""
        lo, hi = 1, S - 2
        for x in (lo, hi - arm + 1):
            for y in (lo, hi - 1):
                self.NF(x, y, x + arm - 1, y + 1)
        for x in (lo, hi - 1):
            for y in (lo, hi - arm + 1):
                self.NF(x, y, x + 1, y + arm - 1)

    def dashed_frame(self):
        """2 px dashed frame on 1..32: 4 px corners and dashes, 3 px gaps, symmetric."""
        lo, hi = 1, S - 2
        runs = [(1, 4), (8, 11), (15, 18), (22, 25), (29, 32)]
        for a, b in runs:
            self.NF(a, lo, b, lo + 1); self.NF(a, hi - 1, b, hi)
            self.NF(lo, a, lo + 1, b); self.NF(hi - 1, a, hi, b)

    # ---- primitives (all strokes 2 px) ----
    def R(self, x0, y0, x1, y1, fill=False):
        if fill:
            self.d.rectangle([x0, y0, x1, y1], fill=1)
        else:
            self.d.rectangle([x0, y0, x1, y1], outline=1, width=2)

    def F(self, x0, y0, x1, y1, c=1):
        self.d.rectangle([x0, y0, x1, y1], fill=c)

    def L(self, *pts, c=1):
        self.d.line(list(pts), fill=c, width=2)

    def C(self, cx, cy, r, fill=False, c=1):
        box = [cx - r, cy - r, cx + r, cy + r]
        if fill:
            self.d.ellipse(box, fill=c)
        else:
            self.d.ellipse(box, outline=c, width=2)

    def A(self, cx, cy, r, a0, a1, c=1):
        self.d.arc([cx - r, cy - r, cx + r, cy + r], a0, a1, fill=c, width=2)

    def P(self, pts, fill=True, c=1):
        if fill:
            self.d.polygon(pts, fill=c)
        else:
            self.d.polygon(pts, outline=c)

    def head(self, x, y, dirn, c=1, s=4):
        """Filled arrowhead whose TIP is at (x,y)."""
        if dirn == "l": self.P([(x, y), (x + s, y - s), (x + s, y + s)], c=c)
        if dirn == "r": self.P([(x, y), (x - s, y - s), (x - s, y + s)], c=c)
        if dirn == "u": self.P([(x, y), (x - s, y + s), (x + s, y + s)], c=c)
        if dirn == "d": self.P([(x, y), (x - s, y - s), (x + s, y - s)], c=c)

    def arrow(self, x0, y0, x1, y1, c=1, s=4):
        dirn = "r" if x1 > x0 else "l" if x1 < x0 else "d" if y1 > y0 else "u"
        if dirn in "lr":
            self.F(min(x0, x1 + (s if dirn == "l" else -s)), y0, max(x0, x1 + (s if dirn == "l" else -s)), y0 + 1, c)
            self.head(x1, y0 + 0.5, dirn, c, s)
        else:
            self.F(x0, min(y0, y1 + (s if dirn == "u" else -s)), x0 + 1, max(y0, y1 + (s if dirn == "u" else -s)), c)
            self.head(x0 + 0.5, y1, dirn, c, s)

    # ---- shared base shapes (the vocabulary) ----
    def window(self, x0, y0, x1, y1, solid=False):
        if solid:
            self.F(x0, y0, x1, y1)
        else:
            self.R(x0, y0, x1, y1)
            self.F(x0, y0, x1, y0 + 4)  # title bar

    def screen(self, x0, y0, x1, y1, cx=None):
        self.R(x0, y0, x1, y1)
        cx = (x0 + x1) // 2 if cx is None else cx
        self.F(cx - 1, y1 + 1, cx, y1 + 2)
        self.F(cx - 5, y1 + 3, cx + 5, y1 + 4)

    def doc(self, x0, y0, x1, y1, lines=0):
        f = 6
        self.L((x0, y0), (x1 - f, y0), (x1, y0 + f), (x1, y1), (x0, y1), (x0, y0))
        self.F(x0, y0, x0 + 1, y1); self.F(x0, y1 - 1, x1, y1); self.F(x1 - 1, y0 + f, x1, y1)
        self.F(x0, y0, x1 - f, y0 + 1)
        self.L((x1 - f, y0), (x1 - f, y0 + f), (x1, y0 + f))
        for i in range(lines):
            y = y0 + 9 + i * 4
            self.F(x0 + 4, y, x1 - 4, y + 1)

    def lens(self, cx, cy, r, sign=None):
        self.C(cx, cy, r)
        h = int(r * 0.72) + 1
        self.L((cx + h, cy + h), (cx + h + 6, cy + h + 6))
        self.F(cx + h + 5, cy + h + 5, cx + h + 7, cy + h + 7)
        if sign in ("+", "-"):
            self.F(cx - 3, cy, cx + 3, cy + 1)
        if sign == "+":
            self.F(cx, cy - 3, cx + 1, cy + 3)

    def folder(self, x0, y0, x1, y1, open_=False):
        self.L((x0, y0), (x0 + 8, y0), (x0 + 10, y0 + 3), (x1, y0 + 3), (x1, y1), (x0, y1), (x0, y0))
        self.F(x0, y0, x0 + 1, y1)
        if open_:
            self.P([(x0 + 4, y0 + 9), (x1 + 2, y0 + 9), (x1 - 2, y1), (x0, y1)], fill=False)
            self.L((x0 + 4, y0 + 9), (x1 + 2, y0 + 9), (x1 - 2, y1))
        else:
            self.F(x0, y0 + 7, x1, y0 + 8)

    def badge(self, sym):
        """Solid 14 px disc in the bottom-right corner with the symbol knocked out.

        Drawn natively: the disc is box 20..33, a 2 px moat clears the base shape
        around it, and each symbol is pixel art on the disc's own 14x14 grid.
        """
        self.n.ellipse([18, 18, 35, 35], fill=0)
        if sym == "ref":
            # The one badge without a disc: a ring knocked out of a solid disc reads
            # as concentric rings at 14 px, so the circular arrow is drawn in ink in
            # the same corner, open in the top-right quarter, head pointing clockwise.
            self.n.arc([21, 21, 32, 32], 0, 270, fill=1, width=2)
            self.n.polygon([(25, 18), (29, 21), (29, 22), (25, 25)], fill=1)
            return
        self.n.ellipse([20, 20, 33, 33], fill=1)
        m = Image.new("1", (14, 14), 0)
        d = ImageDraw.Draw(m)
        f = lambda x0, y0, x1, y1: d.rectangle([x0, y0, x1, y1], fill=1)
        if sym in ("d", "u", "l", "r"):
            f(6, 3, 7, 6)                                   # stem, pointing down
            for i, (x0, x1) in enumerate(((3, 10), (4, 9), (5, 8), (6, 7))):
                f(x0, 7 + i, x1, 7 + i)                     # head, tip on row 10
            m = m.transpose({"d": None, "u": Image.FLIP_TOP_BOTTOM,
                             "r": Image.ROTATE_90, "l": Image.ROTATE_270}[sym]) if sym != "d" else m
        if sym == "+": f(3, 6, 10, 7); f(6, 3, 7, 10)
        if sym == "-": f(3, 6, 10, 7)
        if sym == "x":
            for t in range(7):
                f(3 + t, 3 + t, 4 + t, 3 + t); f(9 - t, 3 + t, 10 - t, 3 + t)
        if sym == "i": f(6, 2, 7, 3); f(6, 5, 7, 11)
        if sym == "clock": f(6, 2, 7, 7); f(6, 6, 10, 7)
        if sym == "rec":
            pass                                         # the solid disc IS the record dot
        mp = m.load(); ip = self.im.load()
        for y in range(14):
            for x in range(14):
                if mp[x, y]:
                    ip[20 + x, 20 + y] = 0

def mic(I, cx=14, top=3, waves=False):
    I.R(cx - 4, top, cx + 4, top + 13)
    I.F(cx - 4, top, cx + 4, top + 1)
    I.A(cx, top + 10, 8, 0, 180)
    I.F(cx, top + 18, cx + 1, top + 21)
    I.F(cx - 5, top + 21, cx + 5, top + 22)
    # rounded cap
    I.F(cx - 4, top, cx - 4, top, 0); I.F(cx + 4, top, cx + 4, top, 0)
    if waves:
        I.A(cx, top + 6, 11, -40, 40)
        I.A(cx, top + 6, 11, 140, 220)


def speaker(I, x=3, cy=14):
    I.R(x, cy - 4, x + 5, cy + 4, fill=True)
    I.P([(x + 5, cy - 4), (x + 12, cy - 10), (x + 12, cy + 10), (x + 5, cy + 4)])


def bubble(I, x0, y0, x1, y1):
    I.R(x0, y0, x1, y1)
    I.P([(x0 + 4, y1), (x0 + 4, y1 + 5), (x0 + 10, y1)])
    I.F(x0 + 6, y1 - 1, x0 + 9, y1, 0)


ICONS = {}


def icon(name):
    def deco(fn):
        ICONS[name] = fn
        return fn
    return deco


# ===================== editing =====================
@icon("copy")
def _(I):
    I.R(1, 6, 18, 27)
    I.F(8, 0, 27, 22, 0)
    I.doc(9, 1, 26, 21)

@icon("cut")
def _(I):
    I.C(7, 21, 4); I.C(20, 21, 4)
    I.L((9, 17), (19, 2)); I.L((18, 17), (8, 2))

@icon("paste")
def _(I):
    I.R(3, 4, 24, 27)
    I.F(9, 1, 18, 7)  # solid clip
    I.F(8, 13, 19, 14); I.F(8, 18, 19, 19); I.F(8, 23, 15, 24)

@icon("undo")
def _(I):
    I.A(14, 15, 9, -90, 90)
    I.F(8, 5, 14, 6); I.F(8, 23, 14, 24)
    I.head(2, 6, "l", s=5)

@icon("redo")
def _(I):
    I.A(13, 15, 9, 90, 270)
    I.F(13, 5, 19, 6); I.F(13, 23, 19, 24)
    I.head(25, 6, "r", s=5)

@icon("select_all")
def _(I):
    I.dashed_frame()
    I.NF(9, 9, 24, 24)

@icon("find")
def _(I):
    I.lens(11, 11, 9); I.F(7, 8, 15, 9); I.F(7, 12, 13, 13)

@icon("search")
def _(I):
    I.lens(11, 11, 9)

@icon("save")
def _(I):
    I.L((1, 1), (21, 1), (26, 6), (26, 26), (1, 26), (1, 1))
    I.F(1, 1, 2, 26); I.F(1, 25, 26, 26); I.F(25, 6, 26, 26); I.F(1, 1, 21, 2)
    I.F(7, 1, 18, 9)            # solid shutter
    I.R(6, 15, 21, 26)          # label

@icon("open")
def _(I):
    I.F(1, 4, 9, 5); I.F(1, 4, 2, 25); I.F(9, 6, 22, 7); I.F(8, 4, 9, 7); I.F(21, 6, 22, 11)
    I.P([(1, 25), (6, 11), (27, 11), (22, 25)], fill=False)
    I.L((1, 25), (6, 11), (27, 11), (22, 25), (1, 25))

@icon("print")
def _(I):
    I.R(7, 1, 20, 9); I.R(1, 8, 26, 20); I.F(1, 8, 26, 9)
    I.F(7, 16, 20, 26, 0); I.R(7, 16, 20, 26)
    I.F(21, 12, 22, 13)

@icon("delete")
def _(I):
    I.F(1, 5, 26, 6); I.R(10, 1, 17, 6)
    I.L((4, 8), (6, 26), (21, 26), (23, 8))
    I.F(5, 25, 22, 26)
    I.F(10, 11, 11, 22); I.F(16, 11, 17, 22)

@icon("rename")
def _(I):
    I.R(1, 8, 26, 21)
    I.F(5, 13, 13, 16)           # selected text
    I.F(17, 4, 18, 25); I.F(15, 4, 20, 5); I.F(15, 24, 20, 25)  # I-beam

@icon("refresh")
def _(I):
    I.A(14, 15, 11, -60, 230)
    I.P([(18, 1), (26, 7), (17, 10)])

@icon("clip_history")
def _(I):
    ICONS["paste"](I); I.badge("clock")

# ===================== text navigation =====================
def word(I, x0):
    I.R(x0, 9, x0 + 9, 18, fill=True)

@icon("word_left")
def _(I):
    I.F(16, 9, 26, 18); I.F(9, 13, 15, 14); I.head(5, 13.5, "l", s=5); I.F(1, 9, 3, 18)

@icon("word_right")
def _(I):
    I.F(1, 9, 11, 18); I.F(12, 13, 18, 14); I.head(22, 13.5, "r", s=5); I.F(24, 9, 26, 18)

@icon("line_start")
def _(I):
    I.F(1, 3, 2, 24); I.F(7, 13, 26, 14); I.head(4, 13.5, "l", s=5)

@icon("line_end")
def _(I):
    I.F(25, 3, 26, 24); I.F(1, 13, 20, 14); I.head(23, 13.5, "r", s=5)

# ===================== windows =====================
@icon("close")
def _(I):
    I.window(1, 2, 26, 25); I.L((8, 11), (19, 21)); I.L((8, 21), (19, 11))

@icon("quit")
def _(I):
    I.window(1, 2, 26, 25); I.badge("x")

@icon("minimize")
def _(I):
    I.window(1, 2, 26, 25); I.F(7, 18, 20, 20)

@icon("maximize")
def _(I):
    I.window(1, 2, 26, 25); I.R(7, 10, 20, 20)

@icon("fullscreen")
def _(I):
    I.brackets(9)
    I.n.rectangle([10, 10, 23, 23], outline=1, width=2)

@icon("minimize_all")
def _(I):
    I.window(8, 1, 27, 14); I.F(0, 6, 20, 21, 0); I.window(1, 7, 19, 20)
    I.badge("d")

@icon("minimize_others")
def _(I):
    I.window(4, 1, 23, 17); I.F(1, 24, 7, 26); I.F(11, 24, 17, 26); I.F(21, 24, 26, 26)

@icon("snap_left")
def _(I):
    I.R(1, 3, 26, 24); I.F(1, 3, 13, 24)

@icon("snap_right")
def _(I):
    I.R(1, 3, 26, 24); I.F(14, 3, 26, 24)

@icon("app_switch")
def _(I):
    I.R(0, 6, 27, 21)
    for i, x in enumerate((3, 11, 19)):
        if i == 1: I.F(x, 9, x + 5, 18)
        else: I.R(x, 9, x + 5, 18)

@icon("window_switch")
def _(I):
    I.window(1, 1, 12, 12, solid=True); I.window(15, 1, 26, 12)
    I.window(1, 15, 12, 26); I.window(15, 15, 26, 26)

@icon("show_desktop")
def _(I):
    I.screen(0, 1, 27, 20); I.F(4, 5, 6, 7); I.F(4, 10, 6, 12); I.F(4, 15, 6, 17)

@icon("peek_desktop")
def _(I):
    I.screen(0, 1, 27, 20)
    for x in range(8, 23, 4): I.F(x, 5, x + 1, 5); I.F(x, 16, x + 1, 16)
    for y in range(5, 17, 4): I.F(8, y, 8, y + 1); I.F(23, y, 23, y + 1)

# ===================== screens / desktops =====================
@icon("display")
def _(I):
    I.screen(9, 1, 27, 15); I.F(0, 9, 17, 27, 0); I.screen(0, 9, 17, 21)

@icon("desktop_new")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("+")

@icon("desktop_prev")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("l")

@icon("desktop_next")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("r")

@icon("desktop_close")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("x")

@icon("gfx_restart")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("ref")

@icon("system_props")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("i")

@icon("network")
def _(I):
    I.screen(8, 0, 19, 7)
    I.F(13, 12, 14, 15); I.F(4, 14, 23, 15); I.F(4, 14, 5, 17); I.F(22, 14, 23, 17)
    I.R(0, 17, 9, 24); I.R(18, 17, 27, 24)
    I.F(3, 26, 6, 27); I.F(21, 26, 24, 27)

@icon("cast")
def _(I):
    I.L((1, 8), (1, 2), (26, 2), (26, 22), (16, 22))
    I.F(0, 2, 1, 9); I.F(16, 21, 26, 22)
    I.F(0, 23, 3, 26); I.A(0, 26, 8, 270, 360); I.A(0, 26, 14, 270, 360)

# ===================== capture =====================
@icon("screenshot")
def _(I):
    I.R(1, 7, 26, 24); I.F(8, 3, 17, 7); I.C(13, 15, 5); I.F(21, 10, 22, 11)

@icon("snip")
def _(I):
    I.dashed_frame()
    I.NF(16, 9, 17, 24); I.NF(9, 16, 24, 17)

@icon("screen_record")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("rec")

@icon("text_recog")
def _(I):
    I.brackets(7)
    I.NF(9, 9, 24, 10); I.NF(16, 9, 17, 25)

@icon("lock")
def _(I):
    I.A(14, 11, 7, 180, 360); I.F(7, 10, 8, 13); I.F(20, 10, 21, 13)
    I.R(3, 13, 25, 27, fill=True)
    I.F(13, 17, 15, 23, 0)

@icon("run")
def _(I):
    I.window(0, 2, 27, 25)
    I.L((4, 11), (8, 15), (4, 19)); I.F(11, 19, 19, 20)

@icon("settings")
def _(I):
    import math
    cx, cy = 13.5, 13.5
    for k in range(8):
        a = k * math.pi / 4
        x, y = cx + 11 * math.cos(a), cy + 11 * math.sin(a)
        I.C(int(round(x)), int(round(y)), 2, fill=True)
    I.C(13, 13, 9, fill=True); I.C(13, 13, 4, fill=True, c=0)

@icon("quick_settings")
def _(I):
    I.R(1, 2, 26, 11); I.F(16, 4, 24, 9)        # toggle ON
    I.R(1, 15, 26, 24); I.R(3, 17, 11, 22, fill=True)  # toggle OFF knob left
    I.F(5, 19, 9, 20, 0)

@icon("tray")
def _(I):
    I.R(0, 19, 27, 26); I.F(3, 22, 6, 23); I.F(9, 22, 12, 23)
    I.L((14, 12), (20, 6), (26, 12))

@icon("quick_menu")
def _(I):
    I.R(4, 0, 27, 21); I.F(8, 5, 23, 6); I.F(8, 10, 23, 11); I.F(8, 15, 23, 16)
    I.F(0, 21, 6, 27)

@icon("task_cycle")
def _(I):
    I.R(0, 17, 27, 26); I.F(3, 20, 7, 23); I.R(10, 19, 15, 24); I.R(18, 19, 23, 24)
    I.F(4, 8, 16, 9); I.head(22, 8.5, "r", s=5)

@icon("explorer")
def _(I):
    I.folder(1, 3, 26, 25)

@icon("accessibility")
def _(I):
    I.C(14, 3, 2, fill=True)
    I.F(2, 8, 25, 9); I.F(12, 8, 15, 17)
    I.L((12, 17), (8, 26)); I.L((15, 17), (19, 26))

# ===================== input / media =====================
@icon("dictation")
def _(I):
    mic(I)

@icon("speech_rec")
def _(I):
    bubble(I, 0, 1, 27, 21)
    for x, h in ((5, 2), (9, 4), (13, 6), (17, 3), (21, 5)):
        I.F(x, 11 - h, x + 1, 11 + h)

@icon("narrator")
def _(I):
    speaker(I); I.A(14, 14, 6, -50, 50); I.A(14, 14, 11, -50, 50)

@icon("volume_mixer")
def _(I):
    for i, (x, k) in enumerate(((3, 18), (12, 7), (21, 13))):
        I.F(x + 1, 1, x + 2, 26); I.F(x - 1, k, x + 4, k + 4)

@icon("emoji")
def _(I):
    I.C(13, 13, 12); I.F(8, 8, 10, 11); I.F(17, 8, 19, 11); I.A(13, 13, 7, 30, 150)

@icon("zoom_in")
def _(I):
    I.lens(11, 11, 9, "+")

@icon("zoom_out")
def _(I):
    I.lens(11, 11, 9, "-")

# ===================== apps / services =====================
@icon("game_bar")
def _(I):
    I.d.rounded_rectangle([0, 6, 27, 22], radius=6, outline=1, width=2)
    I.F(5, 13, 11, 14); I.F(7, 11, 8, 16); I.F(19, 11, 20, 12); I.F(22, 14, 23, 15)

@icon("feedback")
def _(I):
    bubble(I, 1, 1, 26, 19); I.F(13, 5, 14, 11); I.F(13, 14, 14, 15)

@icon("copilot")
def _(I):
    I.P([(12, 0), (15, 9), (24, 12), (15, 15), (12, 26), (9, 15), (0, 12), (9, 9)])
    I.P([(22, 0), (23, 3), (26, 4), (23, 5), (22, 8), (21, 5), (18, 4), (21, 3)])

@icon("quick_assist")
def _(I):
    I.A(14, 15, 11, 180, 360)
    I.F(3, 15, 4, 17); I.F(24, 15, 25, 17)
    I.F(1, 14, 7, 23); I.F(21, 14, 27, 23)
    I.F(23, 23, 24, 26); I.F(15, 25, 24, 26)
    I.F(11, 24, 15, 27)


def render(name):
    I = Icon()
    ICONS[name](I)
    return I.im


# ---- IconsFont emission ------------------------------------------------------

HERE = os.path.dirname(os.path.abspath(__file__))
KB = os.path.dirname(HERE)
HEADER = os.path.join(KB, "base", "fonts", "gfx_icons.h")       # resident IconsFont
HINT_HEADER = os.path.join(KB, "base", "fonts", "hint_icons.h")  # the pack font
SYMBOL = "PolyHintIcons"
Y_ADVANCE = 40           # IconsFont's: glyphs are baseline-aligned against fonts[0]

FIRST_CP = 0x100026      # the slot after ICON_LAYER_ONESHOT
X_OFFSET = 36            # panel column of the grid's left edge (x 36..69)
TOP_ROW = 3              # panel row of the grid's top edge (y 3..36)
BASELINE = 23            # os hints are drawn with the cursor at y 23
X_ADVANCE = X_OFFSET + S

BEGIN = "/* ---- BEGIN hint icons: generated by tools/hint_icons.py, do not edit ---- */"
END = "/* ---- END hint icons ---- */"


RETIRED = {"copy", "cut", "paste", "undo", "redo", "select_all", "find", "save", "open",
           "print", "delete", "rename", "refresh", "quit"}


def font_names():
    """The icons written to the font, in slot order."""
    return [n for n in ICONS if n not in RETIRED]


def macro(name):
    return "ICON_HINT_" + name.upper()


def glyph_bytes(im):
    """Column-native bitmap: per column, (h+7)//8 bytes, bit (y & 7) of byte y >> 3."""
    w, h = im.size
    px = im.load()
    cb = (h + 7) >> 3
    out = []
    for x in range(w):
        col = [0] * cb
        for y in range(h):
            if px[x, y]:
                col[y >> 3] |= 1 << (y & 7)
        out += col
    return out


STYLES = {"a": None, "b": "hint_icons_solid", "c": "hint_icons_bold", "d": "hint_icons_stage",
          "e": "hint_icons_retro"}


def style_render(style):
    if STYLES[style] is None:
        return render
    import importlib
    sys.path.insert(0, HERE)
    mod = importlib.import_module(STYLES[style])
    assert set(mod.ICONS) == set(ICONS), f"style {style} does not cover the same icons"
    assert mod.S == S, f"style {style} is not drawn on the {S} grid"
    return mod.render


def header_style(*texts):
    """The style the headers carry: the hint font's tag, else a legacy block's."""
    for t in texts:
        m = re.search(r"/\* style: ([a-z]) \*/", t or "")
        if m:
            return m.group(1)
    return "a"


def strip_block(text):
    """Remove a previously generated block (both markers inclusive) from `text`."""
    return re.sub(r"\n[ \t]*" + re.escape(BEGIN) + r".*?" + re.escape(END), "", text, flags=re.S)


def bitmap_len(array_body):
    body = re.sub(r"/\*.*?\*/", "", array_body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return len(re.findall(r"0x[0-9A-Fa-f]{2}\b", body))


def resident_icons(src):
    """gfx_icons.h without any hint block: IconsFont ends right before FIRST_CP.

    The hints used to be appended to IconsFont between the BEGIN/END markers;
    stripping them (if present) keeps the resident font to the hand-drawn icons."""
    src = strip_block(src)
    bm = re.search(r"(const uint8_t IconsBitmaps\[\] PROGMEM = \{)(.*?)(\n\};)", src, re.S)
    src = re.sub(r"(\(GFXglyph \*\)IconsGlyphs,\s*0x100000,\s*)0x[0-9A-Fa-f]+",
                 lambda m: m.group(1) + f"0x{FIRST_CP - 1:06X}", src)
    return re.sub(r"// Approx\. \d+ bytes", f"// Approx. {bitmap_len(bm.group(2))} bytes", src)


def build(style="a"):
    """The text of hint_icons.h: one GFXfont holding every icon in font_names()."""
    draw = style_render(style)
    names = font_names()
    bmp_lines, glyph_lines = [], []
    off = 0
    for i, n in enumerate(names):
        cp = FIRST_CP + i
        data = glyph_bytes(draw(n))
        hexs = ", ".join(f"0x{b:02X}" for b in data)
        bmp_lines.append(f"  /* 0x{cp:06X} {macro(n)} {S}x{S} */ {hexs},")
        glyph_lines.append(f"  {{ {off:5d}, {S:3d}, {S:3d}, {X_ADVANCE:3d}, {X_OFFSET:4d}, {TOP_ROW - BASELINE:4d} }},"
                           f"   // 0x{cp:06X} {macro(n)}")
        off += len(data)
    last = FIRST_CP + len(names) - 1
    return f"""// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// GENERATED by tools/hint_icons.py, do not hand-edit.
// The OS shortcut-hint icons (hints/os_hints.c), one {S}x{S} glyph each. A FONT-PACK
// font: fonts.yaml lists it under index.pack_extra_fonts and in the `symbol` bundle,
// so it ships in symbol.plyf and nothing in the firmware includes this header.
// The range starts right after the resident IconsFont, and yAdvance {Y_ADVANCE} matches
// IconsFont so the baseline alignment against fonts[0] moves nothing.
// ⚠️ Bitmap labels are BLOCK comments: the host's tools/gfx_font.py strips only
// block comments inside a bitmap.
/* style: {style} */
// Approx. {off} bytes

const uint8_t {SYMBOL}Bitmaps[] PROGMEM = {{
""" + "\n".join(bmp_lines) + f"""
}};

const GFXglyph {SYMBOL}Glyphs[] PROGMEM = {{
""" + "\n".join(glyph_lines) + f"""
}};

const GFXfont {SYMBOL} PROGMEM = {{
  (uint8_t  *){SYMBOL}Bitmaps,
  (GFXglyph *){SYMBOL}Glyphs, 0x{FIRST_CP:06X}, 0x{last:06X},
  {Y_ADVANCE}
}};
""", names


def sheet(path, scale=4, style="a"):
    from PIL import Image
    render = style_render(style)
    names = font_names()
    cols = 10
    rows = (len(names) + cols - 1) // cols
    cell = S * scale + 16
    out = Image.new("L", (cols * cell, rows * cell), 40)
    for i, n in enumerate(names):
        im = render(n).convert("L").resize((S * scale, S * scale), Image.NEAREST)
        out.paste(im, ((i % cols) * cell + 8, (i // cols) * cell + 8))
    out.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if a generated header is stale")
    ap.add_argument("--sheet", metavar="PNG", help="write a contact sheet instead")
    ap.add_argument("--macros", action="store_true",
                    help="print the ICON_HINT_* #defines for lang/named_glyphs.h")
    ap.add_argument("--style", choices=sorted(STYLES),
                    help="icon style to write (default: the one the header carries)")
    a = ap.parse_args()
    icons = open(HEADER, encoding="utf-8").read()
    hint = open(HINT_HEADER, encoding="utf-8").read() if os.path.exists(HINT_HEADER) else ""
    style = a.style or header_style(hint, icons)
    if a.macros:
        for i, n in enumerate(font_names()):
            print(f'#define {macro(n):<33} U"\\x{FIRST_CP + i:06X}"')
        return 0
    if a.sheet:
        sheet(a.sheet, style=style)
        return 0
    new_icons = resident_icons(icons)
    new_hint, names = build(style)
    if a.check:
        # The ICON_HINT_* block in named_glyphs.h is pasted from --macros, so it can
        # name the wrong icons after a reorder while every slot is still valid.
        ng = open(os.path.join(KB, "lang", "named_glyphs.h"), encoding="utf-8").read()
        have = [(m, int(c, 16)) for m, c in re.findall(r'#define\s+(ICON_HINT_\w+)\s+U"\\x([0-9A-Fa-f]+)"', ng)]
        want = [(macro(n), FIRST_CP + i) for i, n in enumerate(names)]
        if have != want:
            print("lang/named_glyphs.h ICON_HINT_* block is stale: paste python3 tools/hint_icons.py --macros",
                  file=sys.stderr)
            return 1
        stale = [p for p, new, old in ((HEADER, new_icons, icons), (HINT_HEADER, new_hint, hint)) if new != old]
        if stale:
            print(f"{', '.join(os.path.basename(p) for p in stale)} stale: run python3 tools/hint_icons.py",
                  file=sys.stderr)
            return 1
        print(f"hint_icons.h up to date ({len(names)} hint icons, style {style})")
        return 0
    for path, new, old in ((HEADER, new_icons, icons), (HINT_HEADER, new_hint, hint)):
        if new != old:
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(new)
    print(f"{len(names)} hint icons (style {style}) at U+{FIRST_CP:06X}..U+{FIRST_CP + len(names) - 1:06X}"
          f" in {os.path.basename(HINT_HEADER)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
