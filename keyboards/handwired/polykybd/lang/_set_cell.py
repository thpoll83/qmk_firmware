#!/usr/bin/env python3
"""Set a single cell in the key_lut sheet (sheet2.xml) of lang_lut.xlsx, in place.
Inserts the <c> in column order if absent, or replaces it if present. Only
sheet2.xml is rewritten; every other zip entry (and the formula caches) is
copied byte-for-byte.

usage: _set_cell.py <xlsx> <row> <col1based> <value> [num|str]
"""
import sys, re, zipfile, shutil, os
from xml.sax.saxutils import escape

XLSX=sys.argv[1]; ROW=int(sys.argv[2]); COL=int(sys.argv[3]); VAL=sys.argv[4]
KIND=sys.argv[5] if len(sys.argv)>5 else "num"

def colname(n):
    s=""
    while n: n,r=divmod(n-1,26); s=chr(65+r)+s
    return s
REF=f"{colname(COL)}{ROW}"
CELL=(f'<c r="{REF}"><v>{VAL}</v></c>' if KIND=="num"
      else f'<c r="{REF}" t="inlineStr"><is><t xml:space="preserve">{escape(VAL)}</t></is></c>')

tmp="/tmp/_setcell_work"; shutil.rmtree(tmp,ignore_errors=True); os.makedirs(tmp)
with zipfile.ZipFile(XLSX) as z: z.extractall(tmp)
p=os.path.join(tmp,"xl/worksheets/sheet2.xml")
xml=open(p,encoding="utf-8").read()

def colof(ref): return re.match(r'([A-Z]+)\d+',ref).group(1)
def colidx(name):
    n=0
    for ch in name: n=n*26+(ord(ch)-64)
    return n

m=re.search(r'(<row r="%d"[^>]*>)(.*?)(</row>)'%ROW, xml, re.S)
if not m: sys.exit(f"row {ROW} not found")
head,body,tail=m.group(1),m.group(2),m.group(3)
cells=re.findall(r'<c r="[A-Z]+\d+"[^>]*?(?:/>|>.*?</c>)', body, re.S)
out=[]; inserted=False
for c in cells:
    ref=re.search(r'r="([A-Z]+\d+)"',c).group(1)
    ci=colidx(colof(ref))
    if ci==COL:                      # replace existing
        out.append(CELL); inserted=True; continue
    if ci>COL and not inserted:      # insert before first higher column
        out.append(CELL); inserted=True
    out.append(c)
if not inserted: out.append(CELL)
xml=xml[:m.start()]+head+"".join(out)+tail+xml[m.end():]
open(p,"w",encoding="utf-8").write(xml)

out_xlsx=XLSX+".new"
with zipfile.ZipFile(XLSX) as zin, zipfile.ZipFile(out_xlsx,"w",zipfile.ZIP_DEFLATED) as zout:
    for it in zin.infolist():
        data=open(p,"rb").read() if it.filename=="xl/worksheets/sheet2.xml" else zin.read(it.filename)
        zout.writestr(it,data)
shutil.move(out_xlsx,XLSX)
print(f"set {REF} = {VAL} ({KIND})")
