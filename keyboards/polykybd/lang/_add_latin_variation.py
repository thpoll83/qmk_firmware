#!/usr/bin/env python3
"""Append a variation to a letter's row in the latin_sup_ex sheet (sheet3.xml) of
lang_lut.xlsx, in place.  Only sheet3.xml is rewritten; every other zip entry --
and therefore every cached formula result cog reads with data_only=True -- is
copied byte-for-byte.

⚠️ This exists because openpyxl MUST NOT save this workbook: it does not evaluate
formulas, so a round-trip drops every cached <v> and the next run_cog.sh emits
empty tables with no error.  See lang/README.md.

Each letter occupies two rows: the odd row holds the label in column A and the
variation CHARACTERS in B onwards, the row below holds =DEC2HEX(UNICODE(..)) over
each of them -- and it is that cached hex that cog reads.  So appending a
variation means writing both cells, plus the hex the formula would have produced.

usage: _add_latin_variation.py <xlsx> <letter> <char> [<letter> <char> ...]
       _add_latin_variation.py --dry-run <xlsx> <letter> <char> ...
"""
import sys, re, zipfile, shutil, os, tempfile, atexit, unicodedata
from xml.sax.saxutils import escape

SHEET = "xl/worksheets/sheet3.xml"

argv = sys.argv[1:]
DRY = False
if argv and argv[0] == "--dry-run":
    DRY = True
    argv = argv[1:]
if len(argv) < 3 or len(argv) % 2 == 0:
    sys.exit(__doc__.strip().splitlines()[-2].strip())
XLSX, pairs = argv[0], list(zip(argv[1::2], argv[2::2]))
if not os.path.isfile(XLSX):
    sys.exit(f"file not found: {XLSX}")


def colname(n):                                  # 1-based index -> A1 letters
    s = ""
    while n:
        n, r = divmod(n - 1, 26)
        s = chr(65 + r) + s
    return s


def colidx(name):
    n = 0
    for ch in name:
        n = n * 26 + (ord(ch) - 64)
    return n


def cells_of(body):
    return re.findall(r'<c r="[A-Z]+\d+"[^>]*?(?:/>|>.*?</c>)', body, re.S)


def ref_of(cell):
    return re.search(r'r="([A-Z]+\d+)"', cell).group(1)


def col_of(ref):
    return colidx(re.match(r'([A-Z]+)', ref).group(1))


def style_of(cell):
    m = re.search(r'\ss="(\d+)"', cell)
    return f' s="{m.group(1)}"' if m else ""


def splice(xml, row, new_cells):
    """Insert cells (dict col -> xml) into <row r="row">, keeping column order."""
    m = re.search(r'(<row r="%d"[^>]*>)(.*?)(</row>)' % row, xml, re.S)
    if not m:
        sys.exit(f"row {row} not found in {SHEET}")
    out, pending = [], dict(new_cells)
    for c in cells_of(m.group(2)):
        ci = col_of(ref_of(c))
        for k in sorted(k for k in pending if k < ci):
            out.append(pending.pop(k))
        out.append(pending.pop(ci, c) if ci in pending else c)
    out.extend(pending[k] for k in sorted(pending))
    return xml[:m.start()] + m.group(1) + "".join(out) + m.group(3) + xml[m.end():]


# --- locate each letter's row + first free slot (reading is safe, saving is not)
from openpyxl import load_workbook
wb = load_workbook(XLSX, data_only=True)
sh = wb["latin_sup_ex"]
plan = []
for letter, char in pairs:
    if len(char) != 1:
        sys.exit(f"{letter!r}: expected a single character, got {char!r}")
    row = next((r for r in range(1, sh.max_row + 1, 2)
                if sh[f"A{r}"].value == letter), None)
    if row is None:
        sys.exit(f"no row labelled {letter!r} in latin_sup_ex "
                 f"(the label carries the case -- 'S' and 's' are different rows)")
    col = 2
    while sh.cell(row=row, column=col).value is not None:
        if sh.cell(row=row, column=col).value == char:
            sys.exit(f"{letter}: {char!r} is already slot {col - 2}")
        col += 1
    hexv = f"{ord(char):X}"                      # what DEC2HEX(UNICODE(..)) yields
    plan.append((letter, char, row, col, hexv))
    print(f"{letter}: append {char!r} U+{ord(char):04X} "
          f"({unicodedata.name(char, '?')}) at {colname(col)}{row} -> slot {col - 2}")
wb.close()

if DRY:
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix="latinvar_work_")
atexit.register(shutil.rmtree, tmp, ignore_errors=True)
with zipfile.ZipFile(XLSX) as z:
    z.extractall(tmp)
p = os.path.join(tmp, *SHEET.split("/"))
xml = open(p, encoding="utf-8").read()

for letter, char, row, col, hexv in plan:
    ref, hex_ref = f"{colname(col)}{row}", f"{colname(col)}{row + 1}"
    prev, prev_hex = f"{colname(col - 1)}{row}", f"{colname(col - 1)}{row + 1}"

    # inherit the style of the slot to the left so the new cells look like the rest
    def style_at(r, target):
        m = re.search(r'(<row r="%d"[^>]*>)(.*?)(</row>)' % r, xml, re.S)
        for c in cells_of(m.group(2)):
            if ref_of(c) == target:
                return style_of(c)
        return ""

    xml = splice(xml, row, {col: f'<c r="{ref}"{style_at(row, prev)} t="inlineStr">'
                                 f'<is><t xml:space="preserve">{escape(char)}</t></is></c>'})
    xml = splice(xml, row + 1,
                 {col: f'<c r="{hex_ref}"{style_at(row + 1, prev_hex)} t="str">'
                       f'<f aca="false">DEC2HEX(_xlfn.UNICODE({ref}))</f>'
                       f'<v>{hexv}</v></c>'})

    # Helper columns N (min) / O (max) are human-facing only -- cog never reads
    # them -- but a cache that disagrees with the row is worse than none.  N's
    # range is maintained to span exactly the filled slots, so extend it; O
    # already spans past the end, so only its cached value moves.
    m = re.search(r'(<row r="%d"[^>]*>)(.*?)(</row>)' % (row + 1), xml, re.S)
    fixed = []
    for c in cells_of(m.group(2)):
        r = ref_of(c)
        if r.startswith("N"):
            c = re.sub(r'(<f[^>]*>MIN\()(.*?)(\)</f>)',
                       lambda mm: f"{mm.group(1)}{mm.group(2)},HEX2DEC({hex_ref}){mm.group(3)}",
                       c)
            old = int(re.search(r'<v>(-?\d+)</v>', c).group(1))
            c = re.sub(r'<v>-?\d+</v>', f'<v>{min(old, ord(char))}</v>', c)
        elif r.startswith("O"):
            old = int(re.search(r'<v>(-?\d+)</v>', c).group(1))
            c = re.sub(r'<v>-?\d+</v>', f'<v>{max(old, ord(char))}</v>', c)
        fixed.append(c)
    xml = xml[:m.start()] + m.group(1) + "".join(fixed) + m.group(3) + xml[m.end():]

open(p, "w", encoding="utf-8").write(xml)

out = XLSX + ".new"
with zipfile.ZipFile(XLSX) as zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
    for it in zin.infolist():
        zout.writestr(it, open(p, "rb").read() if it.filename == SHEET
                          else zin.read(it.filename))
shutil.move(out, XLSX)
print(f"wrote {XLSX} ({len(plan)} variation(s))")
