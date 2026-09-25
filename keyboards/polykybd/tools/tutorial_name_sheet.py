# Preview the tutorial's name rows (Latin on the left half, native script on the right)
# through the host preview renderer. Usage: python3 tutorial_name_sheet.py  (needs Pillow and
# ../../../../PolyKybdHost/tools on the path; writes the PNG named at the bottom).
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.environ.get('POLYHOST_TOOLS', os.path.join(REPO, '..', 'PolyKybdHost', 'tools')))
import oled_preview as op
from PIL import Image, ImageDraw, ImageFont
R = op.load_renderer(os.path.join(REPO, 'keyboards', 'polykybd', 'base', 'fonts')); op.OVERSHOOT=24
TIER=0xF0000
# (enum, latin, native units, rtl)
C = [
 ('ELGR','Greek',   'ΕΛΛΑΣ', False),
 ('ARSA','Arabic',  'العربية', True),
 ('HEIL','Hebrew',  'עברית', True),
 ('FAIR','Persian', 'فارسی', True),
 ('URPK','Urdu',    'اردو', True),
 ('PSAF','Pashto',  'پښتو', True),
 ('HIIN','Hindi',   'हिन्दी', False),
 ('MRIN','Marathi', 'मराठी', False),
 ('NENP','Nepali',  'नेपाली', False),
 ('BNIN','Bengali', 'বাংলা', False),
 ('TAIN','Tamil',   'தமிழ்', False),
 ('TEIN','Telugu',  'తెలుగు', False),
 ('THTH','Thai',    'ไทย', False),
 ('KAGE','Kartuli', 'ქართული', False),
 ('HYAM','Hayeren', 'ՀԱՅԵՐԵՆ', False),
 ('AMET','Amharic', 'አማርኛ', False),
 ('JAJP','Nihongo', 'にほんご', False),
 ('KOKR','Hangul',  '하ᄂ그ᄅ', False),
 ('IUCA','Inuktut', 'ᐃᓄᒃᑎᑐᑦ', False),
 ('CKUS','Tsalagi', 'ᏣᎳᎩ', False),
 ('MNMN','Mongol',  'МОНГОЛ', False),
]
W,H,G=72,40,6
def key_img(units, tier_first=True):
    im=Image.new('L',(W,H),0)
    cps=[ord(c) for c in units]
    if len(cps)==1 and tier_first and 65<=cps[0]<=90 and R._font(TIER+cps[0]) is not None: cps=[TIER+cps[0]]
    if any(R._font(c) is None for c in cps): return im, True
    ink=set(); R.draw(lambda x,y: ink.add((x,y)), cps, op.BUFFER_X, 30)
    if not ink: return im, False
    xs=[x for x,_ in ink]; ys=[y for _,y in ink]
    dx=(W-(max(xs)-min(xs)+1))//2-min(xs); dy=(H-(max(ys)-min(ys)+1))//2-min(ys)
    for x,y in ink:
        X,Y=x+dx,y+dy
        if 0<=X<W and 0<=Y<H: im.putpixel((X,Y),255)
    return im, False
def row(units):
    units=list(units)
    keys=[u for u in units]
    start=(7-len(keys))//2
    return [(start+k, u) for k,u in enumerate(keys)]
LW=130
sheet=Image.new('L',(LW+2*(7*(W+G))+40, len(C)*(H+14)+10),30)
d=ImageDraw.Draw(sheet); fnt=ImageFont.load_default()
report=[]
for r,(en,lat,nat,rtl) in enumerate(C):
    y=10+r*(H+14)
    ok = len(lat)<=7 and len(nat)<=7
    missing=False
    d.text((6,y+14), f"{lat} ({en[:2].lower()}-{en[2:]})", fill=255, font=fnt)
    for pos,u in row(lat.upper()):
        im,m=key_img(u); missing|=m
        sheet.paste(im,(LW+pos*(W+G),y))
    nat_units=list(nat)[::-1] if rtl else list(nat)
    for pos,u in row(nat_units):
        im,m=key_img(u); missing|=m
        if m: ImageDraw.Draw(im).rectangle([0,0,W-1,H-1],outline=128)
        sheet.paste(im,(LW+7*(W+G)+40+pos*(W+G),y))
    report.append((lat,len(lat),len(nat),'MISSING GLYPHS' if missing else 'ok'))
sheet.save('tutorial_name_candidates.png')
for x in report: print(*x)
