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
"""
import sys, re, zipfile, shutil, os, unicodedata
import xml.dom.minidom as minidom
from xml.sax.saxutils import escape

SHEET = "xl/worksheets/sheet3.xml"
USAGE = """usage: _add_latin_variation.py [--dry-run] [--allow-asymmetric]
                              <xlsx> <letter> <char> [<letter> <char> ...]

  --dry-run           report the plan and change nothing
  --allow-asymmetric  permit adding to one case without its counterpart

e.g.  _add_latin_variation.py lang_lut.xlsx S 'S,' s 's,'   (real chars, not commas)"""


def die(msg):
    sys.exit(f"{os.path.basename(sys.argv[0])}: {msg}\n\n{USAGE}")


argv, DRY, ASYM = sys.argv[1:], False, False
while argv and argv[0].startswith("--"):
    if argv[0] == "--dry-run":
        DRY = True
    elif argv[0] == "--allow-asymmetric":
        ASYM = True
    else:
        die(f"unknown option {argv[0]!r}")
    argv = argv[1:]
if len(argv) < 3:
    die("expected an .xlsx and at least one <letter> <char> pair")
if len(argv) % 2 == 0:
    die("every <letter> needs exactly one <char> -- got an odd argument out")
XLSX, pairs = argv[0], list(zip(argv[1::2], argv[2::2]))
if not os.path.isfile(XLSX):
    die(f"file not found: {XLSX}")


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


def row_xml(xml, row):
    m = re.search(r'(<row r="%d"[^>]*>)(.*?)(</row>)' % row, xml, re.S)
    if not m:
        sys.exit(f"row {row} not found in {SHEET}")
    return m


def splice(xml, row, new_cells):
    """Insert cells (dict col -> xml) into <row r="row">, keeping column order."""
    m = row_xml(xml, row)
    out, pending = [], dict(new_cells)
    for c in cells_of(m.group(2)):
        ci = col_of(ref_of(c))
        for k in sorted(k for k in pending if k < ci):
            out.append(pending.pop(k))
        out.append(pending.pop(ci) if ci in pending else c)
    out.extend(pending[k] for k in sorted(pending))
    return xml[:m.start()] + m.group(1) + "".join(out) + m.group(3) + xml[m.end():]


def style_at(xml, row, target):
    """Reuse the neighbouring cell's style so the new cells match the rest."""
    for c in cells_of(row_xml(xml, row).group(2)):
        if ref_of(c) == target:
            m = re.search(r'\ss="(\d+)"', c)
            return f' s="{m.group(1)}"' if m else ""
    return ""


# --- plan: locate each letter's row + its slot (reading is safe, saving is not)
from openpyxl import load_workbook
wb = load_workbook(XLSX, data_only=True)
sh = wb["latin_sup_ex"]
plan, taken = [], {}          # taken: row -> next free column, across this batch
for letter, char in pairs:
    if len(char) != 1:
        die(f"{letter!r}: expected a single character, got {char!r}")
    row = next((r for r in range(1, sh.max_row + 1, 2)
                if sh[f"A{r}"].value == letter), None)
    if row is None:
        die(f"no row labelled {letter!r} in latin_sup_ex "
            f"(the label carries the case -- 'S' and 's' are different rows)")
    # ⚠️ Start from what THIS BATCH has already claimed, not from the worksheet:
    # two pairs naming the same letter both read the same original free column,
    # and the second splice then replaced the first -- silently dropping a
    # variation the tool reported as written.
    col = taken.get(row, 2)
    while sh.cell(row=row, column=col).value is not None:
        if sh.cell(row=row, column=col).value == char:
            die(f"{letter}: {char!r} is already slot {col - 2}")
        col += 1
    if any(p for p in plan if p[2] == row and p[1] == char):
        die(f"{letter}: {char!r} named twice in one command")
    taken[row] = col + 1
    plan.append((letter, char, row, col, f"{ord(char):X}"))   # DEC2HEX(UNICODE())
wb.close()

# The table is case-symmetric -- every variation exists in both cases -- and a
# half-pair is the shape of the mislabelled-row bug that shipped for ~2 years.
lonely = [l for l, _ in pairs
          if (l.swapcase() not in [x for x, _ in pairs])]
if lonely and not ASYM:
    die(f"one-case addition for {', '.join(sorted(set(lonely)))} -- the table is "
        f"case-symmetric, so pass both cases (or --allow-asymmetric if deliberate)")

for letter, char, row, col, hexv in plan:
    print(f"{letter}: append {char!r} U+{ord(char):04X} "
          f"({unicodedata.name(char, '?')}) at {colname(col)}{row} -> slot {col - 2}")

if DRY:
    sys.exit(0)

with zipfile.ZipFile(XLSX) as z:
    xml = z.read(SHEET).decode("utf-8")     # read the one member; never extractall

for letter, char, row, col, hexv in plan:
    ref, hex_ref = f"{colname(col)}{row}", f"{colname(col)}{row + 1}"
    prev, prev_hex = f"{colname(col - 1)}{row}", f"{colname(col - 1)}{row + 1}"

    xml = splice(xml, row,
                 {col: f'<c r="{ref}"{style_at(xml, row, prev)} t="inlineStr">'
                       f'<is><t xml:space="preserve">{escape(char)}</t></is></c>'})
    xml = splice(xml, row + 1,
                 {col: f'<c r="{hex_ref}"{style_at(xml, row + 1, prev_hex)} t="str">'
                       f'<f aca="false">DEC2HEX(_xlfn.UNICODE({ref}))</f>'
                       f'<v>{hexv}</v></c>'})

    # Helper columns N (min) / O (max) are human-facing only -- cog never reads
    # them -- but a cache that disagrees with the row is worse than none.  N's
    # range is maintained to span exactly the filled slots, so extend it; O
    # already spans past the end, so only its cached value moves.
    m = row_xml(xml, row + 1)
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

# The splices above are string surgery -- deliberately, because anything that
# re-serialises the sheet would rewrite bytes we are trying to leave alone.  So
# prove the result still parses before it replaces a working workbook: a bad
# regex would otherwise ship a corrupt .xlsx that only fails when someone opens it.
try:
    minidom.parseString(xml.encode("utf-8"))
except Exception as e:
    sys.exit(f"refusing to write: the patched {SHEET} is not well-formed XML ({e})")

out = XLSX + ".new"
with zipfile.ZipFile(XLSX) as zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
    for it in zin.infolist():
        zout.writestr(it, xml.encode("utf-8") if it.filename == SHEET
                          else zin.read(it.filename))
shutil.move(out, XLSX)
print(f"wrote {XLSX} ({len(plan)} variation(s))")
