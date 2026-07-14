#!/usr/bin/env python3
"""Faithful preview of the split72 status OLED (128x64) — the same panel that
``split72/status_oled.c`` (`oled_update_buffer`) composes on hardware.

It parses the real committed status fonts + IconsFont straight from the headers
(continuous-bit-packed GFXfont, comment-stripped so ``/* range 0x20-0x7e */`` hex
in the array header is NOT mistaken for pixel bytes — that bit me once), then
draws each element at the same (x, baseline) coordinates the C code uses. Handy
for eyeballing layout tweaks (row nudges, new fields) without flashing.

Two modes:
  * default            -> both halves, cool-white-on-black, as the SSD1306 shows.
  * ``--diag``         -> clipping diagnostic: a 6px gutter + panel-edge frame,
                          and any pixel drawn OUTSIDE the 128x64 panel is RED
                          (i.e. clipped by the hardware's SET_PIXEL_CLIPPED). Use
                          it to check that a nudged row / tall glyph isn't losing
                          pixels at an edge (mirrors the keycap HTML preview's
                          no-clip margin check).

Usage (run from keyboards/polykybd/):
    python3 tools/status_oled_preview.py            [-o out.png]
    python3 tools/status_oled_preview.py --diag     [-o diag.png]

Only stdlib + Pillow. Representative live values (layer/brightness/WPM/lang and
RGB mode/HSV/speed) are placeholders — tweak the dicts in build_panel().
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
FONTDIR = os.path.normpath(os.path.join(HERE, "..", "base", "fonts"))

P_W, P_H = 128, 64  # split72 status OLED

# ---- coordinates, kept in sync with split72/status_oled.c oled_update_buffer ----
TOP_BASE = 15   # first text line (layer icon / hex layer / side)  [was 14]
ROW2 = 29       # layout name / "RGB <mode>"                        [was 30]
ROW3 = 44       # Dsp brightness / HSV
ROW4 = 59       # WPM+lang / Speed                                  [was 58]


# ----------------------------- GFXfont parsing -----------------------------
def _parse_header(text, bitmaps, glyphs, fonts):
    for m in re.finditer(r'const\s+uint8_t\s+(\w+)\s*\[\]\s*PROGMEM\s*=\s*\{(.*?)\};', text, re.S):
        body = re.sub(r'/\*.*?\*/', '', m.group(2), flags=re.S)
        body = re.sub(r'//[^\n]*', '', body)
        bitmaps[m.group(1)] = [int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body)]
    for m in re.finditer(r'const\s+GFXglyph\s+(\w+)\s*\[\]\s*(?:PROGMEM\s*)?=\s*\{(.*?)\};', text, re.S):
        body = re.sub(r'//[^\n]*', '', m.group(2))
        arr = []
        for g in re.finditer(r'\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}', body):
            v = list(map(int, g.groups()))
            arr.append(dict(off=v[0], w=v[1], h=v[2], xa=v[3], xo=v[4], yo=v[5]))
        glyphs[m.group(1)] = arr
    for m in re.finditer(r'const\s+GFXfont\s+(\w+)\s+PROGMEM\s*=\s*\{(.*?)\};', text, re.S):
        parts = [p.strip() for p in re.sub(r'//[^\n]*', '', m.group(2)).split(',')]
        fonts[m.group(1)] = dict(bmp=re.findall(r'\w+', parts[0])[-1],
                                 gly=re.findall(r'\w+', parts[1])[-1],
                                 first=int(parts[2], 0), last=int(parts[3], 0))


def load_fonts():
    B, G, F = {}, {}, {}
    for rel in ("NotoSans_Regular_Base_11pt.h", "NotoSans_Medium_Base_8pt.h", "gfx_icons.h"):
        _parse_header(open(os.path.join(FONTDIR, rel)).read(), B, G, F)

    def bundle(name):
        f = F[name]
        return f, B[f['bmp']], G[f['gly']]

    disp = bundle('NotoSans_Regular11pt7b')
    small = bundle('NotoSans_Medium8pt7b')
    icons = None
    for n, f in F.items():
        if f['first'] <= 0x80 <= f['last'] and f['gly'] in G:
            icons = (f, B[f['bmp']], G[f['gly']])
            break
    return disp, small, icons


def draw(setpix, font, x, y, text):
    """Draw a codepoint list at (x, baseline y). setpix(px, py) records a lit
    pixel; it decides in/out-of-bounds (no clipping here, so diag can see it)."""
    f, bm, gl = font
    for cp in text:
        if not (f['first'] <= cp <= f['last']):
            continue
        g = gl[cp - f['first']]
        bo = g['off']; bit = 0; bits = 0
        for gy in range(g['h']):
            for gx in range(g['w']):
                if (bit & 7) == 0:
                    bits = bm[bo]; bo += 1
                if bits & 0x80:
                    setpix(x + g['xo'] + gx, y + g['yo'] + gy)
                bits = (bits << 1) & 0xFF
                bit += 1
        x += g['xa']


# Top-row role icons (16x16, MSB-first) — mirror of status_oled.c usb_/link_status_bitmap.
USB_BMP = [0x00, 0x80, 0x01, 0xc0, 0x01, 0xc0, 0x03, 0xe0, 0x03, 0xe0, 0x00, 0x80, 0x00, 0xb8, 0x04, 0xb8,
           0x0e, 0xb8, 0x0e, 0x90, 0x04, 0xe0, 0x03, 0x80, 0x00, 0x80, 0x01, 0xc0, 0x03, 0x60, 0x01, 0xc0]
LINK_BMP = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x1c, 0x7f, 0xfe, 0x7f, 0xfe, 0x00, 0x00,
            0x00, 0x00, 0x7f, 0xfe, 0x7f, 0xfe, 0x38, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]


def s(t):
    return [ord(c) for c in t]


def draw_bitmap(setpix, data, ox, oy, w=16, h=16):
    bw = (w + 7) // 8
    for y in range(h):
        for x in range(w):
            if data[y * bw + (x >> 3)] & (0x80 >> (x & 7)):
                setpix(ox + x, oy + y)


def build_panel(side, disp, small, icons):
    """side: 'L' (USB host, layout panel) or 'R' (bridge, RGB panel). The role word
    (USB/Link) follows is_usb_host_side() on hardware; here 'L' models the USB half.
    Returns set of (x,y) pixels."""
    pts = set()
    setp = lambda px, py: pts.add((px, py))
    # top line: layer + role icon + role word
    draw(setp, icons, 0, TOP_BASE, [0x80])                 # ICON_LAYER
    draw(setp, disp, 20, TOP_BASE, s('0'))                 # hex layer
    if side == 'L':
        draw_bitmap(setp, USB_BMP, 38, 0)
        draw(setp, disp, 57, TOP_BASE, s('USB'))
    else:
        draw_bitmap(setp, LINK_BMP, 38, 0)
        draw(setp, disp, 57, TOP_BASE, s('Link'))
    draw(setp, icons, 108, 16, [0x8C])                     # NumLock off
    draw(setp, icons, 108, 38, [0x8E])                     # CapsLock off
    if side == 'L':
        draw(setp, small, 114, 56, s('L'))
    else:
        draw(setp, small, 112, 56, s('R'))   # R nudged 2px left of L (see status_oled.c)
    if side == 'L':
        draw(setp, small, 0, ROW2, s('Qwerty'))
        draw(setp, small, 0, ROW3, s('Dsp*')); draw(setp, small, 42, ROW3, s('50')); draw(setp, small, 64, ROW3, s('l' * 10))
        draw(setp, small, 0, ROW4, s('WPM')); draw(setp, small, 44, ROW4, s('0'))
        draw(setp, small, 68, ROW4, s('L')); draw(setp, small, 84, ROW4, s('0'))
    else:
        draw(setp, small, 0, ROW2, s('RGB')); draw(setp, small, 34, ROW2, s('5')); draw(setp, small, 58, ROW2, s('Rnbw'))
        draw(setp, small, 0, ROW3, s('HSV')); draw(setp, small, 38, ROW3, s('0')); draw(setp, small, 60, ROW3, s('FF')); draw(setp, small, 82, ROW3, s('64'))
        draw(setp, small, 0, ROW4, s('Speed')); draw(setp, small, 58, ROW4, s('80'))
    return pts


# ------------------------------- rendering ---------------------------------
ON = (198, 226, 255)
OFF = (7, 9, 13)
RED = (255, 70, 70)


def render_plain(pts, sc=6):
    im = Image.new('RGB', (P_W * sc, P_H * sc), OFF)
    d = ImageDraw.Draw(im)
    for (x, y) in pts:
        if 0 <= x < P_W and 0 <= y < P_H:
            d.rectangle([x * sc, y * sc, x * sc + sc - 1, y * sc + sc - 1], fill=ON)
    return im


def render_diag(pts, title, sc=8, gut=6):
    cw, ch = P_W + 2 * gut, P_H + 2 * gut
    im = Image.new('RGB', (cw * sc, ch * sc), (18, 18, 22))
    d = ImageDraw.Draw(im)
    d.rectangle([gut * sc, gut * sc, (gut + P_W) * sc - 1, (gut + P_H) * sc - 1], fill=OFF)
    clipped = 0
    for (x, y) in sorted(pts):
        inb = (0 <= x < P_W and 0 <= y < P_H)
        if not inb:
            clipped += 1
        cx, cy = (gut + x) * sc, (gut + y) * sc
        d.rectangle([cx, cy, cx + sc - 1, cy + sc - 1], fill=ON if inb else RED)
    d.rectangle([gut * sc - 2, gut * sc - 2, (gut + P_W) * sc + 1, (gut + P_H) * sc + 1],
                outline=(120, 140, 160), width=2)
    d.text((4, 2), title, fill=(200, 215, 235))
    d.text((4, ch * sc - 16), f"clipped(out-of-bounds) pixels: {clipped}",
           fill=RED if clipped else (140, 200, 150))
    return im, clipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--diag', action='store_true', help='clipping diagnostic (gutter + red out-of-bounds)')
    ap.add_argument('-o', '--out', help='output PNG (default: /tmp/status_oled[_diag].png)')
    args = ap.parse_args()

    disp, small, icons = load_fonts()
    L = build_panel('L', disp, small, icons)
    R = build_panel('R', disp, small, icons)

    if args.diag:
        Li, lc = render_diag(L, 'LEFT (layout)  128x64  |  RED = clipped')
        Ri, rc = render_diag(R, 'RIGHT (RGB)    128x64  |  RED = clipped')
        w = max(Li.width, Ri.width)
        c = Image.new('RGB', (w, Li.height + Ri.height + 12), (18, 18, 22))
        c.paste(Li, (0, 0)); c.paste(Ri, (0, Li.height + 12))
        out = args.out or '/tmp/status_oled_diag.png'
        c.save(out)
        print(f"{out}  clipped L={lc} R={rc}")
    else:
        Li, Ri = render_plain(L), render_plain(R)
        pad, gap = 16, 24
        c = Image.new('RGB', (pad * 2 + Li.width * 2 + gap, pad * 2 + Li.height), (26, 26, 30))
        for i, im in enumerate((Li, Ri)):
            c.paste(im, (pad + i * (Li.width + gap), pad))
        out = args.out or '/tmp/status_oled.png'
        c.save(out)
        print(out)


if __name__ == '__main__':
    sys.exit(main())
