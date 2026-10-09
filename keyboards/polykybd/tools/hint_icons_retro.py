#!/usr/bin/env python3
"""Style E for the OS shortcut-hint icons: RETRO (80s home computer).

  1. PARALLEL LINES are the fill. Two patterns only:
       pinstripe  1 px on / 1 px off, the classic Mac title bar
       sunset     solid at the top, then 2-on-1-off, then 1-on-1-off: the
                  striped gradient of 80s logos. It marks the ACTOR.
  2. SPEED LINES: anything that moves trails three parallel lines.
  3. PERIOD OBJECTS: the CRT monitor with a foot, the window with a
     pinstriped title bar and a close box, the block cursor, tractor-feed
     printer paper, the joystick, the floppy.
  4. HARD PIXEL CORNERS: 2 px outlines, corners cut by one pixel step, no
     curves smoother than the pixel grid allows.
  5. MODIFIER = a square chip in the bottom-right corner, symbol cut out.
"""
from PIL import Image, ImageDraw

S = 34
ICONS = {}


def icon(name):
    def deco(fn):
        ICONS[name] = fn
        return fn
    return deco


def sunset_row(r, h):
    """Is relative row r (of h) lit in the sunset pattern?"""
    a, b = int(h * 0.42), int(h * 0.72)
    if r < a: return True
    if r < b: return (r - a) % 3 != 2
    return (r - b) % 2 == 0


class Icon:
    def __init__(self):
        self.im = Image.new("1", (S, S), 0)
        self.d = ImageDraw.Draw(self.im)

    def F(self, x0, y0, x1, y1, c=1):
        self.d.rectangle([x0, y0, x1, y1], fill=c)

    def box(self, x0, y0, x1, y1, w=2, c=1):
        """2 px outline with the corner pixel stepped off."""
        self.F(x0 + 1, y0, x1 - 1, y0 + w - 1, c); self.F(x0 + 1, y1 - w + 1, x1 - 1, y1, c)
        self.F(x0, y0 + 1, x0 + w - 1, y1 - 1, c); self.F(x1 - w + 1, y0 + 1, x1, y1 - 1, c)

    def P(self, pts, c=1):
        self.d.polygon(pts, fill=c)

    def L(self, pts, w=2, c=1):
        self.d.line(pts, fill=c, width=w)

    # ---- the two fills ----
    def _masked(self, draw_fn, pattern):
        m = Image.new("1", (S, S), 0)
        draw_fn(ImageDraw.Draw(m))
        bb = m.getbbox()
        if not bb:
            return
        y0, y1 = bb[1], bb[3]
        mp, ip = m.load(), self.im.load()
        for y in range(y0, y1):
            if not pattern(y - y0, y1 - y0, y):
                continue
            for x in range(S):
                if mp[x, y]:
                    ip[x, y] = 1

    def sunset(self, x0, y0, x1, y1):
        self._masked(lambda d: d.rectangle([x0, y0, x1, y1], fill=1), lambda r, h, y: sunset_row(r, h))

    def sunset_poly(self, pts):
        self._masked(lambda d: d.polygon(pts, fill=1), lambda r, h, y: sunset_row(r, h))

    def pin(self, x0, y0, x1, y1, phase=0):
        for y in range(y0, y1 + 1):
            if (y - y0 + phase) % 2 == 0:
                self.F(x0, y, x1, y)

    def speed(self, x0, x1, ys):
        """Three parallel speed lines (2 px) at the given rows."""
        for y in ys:
            self.F(x0, y, x1, y + 1)

    # ---- period objects ----
    def window(self, x0, y0, x1, y1, shade=False):
        """Pinstriped title bar with a close box."""
        if shade:
            y1 = y0 + 8
        self.box(x0, y0, x1, y1)
        if not shade:
            self.F(x0, y0 + 8, x1, y0 + 9)
        self.pin(x0 + 3, y0 + 3, x1 - 3, y0 + 6)
        self.F(x0 + 4, y0 + 2, x0 + 9, y0 + 7, 0); self.box(x0 + 5, y0 + 2, x0 + 9, y0 + 6, w=1)

    def crt(self, x0=0, y0=0, x1=33, y1=23, cx=None):
        self.box(x0, y0, x1, y1)
        self.box(x0 + 4, y0 + 4, x1 - 4, y1 - 4, w=1)
        cx = (x0 + x1) // 2 if cx is None else cx
        self.P([(cx - 5, y1 + 1), (cx + 5, y1 + 1), (cx + 8, y1 + 6), (cx - 8, y1 + 6)])

    def sheet(self, x0, y0, x1, y1, f=6):
        self.L([(x1 - f, y0), (x0, y0), (x0, y1), (x1, y1), (x1, y0 + f), (x1 - f, y0)], w=2)
        self.F(x0, y0, x0 + 1, y1); self.F(x0, y1 - 1, x1, y1); self.F(x1 - 1, y0 + f, x1, y1)
        self.F(x1 - f, y0, x1 - f + 1, y0 + f); self.F(x1 - f, y0 + f - 1, x1, y0 + f)

    def arrow(self, tip_x, y, dirn, length=14, s=6):
        """Chunky pixel arrow with three speed lines behind it."""
        if dirn == "l":
            self.P([(tip_x, y), (tip_x + s, y - s), (tip_x + s, y + s)])
            self.F(tip_x + s, y - 1, tip_x + length, y + 1)
        else:
            self.P([(tip_x, y), (tip_x - s, y - s), (tip_x - s, y + s)])
            self.F(tip_x - length, y - 1, tip_x - s, y + 1)

    def lens(self, cx, cy, r):
        self.d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=1, width=3)
        k = int(r * 0.7) + 1
        self.P([(cx + k, cy + k + 3), (cx + k + 3, cy + k), (cx + k + 11, cy + k + 8), (cx + k + 8, cy + k + 11)])

    def chip(self, sym):
        """Square modifier chip in the bottom-right corner, symbol cut out."""
        self.F(17, 17, 33, 33, 0)
        self.F(19, 19, 33, 33)
        k = 0
        if sym == "+": self.F(22, 25, 30, 27, k); self.F(25, 22, 27, 30, k)
        if sym == "-": self.F(22, 25, 30, 27, k)
        if sym == "x":
            for t in range(7):
                self.F(22 + t, 22 + t, 23 + t, 23 + t, k); self.F(29 - t, 22 + t, 30 - t, 23 + t, k)
        if sym == "l": self.P([(22, 26), (26, 22), (26, 30)], k); self.F(26, 25, 30, 27, k)
        if sym == "r": self.P([(30, 26), (26, 22), (26, 30)], k); self.F(22, 25, 26, 27, k)
        if sym == "d": self.P([(26, 30), (22, 26), (30, 26)], k); self.F(25, 22, 27, 26, k)
        if sym == "i": self.F(25, 21, 27, 22, k); self.F(25, 24, 27, 31, k)
        if sym == "clock": self.F(25, 21, 26, 27, k); self.F(25, 26, 30, 27, k)
        if sym == "dot": self.d.ellipse([21, 21, 31, 31], fill=k); self.d.ellipse([23, 23, 29, 29], fill=1)
        if sym == "ref":
            self.d.arc([21, 22, 30, 31], 0, 270, fill=k, width=2)
            self.P([(25, 19), (30, 22.5), (25, 26)], k)


# ===================== editing =====================
@icon("copy")
def _(I):
    I.sheet(1, 8, 21, 33)
    I.F(8, 0, 33, 27, 0)
    I.sheet(10, 1, 32, 25); I.sunset(13, 12, 29, 22)

@icon("cut")
def _(I):
    I.box(1, 22, 12, 33); I.box(21, 22, 32, 33)
    I.P([(9, 22), (12, 22), (27, 1), (24, 1)]); I.P([(21, 22), (24, 22), (9, 1), (6, 1)])
    I.pin(0, 14, 4, 18); I.pin(29, 14, 33, 18)

@icon("paste")
def _(I):
    I.box(3, 4, 30, 33)
    I.F(10, 1, 23, 8, 0); I.F(11, 1, 22, 7)
    I.sunset(8, 12, 25, 29)

@icon("clip_history")
def _(I):
    I.box(3, 4, 30, 33)
    I.F(10, 1, 23, 8, 0); I.F(11, 1, 22, 7)
    I.F(8, 12, 25, 13); I.F(8, 17, 25, 18); I.F(8, 22, 14, 23)
    I.chip("clock")

@icon("undo")
def _(I):
    I.sunset_poly([(10, 6), (26, 6), (31, 11), (31, 22), (26, 27), (14, 27),
                   (14, 22), (25, 22), (26, 21), (26, 12), (25, 11), (10, 11)])
    I.P([(1, 8.5), (10, 1), (10, 16)])
    I.pin(0, 30, 33, 33)                                 # the timeline

@icon("redo")
def _(I):
    I.sunset_poly([(23, 6), (7, 6), (2, 11), (2, 22), (7, 27), (19, 27),
                   (19, 22), (8, 22), (7, 21), (7, 12), (8, 11), (23, 11)])
    I.P([(32, 8.5), (23, 1), (23, 16)])
    I.pin(0, 30, 33, 33)

@icon("select_all")
def _(I):
    for x in range(0, 34, 4):
        I.F(x, 0, x + 1, 0); I.F(x + 2, 33, x + 3, 33)
    for y in range(0, 34, 4):
        I.F(0, y + 2, 0, y + 3); I.F(33, y, 33, y + 1)
    I.sunset(5, 5, 28, 28)

@icon("find")
def _(I):
    I.lens(13, 13, 11); I.pin(7, 9, 19, 17)

@icon("search")
def _(I):
    I.lens(13, 13, 11)

@icon("save")
def _(I):
    I.L([(1, 1), (27, 1), (32, 6), (32, 32), (1, 32), (1, 1)], w=2)
    I.F(1, 1, 2, 32); I.F(1, 31, 32, 32); I.F(31, 6, 32, 32)
    I.F(8, 1, 24, 11); I.F(19, 3, 22, 9, 0)
    I.box(6, 16, 27, 32); I.pin(9, 20, 24, 29)

@icon("open")
def _(I):
    I.box(1, 9, 32, 32); I.F(1, 5, 13, 10); I.F(3, 7, 11, 9, 0)
    I.P([(16, 11), (24, 19), (8, 19)]); I.F(14, 19, 18, 28)
    I.pin(4, 22, 9, 28); I.pin(23, 22, 28, 28)

@icon("print")
def _(I):
    I.box(0, 10, 33, 24); I.pin(3, 13, 14, 15)
    I.F(5, 20, 28, 33, 0); I.box(6, 20, 27, 33)
    for y in range(22, 33, 3):
        I.F(8, y, 9, y); I.F(24, y, 25, y)            # tractor-feed holes
    I.F(12, 24, 21, 25); I.F(12, 28, 19, 29)
    I.box(7, 1, 26, 10)

@icon("delete")
def _(I):
    I.F(1, 5, 32, 7); I.box(11, 1, 22, 6)
    I.sunset_poly([(4, 10), (29, 10), (27, 33), (6, 33)])
    I.F(11, 13, 12, 30, 0); I.F(16, 13, 17, 30, 0); I.F(21, 13, 22, 30, 0)

@icon("rename")
def _(I):
    I.box(0, 8, 33, 26)
    I.F(5, 14, 10, 15); I.F(12, 14, 15, 15)
    I.F(18, 11, 24, 23)                                  # block cursor

@icon("refresh")
def _(I):
    I.d.arc([3, 5, 30, 32], -40, 250, fill=1, width=4)
    I.P([(17, 0), (28, 6), (17, 12)])
    I.pin(0, 0, 8, 4)

# ===================== text navigation =====================
@icon("word_left")
def _(I):
    I.F(0, 8, 2, 26); I.arrow(5, 17, "l", length=17)
    I.F(24, 11, 33, 23)

@icon("word_right")
def _(I):
    I.F(0, 11, 9, 23); I.arrow(28, 17, "r", length=17); I.F(31, 8, 33, 26)

@icon("line_start")
def _(I):
    I.F(0, 4, 2, 30); I.arrow(5, 13, "l", length=28)
    I.pin(8, 22, 33, 27)

@icon("line_end")
def _(I):
    I.F(31, 4, 33, 30); I.arrow(28, 13, "r", length=28)
    I.pin(0, 22, 25, 27)

# ===================== windows =====================
@icon("close")
def _(I):
    I.window(0, 1, 33, 32)
    for t in range(13):
        I.F(10 + t, 14 + t, 12 + t, 15 + t); I.F(22 - t, 14 + t, 24 - t, 15 + t)

@icon("quit")
def _(I):
    I.window(0, 1, 33, 32); I.chip("x")

@icon("minimize")
def _(I):
    I.window(0, 0, 33, 33, shade=True)                 # window-shade: rolled up
    I.P([(16.5, 31), (8, 22), (25, 22)]); I.pin(13, 12, 20, 21)

@icon("maximize")
def _(I):
    I.window(0, 1, 33, 32); I.sunset(5, 13, 28, 28)

@icon("fullscreen")
def _(I):
    for x0, y0, x1, y1 in ((0, 0, 8, 2), (0, 0, 2, 8), (25, 0, 33, 2), (31, 0, 33, 8),
                           (0, 31, 8, 33), (0, 25, 2, 33), (25, 31, 33, 33), (31, 25, 33, 33)):
        I.F(x0, y0, x1, y1)
    I.pin(7, 7, 26, 26)

@icon("minimize_all")
def _(I):
    I.window(6, 0, 33, 0, shade=True); I.window(0, 11, 27, 11, shade=True); I.chip("d")

@icon("minimize_others")
def _(I):
    I.window(4, 0, 29, 21); I.sunset(8, 12, 25, 18)
    I.pin(0, 26, 33, 33)

@icon("snap_left")
def _(I):
    I.box(0, 3, 33, 30); I.sunset(2, 5, 16, 28)

@icon("snap_right")
def _(I):
    I.box(0, 3, 33, 30); I.sunset(17, 5, 31, 28)

@icon("app_switch")
def _(I):
    I.box(0, 9, 8, 24); I.box(25, 9, 33, 24); I.sunset(11, 5, 22, 28)

@icon("window_switch")
def _(I):
    I.sunset(0, 0, 14, 14)
    I.box(19, 0, 33, 14); I.box(0, 19, 14, 33); I.box(19, 19, 33, 33)

@icon("show_desktop")
def _(I):
    I.crt(); I.P([(16.5, 18), (11, 12), (22, 12)]); I.pin(14, 6, 19, 11)

@icon("peek_desktop")
def _(I):
    I.crt()
    for x in range(8, 26, 3): I.F(x, 7, x, 7); I.F(x, 16, x, 16)

# ===================== screens / desktops =====================
@icon("display")
def _(I):
    I.crt(0, 4, 15, 20); I.box(18, 4, 33, 20); I.sunset(20, 6, 31, 18)
    I.P([(23, 21), (28, 21), (30, 26), (21, 26)])

def crt_chip(I, sym):
    I.crt(cx=9); I.chip(sym)

for _n, _s in (("desktop_new", "+"), ("desktop_prev", "l"), ("desktop_next", "r"),
               ("desktop_close", "x"), ("system_props", "i"), ("gfx_restart", "ref"),
               ("screen_record", "dot")):
    ICONS[_n] = (lambda s: (lambda I: crt_chip(I, s)))(_s)

@icon("cast")
def _(I):
    I.L([(1, 10), (1, 1), (32, 1), (32, 24), (20, 24)], w=2)
    I.F(0, 29, 4, 33)
    for r in (10, 16, 22):
        I.d.arc([-r, 33 - r, r, 33 + r], 270, 360, fill=1, width=2)

@icon("network")
def _(I):
    I.F(16, 9, 17, 15); I.F(5, 14, 28, 15); I.F(5, 14, 6, 22); I.F(27, 14, 28, 22)
    I.sunset(10, 0, 23, 9); I.box(0, 22, 11, 33); I.box(22, 22, 33, 33)

# ===================== capture =====================
@icon("screenshot")
def _(I):
    I.box(0, 7, 33, 31); I.F(9, 2, 22, 7)
    I.pin(2, 9, 31, 12)
    I.d.ellipse([10, 13, 23, 26], outline=1, width=2); I.F(15, 18, 18, 21)

@icon("snip")
def _(I):
    for x in range(0, 34, 4):
        I.F(x, 0, x + 1, 1); I.F(x + 2, 32, x + 3, 33)
    for y in range(0, 34, 4):
        I.F(0, y + 2, 1, y + 3); I.F(32, y, 33, y + 1)
    I.F(15, 8, 18, 25); I.F(8, 15, 25, 18)

@icon("text_recog")
def _(I):
    for x0, y0, x1, y1 in ((0, 0, 7, 1), (0, 0, 1, 7), (26, 0, 33, 1), (32, 0, 33, 7),
                           (0, 32, 7, 33), (0, 26, 1, 33), (26, 32, 33, 33), (32, 26, 33, 33)):
        I.F(x0, y0, x1, y1)
    I.F(7, 6, 26, 11); I.F(14, 6, 19, 27); I.pin(7, 6, 26, 11)

# ===================== system =====================
@icon("lock")
def _(I):
    I.F(8, 4, 10, 15); I.F(23, 4, 25, 15); I.F(10, 1, 23, 3)
    I.sunset(3, 15, 30, 33); I.F(15, 20, 18, 27, 0)

@icon("run")
def _(I):
    I.window(0, 1, 33, 32)
    I.P([(5, 14), (8, 14), (13, 19), (8, 24), (5, 24), (10, 19)])
    I.F(16, 16, 22, 25)                                  # block cursor

@icon("settings")
def _(I):
    I.F(13, 0, 20, 33); I.F(0, 13, 33, 20)
    I.P([(4, 7), (7, 4), (29, 26), (26, 29)]); I.P([(26, 4), (29, 7), (7, 29), (4, 26)])
    I.d.ellipse([5, 5, 28, 28], fill=1)
    I.d.ellipse([11, 11, 22, 22], fill=0); I.pin(13, 13, 20, 20)

@icon("quick_settings")
def _(I):
    I.box(0, 2, 33, 13); I.sunset(18, 4, 31, 11)
    I.box(0, 20, 33, 31); I.F(2, 22, 15, 29)

@icon("tray")
def _(I):
    I.box(0, 23, 33, 33); I.pin(3, 26, 30, 30)
    I.P([(25, 2), (32, 9), (18, 9)]); I.F(23, 9, 27, 18)

@icon("quick_menu")
def _(I):
    I.box(7, 0, 33, 24); I.pin(10, 3, 30, 6)
    I.F(11, 10, 29, 11); I.F(11, 15, 29, 16); I.F(11, 20, 24, 21)
    I.F(0, 26, 7, 33)

@icon("task_cycle")
def _(I):
    I.box(0, 21, 33, 33); I.F(4, 25, 9, 29); I.box(13, 25, 18, 29, w=1); I.box(23, 25, 28, 29, w=1)
    I.arrow(32, 9, "r", length=22, s=7); I.speed(0, 6, (4, 13))

@icon("explorer")
def _(I):
    I.box(1, 8, 32, 31); I.F(1, 4, 13, 9); I.pin(3, 11, 30, 14)

@icon("accessibility")
def _(I):
    I.F(14, 0, 19, 5)
    I.F(2, 9, 31, 12); I.F(14, 9, 19, 20)
    I.P([(14, 20), (19, 20), (11, 33), (6, 33)]); I.P([(14, 20), (19, 20), (27, 33), (22, 33)])

# ===================== input / media =====================
@icon("dictation")
def _(I):
    I.box(10, 0, 23, 20); I.pin(13, 3, 20, 17)           # the grille
    I.L([(5, 12), (5, 17), (10, 23), (23, 23), (28, 17), (28, 12)], w=2)
    I.F(16, 24, 17, 29); I.F(10, 30, 23, 32)

@icon("speech_rec")
def _(I):
    I.box(0, 1, 33, 24); I.P([(5, 23), (5, 31), (12, 24)])
    for x, h in ((6, 2), (11, 5), (16, 8), (21, 4), (26, 6)):
        I.F(x, 12 - h, x + 2, 13 + h)

@icon("narrator")
def _(I):
    I.F(1, 11, 7, 22); I.P([(7, 11), (15, 3), (15, 30), (7, 22)])
    for x, h in ((19, 4), (24, 8), (29, 12)):
        I.F(x, 17 - h, x + 1, 16 + h)

@icon("volume_mixer")
def _(I):
    for x, k in ((5, 19), (16, 5), (27, 12)):
        I.F(x, 0, x + 1, 33); I.F(x - 4, k, x + 5, k + 8, 0); I.box(x - 4, k, x + 5, k + 8); I.pin(x - 2, k + 2, x + 3, k + 6)

@icon("emoji")
def _(I):
    I.box(0, 0, 33, 33)
    I.F(9, 8, 12, 13); I.F(21, 8, 24, 13)
    I.F(6, 19, 8, 21); I.F(25, 19, 27, 21); I.F(8, 22, 25, 25)

@icon("zoom_in")
def _(I):
    I.lens(13, 13, 11); I.F(7, 12, 19, 14); I.F(12, 7, 14, 19)

@icon("zoom_out")
def _(I):
    I.lens(13, 13, 11); I.F(7, 12, 19, 14)

# ===================== apps / services =====================
@icon("game_bar")
def _(I):
    I.sunset(2, 22, 31, 33)                              # joystick base
    I.F(15, 4, 18, 22); I.d.ellipse([11, 0, 22, 9], fill=1)
    I.F(24, 17, 29, 21)

@icon("feedback")
def _(I):
    I.box(0, 1, 33, 24); I.P([(5, 23), (5, 31), (12, 24)])
    I.F(15, 5, 18, 15); I.F(15, 18, 18, 20)

@icon("copilot")
def _(I):
    # 8-bit sparkle: arms that taper in 2 px steps
    for half in ((1, 15), (3, 9), (5, 5), (9, 3), (15, 1)):
        hx, hy = half
        I.F(13 - hx, 16 - hy, 13 + hx, 16 + hy)
    I.F(26, 1, 27, 9); I.F(23, 4, 30, 5)

@icon("quick_assist")
def _(I):
    I.L([(3, 17), (3, 9), (9, 3), (24, 3), (30, 9), (30, 17)], w=3)
    I.sunset(0, 16, 8, 28); I.sunset(25, 16, 33, 28)
    I.F(28, 28, 30, 32); I.F(18, 30, 30, 32); I.F(12, 29, 19, 33)


def render(name):
    I = Icon()
    ICONS[name](I)
    return I.im
