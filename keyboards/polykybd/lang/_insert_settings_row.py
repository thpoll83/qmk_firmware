#!/usr/bin/env python3
"""Insert a new SETTINGS row into the key_lut sheet (sheet2.xml) of lang_lut.xlsx.

The settings block is the run of rows whose column A holds a ``{category.name}``
key (today rows 56-61). cog collects it by walking column A until the first empty
cell, so a new setting has to be INSERTED inside that run -- appending below the
`lower/upper/caps/ALT Gr` legend row that follows it would never be seen.

Inserting means renumbering every row below, which is why this is a script and not
a hand edit. It is safe here because the workbook has no formulas, no conditional
formatting and no merged cells outside row 1 -- verify that again before reusing it
on a workbook that has grown any.

The new row is CLONED from the last settings row, so it inherits that row's cell
styles and column layout exactly; only the values differ.

usage: _insert_settings_row.py <xlsx> <{key.name}> <variation 0-3> <lang,lang,...> [value]

  variation  0 lower / 1 upper / 2 caps / 3 ALT Gr -- which of the language's four
             sub-columns carries the value (VAR_SMALL/SHIFT/CAPS/ALTGR in the C).
  value      what to write for each named language (default 1); every other
             language is left blank, which cog emits as 0.
"""
import os
import re
import shutil
import sys
import tempfile
import zipfile
from xml.sax.saxutils import escape

if len(sys.argv) < 5:
    sys.exit(__doc__)
XLSX, KEY, VARIATION, LANGS = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4].split(",")
VALUE = sys.argv[5] if len(sys.argv) > 5 else "1"
if not (0 <= VARIATION <= 3):
    sys.exit("variation must be 0..3")
if not os.path.isfile(XLSX):
    sys.exit(f"file not found: {XLSX}")

tmp = tempfile.mkdtemp(prefix="insrow_")
try:
    with zipfile.ZipFile(XLSX) as z:
        z.extractall(tmp)
    sheet = os.path.join(tmp, "xl/worksheets/sheet2.xml")
    xml = open(sheet, encoding="utf-8").read()

    for bad, what in ((r"<f>", "formulas"), (r"<conditionalFormatting", "conditional formatting")):
        if re.search(bad, xml):
            sys.exit(f"sheet2 has {what}; renumbering would break it -- see the docstring")

    shared = open(os.path.join(tmp, "xl/sharedStrings.xml"), encoding="utf-8").read()
    strings = ["".join(re.findall(r"<t[^>]*>(.*?)</t>", si, re.S))
               for si in re.findall(r"<si>(.*?)</si>", shared, re.S)]

    def col_index(name):
        n = 0
        for ch in name:
            n = n * 26 + (ord(ch) - 64)
        return n

    def col_name(n):
        s = ""
        while n:
            n, r = divmod(n - 1, 26)
            s = chr(65 + r) + s
        return s

    rows = {int(r): (m.start(), m.end()) for m, r in
            ((m, m.group(1)) for m in re.finditer(r'<row r="(\d+)".*?</row>', xml, re.S))}

    def cell_text(row, col):
        """The displayed text of one cell, resolving a shared-string reference."""
        seg = xml[rows[row][0]:rows[row][1]]
        m = re.search(r'<c r="%s%d"[^>]*?(?:/>|>(.*?)</c>)' % (col_name(col), row), seg, re.S)
        if not m or not m.group(1):
            return None
        body = m.group(1)
        if 't="s"' in m.group(0):
            return strings[int(re.search(r"<v>(\d+)</v>", body).group(1))]
        v = re.search(r"<v>(.*?)</v>", body, re.S)
        it = re.search(r"<t[^>]*>(.*?)</t>", body, re.S)
        return (v or it).group(1) if (v or it) else None

    # The settings block: the contiguous run of {key} rows in column A.
    settings = sorted(r for r in rows if (cell_text(r, 1) or "").startswith("{"))
    if not settings or settings != list(range(settings[0], settings[-1] + 1)):
        sys.exit(f"settings rows are not contiguous: {settings}")
    last, new_row = settings[-1], settings[-1] + 1
    if any((cell_text(r, 1) or "") == KEY for r in settings):
        sys.exit(f"{KEY} already exists")

    langs = []                                   # header row 1, one name per 4 columns
    c = 2
    while (name := cell_text(1, c)):
        langs.append(name)
        c += 4
    unknown = [l for l in LANGS if l not in langs]
    if unknown:
        sys.exit(f"unknown language(s): {unknown}")
    targets = {2 + 4 * langs.index(l) + VARIATION for l in LANGS}

    # Clone the last settings row: same row attributes, same per-cell styles.
    seg = xml[rows[last][0]:rows[last][1]]
    head = re.match(r"<row [^>]*>", seg).group(0)
    out = [head.replace('r="%d"' % last, 'r="%d"' % new_row, 1)]
    for c in re.findall(r'<c r="[A-Z]+\d+"[^>]*?(?:/>|>.*?</c>)', seg, re.S):
        ref = re.search(r'r="([A-Z]+)(\d+)"', c)
        col, style = col_index(ref.group(1)), re.search(r'\ss="\d+"', c)
        style = style.group(0) if style else ""
        r = f'{col_name(col)}{new_row}'
        if col == 1:
            out.append(f'<c r="{r}"{style} t="inlineStr"><is><t xml:space="preserve">{escape(KEY)}</t></is></c>')
        elif col in targets:
            out.append(f'<c r="{r}"{style} t="n"><v>{escape(VALUE)}</v></c>')
        else:
            out.append(f'<c r="{r}"{style}/>')
    out.append("</row>")
    new_xml_row = "".join(out)

    # Renumber every row at or below the insertion point, then splice the new one in.
    def bump(m):
        n = int(m.group(2))
        return m.group(1) + (str(n + 1) if n >= new_row else str(n)) + m.group(3)

    tail_start = rows[new_row][0] if new_row in rows else rows[last][1]
    head_xml, tail_xml = xml[:tail_start], xml[tail_start:]
    tail_xml = re.sub(r'(<row r=")(\d+)(")', bump, tail_xml)
    tail_xml = re.sub(r'(<c r="[A-Z]+)(\d+)(")', bump, tail_xml)
    xml = head_xml + new_xml_row + tail_xml

    m = re.search(r'<dimension ref="([A-Z]+)(\d+):([A-Z]+)(\d+)"/>', xml)
    if m:
        xml = xml.replace(m.group(0),
                          f'<dimension ref="{m.group(1)}{m.group(2)}:{m.group(3)}{int(m.group(4)) + 1}"/>')

    open(sheet, "w", encoding="utf-8").write(xml)
    out_xlsx = XLSX + ".new"
    with zipfile.ZipFile(XLSX) as zin, zipfile.ZipFile(out_xlsx, "w", zipfile.ZIP_DEFLATED) as zout:
        for it in zin.infolist():
            data = open(sheet, "rb").read() if it.filename == "xl/worksheets/sheet2.xml" else zin.read(it.filename)
            zout.writestr(it, data)
    shutil.move(out_xlsx, XLSX)
    print(f"inserted {KEY} at row {new_row}; set {len(targets)} language(s) to {VALUE}")
finally:
    shutil.rmtree(tmp, ignore_errors=True)
