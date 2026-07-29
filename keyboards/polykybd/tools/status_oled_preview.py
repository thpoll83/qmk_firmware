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
# Rows B..D are spaced PER PANEL so the bottom row's last pixel lands on screen row
# 63 and the slack splits evenly -- the layout panel's descenders sit on row B
# ("Qwerty Stag!") while the RGB panel's sit on row C ("Cyan"), so one shared set
# cannot space both. See the matching block in split72/status_oled.c.
LOCK_ROW_B = 29   # layout name
LOCK_ROW_C = 48   # speed + language
LOCK_ROW_D = 63   # brightness
RGB_ROW_B = 30    # effect index + name
RGB_ROW_C = 45    # colour name + hue
RGB_ROW_D = 63    # saturation + value
# RGB-off re-flow: three rows on both panels instead of four, again bottomed out on 63.
OFF_ROW_B = 37
OFF_ROW_C = 63
RGB_OFF_ROW_B = 39
RGB_OFF_ROW_C = 63
OFF_GLOBE_Y = 1
OFF_CODE1_BASE = 31
OFF_CODE2_BASE = 43


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
    for rel in ("NotoSans_Regular_Base_11pt.h", "NotoSans_Medium_Base_8pt.h", "gfx_icons.h",
                "lang_label_font.h", "generated/emoji_fonts.h"):
        _parse_header(open(os.path.join(FONTDIR, rel)).read(), B, G, F)

    def bundle(name):
        f = F[name]
        return f, B[f['bmp']], G[f['gly']]

    disp = bundle('NotoSans_Regular11pt7b')
    small = bundle('NotoSans_Medium8pt7b')
    tiny = bundle('NotoSans_Regular_Tiny_6pt7b')   # language index only (see status_oled.c)
    icons = None
    for _n, f in F.items():
        if f['first'] <= 0x80 <= f['last'] and f['gly'] in G:
            icons = (f, B[f['bmp']], G[f['gly']])
            break
    globe = bundle('NotoEmoji_Medium_World_20pt16b')
    return disp, small, icons, tiny, globe


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


def draw_glyph_half(setpix, font, x, y, cp):
    """Mirror of kdisp_draw_glyph_half_at: 2x2-OR downsample, dims rounded up,
    (x, y) is the TOP-LEFT of the halved glyph (no baseline/yOffset math)."""
    f, bm, gl = font
    if not (f['first'] <= cp <= f['last']):
        return
    g = gl[cp - f['first']]
    w, h = g['w'], g['h']
    for dy in range((h + 1) // 2):
        for dx in range((w + 1) // 2):
            for oy in range(2):
                for ox in range(2):
                    sx, sy = dx * 2 + ox, dy * 2 + oy
                    if sx >= w or sy >= h:
                        continue
                    bit = sy * w + sx
                    if bm[g['off'] + (bit >> 3)] & (0x80 >> (bit & 7)):
                        setpix(x + dx, y + dy)
                        break
                else:
                    continue
                break


def measure_width(font, text):
    """Rightmost lit pixel of `text`, mirroring kdisp_gfx_text_bounds' hi. Skips
    out-of-range codepoints exactly as draw() does, so a missing glyph can't raise."""
    f, _bm, gl = font
    x = 0
    hi = 0
    for cp in text:
        if not (f['first'] <= cp <= f['last']):
            continue
        g = gl[cp - f['first']]
        if g['w']:
            hi = max(hi, x + g['xo'] + g['w'] - 1)
        x += g['xa']
    return hi


def draw_right(setpix, font, right_x, y, text):
    """Mirror of oled_draw_text_right: rightmost lit pixel lands on right_x."""
    draw(setpix, font, max(0, right_x - measure_width(font, text)), y, text)


def fill_rect(setpix, x, y, w, h):
    """Mirror of kdisp_fill_rect."""
    for px in range(x, x + w):
        for py in range(y, y + h):
            setpix(px, py)


def round_rect(setpix, x, y, w, h, r):
    """Mirror of kdisp_draw_round_rect (same midpoint-arc walk, so the corners land
    on the same pixels as the firmware's)."""
    if w < 2 or h < 2:
        return
    x0, y0, x1, y1 = x, y, x + w - 1, y + h - 1
    r = max(0, min(r, (w - 1) // 2, (h - 1) // 2))
    for i in range(x0 + r, x1 - r + 1):
        setpix(i, y0); setpix(i, y1)
    for j in range(y0 + r, y1 - r + 1):
        setpix(x0, j); setpix(x1, j)
    cxl, cxr, cyt, cyb = x0 + r, x1 - r, y0 + r, y1 - r
    f, ddF_x, ddF_y, px, py = 1 - r, 1, -2 * r, 0, r
    while px < py:
        if f >= 0:
            py -= 1; ddF_y += 2; f += ddF_y
        px += 1; ddF_x += 2; f += ddF_x
        for sx, sy in ((cxr + px, cyt - py), (cxr + py, cyt - px),
                       (cxl - px, cyt - py), (cxl - py, cyt - px),
                       (cxr + px, cyb + py), (cxr + py, cyb + px),
                       (cxl - px, cyb + py), (cxl - py, cyb + px)):
            setpix(sx, sy)


# ---- RGB speed gauge, kept in sync with split72/status_oled.c ----
COL_W = 17          # indicator column both panels reserve on their inner edge
SPEED_BOX_Y, SPEED_BOX_W, SPEED_BOX_H = 0, COL_W, 39
SPEED_FILL_W = SPEED_BOX_W - 6
SPEED_FILL_BOTTOM = SPEED_BOX_Y + SPEED_BOX_H - 4
SPEED_FILL_H = SPEED_BOX_H - 6


def draw_speed_gauge(setpix, x, speed):
    round_rect(setpix, x, SPEED_BOX_Y, SPEED_BOX_W, SPEED_BOX_H, 3)
    round_rect(setpix, x + 1, SPEED_BOX_Y + 1, SPEED_BOX_W - 2, SPEED_BOX_H - 2, 2)
    fill = (speed * SPEED_FILL_H + 127) // 255
    if fill:
        fill_rect(setpix, x + 3, SPEED_FILL_BOTTOM - fill + 1, SPEED_FILL_W, fill)


# ---- brightness gauge, kept in sync with split72/status_oled.c ----
SUN_BMP = [0x04, 0x00, 0x44, 0x40, 0x20, 0x80, 0x0e, 0x00, 0x1f, 0x00, 0xdf, 0x60,
           0x1f, 0x00, 0x0e, 0x00, 0x20, 0x80, 0x44, 0x40, 0x04, 0x00]
SUN_W = SUN_H = 11
GLOBE_BMP = [0x0f, 0x80, 0x38, 0xe0, 0x68, 0xb0, 0x48, 0x90, 0x90, 0x48, 0x90, 0x48,
             0xff, 0xf8, 0x90, 0x48, 0x90, 0x48, 0x48, 0x90, 0x68, 0xb0, 0x38, 0xe0,
             0x0f, 0x80]
GLOBE_W = GLOBE_H = 13
GLOBE_BIG_BMP = [0x07,0xf0,0x00, 0x0e,0x38,0x00, 0x32,0x26,0x00, 0x24,0x12,0x00,
                 0x44,0x11,0x00, 0xc4,0x11,0x80, 0x84,0x10,0x80, 0x84,0x10,0x80,
                 0xff,0xff,0x80, 0x84,0x10,0x80, 0x84,0x10,0x80, 0xc4,0x11,0x80,
                 0x44,0x11,0x00, 0x24,0x12,0x00, 0x32,0x26,0x00, 0x0e,0x38,0x00,
                 0x07,0xf0,0x00]
GLOBE_BIG_W = GLOBE_BIG_H = 17
WPM_BMP = [0x1f,0x00, 0x71,0xc0, 0x43,0x40, 0xc2,0x60, 0x86,0x20, 0x8e,0x20]
WPM_ICON_W, WPM_ICON_H = 11, 6
DEGREE_BMP = [0xe0, 0xa0, 0xe0]                            # 3x3 degree sign
DEGREE_W = DEGREE_H = 3
SUPER2_BMP = [0x70, 0x88, 0x10, 0x20, 0x40, 0xf8]          # 5x6 superscript 2
SUPER2_W, SUPER2_H = 5, 6
DROPLET_BMP = [0x08, 0x00, 0x08, 0x00, 0x1c, 0x00, 0x1c, 0x00, 0x3e, 0x00,
               0x7f, 0x00, 0x7f, 0x00, 0x7f, 0x00, 0x3e, 0x00, 0x1c, 0x00]
DROPLET_W, DROPLET_H, DROPLET_Y = 9, 10, 53                # saturation
SUN_SMALL_BMP = [0x08, 0x00, 0x41, 0x00, 0x1c, 0x00, 0x3e, 0x00, 0xbe, 0x80,
                 0x3e, 0x00, 0x1c, 0x00, 0x41, 0x00, 0x08, 0x00]
SUN_SMALL_W, SUN_SMALL_H, SUN_SMALL_Y = 9, 9, 54           # value
SV_ICON_GAP = 2


def hue_to_degrees(hue):
    """Mirror of text_helper.c hue_to_degrees()."""
    return hue * 360 // 255


def hue_name(hue, sat):
    """Mirror of text_helper.c get_hue_name()."""
    if sat < 26:
        return 'White'
    deg = hue_to_degrees(hue)
    for limit, name in ((15, 'Red'), (45, 'Orange'), (70, 'Yellow'), (100, 'Lime'),
                        (165, 'Green'), (195, 'Cyan'), (240, 'Azure'), (270, 'Blue'),
                        (300, 'Violet'), (330, 'Magenta'), (345, 'Pink')):
        if deg < limit:
            return name
    return 'Red'


def byte_to_percent(v):
    return (v * 100 + 127) // 255
# GAUGE_* mirror the defines in split72/status_oled.c; FULL_BRIGHT mirrors config.h.
GAUGE_SEGMENTS, GAUGE_BAR_W, GAUGE_PITCH, GAUGE_MIN_H = 10, 4, 6, 3
FULL_BRIGHT = 50


def brightness_to_level(contrast):
    contrast = min(contrast, FULL_BRIGHT)
    return min((contrast * GAUGE_SEGMENTS + FULL_BRIGHT // 2) // FULL_BRIGHT, GAUGE_SEGMENTS)


def draw_brightness_bars(setpix, x, bottom_y, level):
    for i in range(GAUGE_SEGMENTS):
        bx, h = x + i * GAUGE_PITCH, GAUGE_MIN_H + i
        if i < level:
            fill_rect(setpix, bx, bottom_y - h + 1, GAUGE_BAR_W, h)
        else:
            fill_rect(setpix, bx, bottom_y, GAUGE_BAR_W, 1)


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


def draw_brightness_row(setp, small, x, base_y, brightness):
    """Mirror of draw_brightness_row()."""
    draw_bitmap(setp, SUN_BMP, x, base_y - 11, SUN_W, SUN_H)
    draw(setp, small, x + 15, base_y, s(str(brightness)))
    draw_brightness_bars(setp, x + 40, base_y - 1, brightness_to_level(brightness))


def draw_lang_column(setp, tiny, globe, x, code):
    """Mirror of draw_lang_column(): big globe over the code on two lines."""
    draw_glyph_half(setp, globe, x, OFF_GLOBE_Y, 0x1F310)
    f, _bm, gl = tiny
    for half, base in ((0, OFF_CODE1_BASE), (1, OFF_CODE2_BASE)):
        txt = s(code[half * 3:half * 3 + 2])
        lo, hi, cx = 127, 0, 0
        for cp in txt:
            # Same range guard draw()/measure_width() use -- `code` comes from --lang,
            # so an out-of-range char would index gl[] out of bounds (or wrap negative).
            if not (f['first'] <= cp <= f['last']):
                continue
            g = gl[cp - f['first']]
            if g['w']:
                lo = min(lo, cx + g['xo'])
                hi = max(hi, cx + g['xo'] + g['w'] - 1)
            cx += g['xa']
        nx = x + (COL_W - (hi - lo + 1)) // 2 - lo + 1
        draw(setp, tiny, max(x, nx), base, txt)


def build_panel(side, disp, small, icons, tiny, globe, brightness=50, rgb=(128, 255, 100, 80, 5, 'Rainbow'),
                lang='en-US', wpm=0):
    """side: 'L' (USB host, layout panel) or 'R' (bridge, RGB panel). The role word
    (USB/Link) follows is_usb_host_side() on hardware; here 'L' models the USB half.
    rgb=None models RGB being switched off, which re-flows BOTH panels.
    Returns set of (x,y) pixels."""
    rgb_on = rgb is not None
    pts = set()
    def setp(px, py):
        pts.add((px, py))
    # Each half's indicator column sits on its INNER edge (see status_oled.c), so the
    # text origin differs per panel.
    lock_panel = (side == 'L')
    COL_X  = 108 if lock_panel else 0
    TEXT_X = 0 if lock_panel else (20 if rgb_on else 26)
    TEXT_R = 104 if lock_panel else 127
    # top line: layer + role icon + role word
    draw(setp, icons, TEXT_X, TOP_BASE, [0x80])                 # ICON_LAYER
    draw(setp, disp, TEXT_X + 20, TOP_BASE, s('0'))             # hex layer
    if lock_panel:
        draw_bitmap(setp, USB_BMP, TEXT_X + 38, 0)
        draw(setp, disp, TEXT_X + 57, TOP_BASE, s('USB'))
    else:
        draw_bitmap(setp, LINK_BMP, TEXT_X + 38, 0)
        draw(setp, disp, TEXT_X + 57, TOP_BASE, s('Link'))
    # Lock LEDs render on the layout panel only (identical state on both halves) —
    # the RGB panel's column is the speed gauge now.
    if lock_panel:
        draw(setp, icons, COL_X, 16, [0x8C])                    # NumLock off
        draw(setp, icons, COL_X, 38, [0x8E])                    # CapsLock off
        draw(setp, small, COL_X + 6, 56, s('L'))
    else:
        draw(setp, small, COL_X + 5, 56, s('R'))
    if lock_panel:
        draw(setp, small, TEXT_X, LOCK_ROW_B if rgb_on else OFF_ROW_B, s('Qwerty'))
        # Speed (+ the language slot sharing its row) and brightness trade rows when RGB
        # is on -- the ~98px meter can only hold a row on its own.
        speed_base = LOCK_ROW_C if rgb_on else OFF_ROW_C
        draw_bitmap(setp, WPM_BMP, 0, speed_base - 8, WPM_ICON_W, WPM_ICON_H)
        draw(setp, small, 15, speed_base, s(str(wpm)))
        if rgb_on:
            draw_bitmap(setp, GLOBE_BMP, 46, speed_base - 12, GLOBE_W, GLOBE_H)
            draw(setp, tiny, 62, speed_base, s(lang))
            draw_brightness_row(setp, small, 0, LOCK_ROW_D, brightness)
    elif not rgb_on:
        draw(setp, small, TEXT_X, RGB_OFF_ROW_B, s('RGB'))
        draw(setp, small, TEXT_X + 34, RGB_OFF_ROW_B, s('Off'))
        draw_brightness_row(setp, small, TEXT_X, RGB_OFF_ROW_C, brightness)
        draw_lang_column(setp, tiny, globe, COL_X, lang)
    else:
        hue, sat, val, speed, mode, name = rgb
        draw(setp, small, TEXT_X, RGB_ROW_B, s(str(mode)))
        name_x = TEXT_X + 22
        draw(setp, small, name_x, RGB_ROW_B, s(name.rstrip('2')))
        if name.endswith('2'):      # "Splash2" in the fixture -> "Splash" + superscript
            hi = measure_width(small, s(name.rstrip('2')))
            draw_bitmap(setp, SUPER2_BMP, name_x + hi + 2, 18, SUPER2_W, SUPER2_H)
        draw_speed_gauge(setp, COL_X, speed)
        draw(setp, small, TEXT_X, RGB_ROW_C, s(hue_name(hue, sat)))
        draw_right(setp, small, TEXT_R - 4, RGB_ROW_C, s(str(hue_to_degrees(hue))))
        draw_bitmap(setp, DEGREE_BMP, TEXT_R - 2, 34, DEGREE_W, DEGREE_H)
        draw_bitmap(setp, DROPLET_BMP, TEXT_X, DROPLET_Y, DROPLET_W, DROPLET_H)
        draw(setp, small, TEXT_X + DROPLET_W + SV_ICON_GAP, RGB_ROW_D,
             s('%d%%' % byte_to_percent(sat)))
        vtxt = s('%d%%' % byte_to_percent(val))
        vx = TEXT_R - measure_width(small, vtxt)
        draw_bitmap(setp, SUN_SMALL_BMP, vx - SV_ICON_GAP - SUN_SMALL_W, SUN_SMALL_Y,
                    SUN_SMALL_W, SUN_SMALL_H)
        draw(setp, small, vx, RGB_ROW_D, vtxt)
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
    def brightness_arg(v):
        n = int(v)
        if not 0 <= n <= FULL_BRIGHT:
            raise argparse.ArgumentTypeError('brightness must be 0..%d' % FULL_BRIGHT)
        return n

    ap.add_argument('-b', '--brightness', type=brightness_arg, default=FULL_BRIGHT,
                    help='keycap brightness 0..%d shown in the gauge (default %d)' % (FULL_BRIGHT, FULL_BRIGHT))
    ap.add_argument('--lang', default='en-US', help="language code shown, e.g. mn-MN")
    ap.add_argument('--wpm', type=int, default=0, help='WPM value shown')
    ap.add_argument('--rgb-off', action='store_true',
                    help='preview the RGB-off layout (both panels re-flow to three rows)')
    args = ap.parse_args()

    disp, small, icons, tiny, globe = load_fonts()
    rgb = None if args.rgb_off else (128, 255, 100, 80, 5, 'Rainbow')
    L = build_panel('L', disp, small, icons, tiny, globe, args.brightness, rgb, args.lang, args.wpm)
    R = build_panel('R', disp, small, icons, tiny, globe, args.brightness, rgb, args.lang, args.wpm)

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
