#!/usr/bin/env python3
"""Style D for the OS shortcut-hint icons: STAGE AND ACTOR.

A hint appears only while a modifier is held: it previews what is about to
happen. So every icon is a short sentence.

  1. STAGE + ACTOR. The stage is what is affected (a window, a screen, a page,
     a line of text) and is drawn LIGHT: a 2 px line. The actor is what happens
     and is drawn HEAVY: solid, or 4 px. One actor per icon. Where the object
     itself is the point (settings, lock, folder) it is the actor and there is
     no stage.
  2. ONLY 0, 45 AND 90 DEGREES. Those are the only angles a 1-bit grid draws
     cleanly, so every curve becomes a 45-degree chamfer: round things are
     octagons. That is the family's signature.
  3. THE WEDGE. Every direction is the same solid 45-degree wedge.
  4. THE CORNER (from Fluent). A modifier is a solid octagon in the bottom-right
     corner with its symbol cut out.
"""
from PIL import Image, ImageDraw

S = 34
ICONS = {}


def icon(name):
    def deco(fn):
        ICONS[name] = fn
        return fn
    return deco


def octo(x0, y0, x1, y1, c):
    """Points of a rectangle with 45-degree chamfered corners of size c."""
    return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c),
            (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]


class Icon:
    def __init__(self):
        self.im = Image.new("1", (S, S), 0)
        self.d = ImageDraw.Draw(self.im)

    def F(self, x0, y0, x1, y1, c=1):
        self.d.rectangle([x0, y0, x1, y1], fill=c)

    # ---- the two weights, on chamfered (octagonal) shapes ----
    def solid(self, x0, y0, x1, y1, ch=3, c=1):
        self.d.polygon(octo(x0, y0, x1, y1, ch), fill=c)

    def ring(self, x0, y0, x1, y1, ch=3, w=2, c=1):
        """Chamfered outline of width w: fill, then cut the inset."""
        self.solid(x0, y0, x1, y1, ch, c)
        self.solid(x0 + w, y0 + w, x1 - w, y1 - w, max(ch - w // 2, 0), 1 - c)

    def thin(self, x0, y0, x1, y1, ch=2):
        self.ring(x0, y0, x1, y1, ch, w=2)

    def heavy(self, x0, y0, x1, y1, ch=3):
        self.ring(x0, y0, x1, y1, ch, w=4)

    def oct(self, cx, cy, r, w=None, c=1):
        """Octagon of 'radius' r (half the box), solid or w px outline."""
        ch = round(r * 0.6)
        b = (cx - r, cy - r, cx + r, cy + r)
        if w is None:
            self.solid(*b, ch, c)
        else:
            self.ring(*b, ch, w, c)

    def wedge(self, x, y, dirn, s=6, c=1):
        """Solid 45-degree wedge, tip at (x, y), depth s."""
        p = {"r": [(x, y), (x - s, y - s), (x - s, y + s)],
             "l": [(x, y), (x + s, y - s), (x + s, y + s)],
             "u": [(x, y), (x - s, y + s), (x + s, y + s)],
             "d": [(x, y), (x - s, y - s), (x + s, y - s)]}[dirn]
        self.d.polygon(p, fill=c)

    def dash_h(self, x0, x1, y, w=2, on=3, off=2):
        x = x0
        while x <= x1:
            self.F(x, y, min(x + on - 1, x1), y + w - 1)
            x += on + off

    def dash_box(self, x0, y0, x1, y1, on=3, off=2):
        self.dash_h(x0, x1, y0, on=on, off=off); self.dash_h(x0, x1, y1 - 1, on=on, off=off)
        y = y0
        while y <= y1:
            self.F(x0, y, x0 + 1, min(y + on - 1, y1)); self.F(x1 - 1, y, x1, min(y + on - 1, y1))
            y += on + off

    # ---- stages ----
    def window(self, x0, y0, x1, y1):
        self.thin(x0, y0, x1, y1)
        self.F(x0 + 2, y0 + 5, x1 - 2, y0 + 6)                    # title line

    def screen(self, x0=0, y0=1, x1=33, y1=22, cx=None):
        self.thin(x0, y0, x1, y1)
        cx = (x0 + x1) // 2 if cx is None else cx
        self.F(cx - 1, y1 + 1, cx, y1 + 3)
        self.F(cx - 6, y1 + 4, cx + 5, y1 + 5)

    def sheet(self, x0, y0, x1, y1, f=7, heavy=False):
        pts = [(x0, y0), (x1 - f, y0), (x1, y0 + f), (x1, y1), (x0, y1)]
        self.d.polygon(pts, fill=1)
        if not heavy:
            w = 2
            self.d.polygon([(x0 + w, y0 + w), (x1 - f - 1, y0 + w), (x1 - w, y0 + f + 1),
                            (x1 - w, y1 - w), (x0 + w, y1 - w)], fill=0)

    # ---- the corner modifier ----
    def corner(self, sym):
        self.oct(25.5, 25.5, 9, c=0)                                 # moat
        self.oct(25.5, 25.5, 7.5)                                    # 16 px octagon
        k = 0
        if sym == "+": self.F(21, 24, 30, 27, k); self.F(24, 21, 27, 30, k)
        if sym == "-": self.F(21, 24, 30, 27, k)
        if sym == "x":
            for t in range(8):
                self.F(21 + t, 21 + t, 22 + t, 22 + t, k); self.F(29 - t, 21 + t, 30 - t, 22 + t, k)
        if sym == "l": self.wedge(21, 25.5, "l", s=4, c=k); self.F(25, 24, 30, 27, k)
        if sym == "r": self.wedge(30, 25.5, "r", s=4, c=k); self.F(21, 24, 26, 27, k)
        if sym == "d": self.wedge(25.5, 30, "d", s=4, c=k); self.F(24, 21, 27, 26, k)
        if sym == "i": self.F(24, 20, 27, 22, k); self.F(24, 24, 27, 31, k)
        if sym == "clock": self.F(24, 20, 26, 26, k); self.F(24, 24, 30, 26, k)
        if sym == "ref":
            self.oct(25.5, 25.5, 4, w=2, c=k); self.F(26, 20, 31, 25, 1)
            self.d.polygon([(26, 19), (31, 22), (26, 25)], fill=k)
        if sym == "dot":
            pass                                                     # the solid octagon IS the record dot


# ===================== editing =====================
@icon("copy")
def _(I):
    I.sheet(1, 8, 21, 33)                       # stage: the original
    I.F(9, 0, 33, 27, 0)
    I.sheet(11, 1, 32, 25, heavy=True)          # actor: the copy

@icon("cut")
def _(I):
    I.dash_h(0, 33, 16)                          # stage: the content line
    I.oct(8, 27, 5, w=3); I.oct(25, 27, 5, w=3)
    I.d.polygon([(10, 22), (13, 22), (27, 2), (24, 2)], fill=1)
    I.d.polygon([(20, 22), (23, 22), (9, 2), (6, 2)], fill=1)

@icon("paste")
def _(I):
    I.thin(3, 4, 30, 33)
    I.F(10, 1, 23, 8, 0); I.thin(11, 1, 22, 7, ch=1)
    I.sheet(8, 12, 25, 29, f=5, heavy=True)

@icon("clip_history")
def _(I):
    I.thin(3, 4, 30, 33)
    I.F(10, 1, 23, 8, 0); I.thin(11, 1, 22, 7, ch=1)
    I.F(8, 12, 25, 15); I.F(8, 19, 25, 22); I.F(8, 26, 18, 29)
    I.corner("clock")

@icon("undo")
def _(I):
    I.dash_h(0, 33, 31)                          # stage: the timeline
    I.d.polygon([(10, 7), (25, 7), (30, 12), (30, 21), (25, 26), (14, 26),
                 (14, 22), (24, 22), (26, 20), (26, 13), (24, 11), (10, 11)], fill=1)
    I.wedge(1, 9, "l", s=8)

@icon("redo")
def _(I):
    I.dash_h(0, 33, 31)
    I.d.polygon([(23, 7), (8, 7), (3, 12), (3, 21), (8, 26), (19, 26),
                 (19, 22), (9, 22), (7, 20), (7, 13), (9, 11), (23, 11)], fill=1)
    I.wedge(32, 9, "r", s=8)

@icon("select_all")
def _(I):
    I.dash_box(0, 2, 33, 31)
    I.solid(6, 8, 27, 25)

@icon("find")
def _(I):
    I.sheet(0, 0, 22, 30)
    I.F(4, 10, 14, 11); I.F(4, 15, 11, 16)
    I.oct(21, 21, 9, c=0); I.oct(20, 20, 7, w=4)
    I.d.polygon([(25, 28), (28, 25), (34, 31), (31, 34)], fill=1)

@icon("search")
def _(I):
    I.oct(13, 13, 12, w=4)
    I.d.polygon([(21, 25), (25, 21), (34, 30), (30, 34)], fill=1)

@icon("save")
def _(I):
    I.d.polygon(octo(1, 1, 32, 32, 0)[:1] + [(26, 1), (32, 7), (32, 32), (1, 32)], fill=1)
    I.d.polygon([(3, 3), (25, 3), (30, 8), (30, 30), (3, 30)], fill=0)
    I.F(8, 1, 23, 11)                            # actor: the shutter
    I.solid(7, 17, 26, 32, ch=2)

@icon("open")
def _(I):
    I.d.line([(1, 31), (1, 6), (11, 6), (14, 9), (27, 9), (27, 13)], fill=1, width=2)
    I.d.line([(1, 31), (31, 31)], fill=1, width=2)
    I.wedge(19, 8, "u", s=8); I.F(17, 15, 21, 26)          # actor: up and out

@icon("print")
def _(I):
    I.thin(0, 8, 33, 23)
    I.F(8, 1, 25, 2); I.F(8, 1, 9, 8); I.F(24, 1, 25, 8)
    I.F(6, 18, 27, 33, 0); I.sheet(8, 18, 25, 33, f=4, heavy=True)

@icon("delete")
def _(I):
    I.F(1, 5, 32, 8); I.F(11, 1, 22, 4)
    I.d.polygon([(4, 11), (29, 11), (27, 33), (6, 33)], fill=1)
    I.F(11, 15, 12, 29, 0); I.F(16, 15, 17, 29, 0); I.F(21, 15, 22, 29, 0)

@icon("rename")
def _(I):
    I.thin(0, 8, 33, 26)
    I.F(13, 4, 20, 30, 0)
    I.F(15, 2, 18, 32); I.F(11, 2, 22, 4); I.F(11, 30, 22, 32)

@icon("refresh")
def _(I):
    I.oct(16, 18, 13, w=4)
    I.F(18, 3, 33, 12, 0)
    I.d.polygon([(16, 1), (26, 7), (16, 13)], fill=1)

# ===================== text navigation =====================
@icon("word_left")
def _(I):
    I.thin(20, 9, 33, 25, ch=2)                  # stage: the word
    I.F(0, 9, 3, 25); I.wedge(5, 17, "l", s=7); I.F(12, 15, 17, 19)

@icon("word_right")
def _(I):
    I.thin(0, 9, 13, 25, ch=2)
    I.F(30, 9, 33, 25); I.wedge(28, 17, "r", s=7); I.F(16, 15, 21, 19)

@icon("line_start")
def _(I):
    I.dash_h(8, 33, 28)                          # stage: the line of text
    I.F(0, 4, 3, 32); I.wedge(5, 14, "l", s=8); I.F(13, 12, 30, 16)

@icon("line_end")
def _(I):
    I.dash_h(0, 25, 28)
    I.F(30, 4, 33, 32); I.wedge(28, 14, "r", s=8); I.F(3, 12, 20, 16)

# ===================== windows =====================
@icon("close")
def _(I):
    I.window(0, 1, 33, 32)
    I.d.polygon([(9, 12), (12, 12), (24, 24), (24, 27), (21, 27), (9, 15)], fill=1)
    I.d.polygon([(24, 12), (24, 15), (12, 27), (9, 27), (9, 24), (21, 12)], fill=1)

@icon("quit")
def _(I):
    I.window(0, 1, 33, 32); I.corner("x")

@icon("minimize")
def _(I):
    I.window(0, 1, 33, 32); I.wedge(16.5, 21, "d", s=6); I.F(7, 25, 26, 28)

@icon("maximize")
def _(I):
    I.window(0, 1, 33, 32); I.solid(6, 11, 27, 27, ch=2)

@icon("fullscreen")
def _(I):
    I.thin(8, 8, 25, 25)
    for x, y, sx, sy in ((0, 0, 1, 1), (33, 0, -1, 1), (0, 33, 1, -1), (33, 33, -1, -1)):
        I.d.polygon([(x, y), (x + sx * 6, y), (x, y + sy * 6)], fill=1)

@icon("minimize_all")
def _(I):
    I.window(9, 0, 33, 16); I.F(0, 5, 26, 26, 0); I.window(1, 7, 24, 25)
    I.corner("d")

@icon("minimize_others")
def _(I):
    I.solid(4, 0, 29, 20)                         # actor: the one that stays
    I.F(6, 5, 27, 6, 0)
    I.F(0, 28, 7, 29); I.F(13, 28, 20, 29); I.F(26, 28, 33, 29)

@icon("snap_left")
def _(I):
    I.thin(0, 3, 33, 30); I.solid(0, 3, 16, 30)

@icon("snap_right")
def _(I):
    I.thin(0, 3, 33, 30); I.solid(17, 3, 33, 30)

@icon("app_switch")
def _(I):
    I.thin(0, 9, 8, 24, ch=1); I.thin(25, 9, 33, 24, ch=1)
    I.solid(11, 5, 22, 28)

@icon("window_switch")
def _(I):
    I.solid(0, 0, 14, 14)
    I.thin(19, 0, 33, 14); I.thin(0, 19, 14, 33); I.thin(19, 19, 33, 33)

@icon("show_desktop")
def _(I):
    I.screen(); I.wedge(16.5, 18, "d", s=7); I.F(15, 5, 18, 11)

@icon("peek_desktop")
def _(I):
    I.screen()
    I.d.polygon([(4, 11.5), (10, 6), (23, 6), (29, 11.5), (23, 17), (10, 17)], fill=1)
    I.oct(16.5, 11.5, 3, c=0)

# ===================== screens / desktops =====================
@icon("display")
def _(I):
    I.screen(0, 6, 15, 21)                       # stage: this display
    I.solid(18, 6, 33, 21, ch=2)                 # actor: the second one
    I.F(25, 22, 26, 24); I.F(21, 25, 30, 26)

def screen_corner(I, sym):
    I.screen(cx=9); I.corner(sym)

for _n, _s in (("desktop_new", "+"), ("desktop_prev", "l"), ("desktop_next", "r"),
               ("desktop_close", "x"), ("system_props", "i"), ("screen_record", "dot")):
    ICONS[_n] = (lambda s: (lambda I: screen_corner(I, s)))(_s)

@icon("gfx_restart")
def _(I):
    # the one corner without the octagon: a knocked-out ring reads as a "G" at
    # 16 px, so the actor is a heavy octagonal circular arrow in the same corner
    I.screen(cx=9)
    I.oct(25.5, 25.5, 9, c=0)
    I.oct(25.5, 26, 7.5, w=3)
    I.F(26, 17, 34, 25, 0)
    I.d.polygon([(23, 15), (30, 20), (23, 25)], fill=1)

@icon("cast")
def _(I):
    I.d.line([(1, 10), (1, 2), (32, 2), (32, 24), (20, 24)], fill=1, width=2)
    I.solid(0, 28, 5, 33, ch=1)
    for r in (11, 18):
        I.d.polygon([(0, 33 - r), (round(r * 0.45), 33 - r), (r, 33 - round(r * 0.45)), (r, 33),
                     (r - 3, 33), (r - 3, 33 - round((r - 3) * 0.45)), (round((r - 3) * 0.45), 36 - r), (0, 36 - r)], fill=1)

@icon("network")
def _(I):
    I.F(16, 9, 17, 15); I.F(5, 14, 28, 15); I.F(5, 14, 6, 22); I.F(27, 14, 28, 22)
    I.solid(10, 0, 23, 9); I.solid(0, 22, 11, 33); I.solid(22, 22, 33, 33)

# ===================== capture =====================
@icon("screenshot")
def _(I):
    I.solid(0, 7, 33, 31); I.solid(9, 2, 22, 8, ch=1)
    I.oct(16.5, 19, 7, c=0); I.oct(16.5, 19, 4)

@icon("snip")
def _(I):
    I.dash_box(0, 0, 33, 33)
    I.F(15, 7, 18, 26); I.F(7, 15, 26, 18)

@icon("text_recog")
def _(I):
    for x, y, sx, sy in ((0, 0, 1, 1), (33, 0, -1, 1), (0, 33, 1, -1), (33, 33, -1, -1)):
        I.F(min(x, x + sx * 7), min(y, y + sy), max(x, x + sx * 7), max(y, y + sy))
        I.F(min(x, x + sx), min(y, y + sy * 7), max(x, x + sx), max(y, y + sy * 7))
    I.F(8, 8, 25, 11); I.F(15, 8, 18, 26)

# ===================== system =====================
@icon("lock")
def _(I):
    I.d.polygon([(8, 15), (8, 6), (12, 2), (21, 2), (25, 6), (25, 15),
                 (21, 15), (21, 8), (19, 6), (14, 6), (12, 8), (12, 15)], fill=1)
    I.solid(3, 15, 30, 33)
    I.F(15, 21, 18, 27, 0)

@icon("run")
def _(I):
    I.window(0, 1, 33, 32)
    I.d.polygon([(5, 12), (9, 12), (15, 18), (9, 24), (5, 24), (11, 18)], fill=1)
    I.F(17, 22, 27, 25)

@icon("settings")
def _(I):
    I.oct(16.5, 16.5, 12)
    for x0, y0, x1, y1 in ((14, 0, 19, 33), (0, 14, 33, 19)):
        I.F(x0, y0, x1, y1)
    I.d.polygon([(4, 7), (7, 4), (29, 26), (26, 29)], fill=1)
    I.d.polygon([(26, 4), (29, 7), (7, 29), (4, 26)], fill=1)
    I.oct(16.5, 16.5, 5, c=0)

@icon("quick_settings")
def _(I):
    I.thin(0, 2, 33, 14, ch=4); I.solid(19, 2, 33, 14, ch=4)
    I.thin(0, 19, 33, 31, ch=4); I.solid(0, 19, 14, 31, ch=4)

@icon("tray")
def _(I):
    I.thin(0, 23, 33, 33, ch=2)
    I.wedge(25, 6, "u", s=8); I.F(23, 14, 27, 18)

@icon("quick_menu")
def _(I):
    I.thin(7, 0, 33, 24)
    I.F(12, 6, 28, 7); I.F(12, 11, 28, 12); I.F(12, 16, 24, 17)
    I.solid(0, 25, 8, 33, ch=1)

@icon("task_cycle")
def _(I):
    I.thin(0, 21, 33, 33, ch=2)
    I.solid(4, 25, 10, 29, ch=1); I.F(15, 26, 19, 27); I.F(24, 26, 28, 27)
    I.F(2, 8, 22, 11); I.wedge(31, 9.5, "r", s=8)

@icon("explorer")
def _(I):
    I.d.polygon([(1, 4), (12, 4), (15, 8), (32, 8), (32, 31), (1, 31)], fill=1)
    I.F(1, 12, 32, 13, 0)

@icon("accessibility")
def _(I):
    I.oct(16.5, 4, 4)
    I.F(2, 10, 31, 13); I.F(14, 10, 19, 21)
    I.d.polygon([(14, 21), (18, 21), (11, 33), (7, 33)], fill=1)
    I.d.polygon([(15, 21), (19, 21), (26, 33), (22, 33)], fill=1)

# ===================== input / media =====================
@icon("dictation")
def _(I):
    I.solid(11, 0, 22, 20, ch=4)
    I.d.line([(5, 12), (5, 17), (10, 23), (23, 23), (28, 17), (28, 12)], fill=1, width=2)
    I.F(16, 24, 17, 29); I.F(10, 30, 23, 32)

@icon("speech_rec")
def _(I):
    I.thin(0, 1, 33, 24)
    I.d.polygon([(5, 23), (5, 31), (12, 24)], fill=1)
    for x, h in ((6, 2), (11, 5), (16, 8), (21, 4), (26, 6)):
        I.F(x, 12 - h, x + 2, 13 + h)

@icon("narrator")
def _(I):
    I.F(1, 11, 7, 22); I.d.polygon([(7, 11), (15, 3), (15, 30), (7, 22)], fill=1)
    I.d.line([(20, 11), (22, 13), (22, 20), (20, 22)], fill=1, width=2)
    I.d.line([(25, 6), (29, 10), (29, 23), (25, 27)], fill=1, width=2)

@icon("volume_mixer")
def _(I):
    for x, k in ((5, 19), (16, 5), (27, 12)):
        I.F(x, 0, x + 1, 33)
        I.solid(x - 4, k, x + 5, k + 8, ch=2)

@icon("emoji")
def _(I):
    I.oct(16.5, 16.5, 16, w=2)
    I.F(10, 9, 13, 14); I.F(20, 9, 23, 14)
    I.d.polygon([(7, 19), (26, 19), (21, 26), (12, 26)], fill=1)

@icon("zoom_in")
def _(I):
    I.oct(13, 13, 12, w=4); I.F(7, 12, 19, 14); I.F(12, 7, 14, 19)
    I.d.polygon([(21, 25), (25, 21), (34, 30), (30, 34)], fill=1)

@icon("zoom_out")
def _(I):
    I.oct(13, 13, 12, w=4); I.F(7, 12, 19, 14)
    I.d.polygon([(21, 25), (25, 21), (34, 30), (30, 34)], fill=1)

# ===================== apps / services =====================
@icon("game_bar")
def _(I):
    I.solid(0, 7, 33, 27, ch=7)
    I.F(5, 16, 13, 18, 0); I.F(8, 13, 10, 21, 0)
    I.F(21, 13, 24, 16, 0); I.F(25, 18, 28, 21, 0)

@icon("feedback")
def _(I):
    I.thin(0, 1, 33, 24)
    I.d.polygon([(5, 23), (5, 31), (12, 24)], fill=1)
    I.F(15, 5, 18, 15); I.F(15, 18, 18, 20)

@icon("copilot")
def _(I):
    I.d.polygon([(13, 0), (17, 12), (28, 16), (17, 20), (13, 32), (9, 20), (0, 16), (9, 12)], fill=1)
    I.d.polygon([(27, 0), (28, 4), (32, 5), (28, 6), (27, 10), (26, 6), (22, 5), (26, 4)], fill=1)

@icon("quick_assist")
def _(I):
    I.d.line([(3, 17), (3, 9), (9, 3), (24, 3), (30, 9), (30, 17)], fill=1, width=3)
    I.solid(0, 16, 8, 28, ch=2); I.solid(25, 16, 33, 28, ch=2)
    I.F(28, 28, 30, 32); I.F(18, 30, 30, 32)
    I.solid(12, 29, 19, 33, ch=1)


def render(name):
    I = Icon()
    ICONS[name](I)
    return I.im
