#!/usr/bin/env python3
"""Style B for the OS shortcut-hint icons: SOLID.

Same 65 actions and metaphors as tools/hint_icons.py (style A, outline), drawn
natively on the 34x34 grid with a heavier hand:
  - objects are filled silhouettes; their details are CUT OUT at 2 px
  - remaining strokes (arrows, lens, brackets, arcs) are 3-4 px
  - a window is solid with a 2 px cut under its title bar; caption actions
    (close / minimize / maximize) are cut out of its body
  - selection = solid, unselected = 3 px outline
  - modifier badge: a solid 16 px disc in the bottom-right corner, 2 px moat,
    symbol cut out
"""
import math
from PIL import Image, ImageDraw

S = 34
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

    def F(self, x0, y0, x1, y1, c=1):
        self.d.rectangle([x0, y0, x1, y1], fill=c)

    def RR(self, x0, y0, x1, y1, r=2, c=1):
        self.d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=c)

    def RO(self, x0, y0, x1, y1, w=3, r=2, c=1):
        self.d.rounded_rectangle([x0, y0, x1, y1], radius=r, outline=c, width=w)

    def L(self, *pts, w=3, c=1):
        self.d.line(list(pts), fill=c, width=w, joint="curve")

    def D(self, cx, cy, r, c=1):
        self.d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c)

    def ring(self, cx, cy, r, w=3, c=1):
        self.d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=c, width=w)

    def arc(self, cx, cy, r, a0, a1, w=3, c=1):
        self.d.arc([cx - r, cy - r, cx + r, cy + r], a0, a1, fill=c, width=w)

    def P(self, pts, c=1):
        self.d.polygon(pts, fill=c)

    def tri(self, x, y, dirn, s=5, c=1):
        """Filled arrowhead, tip at (x, y), half-width s, length s."""
        if dirn == "l": self.P([(x, y), (x + s, y - s), (x + s, y + s)], c)
        if dirn == "r": self.P([(x, y), (x - s, y - s), (x - s, y + s)], c)
        if dirn == "u": self.P([(x, y), (x - s, y + s), (x + s, y + s)], c)
        if dirn == "d": self.P([(x, y), (x - s, y - s), (x + s, y - s)], c)

    # ---- vocabulary ----
    def window(self, x0, y0, x1, y1):
        self.RR(x0, y0, x1, y1)
        self.F(x0, y0 + 6, x1, y0 + 7, 0)          # cut under the title bar

    def screen(self, x0=0, y0=1, x1=33, y1=22, cx=None):
        self.RR(x0, y0, x1, y1)
        cx = (x0 + x1) // 2 if cx is None else cx
        self.F(cx - 2, y1 + 1, cx + 1, y1 + 3)
        self.F(cx - 7, y1 + 4, cx + 6, y1 + 6)

    def doc(self, x0, y0, x1, y1, f=8):
        """Solid sheet with its top-right corner folded down: the page loses the
        corner, a 2 px cut runs along the fold, and the flap sits inside it."""
        self.P([(x0, y0), (x1 - f, y0), (x1, y0 + f), (x1, y1), (x0, y1)])
        self.P([(x1 - f - 1, y0), (x1 - f - 1, y0 + f + 1), (x1, y0 + f + 1), (x1, y0 + f)], 0)
        self.P([(x1 - f + 1, y0 + 1), (x1 - f + 1, y0 + f - 1), (x1 - 1, y0 + f - 1)])

    def lens(self, cx, cy, r):
        self.ring(cx, cy, r, w=4)
        h = int(r * 0.7) + 2
        self.L((cx + h, cy + h), (cx + h + 7, cy + h + 7), w=5)

    def folder(self, x0, y0, x1, y1):
        self.RR(x0, y0, x0 + 12, y0 + 6, r=2)
        self.RR(x0, y0 + 4, x1, y1)

    def badge(self, sym):
        self.D(25, 25, 10, 0)                      # moat
        self.d.ellipse([18, 18, 33, 33], fill=1)   # 16 px disc
        m = Image.new("1", (16, 16), 0)
        d = ImageDraw.Draw(m)
        f = lambda x0, y0, x1, y1: d.rectangle([x0, y0, x1, y1], fill=1)
        if sym in ("d", "u", "l", "r"):
            f(6, 3, 9, 7)
            for i, (a, b) in enumerate(((3, 12), (4, 11), (5, 10), (6, 9), (7, 8))):
                f(a, 8 + i, b, 8 + i)
            if sym != "d":
                m = m.transpose({"u": Image.FLIP_TOP_BOTTOM, "r": Image.ROTATE_90,
                                 "l": Image.ROTATE_270}[sym])
        if sym == "+": f(3, 6, 12, 9); f(6, 3, 9, 12)
        if sym == "-": f(3, 6, 12, 9)
        if sym == "x":
            d.line([(4, 4), (11, 11)], fill=1, width=3); d.line([(4, 11), (11, 4)], fill=1, width=3)
        if sym == "i": f(6, 2, 9, 4); f(6, 6, 9, 13)
        if sym == "clock": f(6, 3, 9, 9); f(6, 7, 12, 9)
        mp = m.load(); ip = self.im.load()
        for y in range(16):
            for x in range(16):
                if mp[x, y]:
                    ip[18 + x, 18 + y] = 0


# ===================== editing =====================
@icon("copy")
def _(I):
    I.RR(1, 8, 21, 33)
    I.F(8, 0, 33, 26, 0)
    I.doc(10, 1, 32, 25)

@icon("cut")
def _(I):
    I.ring(8, 26, 6, w=4); I.ring(25, 26, 6, w=4)
    I.L((12, 21), (26, 1), w=4); I.L((21, 21), (7, 1), w=4)

@icon("paste")
def _(I):
    I.RR(3, 4, 30, 33)
    I.RR(9, 0, 24, 9, c=0); I.RR(11, 1, 22, 8)
    I.F(9, 15, 24, 16, 0); I.F(9, 21, 24, 22, 0); I.F(9, 27, 18, 28, 0)

@icon("clip_history")
def _(I):
    ICONS["paste"](I); I.badge("clock")

@icon("undo")
def _(I):
    I.arc(18, 18, 11, -90, 90, w=4)
    I.F(10, 7, 18, 10); I.F(10, 26, 18, 29)
    I.tri(1, 8.5, "l", s=8)

@icon("redo")
def _(I):
    I.arc(15, 18, 11, 90, 270, w=4)
    I.F(15, 7, 23, 10); I.F(15, 26, 23, 29)
    I.tri(32, 8.5, "r", s=8)

def thick_dashed_frame(I):
    for a, b in [(0, 5), (10, 14), (19, 23), (28, 33)]:
        I.F(a, 0, b, 2); I.F(a, 31, b, 33); I.F(0, a, 2, b); I.F(31, a, 33, b)

@icon("select_all")
def _(I):
    thick_dashed_frame(I); I.RR(8, 8, 25, 25)

@icon("find")
def _(I):
    I.lens(13, 13, 11); I.F(8, 10, 18, 12); I.F(8, 15, 15, 17)

@icon("search")
def _(I):
    I.lens(13, 13, 11)

@icon("save")
def _(I):
    I.P([(1, 1), (26, 1), (32, 7), (32, 32), (1, 32)])
    I.F(8, 1, 22, 11, 0); I.F(17, 3, 20, 9)          # shutter cut, slider in ink
    I.RR(7, 17, 26, 32, c=0); I.F(10, 21, 23, 22); I.F(10, 26, 23, 27)

@icon("open")
def _(I):
    I.RR(1, 3, 13, 9); I.RR(1, 6, 28, 31)
    I.P([(4, 31), (9, 14), (33, 14), (28, 31)], c=0)
    I.P([(6, 31), (11, 16), (33, 16), (28, 31)])

@icon("print")
def _(I):
    I.F(8, 1, 25, 10)
    I.RR(1, 9, 32, 24)
    I.F(7, 19, 26, 33, 0); I.F(9, 21, 24, 33)
    I.F(12, 25, 21, 26, 0); I.F(12, 29, 21, 30, 0)
    I.F(26, 12, 28, 14, 0)

@icon("delete")
def _(I):
    I.F(1, 5, 32, 8); I.RR(11, 1, 22, 6)
    I.P([(4, 10), (29, 10), (27, 33), (6, 33)])
    I.F(11, 14, 12, 28, 0); I.F(16, 14, 17, 28, 0); I.F(21, 14, 22, 28, 0)

@icon("rename")
def _(I):
    I.RO(1, 9, 32, 25, w=3)
    I.F(6, 14, 16, 20)
    I.F(21, 4, 23, 30); I.F(18, 3, 26, 5); I.F(18, 29, 26, 31)

@icon("refresh")
def _(I):
    I.arc(17, 18, 13, -50, 230, w=4)
    I.P([(20, 0), (33, 7), (20, 14)])

# ===================== text navigation =====================
@icon("word_left")
def _(I):
    I.F(0, 9, 3, 25); I.tri(5, 17, "l", s=8); I.F(13, 15, 19, 18); I.RR(21, 9, 33, 25)

@icon("word_right")
def _(I):
    I.RR(0, 9, 12, 25); I.F(14, 15, 20, 18); I.tri(28, 17, "r", s=8); I.F(30, 9, 33, 25)

@icon("line_start")
def _(I):
    I.F(0, 4, 3, 30); I.tri(5, 17, "l", s=8); I.F(13, 15, 33, 18)

@icon("line_end")
def _(I):
    I.F(30, 4, 33, 30); I.tri(28, 17, "r", s=8); I.F(0, 15, 20, 18)

# ===================== windows =====================
@icon("close")
def _(I):
    I.window(1, 2, 32, 31)
    I.L((10, 13), (23, 26), w=4, c=0); I.L((10, 26), (23, 13), w=4, c=0)

@icon("quit")
def _(I):
    I.window(1, 2, 32, 31); I.badge("x")

@icon("minimize")
def _(I):
    I.window(1, 2, 32, 31); I.F(8, 22, 25, 25, 0)

@icon("maximize")
def _(I):
    I.window(1, 2, 32, 31); I.F(8, 12, 25, 25, 0); I.F(11, 15, 22, 22)

@icon("fullscreen")
def _(I):
    for x0, y0, x1, y1 in [(0, 0, 10, 3), (0, 0, 3, 10), (23, 0, 33, 3), (30, 0, 33, 10),
                           (0, 30, 10, 33), (0, 23, 3, 33), (23, 30, 33, 33), (30, 23, 33, 33)]:
        I.F(x0, y0, x1, y1)
    I.RR(9, 9, 24, 24)

@icon("minimize_all")
def _(I):
    I.window(10, 0, 33, 15); I.RR(1, 6, 25, 25, c=0); I.window(3, 8, 23, 23); I.badge("d")

@icon("minimize_others")
def _(I):
    I.window(5, 1, 28, 21)
    I.F(1, 27, 8, 31); I.F(13, 27, 20, 31); I.F(25, 27, 32, 31)

@icon("snap_left")
def _(I):
    I.RO(1, 4, 32, 29, w=3); I.F(1, 4, 16, 29)

@icon("snap_right")
def _(I):
    I.RO(1, 4, 32, 29, w=3); I.F(17, 4, 32, 29)

@icon("app_switch")
def _(I):
    I.RO(0, 6, 33, 27, w=3)
    I.RO(4, 11, 10, 22, w=2, r=1); I.RO(23, 11, 29, 22, w=2, r=1)
    I.F(13, 10, 20, 23)

@icon("window_switch")
def _(I):
    I.window(1, 1, 15, 15)
    for x0, y0 in ((18, 1), (1, 18), (18, 18)):
        I.RO(x0, y0, x0 + 14, y0 + 14, w=3)

@icon("show_desktop")
def _(I):
    I.screen(); I.F(5, 5, 8, 8, 0); I.F(5, 11, 8, 14, 0); I.F(5, 17, 8, 19, 0)

@icon("peek_desktop")
def _(I):
    I.screen()
    for x in range(9, 26, 5): I.F(x, 5, x + 2, 6, 0); I.F(x, 17, x + 2, 18, 0)
    for y in range(5, 18, 4): I.F(8, y, 9, y + 1, 0); I.F(26, y, 27, y + 1, 0)

# ===================== screens / desktops =====================
@icon("display")
def _(I):
    I.screen(11, 0, 33, 15)
    I.RR(0, 9, 22, 28, c=0)
    I.screen(1, 10, 21, 25)

def screen_badge(I, sym):
    I.screen(cx=9); I.badge(sym)

for _n, _s in (("desktop_new", "+"), ("desktop_prev", "l"), ("desktop_next", "r"),
               ("desktop_close", "x"), ("system_props", "i")):
    ICONS[_n] = (lambda s: (lambda I: screen_badge(I, s)))(_s)

@icon("gfx_restart")
def _(I):
    I.screen(cx=9)
    I.D(25, 25, 10, 0)
    I.arc(26, 26, 7, 0, 270, w=3)
    I.P([(24, 16), (30, 20), (30, 21), (24, 25)])

@icon("cast")
def _(I):
    I.RR(0, 1, 33, 26)
    I.D(0, 26, 17, 0)
    I.D(0, 27, 4)
    I.arc(0, 27, 10, 270, 360, w=3); I.arc(0, 27, 16, 270, 360, w=3)

@icon("network")
def _(I):
    I.RR(10, 0, 23, 9)
    I.F(15, 10, 18, 15); I.F(4, 15, 29, 17); I.F(4, 15, 6, 22); I.F(27, 15, 29, 22)
    I.RR(0, 23, 11, 33); I.RR(22, 23, 33, 33)

@icon("screenshot")
def _(I):
    I.RR(0, 7, 33, 30); I.RR(9, 2, 22, 9)
    I.D(16, 18, 8, 0); I.D(16, 18, 4); I.F(27, 11, 29, 12, 0)

@icon("snip")
def _(I):
    thick_dashed_frame(I); I.F(15, 7, 18, 26); I.F(7, 15, 26, 18)

@icon("screen_record")
def _(I):
    I.screen(cx=9); I.D(25, 25, 10, 0); I.d.ellipse([18, 18, 33, 33], fill=1)

@icon("text_recog")
def _(I):
    for x0, y0, x1, y1 in [(0, 0, 8, 3), (0, 0, 3, 8), (25, 0, 33, 3), (30, 0, 33, 8),
                           (0, 30, 8, 33), (0, 25, 3, 33), (25, 30, 33, 33), (30, 25, 33, 33)]:
        I.F(x0, y0, x1, y1)
    I.F(8, 8, 25, 11); I.F(15, 8, 18, 26)

# ===================== system =====================
@icon("lock")
def _(I):
    I.arc(17, 13, 9, 180, 360, w=4); I.F(8, 12, 11, 15); I.F(23, 12, 26, 15)
    I.RR(3, 15, 30, 33)
    I.D(17, 22, 3, 0); I.F(16, 22, 18, 28, 0)

@icon("run")
def _(I):
    I.window(0, 2, 33, 31)
    I.L((5, 13), (11, 19), (5, 25), w=3, c=0); I.F(14, 24, 25, 26, 0)

@icon("settings")
def _(I):
    cx = cy = 16.5
    for k in range(8):
        a = k * math.pi / 4
        x, y = cx + 13 * math.cos(a), cy + 13 * math.sin(a)
        I.d.ellipse([x - 3.5, y - 3.5, x + 3.5, y + 3.5], fill=1)
    I.d.ellipse([cx - 11, cy - 11, cx + 11, cy + 11], fill=1)
    I.d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=0)

@icon("quick_settings")
def _(I):
    I.RR(0, 2, 33, 14, r=6); I.D(26, 8, 4, 0)
    I.RO(0, 19, 33, 31, w=3, r=6); I.D(7, 25, 4)

@icon("tray")
def _(I):
    I.RR(0, 23, 33, 33); I.F(4, 27, 8, 29, 0); I.F(12, 27, 16, 29, 0)
    I.L((16, 15), (24, 7), (32, 15), w=4)

@icon("quick_menu")
def _(I):
    I.RR(7, 0, 33, 24)
    I.F(12, 6, 28, 8, 0); I.F(12, 12, 28, 14, 0); I.F(12, 18, 28, 20, 0)
    I.F(0, 26, 7, 33)

@icon("task_cycle")
def _(I):
    I.RR(0, 21, 33, 33)
    I.F(4, 24, 10, 30, 0); I.F(14, 24, 19, 30, 0); I.F(15, 25, 18, 29)
    I.F(23, 24, 28, 30, 0); I.F(24, 25, 27, 29)
    I.F(3, 8, 20, 11); I.tri(30, 9.5, "r", s=7)

@icon("explorer")
def _(I):
    I.folder(1, 3, 32, 31); I.F(1, 12, 32, 13, 0)

@icon("accessibility")
def _(I):
    I.D(17, 4, 4)
    I.F(2, 10, 31, 13); I.F(14, 10, 19, 21)
    I.L((15, 21), (9, 33), w=4); I.L((18, 21), (24, 33), w=4)

# ===================== input / media =====================
def mic(I):
    I.RR(11, 0, 22, 19, r=5)
    I.arc(16.5, 12, 11, 0, 180, w=3)
    I.F(15, 23, 18, 28); I.F(9, 29, 24, 31)

@icon("dictation")
def _(I):
    mic(I)

@icon("speech_rec")
def _(I):
    I.RR(0, 1, 33, 25)
    I.P([(5, 24), (5, 32), (13, 24)])
    for x, h in ((5, 2), (10, 5), (15, 8), (20, 4), (25, 6)):
        I.F(x, 13 - h, x + 2, 13 + h, 0)

@icon("narrator")
def _(I):
    I.F(1, 11, 7, 22); I.P([(7, 11), (15, 3), (15, 30), (7, 22)])
    I.arc(15, 17, 7, -55, 55, w=3); I.arc(15, 17, 14, -55, 55, w=3)

@icon("volume_mixer")
def _(I):
    for x, k in ((3, 20), (14, 6), (25, 13)):
        I.F(x + 2, 0, x + 3, 33); I.RR(x - 1, k, x + 6, k + 7, r=1)

@icon("emoji")
def _(I):
    I.D(16.5, 16.5, 16)
    I.F(10, 9, 13, 14, 0); I.F(20, 9, 23, 14, 0)
    I.d.chord([7, 9, 26, 27], 20, 160, fill=0)

@icon("zoom_in")
def _(I):
    I.lens(13, 13, 11); I.F(8, 12, 18, 14); I.F(12, 8, 14, 18)

@icon("zoom_out")
def _(I):
    I.lens(13, 13, 11); I.F(8, 12, 18, 14)

# ===================== apps / services =====================
@icon("game_bar")
def _(I):
    I.RR(0, 7, 33, 27, r=9)
    I.F(5, 16, 13, 18, 0); I.F(8, 13, 10, 21, 0)
    I.D(23, 14, 2, 0); I.D(27, 19, 2, 0)

@icon("feedback")
def _(I):
    I.RR(0, 1, 33, 24)
    I.P([(5, 23), (5, 32), (14, 23)])
    I.F(15, 5, 18, 15, 0); I.F(15, 18, 18, 20, 0)

@icon("copilot")
def _(I):
    I.P([(14, 0), (18, 11), (29, 15), (18, 19), (14, 33), (10, 19), (0, 15), (10, 11)])
    I.P([(27, 0), (28, 4), (33, 5), (28, 6), (27, 10), (26, 6), (21, 5), (26, 4)])

@icon("quick_assist")
def _(I):
    I.arc(17, 18, 14, 180, 360, w=3)
    I.RR(0, 16, 8, 28); I.RR(25, 16, 33, 28)
    I.L((29, 28), (29, 31), (19, 31), w=3)
    I.RR(13, 29, 19, 33, r=1)


def render(name):
    I = Icon()
    ICONS[name](I)
    return I.im
