"""Model the firmware's per-keycap draw path in Python, so keycap chrome can be
placed and MEASURED without flashing.

This mirrors `base/disp_array.c` (`kdisp_write_gfx_char`'s baseline-align + its
courtyard clear, `kdisp_draw_glyph_half_at`, `kdisp_draw_glyph_thin_at`) and the
legend placement in `poly_keymap.c` `render_key()` (base glyph, shift preview and
AltGr preview, using the REAL per-language offsets parsed out of `lang_lut.c`).

Everything works on **ink sets** — `{(x, y), …}` of lit buffer pixels — because the
question that matters is almost always "does this new art land on the existing
legend", and set intersection answers it exactly. See `collision()`.

Coordinates are BUFFER coordinates: the visible keycap window is
x `BUFFER_X .. BUFFER_X+VIS_W-1` (28..99), y `0..H-1` (0..39).

Usage sketch:

    import keycap_preview as K
    legend = K.legend_ink('a')                       # what render_key() draws
    mark   = K.thin_ink(0x2388, 80, 21)              # a decimated corner mark
    print(K.collision(legend, mark))                 # px of overlap -> 0 is clean
    K.sheet([(legend | mark, 'LCTL_T(KC_A)')], 'out.png')
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

_HERE = Path(__file__).resolve()
_QMK = next(p for p in _HERE.parents if (p / 'keyboards' / 'polykybd').is_dir())
_FONTS_DIR = _QMK / 'keyboards' / 'polykybd' / 'base' / 'fonts'
_LANG_LUT = _QMK / 'keyboards' / 'polykybd' / 'lang' / 'lang_lut.c'
# gfx_font.py is the host repo's parser for the generated headers — the same one
# oled_preview.py uses. Reaching sideways is deliberate: a second parser would be
# a second thing to keep correct.
sys.path.insert(0, str(_QMK.parent / 'PolyKybdHost' / 'tools'))

import gfx_font  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

FONTS = gfx_font.load_all_fonts(str(_FONTS_DIR))
ICONS = next(f for f in FONTS if f.name == 'IconsFont')      # g_all_fonts[0]

BUFFER_X = gfx_font.BUFFER_X       # 28 — left edge of the visible window
VIS_W, H = 72, gfx_font.OLED_H     # the keycap the user sees
W = 128                            # the full scratch buffer row
CY_DEFAULT = 3                     # KDISP_CY_DEFAULT
HIDE_KEY = -128

# poly_settings row order (lang/lang_lut.h `enum settings_index`) and the
# variation order (`enum variation_index`).
S_LETTER_H, S_LETTER_V, S_NUM_H, S_NUM_V, S_SYM_H, S_SYM_V = range(6)
VAR_SMALL, VAR_SHIFT, VAR_CAPS, VAR_ALTGR = range(4)


# --- glyphs ------------------------------------------------------------------

def find(cp):
    """Front-to-back lookup, exactly as kdisp_gfx_glyph_font() scans g_all_fonts."""
    for f in FONTS:
        if f.first <= cp <= f.last:
            g = f.glyphs[cp - f.first]
            if g['width'] or g['height']:      # a gap record falls through
                return f, g
    raise KeyError('no glyph for U+%04X' % cp)


def lit(f, g, sx, sy):
    """Column-native (OLED page) bit test — NOT the classic row-major layout."""
    cb = (g['height'] + 7) // 8
    return bool(f.bitmap[g['bitmapOffset'] + sx * cb + (sy >> 3)] & (1 << (sy & 7)))


def full_ink(cp, cur_x, cur_y):
    """A normal glyph draw at cursor (cur_x, cur_y).

    ⚠️ Includes kdisp_write_gfx_char's baseline align to fonts[0] — the shift that
    oled_preview.py does NOT model, and the cause of the flag gap-at-top bug.
    """
    f, g = find(cp)
    y = cur_y + f.yAdvance - ICONS.yAdvance
    x0, y0 = cur_x + g['xOffset'], y + g['yOffset']
    return {(x0 + sx, y0 + sy)
            for sy in range(g['height']) for sx in range(g['width'])
            if lit(f, g, sx, sy)}


def half_ink(cp, x, y):
    """HINT_HALF: 2x2-OR downsample at the literal ink top-left (no baseline align)."""
    f, g = find(cp)
    w, h = g['width'], g['height']
    return {(x + dx, y + dy)
            for dy in range((h + 1) // 2) for dx in range((w + 1) // 2)
            if any(lit(f, g, dx * 2 + ox, dy * 2 + oy)
                   for oy in (0, 1) for ox in (0, 1)
                   if dx * 2 + ox < w and dy * 2 + oy < h)}


def thin_ink(cp, x, y):
    """HINT_THIN: decimating downsample (every second pixel), same placement rule."""
    f, g = find(cp)
    w, h = g['width'], g['height']
    return {(x + dx, y + dy)
            for dy in range((h + 1) // 2) for dx in range((w + 1) // 2)
            if lit(f, g, dx * 2, dy * 2)}


def courtyard(ink, radius=CY_DEFAULT):
    """The pixels a cy_radius draw CLEARS before plotting (its dilation)."""
    return {(x + dx, y + dy) for (x, y) in ink
            for dy in range(-radius, radius + 1)
            for dx in range(-radius, radius + 1)}


# --- render_key()'s legend ---------------------------------------------------

def _poly_settings():
    txt = _LANG_LUT.read_text(encoding='utf-8', errors='replace')
    body = txt[txt.rindex('static const int8_t poly_settings'):]
    rows = re.findall(r'/\*\s*(\S+)\*/\s*(-?\d+),(-?\d+),(-?\d+),(-?\d+)', body)
    per = len(rows) // 6
    out = {}
    for block in range(6):
        for name, *vals in rows[block * per:(block + 1) * per]:
            out[(block, name)] = [int(v) for v in vals]
    return out


SETTINGS = _poly_settings()


def setting(row, lang, var):
    return SETTINGS[(row, lang)][var]


def legend_ink(ch, lang='en-US', kind=None, shifted=None):
    """What render_key() draws for a resting (unshifted) key: the base glyph plus
    the shift preview, placed and clamped by render_key()'s own rules.

    `kind` is 'letter' | 'num' | 'sym'; inferred from `ch` when omitted. `shifted`
    is the upper view (inferred for letters). Returns one ink set.

    ⚠️ The shift preview is what any corner chrome collides with — en-US HIDEs it
    for letters, but 36 of 160 languages do not, and every language shows one on
    the number and symbol rows.
    """
    if kind is None:
        kind = 'letter' if ch.isalpha() else ('num' if ch.isdigit() else 'sym')
    hrow, vrow = {'letter': (S_LETTER_H, S_LETTER_V),
                  'num': (S_NUM_H, S_NUM_V),
                  'sym': (S_SYM_H, S_SYM_V)}[kind]
    h_small = setting(hrow, lang, VAR_SMALL)
    v_small = setting(vrow, lang, VAR_SMALL)
    base_x = BUFFER_X + h_small
    ink = full_ink(ord(ch), base_x, 23 + v_small)

    h_pv, v_pv = setting(hrow, lang, VAR_SHIFT), setting(vrow, lang, VAR_SHIFT)
    if h_pv == HIDE_KEY or v_pv == HIDE_KEY:
        return ink
    if shifted is None:
        if kind != 'letter':
            return ink                      # caller must name the shifted glyph
        shifted = ch.upper()
    _, bg = find(ord(ch))
    _, pg = find(ord(shifted))
    bmax = bg['xOffset'] + bg['width'] - 1
    pmin, pmax = pg['xOffset'], pg['xOffset'] + pg['width'] - 1
    px = BUFFER_X + h_pv
    if px + pmin < base_x + bmax + 2:                     # keep clear of the base
        px = base_x + bmax + 2 - pmin
    if px + pmax > BUFFER_X + VIS_W - 1:                  # clamp to the right edge
        px = (BUFFER_X + VIS_W - 1) - pmax
    return ink | full_ink(ord(shifted), px, 23 + v_pv)


# --- measuring ---------------------------------------------------------------

def collision(a, b):
    """Pixels where two ink sets overlap.

    ⚠️ THE metric. A "how many of `a`'s pixels survived after drawing `b`" count
    reads 0 for a real collision, because lit-on-lit loses no pixels — it just
    reads as one merged blob. Intersect, don't subtract.
    """
    return len(a & b)


def erased(legend, mark_ink, radius=CY_DEFAULT):
    """Legend pixels a courtyard-clearing draw of `mark_ink` would wipe out."""
    return len(legend & courtyard(mark_ink, radius))


def extent(ink):
    """(xmin, xmax, ymin, ymax); use to check the visible window."""
    xs = [x for x, _ in ink]
    ys = [y for _, y in ink]
    return min(xs), max(xs), min(ys), max(ys)


def offscreen(ink):
    """Ink outside the visible keycap window — must be empty."""
    return {(x, y) for (x, y) in ink
            if not (BUFFER_X <= x < BUFFER_X + VIS_W and 0 <= y < H)}


# --- rendering ---------------------------------------------------------------

_LIT = (222, 238, 255)      # what the OLEDs read as by eye (not the camera cyan)
_BG = (18, 18, 22)
_FRAME = (58, 62, 72)
_LABEL = (176, 180, 190)


def _cap(ink, scale):
    img = Image.new('L', (W, H), 0)
    px = img.load()
    for (x, y) in ink:
        if 0 <= x < W and 0 <= y < H:
            px[x, y] = 255
    crop = img.crop((BUFFER_X, 0, BUFFER_X + VIS_W, H))
    w, h = VIS_W * scale, H * scale
    out = Image.new('RGB', (w + 2 * scale, h + 2 * scale), _BG)
    ImageDraw.Draw(out).rounded_rectangle(
        [0, 0, w + 2 * scale - 1, h + 2 * scale - 1],
        radius=scale + 2, fill=(0, 0, 0), outline=_FRAME, width=1)
    rgb = Image.merge('RGB', tuple(crop.point(lambda v, c=c: v and c) for c in _LIT))
    out.paste(rgb.resize((w, h), Image.NEAREST), (scale, scale))
    return out


def sheet(cells, path, scale=5, gap=26, pad=22):
    """cells: [(ink_set, label)] laid out in one row. Writes a PNG."""
    font = ImageFont.truetype(
        '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 15)
    caps = [_cap(ink, scale) for ink, _ in cells]
    cw, chh = caps[0].size
    out = Image.new('RGB', (pad * 2 + cw * len(caps) + gap * (len(caps) - 1),
                            pad * 2 + chh + 26), _BG)
    d = ImageDraw.Draw(out)
    for i, (cap, (_, label)) in enumerate(zip(caps, cells)):
        x = pad + i * (cw + gap)
        out.paste(cap, (x, pad))
        d.text((x + (cw - d.textlength(label, font=font)) / 2, pad + chh + 8),
               label, font=font, fill=_LABEL)
    out.save(path)
    return path
