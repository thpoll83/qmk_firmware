#!/usr/bin/env python3
"""Faithful preview of the split42 status OLED — the PORTRAIT 32x128 layout that
``split42/status_oled.c`` (`oled_update_buffer`) composes on hardware.

The panel is physically 128x32 but mounted rotated 90°, so the firmware composes
in a logical 32-wide x 128-tall space and software-rotates each pixel into the
128x32 page buffer. This tool renders that same logical portrait space (it does
NOT re-apply the rotation — the point is to eyeball the upright portrait the user
sees), parsing the real committed fonts straight from the headers.

It mirrors the C coordinate-for-coordinate: role icon + Usb/Lnk, layer icon +
number, half-scale layout name, a per-side band (LEFT = brightness bars + WPM,
RIGHT = Num/Caps lock), a half-scale globe + centered lang index, and the bottom
L/R marker. Short layout names (Qwrty/Stag!/ColDH/Neo/Wkmn/Unkn) match
`layout_name_short()`.

Modes:
  * default   -> both side variants, cool-white-on-black.
  * --diag    -> clipping diagnostic: any pixel outside the 32x128 logical area is
                 RED (mirrors the C pset() clip), with a panel-edge frame.

Usage (run from keyboards/polykybd/):
    python3 tools/status_oled42_preview.py         [-o out.png]
    python3 tools/status_oled42_preview.py --diag  [-o diag.png]

Only stdlib + Pillow.
"""
import argparse
import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
FONTDIR = os.path.normpath(os.path.join(HERE, "..", "base", "fonts"))

P_W, P_H = 32, 128            # logical portrait (= physical 128x32 rotated 90°)
FULL_BRIGHT = 50             # config.h

# Role icons (16x16 MSB-first) — mirror of status_oled.c usb_/link_status_bitmap.
USB_BMP = [0x00, 0x80, 0x01, 0xc0, 0x01, 0xc0, 0x03, 0xe0, 0x03, 0xe0, 0x00, 0x80, 0x00, 0xb8, 0x04, 0xb8,
           0x0e, 0xb8, 0x0e, 0x90, 0x04, 0xe0, 0x03, 0x80, 0x00, 0x80, 0x01, 0xc0, 0x03, 0x60, 0x01, 0xc0]
LINK_BMP = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x1c, 0x7f, 0xfe, 0x7f, 0xfe, 0x00, 0x00,
            0x00, 0x00, 0x7f, 0xfe, 0x7f, 0xfe, 0x38, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]

# short names — must match status_oled.c layout_name_short()
SHORT_NAMES = ["Qwrty", "Stag!", "ColDH", "Neo", "Wkmn"]


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
                                 first=int(parts[2], 0), last=int(parts[3], 0),
                                 yadv=int(parts[4], 0))


def load():
    B, G, F = {}, {}, {}
    heads = ["util_font.h", "lang_label_font.h", "gfx_icons.h",
             os.path.join("generated", "emoji_fonts.h")]
    for rel in heads:
        p = os.path.join(FONTDIR, rel)
        if os.path.exists(p):
            _parse_header(open(p).read(), B, G, F)

    def bundle(name):
        f = F[name]
        return f, B[f['bmp']], G[f['gly']]

    mid = bundle('NotoSans_Regular_Mid_10pt7b')
    tiny = bundle('NotoSans_Regular_Tiny_6pt7b')
    icons = next((F[k], B[F[k]['bmp']], G[F[k]['gly']]) for k in F
                 if F[k]['first'] <= 0x80 <= F[k]['last'] and F[k]['gly'] in G)
    world = bundle('NotoEmoji_Medium_World_20pt16b')
    return mid, tiny, icons, world


# ----------------------------- draw primitives -----------------------------
def _glyph(font, cp):
    f, bm, gl = font
    if not (f['first'] <= cp <= f['last']):
        return None, 0
    return gl[cp - f['first']], bm


def draw_glyph(setpix, font, x, baseline, cp):
    g, bm = _glyph(font, cp)
    if not g:
        return 0
    bo = g['off']; bit = 0; bits = 0
    for gy in range(g['h']):
        for gx in range(g['w']):
            if (bit & 7) == 0:
                bits = bm[bo]; bo += 1
            if bits & 0x80:
                setpix(x + g['xo'] + gx, baseline + g['yo'] + gy)
            bits = (bits << 1) & 0xFF; bit += 1
    return g['xa']


def text_adv(font, text):
    return sum(_glyph(font, ord(c))[0]['xa'] for c in text if _glyph(font, ord(c))[0])


def draw_text(setpix, font, x, baseline, text):
    for c in text:
        x += draw_glyph(setpix, font, x, baseline, ord(c))


def draw_text_center(setpix, font, baseline, text):
    draw_text(setpix, font, max(0, (P_W - text_adv(font, text)) // 2), baseline, text)


def draw_glyph_center(setpix, font, baseline, cp):
    g, _ = _glyph(font, cp)
    if g:
        draw_glyph(setpix, font, max(0, (P_W - g['xa']) // 2), baseline, cp)


def draw_glyph_half(setpix, font, x, top_y, cp):
    g, bm = _glyph(font, cp)
    if not g:
        return 0
    bo = g['off']; bit = 0; bits = 0
    for gy in range(g['h']):
        for gx in range(g['w']):
            if (bit & 7) == 0:
                bits = bm[bo]; bo += 1
            if bits & 0x80:
                setpix(x + gx // 2, top_y + gy // 2)
            bits = (bits << 1) & 0xFF; bit += 1
    return (g['h'] + 1) // 2


def draw_text_center_half(setpix, font, top_y, text):
    full = set(); cx = 0
    for c in text:
        g, bm = _glyph(font, ord(c))
        if not g:
            cx += 4; continue
        bo = g['off']; bit = 0; bits = 0
        for gy in range(g['h']):
            for gx in range(g['w']):
                if (bit & 7) == 0:
                    bits = bm[bo]; bo += 1
                if bits & 0x80:
                    full.add((cx + g['xo'] + gx, g['yo'] + gy))
                bits = (bits << 1) & 0xFF; bit += 1
        cx += g['xa']
    if not full:
        return
    minx = min(px for px, _ in full); miny = min(py for _, py in full)
    half = {((px - minx) // 2, (py - miny) // 2) for px, py in full}
    hw = max(px for px, _ in half) + 1
    ox = max(0, (P_W - hw) // 2)
    for (px, py) in half:
        setpix(ox + px, top_y + py)


def draw_bitmap(setpix, data, ox, oy, w=16, h=16):
    bw = (w + 7) // 8
    for y in range(h):
        for x in range(w):
            if data[y * bw + (x >> 3)] & (0x80 >> (x & 7)):
                setpix(ox + x, oy + y)


def draw_brightness(setpix, contrast, top_y):
    bars = min(10, (contrast * 10 + FULL_BRIGHT // 2) // FULL_BRIGHT)
    for i in range(10):
        bx = i * 3
        if i < bars:
            for yy in range(6):
                setpix(bx, top_y + yy); setpix(bx + 1, top_y + yy)
        else:
            setpix(bx, top_y + 5); setpix(bx + 1, top_y + 5)


# ------------------------------- compose -----------------------------------
WPM_BMP = [0x1f,0x00, 0x71,0xc0, 0x43,0x40, 0xc2,0x60, 0x86,0x20, 0x8e,0x20]


def build(side, mid, tiny, icons, world, contrast=35, layout_name=SHORT_NAMES[0],
          lang='en-US', wpm=0):
    """side 'L' (USB host, brightness+WPM) or 'R' (Link bridge, locks)."""
    pts = set()
    def setp(px, py):
        pts.add((px, py))
    # role
    if side == 'L':
        draw_bitmap(setp, USB_BMP, -3, 0); draw_text(setp, tiny, 10, 12, 'Usb')
    else:
        draw_bitmap(setp, LINK_BMP, -8, 0); draw_text(setp, tiny, 10, 12, 'Lnk')
    # Asymmetric halves: layout half = layer/layout/brightness/speed, lock half =
    # locks + language. Mirrors status_oled.c.
    if side == 'L':
        draw_glyph(setp, icons, 0, 42, 0x80)
        draw_text(setp, tiny, 17, 41, '0')
        draw_text_center_half(setp, mid, 59, layout_name)
        draw_brightness(setp, contrast, 76)
        draw_bitmap(setp, WPM_BMP, (P_W - 11) // 2, 95, 11, 6)
        draw_text_center(setp, tiny, 113, str(wpm))
    else:
        draw_glyph_center(setp, icons, 44, 0x8C)   # NumLock off
        draw_glyph_center(setp, icons, 68, 0x8E)   # CapsLock off
        gh = draw_glyph_half(setp, world, (P_W - 20) // 2, 72, 0x1F310)
        for half in range(2):
            draw_text_center(setp, tiny, 72 + gh + 10 + half * 11, lang[half * 3:half * 3 + 2])
    # side marker
    draw_text_center(setp, tiny, 126, side)
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
    d.rectangle([0, 0, P_W * sc - 1, P_H * sc - 1], outline=(60, 62, 70))
    return im


def render_diag(pts, title, sc=6, gut=4):
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
    d.text((4, ch * sc - 14), f"out-of-bounds px: {clipped}", fill=RED if clipped else (140, 200, 150))
    return im, clipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--diag', action='store_true', help='clipping diagnostic (gutter + red out-of-bounds)')
    ap.add_argument('-o', '--out', help='output PNG (default: /tmp/status_oled42[_diag].png)')
    args = ap.parse_args()

    mid, tiny, icons, world = load()
    L = build('L', mid, tiny, icons, world)
    R = build('R', mid, tiny, icons, world)

    if args.diag:
        # Exercise EVERY short layout name — glyph widths differ, so a clip specific
        # to e.g. "ColDH" would never surface if we only rendered "Qwrty". The R side
        # carries a constant ~33 from the intentional half-off Link icon; a name that
        # clips adds beyond the L=0 / R=33 baseline.
        def clip_count(pts):
            return sum(1 for (x, y) in pts if not (0 <= x < P_W and 0 <= y < P_H))
        for nm in SHORT_NAMES + ["Unkn"]:
            lc = clip_count(build('L', mid, tiny, icons, world, layout_name=nm))
            rc = clip_count(build('R', mid, tiny, icons, world, layout_name=nm))
            print(f"  {nm:6} clipped L={lc} R={rc}")
        Li, lc = render_diag(L, 'LEFT (brightness/WPM)  RED=clipped')
        Ri, rc = render_diag(R, 'RIGHT (Num/Caps lock)  RED=clipped')
        gap = 24
        c = Image.new('RGB', (Li.width + gap + Ri.width, max(Li.height, Ri.height)), (18, 18, 22))
        c.paste(Li, (0, 0)); c.paste(Ri, (Li.width + gap, 0))
        out = args.out or '/tmp/status_oled42_diag.png'
        c.save(out)
        print(f"{out}  representative (Qwrty): L={lc} R={rc}")
    else:
        Li, Ri = render_plain(L), render_plain(R)
        pad, gap = 16, 40
        c = Image.new('RGB', (pad * 2 + Li.width + gap + Ri.width, pad * 2 + Li.height), (26, 26, 30))
        c.paste(Li, (pad, pad)); c.paste(Ri, (pad + Li.width + gap, pad))
        out = args.out or '/tmp/status_oled42.png'
        c.save(out)
        print(out)


if __name__ == '__main__':
    sys.exit(main())
