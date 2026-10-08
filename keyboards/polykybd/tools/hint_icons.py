#!/usr/bin/env python3
"""Draw the OS shortcut-hint icon set and write it into the resident IconsFont.

One family for every hint in hints/os_hints.c: a 34x34 grid, 2 px strokes,
outline = the object, solid = the part that acts, and a solid disc in the
bottom-right corner for a modifier (+ - x arrows i refresh clock record).
Icons are AUTHORED on a 28 grid and drawn at 34 (ScaledDraw): coordinates
scale, strokes and thin bars keep their authored 2 px.

The glyphs are appended to IconsFont (base/fonts/gfx_icons.h) at U+100026..,
between the BEGIN/END hint-icon markers in both arrays, and the font's `last`
is moved to match. IconsFont is g_all_fonts[0], so growing it shifts no pack
font index and needs no font-pack reship. Each glyph carries the keycap
placement in its own metrics (xOffset 36, top at panel row 3), so a hint
string is the bare glyph with no leading spaces.

    python3 tools/hint_icons.py            # rewrite the block in gfx_icons.h
    python3 tools/hint_icons.py --check    # exit 1 if the header is stale
    python3 tools/hint_icons.py --sheet out.png   # contact sheet, 4x

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
        """Solid disc in the bottom-right corner with the symbol knocked out."""
        cx, cy, r = 21, 21, 6
        self.C(cx, cy, r + 2, fill=True, c=0)  # 1-2 px clearance moat
        self.C(cx, cy, r, fill=True)
        k = 0
        if sym == "+": self.F(cx - 3, cy, cx + 3, cy + 1, k); self.F(cx, cy - 3, cx + 1, cy + 3, k)
        if sym == "-": self.F(cx - 3, cy, cx + 3, cy + 1, k)
        if sym == "x": self.L((cx - 3, cy - 3), (cx + 3, cy + 3), c=k); self.L((cx - 3, cy + 3), (cx + 3, cy - 3), c=k)
        if sym == "r": self.F(cx - 3, cy, cx + 1, cy + 1, k); self.head(cx + 4, cy + 0.5, "r", k, 3)
        if sym == "l": self.F(cx - 1, cy, cx + 3, cy + 1, k); self.head(cx - 4, cy + 0.5, "l", k, 3)
        if sym == "d": self.F(cx, cy - 3, cx + 1, cy + 1, k); self.head(cx + 0.5, cy + 4, "d", k, 3)
        if sym == "u": self.F(cx, cy - 1, cx + 1, cy + 3, k); self.head(cx + 0.5, cy - 4, "u", k, 3)
        if sym == "i": self.F(cx, cy - 3, cx + 1, cy - 2, k); self.F(cx, cy, cx + 1, cy + 3, k)
        if sym == "o": self.C(cx, cy, 3, c=k)
        if sym == "clock": self.F(cx, cy - 4, cx + 1, cy + 1, k); self.F(cx, cy, cx + 3, cy + 1, k)
        if sym == "lens":
            self.d.ellipse([cx - 4, cy - 4, cx + 1, cy + 1], outline=k, width=1)
            self.d.line([cx + 1, cy + 1, cx + 3, cy + 3], fill=k, width=2)
        if sym == "gear": self.C(cx, cy, 3, c=k); self.F(cx, cy - 5, cx + 1, cy + 5, k); self.F(cx - 5, cy, cx + 5, cy + 1, k); self.C(cx, cy, 1, fill=True, c=1)
        if sym == "rec": pass
        if sym == "ref":
            self.d.arc([cx - 4, cy - 4, cx + 4, cy + 4], 300, 600 - 20, fill=k, width=2)
            self.P([(cx + 1, cy - 6), (cx + 5, cy - 4), (cx + 1, cy - 1)], c=k)


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
    for x in range(1, 27, 6):
        I.F(x, 1, x + 3, 2); I.F(x, 25, x + 3, 26)
    for y in range(1, 27, 6):
        I.F(1, y, 2, y + 3); I.F(25, y, 26, y + 3)
    I.F(7, 7, 20, 20)

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
    for (x0, y0, x1, y1) in [(1, 1, 8, 2), (1, 1, 2, 8), (19, 1, 26, 2), (25, 1, 26, 8),
                             (1, 25, 8, 26), (1, 19, 2, 26), (19, 25, 26, 26), (25, 19, 26, 26)]:
        I.F(x0, y0, x1, y1)
    I.R(8, 8, 19, 19)

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
    for x in range(1, 27, 6):
        I.F(x, 2, x + 3, 3); I.F(x, 24, x + 3, 25)
    for y in range(2, 25, 6):
        I.F(1, y, 2, y + 3); I.F(25, y, 26, y + 3)
    I.F(13, 7, 14, 20); I.F(7, 13, 20, 14)

@icon("screen_record")
def _(I):
    I.screen(0, 1, 27, 20, cx=7); I.badge("rec")

@icon("text_recog")
def _(I):
    for (x, y, sx, sy) in [(1, 1, 1, 1), (26, 1, -1, 1), (1, 26, 1, -1), (26, 26, -1, -1)]:
        I.L((x, y), (x + sx * 6, y)); I.L((x, y), (x, y + sy * 6))
    I.F(7, 7, 20, 9); I.F(13, 7, 14, 21)

# ===================== system =====================
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
HEADER = os.path.join(KB, "base", "fonts", "gfx_icons.h")

FIRST_CP = 0x100026      # the slot after ICON_LAYER_ONESHOT
X_OFFSET = 36            # panel column of the grid's left edge (x 36..69)
TOP_ROW = 3              # panel row of the grid's top edge (y 3..36)
BASELINE = 23            # os hints are drawn with the cursor at y 23
X_ADVANCE = X_OFFSET + S

BEGIN = "/* ---- BEGIN hint icons: generated by tools/hint_icons.py, do not edit ---- */"
END = "/* ---- END hint icons ---- */"


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


def strip_block(text):
    """Remove a previously generated block (both markers inclusive) from `text`."""
    return re.sub(r"\n[ \t]*" + re.escape(BEGIN) + r".*?" + re.escape(END), "", text, flags=re.S)


def bitmap_len(array_body):
    body = re.sub(r"/\*.*?\*/", "", array_body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return len(re.findall(r"0x[0-9A-Fa-f]{2}\b", body))


def build(src):
    src = strip_block(src)
    bm = re.search(r"(const uint8_t IconsBitmaps\[\] PROGMEM = \{)(.*?)(\n\};)", src, re.S)
    base = bitmap_len(bm.group(2))
    names = list(ICONS)
    bmp_lines, glyph_lines = [], []
    off = base
    for i, n in enumerate(names):
        cp = FIRST_CP + i
        data = glyph_bytes(render(n))
        hexs = ", ".join(f"0x{b:02X}" for b in data)
        bmp_lines.append(f"  /* 0x{cp:06X} {macro(n)} {S}x{S} */ {hexs},")
        glyph_lines.append(f"  {{ {off:5d}, {S:3d}, {S:3d}, {X_ADVANCE:3d}, {X_OFFSET:4d}, {TOP_ROW - BASELINE:4d} }},"
                           f"   // 0x{cp:06X} {macro(n)}")
        off += len(data)
    last = FIRST_CP + len(names) - 1
    bmp_block = "\n  " + BEGIN + "\n" + "\n".join(bmp_lines) + "\n  " + END
    src = src[:bm.end(2)] + bmp_block + src[bm.end(2):]
    gl = re.search(r"(const GFXglyph IconsGlyphs\[\] PROGMEM = \{)(.*?)(\n\};)", src, re.S)
    gl_block = "\n  " + BEGIN + "\n" + "\n".join(glyph_lines) + "\n  " + END
    src = src[:gl.end(2)] + gl_block + src[gl.end(2):]
    src = re.sub(r"(\(GFXglyph \*\)IconsGlyphs,\s*0x100000,\s*)0x[0-9A-Fa-f]+",
                 lambda m: m.group(1) + f"0x{last:06X}", src)
    src = re.sub(r"// Approx\. \d+ bytes", f"// Approx. {off} bytes", src)
    return src, names


def sheet(path, scale=4):
    from PIL import Image
    names = list(ICONS)
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
    ap.add_argument("--check", action="store_true", help="exit 1 if gfx_icons.h is stale")
    ap.add_argument("--sheet", metavar="PNG", help="write a contact sheet instead")
    a = ap.parse_args()
    if a.sheet:
        sheet(a.sheet)
        return 0
    src = open(HEADER, encoding="utf-8").read()
    new, names = build(src)
    if a.check:
        if new != src:
            print("gfx_icons.h is stale: run python3 tools/hint_icons.py", file=sys.stderr)
            return 1
        print(f"gfx_icons.h up to date ({len(names)} hint icons)")
        return 0
    if new != src:
        with open(HEADER, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(new)
    print(f"{len(names)} hint icons at U+{FIRST_CP:06X}..U+{FIRST_CP + len(names) - 1:06X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
