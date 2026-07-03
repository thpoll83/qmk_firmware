#!/usr/bin/env python3
"""Emit the fontconvert `-S` sequence + `-F<base>` fonts.yaml entry and the
glyph_script_blocks[] row for a new glyph script.

Edit the SCRIPTS list below (or import build() from your own script), then run.
Key order per script: letters KC_A..KC_Z (a..z), then — for scripts WITH their
own numerals — KC_1..KC_0 (1,2,3,4,5,6,7,8,9,0). The emitted `-F<base>` MUST
equal the base you put in glyph_script_blocks[] (poly_keymap.c); blocks are
0x40-aligned starting 0xE800.
"""
AZ = "abcdefghijklmnopqrstuvwxyz"
DIG = "1234567890"   # KC_1..KC_0 order


def restyle(digits=True):
    """Latin restyle: uppercase ASCII A..Z (+ 0..9). Font supplies the look."""
    seq = [ord(c.upper()) for c in AZ]
    if digits:
        seq += [ord(d) for d in DIG]
    return seq


def cipher(base_cp, digits_base=None):
    """1:1 cipher whose glyphs sit at consecutive codepoints from base_cp."""
    seq = [base_cp + i for i in range(26)]
    if digits_base is not None:
        seq += [digits_base + i for i in range(10)]
    return seq


def from_map(letter_map, digit_map=None):
    """Explicit per-key codepoints (transliteration / symbol-per-key)."""
    seq = [letter_map[c] for c in AZ]
    if digit_map:
        seq += [digit_map[d] for d in DIG]
    return seq


def emit(name, base, source, seq):
    has_digits = len(seq) > 26
    cps = ", ".join(f"{c:04X}" for c in seq)
    print(f"# --- {name}: base 0x{base:04X}, {len(seq)} glyphs, source '{source}' ---")
    print("state.h enum:")
    print(f"    GLYPH_{name.upper()} = <N>,")
    print("poly_keymap.c glyph_script_blocks[]:")
    print(f"    [GLYPH_{name.upper()}] = {{ 0x{base:04X}u, {str(has_digits).lower()} }},")
    print("fonts.yaml (fonts: list, after the last gscript entry):")
    print(f"  - {{category: gscript, variant: _{name.capitalize()}_, source: {source}, "
          f"extra_args: ['-F0x{base:04X}'], sequence: '{cps}'}}")
    print()


# ---- edit these entries for your new script(s) ----------------------------
SCRIPTS = [
    # ("newscript", 0xEA80, "my-source", restyle(digits=True)),
    # ("mycipher",  0xEAC0, "unifont-csur", cipher(0xF000)),
]

if __name__ == "__main__":
    if not SCRIPTS:
        print("Edit the SCRIPTS list in emit_sequence.py first "
              "(see restyle()/cipher()/from_map()).")
    for name, base, source, seq in SCRIPTS:
        emit(name, base, source, seq)
