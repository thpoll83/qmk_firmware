#!/usr/bin/env python3
"""Generate key_lut columns + named-glyph additions + fonts.yaml ranges for the
alphabetic batch: zh-TW (Bopomofo/Zhuyin), ka-GE (Georgian), hy-AM (Armenian),
and id-ID (Indonesian = plain US QWERTY, empty column + flag + host fold).

None of these scripts use combining marks, so there are NO dotted-circle
composites - each is just a block-range font plus base glyphs, like the existing
Cyrillic/Greek/Hebrew alphabets.

Outputs: /tmp/alpha_cols.json, /tmp/alpha_named.json, /tmp/alpha_fonts.json
"""
import json, re
from fontTools.ttLib import TTFont
F="/home/user/qmk_firmware/keyboards/handwired/polykybd/fonts/"
def cmap(p): return set(TTFont(F+p).getBestCmap().keys())
CM={"ka":cmap("noto-sans-georgian/NotoSansGeorgian-Regular.ttf"),
    "hy":cmap("noto-sans-armenian/NotoSansArmenian-Regular.ttf"),
    "zh":cmap("noto-sans-tc/NotoSansTC-Regular.ttf")}

ROW={}; L="ABCDEFGHIJKLMNOPQRSTUVWXYZ"
for i,c in enumerate(L): ROW["KC_"+c]=2+i
for i in range(1,10): ROW[f"KC_{i}"]=27+i
ROW["KC_0"]=37
ROW.update({"KC_MINUS":43,"KC_EQUAL":44,"KC_LBRC":45,"KC_RBRC":46,
            "KC_BACKSLASH":47,"KC_NONUS_HASH":48,"KC_SEMICOLON":49,
            "KC_QUOTE":50,"KC_GRAVE":51,"KC_COMMA":52,"KC_DOT":53,
            "KC_SLASH":54,"KC_NONUS_BACKSLASH":55})

# keysym -> codepoint from keysymdef.h (Georgian_*, Armenian_*)
KS={}
for line in open("/usr/include/X11/keysymdef.h"):
    m=re.search(r'#define XK_((?:Georgian|Armenian)_\w+)\s+0x[0-9a-fA-F]+\s+/\*\s*U\+([0-9A-Fa-f]{4,6})',line)
    if m: KS[m.group(1)]=int(m.group(2),16)

LIT={'comma':'COMMA','dollar':'$','parenright':')','parenleft':'(','percent':'%',
     'question':'?','semicolon':';','colon':':','apostrophe':"'",'minus':'-',
     'quotedbl':'QUOTE','guillemotright':'DBL_ANGLE_QMARK_R','guillemotleft':'DBL_ANGLE_QMARK_L'}

named=[]; seen=set()
def add(name,cp):
    if name not in seen: named.append([name,f"{cp:04X}"]); seen.add(name)

def conv(ks, pfx, cm):
    """keysym -> ('str',token) | None"""
    if ks is None: return None
    if ks in LIT: return ("str", LIT[ks])              # ASCII via latin font
    cp=None
    if ks in KS: cp=KS[ks]
    else:
        m=re.match(r'U([0-9A-Fa-f]{4,6})$', ks)
        if m: cp=int(m.group(1),16)
    if cp is None: return None                          # Latin letter / unmapped
    if cp not in cm: return None                        # no glyph in script font
    add(f"{pfx}_{cp:04X}", cp); return ("str", f"{pfx}_{cp:04X}")

EL_SETTINGS={56:[None,["str","HIDE"],None,["num",50]],57:[None,["str","HIDE"],None,["num",12]],
             58:[None,["num",27],None,["num",50]],59:[None,["num",0],None,["num",12]],
             60:[None,["num",27],None,["num",50]],61:[None,["num",0],None,["num",12]]}

cols={}; fonts=[]

# ---- Georgian (xkb ge basic): unicameral; number row = US (NULL) ----------
GE={"KC_Q":("Georgian_khar",None),"KC_W":("Georgian_cil","Georgian_char"),
 "KC_E":("Georgian_en",None),"KC_R":("Georgian_rae","Georgian_ghan"),
 "KC_T":("Georgian_tar","Georgian_tan"),"KC_Y":("Georgian_qar",None),
 "KC_U":("Georgian_un",None),"KC_I":("Georgian_in",None),"KC_O":("Georgian_on",None),
 "KC_P":("Georgian_par",None),"KC_A":("Georgian_an",None),
 "KC_S":("Georgian_san","Georgian_shin"),"KC_D":("Georgian_don",None),
 "KC_F":("Georgian_phar",None),"KC_G":("Georgian_gan",None),"KC_H":("Georgian_hae",None),
 "KC_J":("Georgian_jhan","Georgian_zhar"),"KC_K":("Georgian_kan",None),
 "KC_L":("Georgian_las",None),"KC_Z":("Georgian_zen","Georgian_jil"),
 "KC_X":("Georgian_xan",None),"KC_C":("Georgian_can","Georgian_chin"),
 "KC_V":("Georgian_vin",None),"KC_B":("Georgian_ban",None),"KC_N":("Georgian_nar",None),
 "KC_M":("Georgian_man",None)}
gcol={}
for k,(b,s) in GE.items():
    cb=conv(b,"GEORGIAN",CM["ka"]); cs=conv(s,"GEORGIAN",CM["ka"])
    gcol[ROW[k]]=[cb,cs,cb,None]      # CAPS=base (unicameral)
gcol.update(EL_SETTINGS); cols["ka-GE"]=gcol
fonts.append({"lang":"ka-GE","cat":"georgian","src":"noto-sans-georgian","range":[0x10A0,0x10FF]})

# ---- Armenian (xkb am basic): bicameral; whole board remapped -------------
AM={"KC_GRAVE":("Armenian_separation_mark","Armenian_exclam"),
 "KC_1":("Armenian_fe","Armenian_FE"),"KC_2":("Armenian_dza","Armenian_DZA"),
 "KC_3":("Armenian_hyphen","U2014"),"KC_4":("comma","dollar"),
 "KC_5":("Armenian_full_stop","U2026"),"KC_6":("Armenian_question","percent"),
 "KC_7":("U2024","Armenian_ligature_ew"),"KC_8":("Armenian_accent","Armenian_apostrophe"),
 "KC_9":("parenright","parenleft"),"KC_0":("Armenian_o","Armenian_O"),
 "KC_MINUS":("Armenian_e","Armenian_E"),"KC_EQUAL":("Armenian_ghat","Armenian_GHAT"),
 "KC_Q":("Armenian_tche","Armenian_TCHE"),"KC_W":("Armenian_pyur","Armenian_PYUR"),
 "KC_E":("Armenian_ben","Armenian_BEN"),"KC_R":("Armenian_se","Armenian_SE"),
 "KC_T":("Armenian_men","Armenian_MEN"),"KC_Y":("Armenian_vo","Armenian_VO"),
 "KC_U":("Armenian_vyun","Armenian_VYUN"),"KC_I":("Armenian_ken","Armenian_KEN"),
 "KC_O":("Armenian_at","Armenian_AT"),"KC_P":("Armenian_to","Armenian_TO"),
 "KC_LBRC":("Armenian_tsa","Armenian_TSA"),"KC_RBRC":("Armenian_tso","Armenian_TSO"),
 "KC_A":("Armenian_je","Armenian_JE"),"KC_S":("Armenian_vev","Armenian_VEV"),
 "KC_D":("Armenian_gim","Armenian_GIM"),"KC_F":("Armenian_yech","Armenian_YECH"),
 "KC_G":("Armenian_ayb","Armenian_AYB"),"KC_H":("Armenian_nu","Armenian_NU"),
 "KC_J":("Armenian_ini","Armenian_INI"),"KC_K":("Armenian_tyun","Armenian_TYUN"),
 "KC_L":("Armenian_ho","Armenian_HO"),"KC_SEMICOLON":("Armenian_pe","Armenian_PE"),
 "KC_QUOTE":("Armenian_re","Armenian_RE"),
 "KC_BACKSLASH":("guillemotright","guillemotleft"),
 "KC_NONUS_HASH":("guillemotright","guillemotleft"),
 "KC_NONUS_BACKSLASH":("question","Armenian_hyphen"),
 "KC_Z":("Armenian_zhe","Armenian_ZHE"),"KC_X":("Armenian_da","Armenian_DA"),
 "KC_C":("Armenian_cha","Armenian_CHA"),"KC_V":("Armenian_hi","Armenian_HI"),
 "KC_B":("Armenian_za","Armenian_ZA"),"KC_N":("Armenian_lyun","Armenian_LYUN"),
 "KC_M":("Armenian_ke","Armenian_KE"),"KC_COMMA":("Armenian_khe","Armenian_KHE"),
 "KC_DOT":("Armenian_sha","Armenian_SHA"),"KC_SLASH":("Armenian_ra","Armenian_RA")}
acol={}
for k,(b,s) in AM.items():
    cb=conv(b,"ARMENIAN",CM["hy"]); cs=conv(s,"ARMENIAN",CM["hy"])
    bcp=KS.get(b)
    is_lower = bcp is not None and 0x561<=bcp<=0x586
    cc=None if is_lower else cb          # letters: capslock uppercases; else lock base
    acol[ROW[k]]=[cb,cs,cc,None]
acol.update(EL_SETTINGS); cols["hy-AM"]=acol
fonts.append({"lang":"hy-AM","cat":"armenian","src":"noto-sans-armenian","range":[0x530,0x58F]})

# ---- zh-TW Bopomofo / Zhuyin (standard 大千 layout) ------------------------
# QMK key -> Bopomofo/tone codepoint (single level; shift not used by the IME)
ZH_CP={
 "KC_1":0x3105,"KC_Q":0x3106,"KC_A":0x3107,"KC_Z":0x3108,   # ㄅㄆㄇㄈ
 "KC_2":0x3109,"KC_W":0x310A,"KC_S":0x310B,"KC_X":0x310C,   # ㄉㄊㄋㄌ
 "KC_E":0x310D,"KC_D":0x310E,"KC_C":0x310F,                 # ㄍㄎㄏ
 "KC_R":0x3110,"KC_F":0x3111,"KC_V":0x3112,                 # ㄐㄑㄒ
 "KC_5":0x3113,"KC_T":0x3114,"KC_G":0x3115,"KC_B":0x3116,   # ㄓㄔㄕㄖ
 "KC_Y":0x3117,"KC_H":0x3118,"KC_N":0x3119,                 # ㄗㄘㄙ
 "KC_U":0x3127,"KC_J":0x3128,"KC_M":0x3129,                 # ㄧㄨㄩ
 "KC_8":0x311A,"KC_I":0x311B,"KC_K":0x311C,"KC_COMMA":0x311D, # ㄚㄛㄜㄝ
 "KC_9":0x311E,"KC_O":0x311F,"KC_L":0x3120,"KC_DOT":0x3121, # ㄞㄟㄠㄡ
 "KC_0":0x3122,"KC_P":0x3123,"KC_SEMICOLON":0x3124,"KC_SLASH":0x3125, # ㄢㄣㄤㄥ
 "KC_MINUS":0x3126,                                          # ㄦ
 "KC_3":0x02C7,"KC_4":0x02CB,"KC_6":0x02CA,"KC_7":0x02D9,   # tones ˇ ˋ ˊ ˙
}
zcol={}
for k,cp in ZH_CP.items():
    assert cp in CM["zh"] or 0x2c6<=cp<=0x2dd, hex(cp)
    if 0x3105<=cp<=0x312f:
        add(f"ZHUYIN_{cp:04X}",cp); tok=("str",f"ZHUYIN_{cp:04X}")
    else:                                   # tone mark (rendered via _LetterMod_)
        add(f"ZHUYIN_{cp:04X}",cp); tok=("str",f"ZHUYIN_{cp:04X}")
    zcol[ROW[k]]=[tok,None,tok,None]        # CAPS=base, shift unused
zcol.update(EL_SETTINGS); cols["zh-TW"]=zcol
fonts.append({"lang":"zh-TW","cat":"bopomofo","src":"noto-sans-tc","range":[0x3105,0x312F]})

# ---- id-ID Indonesian: plain US QWERTY (no overrides) ----------------------
cols["id-ID"]={}        # empty -> all keys fall back to en-US; flag + host fold only

json.dump(cols,open("/tmp/alpha_cols.json","w"),indent=1)
json.dump(named,open("/tmp/alpha_named.json","w"),indent=1)
json.dump(fonts,open("/tmp/alpha_fonts.json","w"),indent=1)
print("langs:",list(cols))
for ln,c in cols.items():
    print(f"  {ln}: {len([r for r in c if r<56 and any(x for x in c[r])])} mapped keys")
print(f"named additions: {len(named)} ({named[0] if named else '-'} .. {named[-1] if named else '-'})")
for f_ in fonts: print(f"  font {f_['lang']}: cat={f_['cat']} range {hex(f_['range'][0])}-{hex(f_['range'][1])}")
