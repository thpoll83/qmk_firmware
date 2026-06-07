#!/usr/bin/env python3
"""Generate key_lut columns + named-glyph + fonts.yaml additions for the batch:
az-AZ (Azerbaijani Latin), is-IS (Icelandic), vi-VN (Vietnamese TCVN/AltGr).

az/is are Latin clones (only keys differing from en-US are filled). vi-VN puts
the precomposed Vietnamese letters (ă â ê ô đ ư ơ ₫) on the number/bracket row and
the FIVE tone marks (combining grave/hook/tilde/acute/dot-below) on keys 5-9 -
those combining marks are emitted as dotted-circle composites (fontconvert -C)
just like the Indic/Thai matras.

Outputs: /tmp/b3_cols.json, /tmp/b3_named.json, /tmp/b3_fonts.json
"""
import json
from fontTools.ttLib import TTFont
F="/home/user/qmk_firmware/keyboards/handwired/polykybd/fonts/"
NS=set(TTFont(F+"noto-sans/NotoSans-Regular.ttf").getBestCmap().keys())

ROW={}; L="ABCDEFGHIJKLMNOPQRSTUVWXYZ"
for i,c in enumerate(L): ROW["KC_"+c]=2+i
for i in range(1,10): ROW[f"KC_{i}"]=27+i
ROW["KC_0"]=37
ROW.update({"KC_MINUS":43,"KC_EQUAL":44,"KC_LBRC":45,"KC_RBRC":46,
            "KC_BACKSLASH":47,"KC_NONUS_HASH":48,"KC_SEMICOLON":49,
            "KC_QUOTE":50,"KC_GRAVE":51,"KC_COMMA":52,"KC_DOT":53,
            "KC_SLASH":54,"KC_NONUS_BACKSLASH":55})

# keysym -> cell token.  Single ascii letters/digits handled directly.
TOK={
 'quotedbl':'QUOTE','semicolon':';','colon':':','question':'?','backslash':'BACKSLASH',
 'slash':'/','period':'.','comma':'COMMA','percent':'%','asciicircum':'^','ampersand':'&',
 'asterisk':'*','parenleft':'(','minus':'-','underscore':'_','plus':'+','apostrophe':"'",
 'asciitilde':'~','grave':'GRAVE_ACCENT',
 'udiaeresis':'UMLAUT_U_SMALL','Udiaeresis':'UMLAUT_U',
 'odiaeresis':'UMLAUT_O_SMALL','Odiaeresis':'UMLAUT_O',
 'ccedilla':'C_WITH_CEDILLA_SMALL','Ccedilla':'C_WITH_CEDILLA',
 'scedilla':'S_WITH_CEDILLA_SMALL','Scedilla':'S_WITH_CEDILLA',
 'gbreve':'G_WITH_BREVE_SMALL','Gbreve':'G_WITH_BREVE',
 'idotless':'I_DOTLESS_SMALL','Iabovedot':'I_WITH_DOT',
 'schwa':'LATIN_0259','SCHWA':'LATIN_018F','numerosign':'NUMERO_SIGN',
 'eth':'LATIN_00F0','ETH':'LATIN_00D0','thorn':'LATIN_00FE','THORN':'LATIN_00DE',
 'ae':'LETTER_AE_SMALL','AE':'LATIN_00C6',
 'dead_abovering':'MOD_RING','dead_diaeresis':'DIAERESIS','dead_acute':'ACUTE_ACCENT',
 'abreve':'LATIN_0103','Abreve':'LATIN_0102','acircumflex':'LATIN_00E2','Acircumflex':'LATIN_00C2',
 'ecircumflex':'LATIN_00EA','Ecircumflex':'LATIN_00CA','ocircumflex':'LATIN_00F4','Ocircumflex':'LATIN_00D4',
 'dstroke':'LATIN_0111','Dstroke':'LATIN_0110',
 'uhorn':'LATIN_01B0','Uhorn':'LATIN_01AF','ohorn':'LATIN_01A1','Ohorn':'LATIN_01A0',
 'DongSign':'DONG_SIGN',
}
# keysyms (and tokens) that are letters -> CAPS=NULL (capslock uppercases)
LETTER_KS={'udiaeresis','Udiaeresis','odiaeresis','Odiaeresis','ccedilla','Ccedilla',
 'scedilla','Scedilla','gbreve','Gbreve','idotless','Iabovedot','schwa','SCHWA','eth','ETH',
 'thorn','THORN','ae','AE','abreve','Abreve','acircumflex','Acircumflex','ecircumflex',
 'Ecircumflex','ocircumflex','Ocircumflex','dstroke','Dstroke','uhorn','Uhorn','ohorn','Ohorn','i','I'}

named=[]; seen=set()
# names already defined in the workbook -> never re-emit (would duplicate #define)
import re as _re
EXIST=set(_re.findall(r'#define (\w+)\s', open(F+"../lang/named_glyphs.h").read()))
def add_named(name,cp):
    if name in EXIST or name in seen: return
    named.append([name,f"{cp:04X}"]); seen.add(name)
# new base glyphs we introduce
for nm,cp in [("LATIN_0259",0x259),("LATIN_018F",0x18F),
              ("LATIN_01A0",0x1A0),("LATIN_01A1",0x1A1),("LATIN_01AF",0x1AF),("LATIN_01B0",0x1B0)]:
    pass  # added lazily when referenced

def t(ks):
    """keysym -> ('str',token) | None ; registers new base glyphs as needed"""
    if ks is None: return None
    if len(ks)==1 and ks.isdigit(): return ("str","ZERO") if ks=='0' else ("num",int(ks))
    if len(ks)==1 and ks.isalpha(): return ("str",ks)
    tok=TOK.get(ks)
    if tok is None: return None
    if tok.startswith("LATIN_"):
        cp=int(tok.split("_")[1],16); add_named(tok,cp)
    return ("str",tok)

def is_letter(ks): return ks in LETTER_KS or (ks is not None and len(ks)==1 and ks.isalpha())

# fr-FR-style Latin settings block
SET={56:[None,["str","HIDE"],None,["num",50]],57:[None,["str","HIDE"],None,["num",13]],
     58:[None,["num",28],None,["num",50]],59:[None,["num",0],None,["num",13]],
     60:[None,["num",28],None,["num",50]],61:[None,["num",0],None,["num",13]]}

cols={}; fonts=[]

def build_latin(name, tbl):
    col={}
    for k,(b,s,a) in tbl.items():
        cb,cs,ca=t(b),t(s),t(a)
        cc=None if is_letter(b) else cb       # letters uppercase via capslock; else lock base
        col[ROW[k]]=[cb,cs,cc,ca]
    col.update({r:v for r,v in SET.items()})
    cols[name]=col

# ---- az-AZ (xkb az, US base) ----------------------------------------------
AZ={"KC_2":("2","quotedbl",None),"KC_3":("3","numerosign",None),
 "KC_4":("4","semicolon",None),"KC_6":("6","colon",None),"KC_7":("7","question",None),
 "KC_W":("udiaeresis","Udiaeresis",None),"KC_I":("i","Iabovedot",None),
 "KC_LBRC":("odiaeresis","Odiaeresis",None),"KC_RBRC":("gbreve","Gbreve",None),
 "KC_BACKSLASH":("backslash","slash",None),"KC_NONUS_HASH":("backslash","slash",None),
 "KC_SEMICOLON":("idotless","I",None),"KC_QUOTE":("schwa","SCHWA",None),
 "KC_COMMA":("ccedilla","Ccedilla",None),"KC_DOT":("scedilla","Scedilla",None),
 "KC_SLASH":("period","comma",None)}
build_latin("az-AZ",AZ)
fonts.append({"kind":"range","cat":"latin","variant":"_Schwa_","range":[0x259,0x259],
              "note":"Latin small letter schwa (U+0259) for Azerbaijani az-AZ - not in the existing LatinExtB range (0x180-0x24F)."})

# ---- is-IS (xkb is, US base) ----------------------------------------------
IS={"KC_GRAVE":("dead_abovering","dead_diaeresis",None),"KC_2":("2","quotedbl",None),
 "KC_MINUS":("odiaeresis","Odiaeresis",None),"KC_EQUAL":("minus","underscore",None),
 "KC_LBRC":("eth","ETH",None),"KC_RBRC":("apostrophe","question",None),
 "KC_SEMICOLON":("ae","AE",None),"KC_QUOTE":("dead_acute","dead_acute",None),
 "KC_BACKSLASH":("plus","asterisk",None),"KC_NONUS_HASH":("plus","asterisk",None),
 "KC_SLASH":("thorn","THORN",None)}
build_latin("is-IS",IS)

# ---- vi-VN (xkb vn TCVN, US base) -----------------------------------------
VIET_DC={0x300:None,0x301:None,0x303:None,0x309:None,0x323:None}   # marks used
def tone(cp):
    VIET_DC[cp]=True; return ("str",f"VIET_DC_{cp:04X}")
VN={
 "KC_1":(t("abreve"),t("Abreve"),None,False),"KC_2":(t("acircumflex"),t("Acircumflex"),None,False),
 "KC_3":(t("ecircumflex"),t("Ecircumflex"),None,False),"KC_4":(t("ocircumflex"),t("Ocircumflex"),None,False),
 "KC_5":(tone(0x300),t("percent"),None,True),"KC_6":(tone(0x309),("str","^"),None,True),
 "KC_7":(tone(0x303),t("ampersand"),None,True),"KC_8":(tone(0x301),t("asterisk"),None,True),
 "KC_9":(tone(0x323),t("parenleft"),None,True),"KC_0":(t("dstroke"),t("Dstroke"),None,False),
 "KC_EQUAL":(t("DongSign"),t("plus"),None,True),
 "KC_LBRC":(t("uhorn"),t("Uhorn"),None,False),"KC_RBRC":(t("ohorn"),t("Ohorn"),None,False),
}
vcol={}
for k,(cb,cs,_,sym) in VN.items():
    cc=cb if sym else None        # symbol/tone keys: lock base under capslock; letters: NULL
    vcol[ROW[k]]=[cb,cs,cc,None]
vcol.update({r:v for r,v in SET.items()})
cols["vi-VN"]=vcol
# composite font + PUA named glyphs for the tones (sorted)
seq=[];
for j,cp in enumerate(sorted(VIET_DC)):
    add_named(f"VIET_DC_{cp:04X}", 0xE1A0+j); seq.append(f"25CC {cp:04X}")
fonts.append({"kind":"composite","cat":"vietnamese","variant":"_VietTones_","src":"noto-sans",
              "pua":0xE1A0,"seq":", ".join(seq),
              "note":"Vietnamese combining tone marks (grave/hook/tilde/acute/dot-below) on a dotted circle U+25CC, PUA 0xE1A0.., addressed via VIET_DC_* by vi-VN."})

json.dump(cols,open("/tmp/b3_cols.json","w"),indent=1)
json.dump(named,open("/tmp/b3_named.json","w"),indent=1)
json.dump(fonts,open("/tmp/b3_fonts.json","w"),indent=1)
print("langs:",list(cols))
for ln,c in cols.items(): print(f"  {ln}: {len([r for r in c if r<56 and any(x for x in c[r])])} keys")
print("named additions:",len(named),named)
for f_ in fonts: print("  font:",f_)
