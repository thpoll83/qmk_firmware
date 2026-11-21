#!/usr/bin/env python3
"""Style C for the OS shortcut-hint icons: BOLD.

Four recurring elements, one 4 px stroke, native 34x34 grid:
  FRAME   one rounded rectangle (4 px, r 5) is the base of every window, screen,
          sheet and panel. A window adds a heavy title edge, a screen adds a
          stand, a sheet adds a folded corner.
  CHEVRON every arrow ends in the same open V (4 px). It is also the Run prompt,
          the minimize/maximize sign, the tray caret and the fullscreen corners.
  DOT     a solid rounded square marks the active or selected thing: the
          selected tile, the clip, the caret, a toggle knob, the record dot.
  NOTCH   a modifier sits in the bottom-right corner, cut out of the frame
          (3 px gap), drawn with the same stroke: + - x chevrons i clock refresh.
"""
import math
from PIL import Image, ImageDraw

S = 34
W = 4            # the one stroke
ICONS = {}


def icon(name):
    def deco(fn):
        ICONS[name] = fn
        return fn
    return deco


class Icon:
    def __init__(self):
        self.im = Image.new("1", (S, S), 0)
        self.d = ImageDraw.Draw(self.im)

    # ---- primitives ----
    def F(self, x0, y0, x1, y1, c=1):
        self.d.rectangle([x0, y0, x1, y1], fill=c)

    def RR(self, x0, y0, x1, y1, r=3, c=1):
        self.d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=c)

    def RO(self, x0, y0, x1, y1, r=5, w=W, c=1):
        self.d.rounded_rectangle([x0, y0, x1, y1], radius=r, outline=c, width=w)

    def L(self, *pts, w=W, c=1):
        """Stroke with round caps: a 4 px line reads as one weight in every direction."""
        self.d.line(list(pts), fill=c, width=w, joint="curve")
        h = (w - 1) / 2
        for x, y in (pts[0], pts[-1]):
            self.d.ellipse([x - h, y - h, x + h, y + h], fill=c)

    def ring(self, cx, cy, r, w=W, c=1):
        self.d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=c, width=w)

    def disc(self, cx, cy, r, c=1):
        self.d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c)

    def arc(self, cx, cy, r, a0, a1, w=W, c=1):
        self.d.arc([cx - r, cy - r, cx + r, cy + r], a0, a1, fill=c, width=w)

    # ---- the four recurring elements ----
    def frame(self, x0, y0, x1, y1, r=5):
        self.RO(x0, y0, x1, y1, r=r)

    def window(self, x0, y0, x1, y1):
        self.frame(x0, y0, x1, y1)
        self.F(x0 + 1, y0 + 3, x1 - 1, y0 + 7)            # heavy title edge

    def screen(self, x0=0, y0=1, x1=33, y1=22, cx=None):
        self.frame(x0, y0, x1, y1)
        cx = (x0 + x1) // 2 if cx is None else cx
        self.F(cx - 1, y1, cx + 2, y1 + 4)
        self.RR(cx - 7, y1 + 5, cx + 8, y1 + 8, r=1)

    def sheet(self, x0, y0, x1, y1, f=9):
        self.L((x1 - f, y0 + 1), (x0 + 2, y0 + 1), (x0 + 2, y1 - 1), (x1 - 2, y1 - 1),
               (x1 - 2, y0 + f), (x1 - f, y0 + 1))
        self.L((x1 - f, y0 + 1), (x1 - f, y0 + f), (x1 - 2, y0 + f))

    def chev(self, x, y, dirn, s=6, w=W, c=1):
        """Open V with its tip at (x, y), arms s long."""
        if dirn == "r": self.L((x - s, y - s), (x, y), (x - s, y + s), w=w, c=c)
        if dirn == "l": self.L((x + s, y - s), (x, y), (x + s, y + s), w=w, c=c)
        if dirn == "u": self.L((x - s, y + s), (x, y), (x + s, y + s), w=w, c=c)
        if dirn == "d": self.L((x - s, y - s), (x, y), (x + s, y - s), w=w, c=c)

    def arrow(self, x0, y, x1, s=6):
        """Horizontal arrow from x0 to the chevron tip at x1."""
        self.L((x0, y), (x1, y))
        self.chev(x1, y, "r" if x1 > x0 else "l", s=s)

    def dot(self, x0, y0, x1, y1, c=1):
        # radius 2 rounds a small square into a "+": only round what can carry it
        m = min(x1 - x0, y1 - y0) + 1
        self.RR(x0, y0, x1, y1, r=2 if m >= 7 else (1 if m >= 5 else 0), c=c)

    def lens(self, cx=13, cy=13, r=10):
        self.ring(cx, cy, r)
        k = int(r * 0.71) + 2
        self.L((cx + k, cy + k), (cx + k + 7, cy + k + 7), w=5)

    def notch(self, sym):
        """Cut the bottom-right corner out of whatever is there (x/y 18..33, a 3 px
        gap around a 13 px cell at 21..33) and draw the modifier in the stroke."""
        self.F(18, 18, 33, 33, 0)
        c = 27.5
        if sym == "+": self.F(21, 26, 33, 29); self.F(26, 21, 29, 33)
        if sym == "-": self.F(21, 26, 33, 29)
        if sym == "x": self.L((22, 22), (32, 32)); self.L((22, 32), (32, 22))
        if sym == "l": self.chev(22, c, "l", s=6)
        if sym == "r": self.chev(32, c, "r", s=6)
        if sym == "d": self.chev(c, 32, "d", s=6)
        if sym == "i": self.F(26, 21, 29, 23); self.F(26, 25, 29, 33)
        if sym == "dot": self.disc(c, c, 6)
        if sym == "clock":
            self.ring(c, c, 6, w=3); self.F(27, 24, 28, 28); self.F(27, 27, 30, 28)
        if sym == "ref":
            self.arc(c, 28.5, 5, 0, 270, w=3)
            self.d.polygon([(26, 20), (32, 23.5), (26, 27)], fill=1)

# ===================== editing =====================
@icon("copy")
def _(I):
    I.frame(1, 8, 22, 33)
    I.F(8, 0, 33, 28, 0)
    I.sheet(10, 0, 33, 26)

@icon("cut")
def _(I):
    I.ring(8, 26, 5); I.ring(25, 26, 5)
    I.L((12, 21), (25, 2)); I.L((21, 21), (8, 2))

@icon("paste")
def _(I):
    I.frame(3, 4, 30, 33)
    I.F(9, 0, 24, 9, 0); I.dot(11, 1, 22, 8)
    I.F(9, 15, 24, 18); I.F(9, 23, 19, 26)

@icon("clip_history")
def _(I):
    ICONS["paste"](I); I.notch("clock")

@icon("undo")
def _(I):
    I.arc(18, 19, 10, -90, 90)
    I.L((18, 9), (6, 9)); I.L((18, 29), (10, 29))
    I.chev(4, 9, "l")

@icon("redo")
def _(I):
    I.arc(15, 19, 10, 90, 270)
    I.L((15, 9), (27, 9)); I.L((15, 29), (23, 29))
    I.chev(29, 9, "r")

def dashed(I):
    for a, b in ((0, 6), (11, 15), (18, 22), (27, 33)):
        I.F(a, 0, b, 3); I.F(a, 30, b, 33); I.F(0, a, 3, b); I.F(30, a, 33, b)

@icon("select_all")
def _(I):
    dashed(I); I.dot(9, 9, 24, 24)

@icon("find")
def _(I):
    I.lens(); I.F(8, 10, 18, 12); I.F(8, 15, 15, 17)

@icon("search")
def _(I):
    I.lens()

@icon("save")
def _(I):
    I.L((25, 2), (2, 2), (2, 31), (31, 31), (31, 8), (25, 2))
    I.dot(9, 2, 23, 11)
    I.F(9, 19, 24, 22)

@icon("open")
def _(I):
    I.L((2, 30), (2, 4), (12, 4), (15, 8), (25, 8), (25, 12))
    I.L((2, 30), (8, 14), (32, 14), (26, 30), (2, 30))

@icon("print")
def _(I):
    I.L((8, 9), (8, 2), (25, 2), (25, 9))
    I.frame(0, 9, 33, 24)
    I.F(6, 19, 27, 33, 0); I.frame(8, 20, 25, 33, r=3)

@icon("delete")
def _(I):
    I.L((2, 6), (31, 6)); I.L((12, 6), (12, 2), (21, 2), (21, 6))
    I.L((5, 10), (7, 31), (26, 31), (28, 10))
    I.L((13.5, 15), (13.5, 25)); I.L((19.5, 15), (19.5, 25))

@icon("rename")
def _(I):
    I.frame(0, 8, 33, 26)
    I.F(12, 5, 21, 29, 0)                       # 3 px clearance where the caret crosses
    I.F(15, 2, 18, 32); I.F(11, 2, 22, 4); I.F(11, 30, 22, 32)
    I.dot(5, 13, 9, 21)

@icon("refresh")
def _(I):
    I.arc(16, 18, 12, -40, 250)
    I.L((19, 4), (26, 9), (19, 14))           # chevron head on the arc's open end

# ===================== text navigation =====================
@icon("word_left")
def _(I):
    I.F(0, 8, 3, 26); I.chev(7, 17, "l"); I.L((7, 17), (18, 17)); I.dot(22, 11, 33, 23)

@icon("word_right")
def _(I):
    I.dot(0, 11, 11, 23); I.L((15, 17), (26, 17)); I.chev(26, 17, "r"); I.F(30, 8, 33, 26)

@icon("line_start")
def _(I):
    I.F(0, 4, 3, 30); I.chev(8, 17, "l", s=7); I.L((8, 17), (32, 17))

@icon("line_end")
def _(I):
    I.F(30, 4, 33, 30); I.chev(25, 17, "r", s=7); I.L((1, 17), (25, 17))

# ===================== windows =====================
@icon("close")
def _(I):
    I.window(0, 1, 33, 32); I.L((11, 15), (22, 26), w=4); I.L((11, 26), (22, 15), w=4)

@icon("quit")
def _(I):
    I.window(0, 1, 33, 32); I.notch("x")

@icon("minimize")
def _(I):
    I.window(0, 1, 33, 32); I.chev(16.5, 25, "d", s=7)

@icon("maximize")
def _(I):
    I.window(0, 1, 33, 32); I.chev(16.5, 15, "u", s=7)

@icon("fullscreen")
def _(I):
    I.L((2, 10), (2, 2), (10, 2)); I.L((23, 2), (31, 2), (31, 10))
    I.L((2, 23), (2, 31), (10, 31)); I.L((23, 31), (31, 31), (31, 23))
    I.dot(11, 11, 22, 22)

@icon("minimize_all")
def _(I):
    I.window(9, 0, 33, 16); I.F(0, 5, 26, 27, 0); I.window(1, 7, 23, 25); I.notch("d")

@icon("minimize_others")
def _(I):
    I.window(4, 0, 29, 21); I.dot(0, 27, 8, 32); I.dot(13, 27, 20, 32); I.dot(25, 27, 33, 32)

@icon("snap_left")
def _(I):
    I.frame(0, 3, 33, 30); I.dot(0, 3, 16, 30)

@icon("snap_right")
def _(I):
    I.frame(0, 3, 33, 30); I.dot(17, 3, 33, 30)

@icon("app_switch")
def _(I):
    I.frame(0, 6, 33, 27)
    I.F(7, 13, 9, 20); I.dot(13, 11, 20, 22); I.F(24, 13, 26, 20)

@icon("window_switch")
def _(I):
    I.dot(0, 0, 14, 14)
    I.frame(19, 0, 33, 14, r=3); I.frame(0, 19, 14, 33, r=3); I.frame(19, 19, 33, 33, r=3)

@icon("show_desktop")
def _(I):
    I.screen(); I.dot(7, 7, 11, 10); I.dot(7, 13, 11, 16)

@icon("peek_desktop")
def _(I):
    for a, b in ((0, 6), (11, 15), (18, 22), (27, 33)):
        I.F(a, 1, b, 4); I.F(a, 19, b, 22)
    for a, b in ((1, 5), (9, 13), (17, 22)):
        I.F(0, a, 3, b); I.F(30, a, 33, b)
    I.F(15, 22, 18, 26); I.RR(9, 27, 24, 30, r=1)

# ===================== screens / desktops =====================
@icon("display")
def _(I):
    I.frame(12, 0, 33, 16); I.F(0, 8, 24, 30, 0)
    I.screen(0, 10, 21, 25)

def screen_notch(I, sym):
    I.screen(cx=9); I.notch(sym)

for _n, _s in (("desktop_new", "+"), ("desktop_prev", "l"), ("desktop_next", "r"),
               ("desktop_close", "x"), ("system_props", "i"), ("gfx_restart", "ref"),
               ("screen_record", "dot")):
    ICONS[_n] = (lambda s: (lambda I: screen_notch(I, s)))(_s)

@icon("cast")
def _(I):
    I.L((2, 10), (2, 3), (31, 3), (31, 24), (19, 24))
    I.dot(0, 28, 5, 33)
    I.arc(0, 33, 11, 270, 360); I.arc(0, 33, 18, 270, 360)

@icon("network")
def _(I):
    I.frame(9, 0, 24, 10, r=3)
    I.F(15, 10, 18, 14); I.F(4, 14, 29, 17); I.F(4, 14, 7, 20); I.F(26, 14, 29, 20)
    I.frame(0, 22, 12, 33, r=3); I.frame(21, 22, 33, 33, r=3)

# ===================== capture =====================
@icon("screenshot")
def _(I):
    I.frame(0, 7, 33, 31); I.dot(10, 2, 23, 8)
    I.ring(16.5, 19, 6); I.F(26, 11, 28, 13)

@icon("snip")
def _(I):
    dashed(I); I.F(15, 9, 18, 24); I.F(9, 15, 24, 18)

@icon("text_recog")
def _(I):
    I.L((2, 9), (2, 2), (9, 2)); I.L((24, 2), (31, 2), (31, 9))
    I.L((2, 24), (2, 31), (9, 31)); I.L((24, 31), (31, 31), (31, 24))
    I.F(9, 9, 24, 12); I.F(15, 9, 18, 25)

# ===================== system =====================
@icon("lock")
def _(I):
    I.arc(16.5, 13, 8, 180, 360); I.F(7, 12, 10, 15); I.F(23, 12, 26, 15)
    I.frame(2, 15, 31, 33)
    I.dot(15, 21, 18, 27)

@icon("run")
def _(I):
    I.window(0, 1, 33, 32)
    I.chev(13, 20, "r", s=5); I.dot(17, 25, 27, 28)

@icon("settings")
def _(I):
    cx = cy = 16.5
    for k in range(8):
        a = k * math.pi / 4
        x, y = cx + 13 * math.cos(a), cy + 13 * math.sin(a)
        I.d.ellipse([x - 3.5, y - 3.5, x + 3.5, y + 3.5], fill=1)
    I.d.ellipse([cx - 11, cy - 11, cx + 11, cy + 11], fill=1)
    I.d.ellipse([cx - 7, cy - 7, cx + 7, cy + 7], fill=0)
    I.d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=1)

@icon("quick_settings")
def _(I):
    I.RO(0, 1, 33, 14, r=6); I.dot(21, 4, 29, 11)
    I.RO(0, 19, 33, 32, r=6); I.dot(4, 22, 12, 29)

@icon("tray")
def _(I):
    I.RR(0, 23, 33, 33); I.F(5, 26, 9, 30, 0); I.F(13, 26, 17, 30, 0)
    I.chev(25, 7, "u", s=6)

@icon("quick_menu")
def _(I):
    I.frame(7, 0, 33, 24)
    I.F(12, 7, 28, 9); I.F(12, 14, 24, 16)
    I.dot(0, 26, 7, 33)

@icon("task_cycle")
def _(I):
    I.RR(0, 21, 33, 33)
    I.F(13, 25, 19, 29, 0); I.F(23, 25, 29, 29, 0)
    I.L((3, 9), (26, 9)); I.chev(28, 9, "r")

@icon("explorer")
def _(I):
    I.L((2, 30), (2, 4), (12, 4), (15, 8), (31, 8), (31, 30), (2, 30))
    I.F(2, 12, 31, 14)

@icon("accessibility")
def _(I):
    I.disc(16.5, 4, 3.5)
    I.L((3, 11), (30, 11)); I.L((16.5, 11), (16.5, 20))
    I.L((16, 20), (9, 31)); I.L((17, 20), (24, 31))

# ===================== input / media =====================
@icon("dictation")
def _(I):
    I.RO(11, 0, 22, 19, r=5)
    I.arc(16.5, 12, 11, 10, 170)
    I.F(15, 23, 18, 28); I.RR(9, 29, 24, 32, r=1)

@icon("speech_rec")
def _(I):
    I.frame(0, 1, 33, 24)
    I.L((6, 24), (6, 32), (13, 25))
    for x, h in ((7, 2), (13, 5), (19, 3), (25, 4)):
        I.F(x, 12 - h, x + 2, 13 + h)

@icon("narrator")
def _(I):
    I.L((2, 12), (2, 22), (8, 22), (15, 29), (15, 5), (8, 12), (2, 12))
    I.arc(15, 17, 8, -50, 50); I.arc(15, 17, 15, -50, 50)

@icon("volume_mixer")
def _(I):
    for x, k in ((5, 20), (16, 5), (27, 13)):
        I.F(x, 0, x + 1, 33); I.F(x - 4, k - 1, x + 5, k + 7, 0); I.dot(x - 3, k, x + 4, k + 6)

@icon("emoji")
def _(I):
    I.ring(16.5, 16.5, 15)
    I.dot(10, 9, 13, 14); I.dot(20, 9, 23, 14)
    I.arc(16.5, 17, 9, 25, 155)

@icon("zoom_in")
def _(I):
    I.lens(); I.F(7, 12, 19, 14); I.F(12, 7, 14, 19)

@icon("zoom_out")
def _(I):
    I.lens(); I.F(7, 12, 19, 14)

# ===================== apps / services =====================
@icon("game_bar")
def _(I):
    I.RO(0, 7, 33, 28, r=9)
    I.F(6, 16, 14, 18); I.F(9, 13, 11, 21)
    I.dot(21, 13, 24, 16); I.dot(25, 18, 28, 21)

@icon("feedback")
def _(I):
    I.frame(0, 1, 33, 24)
    I.L((6, 24), (6, 32), (13, 25))
    I.F(15, 6, 18, 15); I.dot(15, 18, 18, 20)

@icon("copilot")
def _(I):
    I.d.polygon([(13, 1), (17, 12), (28, 16), (17, 20), (13, 32), (9, 20), (0, 16), (9, 12)], fill=1)
    I.dot(25, 1, 31, 7)

@icon("quick_assist")
def _(I):
    I.arc(16.5, 18, 14, 180, 360)
    I.dot(0, 16, 8, 28); I.dot(25, 16, 33, 28)
    I.L((29, 28), (29, 31), (19, 31))
    I.dot(13, 29, 19, 33)


def render(name):
    I = Icon()
    ICONS[name](I)
    return I.im
