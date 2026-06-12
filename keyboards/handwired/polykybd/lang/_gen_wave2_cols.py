#!/usr/bin/env python3
"""Generate key_lut columns + named glyphs + font codepoint sets for the 2026-06
Wave-2 batch (new scripts):

  ps-AF Pashto      — Arabic script, from xkb af(ps)            (extends Arabic font)
  iu-CA Inuktitut   — Canadian Aboriginal Syllabics, xkb ca(ike) (new font)
  cr-CA Cree        — Canadian Aboriginal Syllabics (no xkb)    (shares iu font)
  ck-US Cherokee    — Cherokee syllabary (no xkb)               (new font)

Pashto/Inuktitut are transcribed from the authoritative xkb symbols; Cherokee and
Cree have no xkb layout, so they use a documented systematic syllabary arrangement
(showcase — verify on hardware). ck = pseudo-code (Cherokee has no ISO-639-1).

Outputs: /tmp/w2_cols.json, /tmp/w2_named.json, /tmp/w2_fonts.json (codepoint sets
per script, for the fonts.yaml ranges).
"""
import json, re, os
_HERE = os.path.dirname(os.path.abspath(__file__))

XKB = "/usr/share/X11/xkb/symbols"
KEYSYMDEF = "/usr/include/X11/keysymdef.h"

# ── QMK keycode rows (xkb code -> lang_lut row) ──────────────────────────────
ROW = {}
for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
    ROW["KC_" + c] = 2 + i
for i in range(1, 10):
    ROW[f"KC_{i}"] = 27 + i
ROW["KC_0"] = 37
ROW.update({"KC_MINUS": 43, "KC_EQUAL": 44, "KC_LBRC": 45, "KC_RBRC": 46,
            "KC_BACKSLASH": 47, "KC_NONUS_HASH": 48, "KC_SEMICOLON": 49,
            "KC_QUOTE": 50, "KC_GRAVE": 51, "KC_COMMA": 52, "KC_DOT": 53,
            "KC_SLASH": 54, "KC_NONUS_BACKSLASH": 55})
XKB2KC = {"TLDE": "KC_GRAVE", "BKSL": "KC_BACKSLASH", "LSGT": "KC_NONUS_BACKSLASH",
          "SPCE": None}
for i, c in enumerate("1234567890"):
    XKB2KC[f"AE{i+1:02d}"] = f"KC_{c}"
XKB2KC.update({"AE11": "KC_MINUS", "AE12": "KC_EQUAL"})
for i, c in enumerate("QWERTYUIOP"):
    XKB2KC[f"AD{i+1:02d}"] = f"KC_{c}"
XKB2KC.update({"AD11": "KC_LBRC", "AD12": "KC_RBRC"})
for i, c in enumerate("ASDFGHJKL"):
    XKB2KC[f"AC{i+1:02d}"] = f"KC_{c}"
XKB2KC.update({"AC10": "KC_SEMICOLON", "AC11": "KC_QUOTE"})
for i, c in enumerate("ZXCVBNM"):
    XKB2KC[f"AB{i+1:02d}"] = f"KC_{c}"
XKB2KC.update({"AB08": "KC_COMMA", "AB09": "KC_DOT", "AB10": "KC_SLASH"})

S = lambda v: ["str", v]
N = lambda v: ["num", v]

# ASCII that can't be a bare U"<c>" literal -> route through a named glyph
ASCII_NAMED = {0x5C: "BACKSLASH", 0x22: "QUOTE"}
def ascii_cell(cp):
    if chr(cp).isdigit():
        return N(int(chr(cp)))
    if cp in ASCII_NAMED:
        return S(ASCII_NAMED[cp])
    return S(chr(cp))

# ── keysym -> codepoint ──────────────────────────────────────────────────────
_KS = {}  # name -> cp
for m in re.finditer(r'#define\s+XK_(\w+)\s+0x[0-9a-fA-F]+\s*/\*\s*U\+([0-9A-Fa-f]+)', open(KEYSYMDEF).read()):
    _KS.setdefault(m.group(1), int(m.group(2), 16))
# common ASCII/punct keysyms the xkb files use directly
_LIT = {"exclam": 0x21, "quotedbl": 0x22, "numbersign": 0x23, "dollar": 0x24,
        "percent": 0x25, "ampersand": 0x26, "apostrophe": 0x27, "parenleft": 0x28,
        "parenright": 0x29, "asterisk": 0x2A, "plus": 0x2B, "comma": 0x2C,
        "minus": 0x2D, "period": 0x2E, "slash": 0x2F, "colon": 0x3A,
        "semicolon": 0x3B, "less": 0x3C, "equal": 0x3D, "greater": 0x3E,
        "question": 0x3F, "at": 0x40, "bracketleft": 0x5B, "backslash": 0x5C,
        "bracketright": 0x5D, "asciicircum": 0x5E, "underscore": 0x5F,
        "braceleft": 0x7B, "bar": 0x7C, "braceright": 0x7D, "asciitilde": 0x7E,
        "space": 0x20, "multiply": 0xD7}
# format/combining chars and dead keys we never paint on a keycap
SKIP = {0x200C, 0x200D, 0x200E, 0x200F}            # ZWNJ ZWJ LRM RLM
def is_combining(cp): return 0x0300 <= cp <= 0x036F or 0x064B <= cp <= 0x065F or cp == 0x0670 or 0x06D6 <= cp <= 0x06ED

def keysym_cp(ks):
    """xkb keysym token -> codepoint, or None if it's a dead key / unpaintable."""
    if ks in ("NoSymbol",) or ks.startswith("dead_"):
        return None
    m = re.fullmatch(r'0x100([0-9a-fA-F]{4,5})', ks)        # 0x100xxxx unicode literal
    if m:
        return int(m.group(1), 16)
    m = re.fullmatch(r'U([0-9A-Fa-f]{4,5})', ks)            # Uxxxx
    if m:
        return int(m.group(1), 16)
    if len(ks) == 1:
        return ord(ks)
    if ks in _LIT:
        return _LIT[ks]
    if ks in _KS:
        return _KS[ks]
    return None

def parse_xkb(fname, variant):
    """-> {xkb_code: [level1, level2, level3, level4] as keysym strings}."""
    import subprocess
    txt = subprocess.run(["awk", f'/xkb_symbols "{variant}"/{{f=1}} f{{print}} /^}};/{{if(f)exit}}',
                          f"{XKB}/{fname}"], capture_output=True, text=True).stdout
    out = {}
    for m in re.finditer(r'key\s*<(\w+)>\s*\{\s*\[([^\]]*)\]', txt):
        lv = [x.strip() for x in m.group(2).split(",")]
        out[m.group(1)] = lv
    return out

# ── named-glyph registry (codepoint -> token), grouped by script for the font sets
named = {}       # token -> "%04X"
fontset = {"pashto": set(), "canadian": set(), "cherokee": set()}
def glyph(cp, prefix):
    tok = f"{prefix}_{cp:04X}"
    named[tok] = f"{cp:04X}"
    return tok

# Existing Arabic coverage (fonts.yaml): _Isolated_ 0x60c,0x61b,0x61f-0x669;
# _PerArab_ 0x679,67e,686,688,691,698,6a9,6af,6ba,6be,6c1-6c3,6cc,6d2-6d4,6f0-6f9;
# _Sorani_ 695,6a4,6b5,6c6,6ce,6d5; _FormsB_ fef5,fefb.
ARABIC_COVERED = set([0x60C, 0x61B]) | set(range(0x61F, 0x66A)) | \
    {0x679, 0x67E, 0x686, 0x688, 0x691, 0x698, 0x6A9, 0x6AF, 0x6BA, 0x6BE} | \
    set(range(0x6C1, 0x6C4)) | {0x6CC} | set(range(0x6D2, 0x6D5)) | set(range(0x6F0, 0x6FA)) | \
    {0x695, 0x6A4, 0x6B5, 0x6C6, 0x6CE, 0x6D5}

def arabic_token(cp):
    """An Arabic codepoint -> named glyph; register in _Pashto_ font set if the
    existing Arabic ranges don't already cover it."""
    if cp not in ARABIC_COVERED:
        fontset["pashto"].add(cp)
    return glyph(cp, "ARABIC")

# ── Pashto (af ps): Arabic; show base + (letter) shift/altgr, skip harakat ────
def build_pashto():
    raw = parse_xkb("af", "ps")
    cols = {}
    for code, levels in raw.items():
        kc = XKB2KC.get(code)
        if not kc:
            continue
        cps = [keysym_cp(l) if i < len(levels) else None for i, l in enumerate(levels[:3])]
        cps += [None] * (3 - len(cps))
        cells = []
        for cp in cps:
            if cp is None or cp in SKIP or is_combining(cp):
                cells.append(None)
            elif 0x0600 <= cp <= 0x06FF:   # Arabic block (skip FB50-FEFF presentation
                cells.append(S(arabic_token(cp)))   # forms — they'd balloon the font range)
            elif cp == 0x20AC:
                cells.append(S("EURO_SIGN"))
            elif 0x20 <= cp <= 0x7E:                                 # ascii
                cells.append(ascii_cell(cp))
            else:
                cells.append(None)
        small, shift, altgr = cells
        if small is None:           # never leave a NULL base on a remapped key
            continue
        cols[ROW[kc]] = [small, shift, small, altgr]   # CAPS=base (Arabic: no case)
    # Arabic settings block (ar-SA style)
    cols.update({56: [None, N(42), None, N(52)], 57: [None, N(0), None, N(13)],
                 58: [None, N(27), None, N(52)], 59: [None, N(0), None, N(13)],
                 60: [None, N(42), None, N(52)], 61: [None, N(0), None, N(13)]})
    return cols

# ── Inuktitut (ca ike): UCAS, 2 levels (base/shift) ──────────────────────────
def build_inuktitut():
    raw = parse_xkb("ca", "ike")
    cols = {}
    for code, levels in raw.items():
        kc = XKB2KC.get(code)
        if not kc:
            continue
        l1 = keysym_cp(levels[0]) if levels else None
        l2 = keysym_cp(levels[1]) if len(levels) > 1 else None
        def cell(cp):
            if cp is None or cp in SKIP or cp < 0x20:
                return None
            if 0x1400 <= cp <= 0x167F:
                fontset["canadian"].add(cp); return S(glyph(cp, "UCAS"))
            if 0x20 <= cp <= 0x7E:
                return ascii_cell(cp)
            return None
        small, shift = cell(l1), cell(l2)
        if small is None:
            continue
        cols[ROW[kc]] = [small, shift, small, None]
    cols.update(SET_SYLLABIC)
    return cols

# VAR_ALTGR h/v offsets (index 3) put the AltGr hint at the right edge (h=50 ->
# right-clamped by the firmware) and ~9 px low so a Latin descender (q/p/y) clears
# the 40 px keycap. Cherokee uses the shift preview (h=36) too, so letter keys can
# show base + shift syllable + AltGr Latin all at once.
SET_SYLLABIC = {56: [None, N(24), None, N(64)], 57: [None, N(0), None, N(9)],
                58: [None, N(24), None, N(64)], 59: [None, N(0), None, N(9)],
                60: [None, N(24), None, N(64)], 61: [None, N(0), None, N(9)]}

# ── Cherokee (no xkb): syllabary chart laid across the keys (showcase) ────────
# 85 syllables U+13A0..13F4 placed base+shift on the letter/number rows in chart
# order so the whole syllabary is visible. Documented as a showcase arrangement.
CHEROKEE_ORDER = list(range(0x13A0, 0x13F5))   # 85 codepoints
CHEROKEE_KEYS = (["KC_" + c for c in "QWERTYUIOP"] + ["KC_LBRC", "KC_RBRC"] +
                 ["KC_" + c for c in "ASDFGHJKL"] + ["KC_SEMICOLON", "KC_QUOTE"] +
                 ["KC_" + c for c in "ZXCVBNM"] + ["KC_COMMA", "KC_DOT", "KC_SLASH"] +
                 ["KC_" + d for d in "1234567890"] + ["KC_MINUS", "KC_EQUAL", "KC_GRAVE"])
def build_cherokee():
    cols, i = {}, 0
    for kc in CHEROKEE_KEYS:
        base = CHEROKEE_ORDER[i] if i < len(CHEROKEE_ORDER) else None
        shift = CHEROKEE_ORDER[i + 1] if i + 1 < len(CHEROKEE_ORDER) else None
        if base is None:
            break
        fontset["cherokee"].add(base)
        cell_s = S(glyph(base, "CHEROKEE"))
        cell_sh = None
        if shift is not None:
            fontset["cherokee"].add(shift); cell_sh = S(glyph(shift, "CHEROKEE"))
        cols[ROW[kc]] = [cell_s, cell_sh, cell_s, None]
        i += 2
    cols.update(SET_SYLLABIC)
    return cols

# ── Cree (no xkb): core Plains-Cree syllabics across the keys (showcase) ──────
# The Cree finals/syllabics most used, in vowel-series order, base+shift.
CREE_ORDER = [0x1403, 0x1431, 0x1404, 0x1432, 0x1405, 0x1433, 0x1406, 0x1434,  # vowels e i o a
    0x140A, 0x1438, 0x140B, 0x1439, 0x140C, 0x143A, 0x140D, 0x143B,            # w- series
    0x1426, 0x1428, 0x142A, 0x142C, 0x142E, 0x1430,                            # p-
    0x144E, 0x144F, 0x1450, 0x1451, 0x1455, 0x1466,                            # t-
    0x146B, 0x146C, 0x146D, 0x146E, 0x1472, 0x1483,                            # k-
    0x148B, 0x148C, 0x148D, 0x148E, 0x1492, 0x14A3,                            # c-
    0x14A5, 0x14A6, 0x14A7, 0x14A8, 0x14AC, 0x14BB,                            # m-
    0x14C0, 0x14C1, 0x14C2, 0x14C3, 0x14C7, 0x14D0,                            # n-
    0x14D5, 0x14D6, 0x14D7, 0x14D8, 0x14DC, 0x14EA,                            # s-
    0x14EF, 0x14F0, 0x14F1, 0x14F2, 0x14F6, 0x1505,                            # y-
    0x152A, 0x152D, 0x153E, 0x155D, 0x1559, 0x1483]                            # finals/misc
CREE_KEYS = CHEROKEE_KEYS
def build_cree():
    cols, i = {}, 0
    for kc in CREE_KEYS:
        if i >= len(CREE_ORDER):
            break
        base = CREE_ORDER[i]; shift = CREE_ORDER[i + 1] if i + 1 < len(CREE_ORDER) else None
        fontset["canadian"].add(base)
        cell_s = S(glyph(base, "UCAS")); cell_sh = None
        if shift is not None:
            fontset["canadian"].add(shift); cell_sh = S(glyph(shift, "UCAS"))
        cols[ROW[kc]] = [cell_s, cell_sh, cell_s, None]
        i += 2
    cols.update(SET_SYLLABIC)
    return cols

# ── CLDR keyboard parser (lang/layouts/<loc>-<plat>.xml, via dl-layouts.sh) ───
# CLDR ISO key position -> QMK keycode.
ISO2KC = {"E00": "KC_GRAVE", "E11": "KC_MINUS", "E12": "KC_EQUAL",
          "D11": "KC_LBRC", "D12": "KC_RBRC", "C10": "KC_SEMICOLON",
          "C11": "KC_QUOTE", "C12": "KC_NONUS_HASH", "B00": "KC_NONUS_BACKSLASH",
          "B08": "KC_COMMA", "B09": "KC_DOT", "B10": "KC_SLASH"}
for _i, _c in enumerate("1234567890"): ISO2KC[f"E{_i+1:02d}"] = f"KC_{_c}"
for _i, _c in enumerate("QWERTYUIOP"): ISO2KC[f"D{_i+1:02d}"] = f"KC_{_c}"
for _i, _c in enumerate("ASDFGHJKL"):  ISO2KC[f"C{_i+1:02d}"] = f"KC_{_c}"
for _i, _c in enumerate("ZXCVBNM"):    ISO2KC[f"B{_i+1:02d}"] = f"KC_{_c}"

def parse_cldr(path):
    """-> {modifier: {iso: output_string}}. modifier '' = base, 'shift' = shifted."""
    txt = open(path, encoding="utf-8").read()
    ent = {"&apos;": "'", "&quot;": '"', "&amp;": "&", "&lt;": "<", "&gt;": ">"}
    out = {}
    for km in re.finditer(r'<keyMap( modifiers="([^"]*)")?>(.*?)</keyMap>', txt, re.S):
        mod = (km.group(2) or "").split()[0] if km.group(2) else ""
        d = {}
        for m in re.finditer(r'<map iso="(\w+)" to="([^"]*)"', km.group(3)):
            to = m.group(2)
            # CLDR \u{XXXX} / \uXXXX escapes -> the actual character
            to = re.sub(r'\\u\{([0-9A-Fa-f]+)\}', lambda mm: chr(int(mm.group(1), 16)), to)
            to = re.sub(r'\\u([0-9A-Fa-f]{4})', lambda mm: chr(int(mm.group(1), 16)), to)
            for k, v in ent.items():
                to = to.replace(k, v)
            d[m.group(1)] = to
        out.setdefault(mod, {}).update(d)
    return out

# ── Cherokee ck-US: the Cherokee Nation layout (DIRECT 1:1), from CLDR windows ─
def chero_cell(s):
    if not s:
        return None
    if len(s) == 1 and s.isdigit():
        return N(int(s))
    toks = []
    for ch in s:
        cp = ord(ch)
        if 0x13A0 <= cp <= 0x13FD:
            fontset["cherokee"].add(cp); toks.append(glyph(cp, "CHEROKEE"))
        elif cp in ASCII_NAMED:
            toks.append(ASCII_NAMED[cp])
        elif 0x21 <= cp <= 0x7E:
            toks.append(ch)
    if not toks:
        return None
    # A keycap shows one glyph. The Cherokee Nation layout puts multi-syllable word
    # macros on the number row base (1->ᏣᎳᎩ, 2->ᎣᏏᏲ, 3->ᏩᏙ; the digit is on AltR);
    # those are "composed of multiple glyphs", so per the layout-matching rule we do
    # not paint them — the key keeps its single-glyph Shift syllable and falls back
    # to the plain digit for the base.
    if len(toks) > 1:
        return None
    return S(toks[0])

def build_cherokee():
    m = parse_cldr(os.path.join(_HERE, "layouts", "chr-windows.xml"))
    base, shift = m.get("", {}), m.get("shift", {})
    # AltGr layer: the Cherokee Nation board's "Latin escape" (AltGr+letter -> the
    # Latin letter), the plain digit/symbol on the number row, and one extra syllable
    # on the grave key. All single glyphs (1:1), so they make good per-key hints.
    altr = next((m[k] for k in m if "altR" in k and "shift" not in k), {})
    cols = {}
    for iso, kc in ISO2KC.items():
        b = chero_cell(base.get(iso))
        sh = chero_cell(shift.get(iso))
        ag = chero_cell(altr.get(iso))
        if b is None:
            # macro keys (1/2/3): the base already falls back to the en-US digit,
            # which is exactly what AltGr types there - drop the redundant hint.
            ag = None
        if b is None and sh is None and ag is None:
            continue                                              # nothing paintable -> en-US fallback
        cols[ROW[kc]] = [b, sh, b, ag]                            # CAPS=base; b=None on the macro keys
    cols.update(SET_SYLLABIC)
    return cols

# ── Cree cr-CA: roman board + 1:1 AltGr syllabic HINT per key (the rotation
# system — consonant shape + vowel rotation). We show the base citation syllabic
# (the -a form / standalone vowel); the vowel rotation stays composed (typed). ──
CREE_HINT = {  # roman QWERTY key -> Unified Canadian Aboriginal Syllabics citation cp
    "KC_A": 0x140A, "KC_E": 0x1401, "KC_I": 0x1403, "KC_O": 0x1405,   # vowels ᐊᐁᐃᐅ
    "KC_P": 0x1438, "KC_T": 0x1455, "KC_K": 0x1472, "KC_C": 0x1490,   # pa ta ka ca
    "KC_M": 0x14AA, "KC_N": 0x14C7, "KC_S": 0x14F4, "KC_Y": 0x152D,   # ma na sa ya
    "KC_W": 0x1417, "KC_L": 0x14DA, "KC_R": 0x154B}                   # wa la ra
SET_CREE = {56: [None, S("HIDE"), None, N(50)], 57: [None, S("HIDE"), None, N(9)],
            58: [None, N(35), None, None],      59: [None, N(0), None, None],
            60: [None, N(35), None, None],      61: [None, N(0), None, None]}
def build_cree_hints():
    cols = {}
    for kc, cp in CREE_HINT.items():
        fontset["canadian"].add(cp)
        lo = kc[3].lower()
        cols[ROW[kc]] = [S(lo), S(lo.upper()), None, S(glyph(cp, "UCAS"))]
    cols.update(SET_CREE)
    return cols

SPEC = {"iu-CA": build_inuktitut(), "cr-CA": build_cree_hints(), "ck-US": build_cherokee()}
ORDER = ["iu-CA", "cr-CA", "ck-US"]

spec_json = {n: {str(r): v for r, v in SPEC[n].items()} for n in ORDER}
json.dump(spec_json, open("/tmp/w2_cols.json", "w"), indent=1)
json.dump([[k, v] for k, v in sorted(named.items())], open("/tmp/w2_named.json", "w"), indent=1)
json.dump({k: sorted(v) for k, v in fontset.items()}, open("/tmp/w2_fonts.json", "w"), indent=1)
print("order:", ",".join(ORDER))
for n in ORDER:
    print(f"  {n}: {len(SPEC[n])-6} keys")
print(f"named glyphs: {len(named)} | font codepoints: " +
      ", ".join(f"{k}={len(v)}" for k, v in fontset.items()))
