"""Decide, per (language, key), whether the Shift PREVIEW is redundant.

The unshifted keycap draws a small preview of what Shift would type. On a key whose
Shift form is just the same letter in upper case -- `ä`/`Ä`, `ç`/`Ç`, `ł`/`Ł`,
`ђ`/`Ђ` -- that preview tells the reader nothing they do not already know, and it
costs the crowded right-hand side of the panel. On `1`/`!` or `7`/`&` it is the whole
point of the feature.

⚠️ This is NOT the same as emptying the Shift cell, and the difference is what makes
this file exist. The cell has TWO jobs: it is the preview in the unshifted view AND
the legend drawn when Shift is actually held. Clearing it suppresses the preview and
ALSO destroys the uppercase, so the keycap then shows `ä` while Shift is down --
reported from hardware 2026-09-02 after a tuning pass cleared 164 of them, because
`translate_keycode()` falls back to `lower_case` when VAR_SHIFT is NULL and nothing
else in the sheet carries the capital (VAR_CAPS is empty on those keys).

So the suppression is computed HERE, at build time, where Python's Unicode case
tables are available, and emitted as a bitmap the render path consults. The Shift
cell keeps the uppercase.

Fail SAFE: anything this cannot resolve confidently keeps its preview. A preview that
should not be there is a cosmetic nuisance; one that is missing is information lost.
"""
import re

# The display-list control codes a legend can carry (cursor nudges and the size/
# composite ops). They are not glyphs, so they take no part in the comparison.
_CTRL_MAX = 0x20

_ESCAPES = {'f': 0x0C, 'v': 0x0B, 'b': 0x08, 'n': 0x0A, 'r': 0x0D, 't': 0x09,
            'a': 0x07, '0': 0x00, '\\': 0x5C, '"': 0x22, "'": 0x27}


def _decode_literal(body):
    """Codepoints of a C `U"..."` body, or None if an escape is not understood."""
    out, i = [], 0
    while i < len(body):
        ch = body[i]
        if ch != '\\':
            out.append(ord(ch)); i += 1; continue
        if i + 1 >= len(body):
            return None
        nxt = body[i + 1]
        if nxt in ('x', 'u', 'U'):
            j = i + 2
            while j < len(body) and body[j] in '0123456789abcdefABCDEF':
                j += 1
            if j == i + 2:
                return None
            out.append(int(body[i + 2:j], 16)); i = j; continue
        if nxt in _ESCAPES:
            out.append(_ESCAPES[nxt]); i += 2; continue
        return None
    return out


def _append_named(out, value):
    """A named glyph resolves to ONE codepoint (the sheet's CALC DEC) or, when the
    caller's table comes from `named_glyphs.h`, to the macro's whole codepoint
    sequence. Accept both so the build-time emitter and the host preview can share
    this resolver instead of each writing their own -- two resolvers is how the
    keycap and the picture of it drift apart."""
    if isinstance(value, int):
        out.append(value)
    else:
        out.extend(value)


def resolve_cell(text, name_to_cp):
    """Codepoints a key_lut cell renders, or None when it cannot be resolved.

    A cell is a sequence of `U"..."` literals and bare named-glyph tokens, e.g.
    `U"\\f\\f" UMLAUT_A_SMALL`, `UMLAUT_A_SMALL`, `U"\\x06\\x06?"` or a bare `?`.
    """
    if text is None:
        return None
    s = str(text).strip()
    if not s or s == 'NULL':
        return None
    out, pos = [], 0
    for m in re.finditer(r'[uU]"((?:[^"\\]|\\.)*)"', s):
        gap = s[pos:m.start()].strip()
        if gap:
            for tok in gap.split():
                if tok not in name_to_cp:
                    return None
                _append_named(out, name_to_cp[tok])
        body = _decode_literal(m.group(1))
        if body is None:
            return None
        out += body
        pos = m.end()
    tail = s[pos:].strip()
    if tail:
        for tok in tail.split():
            if tok in name_to_cp:
                _append_named(out, name_to_cp[tok])
            elif len(tok) == 1:
                out.append(ord(tok))          # a bare literal character cell
            else:
                return None
    return out


def _single_glyph(cps):
    """The one printable codepoint of a legend, or None if it is not exactly one."""
    if cps is None:
        return None
    glyphs = [c for c in cps if c >= _CTRL_MAX]
    return glyphs[0] if len(glyphs) == 1 else None


def redundant_from_codepoints(base_cps, shift_cps):
    """THE RULE, on already-resolved legends. Both the build-time emitter and the
    host preview go through here, so the keycap and the picture of it cannot drift.

    Both directions of the case pair count, because a handful of layouts store the
    capital as the BASE and the small form as the Shift (da-DK / fo-FO `Ø`/`ø`,
    `Æ`/`æ`). An exact duplicate (hu-HU `ü`/`ü`, fr-CA `^`/`^`) needs no clause of
    its own -- a caseless character is its own upper case and a cased one its own
    lower, so one of the two below always catches it.
    """
    b = _single_glyph(base_cps)
    s = _single_glyph(shift_cps)
    if b is None or s is None:
        return False
    B, S = chr(b), chr(s)
    return B.upper() == S or B.lower() == S


def preview_is_redundant(base_cell, shift_cell, name_to_cp):
    """`redundant_from_codepoints` over two RAW key_lut cells (the cog entry point)."""
    return redundant_from_codepoints(resolve_cell(base_cell, name_to_cp),
                                     resolve_cell(shift_cell, name_to_cp))
