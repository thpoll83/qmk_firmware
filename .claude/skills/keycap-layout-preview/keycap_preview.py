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

# poly_settings rows, named as `lang_lut.c` labels them rather than by ordinal.
# ⚠️ They USED to be `range(6)` ordinals, and that silently read the wrong rows:
# `lang_lut.c` emits FIFTEEN blocks (the six H/V offsets, three altgrhalf, six
# held-offset), so a parser that divides the row list by 6 gives 400 rows per
# "block" instead of 160 and every language name appears three times inside one —
# last write wins. Measured: `setting(S_LETTER_H, 'en-US', VAR_SHIFT)` returned 35
# from `{num.hoffset}` instead of HIDE_KEY from `{letter.hoffset}`, i.e. the model
# drew an en-US letter a shift preview the firmware hides. Key by the label.
S_LETTER_H, S_LETTER_V = 'letter.hoffset', 'letter.voffset'
S_NUM_H, S_NUM_V = 'num.hoffset', 'num.voffset'
S_SYM_H, S_SYM_V = 'sym.hoffset', 'sym.voffset'
S_LETTER_HALF, S_NUM_HALF, S_SYM_HALF = ('letter.altgrhalf', 'num.altgrhalf',
                                         'sym.altgrhalf')
VAR_SMALL, VAR_SHIFT, VAR_CAPS, VAR_ALTGR = range(4)

WIN_X0, WIN_X1 = BUFFER_X, BUFFER_X + VIS_W - 1
WIN_Y1 = H - 1
ALTGR_HALF_MIN_INK_H = 7            # the firmware's mark guard, measured there


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


def small_ink(cp, cur_x, cur_y):
    """HINT_SMALL (`\\x10`): half scale, KEEPING the baseline and advance.

    ⚠️ NOT `half_ink()` — that is HINT_HALF, which takes the literal ink top-left
    and does not advance. This mirrors `kdisp_write_gfx_char_half()`: only the
    glyph's own offsets and extents are halved, never the baseline. The halving is
    FLOOR (Python `//` already floors for negatives; the firmware spells it
    `glyph_half_floor()` because C truncates toward zero, which would put
    lowercase 1 px off the run's baseline).
    """
    f, g = find(cp)
    y = cur_y + f.yAdvance - ICONS.yAdvance
    x0, y0 = cur_x + g['xOffset'] // 2, y + g['yOffset'] // 2
    w, h = g['width'], g['height']
    return {(x0 + dx, y0 + dy)
            for dy in range((h + 1) // 2) for dx in range((w + 1) // 2)
            if any(lit(f, g, dx * 2 + ox, dy * 2 + oy)
                   for oy in (0, 1) for ox in (0, 1)
                   if dx * 2 + ox < w and dy * 2 + oy < h)}


def rel_box(render, cp):
    """(xmin, xmax, ymin, ymax) of a glyph's ink RELATIVE to its cursor.

    The firmware's `kdisp_gfx_text_bbox()` in the units every placement rule below
    is written in. Rendering at the origin is exact because the ink is linear in
    the cursor.
    """
    ink = render(cp, 0, 0)
    xs, ys = [x for x, _ in ink], [y for _, y in ink]
    return min(xs), max(xs), min(ys), max(ys)


def clamp(x, y, box):
    """`legend_plan_clamp()` — every element, all four edges, one helper.

    ⚠️ The ORDER decides which edge loses on an over-size glyph: east before west
    (so west wins) and north before south (so the top clips). Preserved from the
    firmware rather than chosen.
    """
    xmin, xmax, ymin, ymax = box
    if x + xmax > WIN_X1: x = WIN_X1 - xmax
    if x + xmin < WIN_X0: x = WIN_X0 - xmin
    if y + ymin < 0:      y = -ymin
    if y + ymax > WIN_Y1: y = WIN_Y1 - ymax
    return x, y


# --- render_key()'s legend ---------------------------------------------------

def _poly_settings():
    """`{row label: {lang: [small, shift, caps, altgr]}}` out of `lang_lut.c`.

    Split on the `// {letter.hoffset}` block markers the generator emits, so
    adding a settings row cannot shift the rows already here — see the note on
    the S_* constants for what the ordinal version did instead.
    """
    txt = _LANG_LUT.read_text(encoding='utf-8', errors='replace')
    body = txt[txt.rindex('static const int8_t poly_settings'):]
    parts = re.split(r'//\s*\{([a-z]+\.[a-z]+)\}', body)
    out = {}
    for label, block in zip(parts[1::2], parts[2::2]):
        rows = re.findall(r'/\*\s*(\S+)\*/\s*(-?\d+),(-?\d+),(-?\d+),(-?\d+)', block)
        out[label] = {name: [int(v) for v in vals] for name, *vals in rows}
    if S_LETTER_H not in out:                       # a rename would fail open
        raise RuntimeError('no %r block in %s' % (S_LETTER_H, _LANG_LUT))
    return out


SETTINGS = _poly_settings()


def setting(row, lang, var):
    return SETTINGS[row][lang][var]


def legend_ink(ch, lang='en-US', kind=None, shifted=None, altgr=None):
    """What render_key() draws for a resting (unshifted) key: the base glyph, the
    shift preview and the AltGr hint, placed and clamped by render_key()'s own
    rules. Returns one ink set.

    `kind` is 'letter' | 'num' | 'sym'; inferred from `ch` when omitted. `shifted`
    is the upper view (inferred for letters). `altgr` is the AltGr cell's glyph.

    ⚠️ The two hints are what corner chrome collides with, and BOTH have to be
    passed to be measured. en-US HIDEs the shift preview for letters, but 36 of 160
    languages do not, and every language shows one on the number and symbol rows;
    the AltGr hint sits lower-right on the layouts that define one. Omitting
    `altgr` measures a keycap that has no AltGr cell — which is a real case, but it
    is not the same question as "does my mark clear this key's legend".

    ⚠️ Model limits, both from the firmware's big-legend tiers: `base_plan.big` is
    always false here (this models the SMALL face, which is what a resting keycap
    draws unless the glyph-size setting is raised), so the AltGr's
    push-clear-of-the-base branch and the M/L nominal baselines are not modelled.
    Measure a raised glyph size with `tools/glyph_size_preview.py` instead.
    """
    if kind is None:
        kind = 'letter' if ch.isalpha() else ('num' if ch.isdigit() else 'sym')
    hrow, vrow, halfrow = {
        'letter': (S_LETTER_H, S_LETTER_V, S_LETTER_HALF),
        'num': (S_NUM_H, S_NUM_V, S_NUM_HALF),
        'sym': (S_SYM_H, S_SYM_V, S_SYM_HALF)}[kind]

    # --- the base legend: the language's own origin, clamped onto the panel -----
    bbox = rel_box(full_ink, ord(ch))
    base_x = BUFFER_X + setting(hrow, lang, VAR_SMALL)
    base_y = 23 + setting(vrow, lang, VAR_SMALL)
    base_x, base_y = clamp(base_x, base_y, bbox)
    base_ink_max = base_x + bbox[1]

    # --- the shift preview -----------------------------------------------------
    pv_x = pv_y = None
    pbox = None
    h_pv, v_pv = setting(hrow, lang, VAR_SHIFT), setting(vrow, lang, VAR_SHIFT)
    if h_pv != HIDE_KEY and v_pv != HIDE_KEY:
        if shifted is None and kind == 'letter':
            shifted = ch.upper()
        if shifted is not None:
            pbox = rel_box(full_ink, ord(shifted))
            pv_x = BUFFER_X + h_pv
            if pv_x + pbox[0] < base_ink_max + 2:          # keep clear of the base
                pv_x = base_ink_max + 2 - pbox[0]
            pv_y = 23 + v_pv
            pv_x, pv_y = clamp(pv_x, pv_y, pbox)
            if pv_x + pbox[0] <= base_ink_max:             # forced to overlap -> stagger
                base_y -= 6                                # lift the flat base
                base_x, base_y = clamp(base_x, base_y, bbox)
                base_ink_max = base_x + bbox[1]
                pv_y += 4                                  # drop the preview
                pv_x, pv_y = clamp(pv_x, pv_y, pbox)

    # --- the AltGr hint, laid out as a PAIR with the shift preview -------------
    alt_x = alt_y = None
    alt_render = full_ink
    h_alt, v_alt = setting(hrow, lang, VAR_ALTGR), setting(vrow, lang, VAR_ALTGR)
    if altgr is not None and h_alt != HIDE_KEY and v_alt != HIDE_KEY:
        abox = rel_box(full_ink, ord(altgr))
        # Half size is DATA per layout per category, plus the mark guard: halving a
        # glyph that is already tiny destroys it (a Hebrew nikud is 2x3 px and comes
        # out a dot), so the threshold is on ink HEIGHT and is measured, not chosen.
        if setting(halfrow, lang, VAR_ALTGR) != 0 and abox[3] - abox[2] + 1 > ALTGR_HALF_MIN_INK_H:
            alt_render = small_ink
            abox = rel_box(small_ink, ord(altgr))
        alt_x, alt_y = clamp(BUFFER_X + h_alt, 23 + v_alt, abox)
        # Both hints sit right of the base — shift upper, AltGr lower — and their
        # VERTICAL offsets are all that hold them apart. True for a narrow Latin
        # pair, false for a tall script. When the boxes intersect in BOTH axes, pull
        # the shift LEFT into the gap between the base and the right-clamped AltGr,
        # never past the base's own 2 px margin and never right.
        if pv_x is not None:
            if (pv_x + pbox[0] <= alt_x + abox[1] and alt_x + abox[0] <= pv_x + pbox[1]
                    and pv_y + pbox[2] <= alt_y + abox[3] and alt_y + abox[2] <= pv_y + pbox[3]):
                want = max(alt_x + abox[0] - 2 - pbox[1], base_ink_max + 2 - pbox[0])
                if want < pv_x:
                    pv_x = want
                    pv_x, pv_y = clamp(pv_x, pv_y, pbox)

    ink = full_ink(ord(ch), base_x, base_y)
    if pv_x is not None:
        ink |= full_ink(ord(shifted), pv_x, pv_y)
    if alt_x is not None:
        ink |= alt_render(ord(altgr), alt_x, alt_y)
    return ink


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


def _label_font(size=15):
    """A mono label face if the host has one, else PIL's built-in bitmap font.

    Only the caption under each keycap uses this — the keycap itself is drawn from
    the firmware's own font headers — so a fallback costs nothing but looks.
    """
    for name in ('DejaVuSansMono-Bold.ttf', 'DejaVuSansMono.ttf',
                 'LiberationMono-Bold.ttf', 'Menlo.ttc', 'consolab.ttf'):
        try:
            return ImageFont.truetype(name, size)      # PIL searches the font dirs
        except OSError:
            continue
    return ImageFont.load_default()


def sheet(cells, path, scale=5, gap=26, pad=22):
    """cells: [(ink_set, label)] laid out in one row. Writes a PNG."""
    font = _label_font()
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
