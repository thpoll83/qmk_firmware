#!/usr/bin/env python3
"""One-time: relocate the per-row MIN/MAX helper columns in latin_sup_ex.

The variation slots grow rightwards from column B, and the human-facing MIN/MAX
helpers sit to their right.  They have now been overrun twice:

  * originally at N/O -- ten slots past B -- which the free-column scan walked
    into as soon as a row gained an 11th variation;
  * then moved to S/T, which held only while MAX_SLOTS was 16.  Raising it for
    the 6-bit widening (64 slots) put the scan straight through them again.

So the position is no longer a literal: `_add_latin_variation.py` derives it from
FIRST_COL + MAX_SLOTS, which makes a collision unrepresentable rather than merely
further away.  This script moves the existing cells to match.

Renaming is enough -- a helper's formula references its own row's slot cells
(MIN(HEX2DEC(B46),...)), so only the cell's own `r=` changes.  The sheet-wide
aggregates further down reference the helper COLUMN as a range (S2:S92) and are
rewritten too.

Uses the same raw-XML surgery as the sibling tool, for the same reason: openpyxl
would drop every cached formula value and silently empty the generated tables.
"""
import re
import shutil
import sys
import zipfile
import xml.dom.minidom as minidom

SHEET = "xl/worksheets/sheet3.xml"


def colname(n):
    s = ""
    while n:
        n, r = divmod(n - 1, 26)
        s = chr(65 + r) + s
    return s


def main():
    if len(sys.argv) != 4:
        sys.exit(f"usage: {sys.argv[0]} <xlsx> <new-min-col> <new-max-col>\n"
                 f"  e.g. {sys.argv[0]} lang_lut.xlsx BN BO")
    xlsx, new_min, new_max = sys.argv[1:4]
    old_min, old_max = "S", "T"
    if new_min == old_min and new_max == old_max:
        sys.exit("nothing to do: helpers are already at those columns")

    with zipfile.ZipFile(xlsx) as z:
        xml = z.read(SHEET).decode("utf-8")

    # 1. the aggregate RANGES (…S2:S92) — do these first, while the column letter
    #    is still unambiguous.
    n_range = 0
    for old, new in ((old_min, new_min), (old_max, new_max)):
        xml, k = re.subn(r'\b%s(\d+):%s(\d+)\b' % (old, old),
                         lambda m, nw=new: f"{nw}{m.group(1)}:{nw}{m.group(2)}", xml)
        n_range += k

    # 2. the cells' own r="S46" attributes.  These need their own pass: a ref
    #    regex that excludes a preceding quote (so it does not corrupt unrelated
    #    attributes) cannot match them by construction.
    n_cell = 0
    for old, new in ((old_min, new_min), (old_max, new_max)):
        xml, k = re.subn(r'(<c r=")%s(\d+)"' % old,
                         lambda m, nw=new: f'{m.group(1)}{nw}{m.group(2)}"', xml)
        n_cell += k

    # 3. references to a single helper cell inside a formula (DEC2HEX(S95)).
    n_ref = 0
    for old, new in ((old_min, new_min), (old_max, new_max)):
        xml, k = re.subn(r'(?<![A-Z0-9"])%s(\d+)(?![0-9])' % old,
                         lambda m, nw=new: f"{nw}{m.group(1)}", xml)
        n_ref += k

    # 4. guard: nothing may still name the old columns.
    left = re.findall(r'<c r="(%s|%s)\d+"' % (old_min, old_max), xml)
    if left:
        sys.exit(f"refusing to write: {len(left)} helper cells still at {old_min}/{old_max}")

    try:
        minidom.parseString(xml.encode("utf-8"))
    except Exception as e:
        sys.exit(f"refusing to write: patched {SHEET} is not well-formed XML ({e})")

    out = xlsx + ".new"
    with zipfile.ZipFile(xlsx) as zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
        for it in zin.infolist():
            zout.writestr(it, xml.encode("utf-8") if it.filename == SHEET
                          else zin.read(it.filename))
    shutil.move(out, xlsx)
    print(f"moved helpers {old_min}/{old_max} -> {new_min}/{new_max} "
          f"({n_cell} cells, {n_ref} refs, {n_range} ranges)")


if __name__ == "__main__":
    main()
