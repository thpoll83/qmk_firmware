#!/usr/bin/env python3
"""Render emoji-picker slots the way the firmware places them, and report cuts.

Mirrors `make_slot_str()` in `keyboards/polykybd/emoji/emoji_layer.c`: the x
before a superscript or subscript, `slot_lift()`, the box clamp in 2 px steps,
and the centring of a symbol too wide for the two leading spaces. It draws the
resulting display list with PolyKybdHost's `oled_preview.Renderer`, the same
renderer `oled_preview.py` uses, so the fonts are the committed headers.

Usage (from anywhere; the host checkout must sit beside qmk_firmware):

    python3 tab_sheet.py emj_cat3                     # one tab, all pages
    python3 tab_sheet.py emj_cat3 emj_cat10 -o /tmp/s # several tabs
    python3 tab_sheet.py 0x2211 0x221A 0x1F891        # single codepoints
    python3 tab_sheet.py emj_cat3 --cut-only          # report only, no PNG

Output: one PNG per tab (or one for the codepoints), split into pages of
--per-page slots (38 = split72), and a list of every glyph that loses pixels at
the panel edge, with the count. A glyph no loaded font covers is reported as
"Glyph missing!", which is what the keyboard shows.

⚠️ After a change to fonts.yaml, run the FULL `generate_fonts.py` first: `--only`
does not rewrite `all_fonts_order.json`, and this renderer then finds no glyph for
a new font.
⚠️ Keep this module's names to itself. An earlier version of this sheet ran the
clamp model with exec() in its own namespace and set `H` to the sheet height; the
model read `H` as the panel height and stopped moving tall glyphs, so the sheet
showed ∑ cut at the bottom while the firmware draws it whole.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

_HERE = Path(__file__).resolve()
_QMK = next(p for p in _HERE.parents if (p / 'keyboards' / 'polykybd').is_dir())
_PK = _QMK / 'keyboards' / 'polykybd'
sys.path.insert(0, str(_QMK.parent / 'PolyKybdHost' / 'tools'))

import oled_preview  # noqa: E402
from gfx_font import BUFFER_X, OLED_H, OLED_W  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

PANEL_H, PANEL_W = OLED_H, OLED_W          # SCREEN_HEIGHT, SCREEN_WIDTH: 40, 72
BASELINE = 23
DOWN2, UP2, RIGHT2 = 0x05, 0x0C, 0x06       # \x05, \f, \x06 display-list moves

RENDERER = oled_preview.load_renderer(str(_PK / 'base' / 'fonts'))


def slot_lift(cp: int) -> int:
    if cp in (0x0024, 0x058F, 0x09F3, 0x0BF9, 0x0E3F):
        return 8
    if 0x0370 <= cp <= 0x03FF or 0x2000 <= cp <= 0x218F:
        return 8
    return 0


def is_sup_or_sub(cp: int) -> bool:
    return cp in (0x00B9, 0x00B2, 0x00B3) or 0x2070 <= cp <= 0x209F


def overshoot(top: int, bottom: int) -> int:
    return (-top if top < 0 else 0) + (bottom - (PANEL_H - 1) if bottom > PANEL_H - 1 else 0)


def _c_div(a: int, b: int) -> int:
    """C integer division: truncates toward zero (Python's // floors)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def slot_seq(cp: int) -> list[int] | None:
    """The display list make_slot_str() builds for cp, or None for 'Glyph missing!'."""
    if RENDERER._font(cp) is None:
        return None
    if is_sup_or_sub(cp):
        glyph = [ord('x')] + ([UP2] * 4 if cp < 0x2070 else []) + [cp]
        lift = 0
    else:
        glyph = [cp]
        lift = slot_lift(cp)
    xmin, xmax, ymin, ymax = RENDERER.bbox(glyph)
    top, bottom = BASELINE + ymin, BASELINE + ymax
    move = lift
    if bottom - top + 1 <= PANEL_H:
        t, b = top + lift, bottom + lift
        need = -t if t < 0 else ((PANEL_H - 1) - b if b > PANEL_H - 1 else 0)
        if need:
            d = lift + need
            lo = _c_div(d, 2) * 2 if d >= 0 else -(_c_div(1 - d, 2) * 2)
            hi = lo + (2 if d & 1 else 0)
            ol, oh = overshoot(top + lo, bottom + lo), overshoot(top + hi, bottom + hi)
            best = lo if (ol < oh or (ol == oh and abs(lo - lift) <= abs(hi - lift))) else hi
            if overshoot(top + best, bottom + best) < overshoot(t, b):
                move = best
    space = RENDERER._font(ord(' '))
    lead = 2 * space.glyphs[ord(' ') - space.first]['xAdvance'] if space else 14
    out: list[int] = []
    if lead + xmax > PANEL_W - 1:
        off = _c_div(PANEL_W - (xmax - xmin + 1), 2) - xmin
        out += [RIGHT2] * min(max(_c_div(off, 2), 0) if off > 0 else 0, 8)
    else:
        out += [ord(' '), ord(' ')]
    steps = _c_div(move, 2)
    out += [DOWN2] * steps if steps > 0 else [UP2] * (-steps)
    return out + glyph


def render(cp: int) -> tuple[Image.Image, int, bool]:
    """(1-bit panel image, pixels lost at the panel edge, glyph missing?)."""
    img = Image.new('1', (PANEL_W, PANEL_H), 0)
    seq = slot_seq(cp)
    if seq is None:
        return img, 0, True
    px, lost = img.load(), [0]

    def plot(x, y):
        if 0 <= x < PANEL_W and 0 <= y < PANEL_H:
            px[x, y] = 1
        else:
            lost[0] += 1

    saved = oled_preview.OVERSHOOT
    oled_preview.OVERSHOOT = 200            # count every ink pixel, not only the slack
    try:
        RENDERER.draw(plot, seq, BUFFER_X, BASELINE)
    finally:
        oled_preview.OVERSHOOT = saved
    return img, lost[0], False


def tab(name: str) -> list[int]:
    src = re.sub(r'//.*', '', (_PK / 'emoji' / 'emoji_data.h').read_text())
    m = re.search(rf'static const uint32_t {name}\[\] = \{{(.*?)\}};', src, re.S)
    if not m:
        sys.exit(f'no array {name} in emoji_data.h')
    return [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)', m.group(1))]


def sheet(title: str, cps: list[int], path: Path, per_page: int, cols: int = 10, sc: int = 2):
    try:
        font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 20)
        small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 12)
    except OSError:
        font = small = ImageFont.load_default()
    cell_w, cell_h = (PANEL_W + 4) * sc, (PANEL_H + 4) * sc + 16
    pages = [cps[i:i + per_page] for i in range(0, len(cps), per_page)]
    rows = -(-per_page // cols)
    sheet_h = 50 + len(pages) * (34 + rows * cell_h)
    out = Image.new('RGB', (cols * cell_w + 20, sheet_h), (40, 40, 40))
    d = ImageDraw.Draw(out)
    d.text((10, 12), f'{title}  ({len(cps)} slots, {len(pages)} pages)', fill='white', font=font)
    y = 50
    for pi, page in enumerate(pages):
        d.text((10, y), f'page {pi + 1}/{len(pages)}', fill=(180, 200, 255), font=font)
        y += 34
        for i, cp in enumerate(page):
            img, lost, missing = render(cp)
            x0, y0 = 10 + (i % cols) * cell_w, y + (i // cols) * cell_h
            out.paste(img.convert('RGB').resize((PANEL_W * sc, PANEL_H * sc), Image.NEAREST),
                      (x0 + 2 * sc, y0 + 2 * sc))
            label = 'Glyph missing!' if missing else (f'U+{cp:04X} cut {lost}' if lost else f'U+{cp:04X}')
            d.text((x0 + 4, y0 + PANEL_H * sc + 3), label,
                   fill=(255, 140, 140) if (lost or missing) else (170, 170, 170), font=small)
        y += rows * cell_h
    out.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('items', nargs='+', help='emoji_data.h array names (emj_cat3) or codepoints (0x2211)')
    ap.add_argument('-o', '--out-dir', default='.', help='where the PNGs go')
    ap.add_argument('--per-page', type=int, default=38, help='slots per page (38 = split72)')
    ap.add_argument('--cut-only', action='store_true', help='print the cut list, write no PNG')
    a = ap.parse_args()
    groups = []
    loose = [int(i, 16) for i in a.items if i.lower().startswith('0x')]
    groups += [(n, tab(n)) for n in a.items if not n.lower().startswith('0x')]
    if loose:
        groups.append(('codepoints', loose))
    bad = 0
    for title, cps in groups:
        for cp in cps:
            _, lost, missing = render(cp)
            if lost or missing:
                bad += 1
                print(f'{title} U+{cp:04X} {chr(cp)}: ' + ('Glyph missing!' if missing else f'{lost} px cut'))
        if not a.cut_only:
            print('wrote', sheet(title, cps, Path(a.out_dir) / f'tab_{title}.png', a.per_page))
    print(f'{bad} slot(s) cut or missing')


if __name__ == '__main__':
    main()
