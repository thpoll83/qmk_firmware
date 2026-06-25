#!/usr/bin/env python3
"""Generate key_lut column values for the 5 Latin regional-variant layouts
(en-GB, es-MX/latam, de-CH, fr-BE, fr-CA) from xkb ground truth.

Output: a JSON dict  { lang_name: { row_number: [c0,c1,c2,c3] } }
where each cell is None | ["num", int] | ["str", token].
Only keys that differ from the en-US base are emitted; everything else is left
NULL so translate_keycode() falls back to English (matching the existing
de-DE/fr-FR/es-ES columns).
"""
import json, sys

# ---- xkb keysym -> xlsx cell token -----------------------------------------
# value forms:  ("num", n)  numeric cell   |   ("str", s)  inline-string cell
# A bare ascii punctuation char is emitted verbatim (cog wraps it in U"...").
LIT = {  # keysym -> literal single char
    'exclam':'!', 'at':'@', 'numbersign':'#', 'dollar':'$', 'percent':'%',
    'ampersand':'&', 'asterisk':'*', 'parenleft':'(', 'parenright':')',
    'minus':'-', 'underscore':'_', 'plus':'+', 'bracketleft':'[',
    'bracketright':']', 'braceleft':'{', 'braceright':'}', 'semicolon':';',
    'colon':':', 'apostrophe':"'", 'less':'<', 'greater':'>', 'slash':'/',
    'question':'?', 'period':'.', 'asciicircum':'^', 'asciitilde':'~',
    'bar':'|', 'dead_circumflex':'^', 'dead_tilde':'~',
}
NAMED = {  # keysym -> named_glyphs token
    'quotedbl':'QUOTE', 'equal':'EQUALS', 'backslash':'BACKSLASH',
    'grave':'GRAVE_ACCENT', 'comma':'COMMA',
    'section':'SECTION', 'degree':'DEGREE', 'notsign':'NOT_SIGN',
    'EuroSign':'EURO_SIGN', 'sterling':'POUND_SIGN', 'cent':'CENT_SIGN',
    'currency':'CURRENCY_SIGN', 'yen':'YEN_SIGN', 'mu':'MICRO_SIGN',
    'periodcentered':'MIDDLE_DOT', 'twosuperior':'SUPER_SCRIPT_2',
    'threesuperior':'SUPER_SCRIPT_3', 'onesuperior':'SUPER_SCRIPT_1',
    'onequarter':'QUATER_SIGN', 'onehalf':'HALF_SIGN',
    'threequarters':'THREE_QUATER_SIGN', 'plusminus':'PLUS_MINUS',
    'brokenbar':'BROKEN_BAR', 'paragraph':'PILCROW',
    'masculine':'MASC_ORDINAL_IND', 'ordfeminine':'FEM_ORDINAL_IND',
    'guillemotleft':'DBL_ANGLE_QMARK_L', 'guillemotright':'DBL_ANGLE_QMARK_R',
    'registered':'REGISTERED_SIGN', 'copyright':'COPYRIGHT_SIGN',
    'multiply':'MUL_SIGN', 'division':'DEVISION_SIGN',
    'exclamdown':'INVERTED_EMARK', 'questiondown':'INVERTED_QMARK',
    # accented letters (lower / upper)
    'adiaeresis':'UMLAUT_A_SMALL','Adiaeresis':'UMLAUT_A',
    'odiaeresis':'UMLAUT_O_SMALL','Odiaeresis':'UMLAUT_O',
    'udiaeresis':'UMLAUT_U_SMALL','Udiaeresis':'UMLAUT_U',
    'ccedilla':'C_WITH_CEDILLA_SMALL','Ccedilla':'C_WITH_CEDILLA',
    'ntilde':'N_WITH_TILDE_SMALL','Ntilde':'N_WITH_TILDE',
    'eacute':'E_WITH_ACUTE_SMALL','Eacute':'LATIN_00C9',
    'egrave':'E_WITH_GRAVE_SMALL','Egrave':'LATIN_00C8',
    'agrave':'A_WITH_GRAVE_SMALL','Agrave':'LATIN_00C0',
    'ugrave':'U_WITH_GRAVE_SMALL','Ugrave':'U_WITH_GRAVE',
    'ae':'LETTER_AE_SMALL','AE':'LATIN_00C6',
    'oe':'LATIN_0153','OE':'LATIN_0152',
    'ssharp':'ESZETT',
    # dead keys -> shown as the bare diacritic
    'dead_acute':'ACUTE_ACCENT','dead_grave':'GRAVE_ACCENT',
    'dead_diaeresis':'DIAERESIS','dead_cedilla':'CEDILLA',
    'dead_caron':'MOD_CARON','dead_breve':'MOD_BREVE','dead_macron':'MOD_MACRON',
    'dead_abovering':'MOD_RING','dead_doubleacute':'MOD_HUNGARUMLAUT',
    'dead_ogonek':'MOD_OGONEK','dead_abovedot':'MOD_DOT_ACCENT',
}

def cell(keysym):
    """xkb keysym -> cell tuple, or None if we don't render it."""
    if keysym is None:
        return None
    if len(keysym) == 1 and keysym.isdigit():
        return ("str", "ZERO") if keysym == '0' else ("num", int(keysym))
    if len(keysym) == 1 and (keysym.isalpha()):
        return ("str", keysym)          # plain latin letter
    if keysym in LIT:
        return ("str", LIT[keysym])
    if keysym in NAMED:
        return ("str", NAMED[keysym])
    return None                          # exotic / unsupported -> skip

def is_letter(keysym):
    return keysym is not None and len(keysym) == 1 and keysym.isalpha()

# ---- QMK keycode row numbers in key_lut ------------------------------------
ROW = {}
letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
for i,c in enumerate(letters): ROW["KC_"+c] = 2+i           # KC_A=2 .. KC_Z=27
for i in range(1,10): ROW[f"KC_{i}"] = 27+i                  # KC_1=28 .. KC_9=36
ROW["KC_0"]=37
ROW.update({"KC_MINUS":43,"KC_EQUAL":44,"KC_LBRC":45,"KC_RBRC":46,
            "KC_BACKSLASH":47,"KC_NONUS_HASH":48,"KC_SEMICOLON":49,
            "KC_QUOTE":50,"KC_GRAVE":51,"KC_COMMA":52,"KC_DOT":53,
            "KC_SLASH":54,"KC_NONUS_BACKSLASH":55})

# ---- per-layout override tables: QMK key -> (base, shift, altgr) ------------
# (only keys that differ from en-US; letters only where physically swapped)
GB = {
 "KC_2":("2","quotedbl",None), "KC_3":("3","sterling",None),
 "KC_4":("4","dollar","EuroSign"),
 "KC_QUOTE":("apostrophe","at",None),
 "KC_GRAVE":("grave","notsign","bar"),
 "KC_NONUS_HASH":("numbersign","asciitilde",None),
 "KC_BACKSLASH":("numbersign","asciitilde",None),
 "KC_NONUS_BACKSLASH":("backslash","bar",None),
}
# Latin-American Spanish (xkb latam) -> es-MX
MX = {
 "KC_GRAVE":("bar","degree","notsign"),
 "KC_1":("1","exclam","bar"),
 "KC_3":("3","numbersign","periodcentered"),
 "KC_4":("4","dollar","asciitilde"),
 "KC_6":("6","ampersand","notsign"),
 "KC_MINUS":("apostrophe","question","backslash"),
 "KC_EQUAL":("questiondown","exclamdown",None),
 "KC_LBRC":("dead_acute","dead_diaeresis",None),
 "KC_RBRC":("plus","asterisk","asciitilde"),
 "KC_SEMICOLON":("ntilde","Ntilde",None),
 "KC_QUOTE":("braceleft","bracketleft","dead_circumflex"),
 "KC_BACKSLASH":("braceright","bracketright","dead_grave"),
 "KC_NONUS_HASH":("braceright","bracketright","dead_grave"),
 "KC_NONUS_BACKSLASH":("less","greater","backslash"),
}
# Swiss German (xkb ch basic) -> de-CH ; QWERTZ (Y/Z swap)
CH = {
 "KC_Y":("z","Z",None), "KC_Z":("y","Y",None),
 "KC_E":("e","E","EuroSign"),
 "KC_GRAVE":("section","degree",None),
 "KC_1":("1","plus","bar"), "KC_2":("2","quotedbl","at"),
 "KC_3":("3","asterisk","numbersign"), "KC_4":("4","ccedilla",None),
 "KC_6":("6","ampersand","notsign"), "KC_7":("7","slash","bar"),
 "KC_8":("8","parenleft","cent"), "KC_9":("9","parenright",None),
 "KC_0":("0","equal",None),
 "KC_MINUS":("apostrophe","question","dead_acute"),
 "KC_EQUAL":("dead_circumflex","dead_grave","dead_tilde"),
 "KC_LBRC":("udiaeresis","egrave","bracketleft"),
 "KC_RBRC":("dead_diaeresis","exclam","bracketright"),
 "KC_SEMICOLON":("odiaeresis","eacute",None),
 "KC_QUOTE":("adiaeresis","agrave","braceleft"),
 "KC_BACKSLASH":("dollar","sterling","braceright"),
 "KC_NONUS_HASH":("dollar","sterling","braceright"),
 "KC_NONUS_BACKSLASH":("less","greater","backslash"),
 "KC_COMMA":("comma","semicolon",None),
 "KC_DOT":("period","colon",None),
 "KC_SLASH":("minus","underscore",None),
}
# Belgian AZERTY (xkb be basic) -> fr-BE
BE = {
 "KC_Q":("a","A",None), "KC_W":("z","Z",None),
 "KC_A":("q","Q",None), "KC_Z":("w","W",None),
 "KC_E":("e","E","EuroSign"),
 "KC_M":("comma","question",None),            # AB07
 "KC_SEMICOLON":("m","M",None),               # AC10
 "KC_GRAVE":("twosuperior","threesuperior","notsign"),
 "KC_1":("ampersand","1","bar"),
 "KC_2":("eacute","2","at"),
 "KC_3":("quotedbl","3","numbersign"),
 "KC_4":("apostrophe","4",None),
 "KC_5":("parenleft","5",None),
 "KC_6":("section","6","asciicircum"),
 "KC_7":("egrave","7","braceleft"),
 "KC_8":("exclam","8","bracketleft"),
 "KC_9":("ccedilla","9","braceleft"),
 "KC_0":("agrave","0","braceright"),
 "KC_MINUS":("parenright","degree","backslash"),
 "KC_EQUAL":("minus","underscore",None),
 "KC_LBRC":("dead_circumflex","dead_diaeresis","bracketleft"),
 "KC_RBRC":("dollar","asterisk","bracketright"),
 "KC_QUOTE":("ugrave","percent","dead_acute"),
 "KC_BACKSLASH":("mu","sterling","dead_grave"),
 "KC_NONUS_HASH":("mu","sterling","dead_grave"),
 "KC_NONUS_BACKSLASH":("less","greater","backslash"),
 "KC_COMMA":("semicolon","period",None),
 "KC_DOT":("colon","slash",None),
 "KC_SLASH":("equal","plus",None),
}
# Canadian French (xkb ca "French (Canada)") -> fr-CA ; QWERTY
CA = {
 "KC_E":("e","E","EuroSign"),
 "KC_GRAVE":("numbersign","bar","backslash"),
 "KC_1":("1","exclam","plusminus"),
 "KC_2":("2","quotedbl","at"),
 "KC_3":("3","slash","sterling"),
 "KC_4":("4","dollar","cent"),
 "KC_5":("5","percent","currency"),
 "KC_6":("6","question","notsign"),
 "KC_7":("7","ampersand","brokenbar"),
 "KC_8":("8","asterisk","twosuperior"),
 "KC_9":("9","parenleft","threesuperior"),
 "KC_0":("0","parenright","onequarter"),
 "KC_MINUS":("minus","underscore","onehalf"),
 "KC_EQUAL":("equal","plus","threequarters"),
 "KC_LBRC":("dead_circumflex","dead_circumflex","bracketleft"),
 "KC_RBRC":("dead_cedilla","dead_diaeresis","bracketright"),
 "KC_SEMICOLON":("semicolon","colon","asciitilde"),
 "KC_QUOTE":("dead_grave","dead_grave","braceleft"),
 "KC_BACKSLASH":("less","greater","braceright"),
 "KC_NONUS_HASH":("less","greater","braceright"),
 "KC_NONUS_BACKSLASH":("guillemotleft","guillemotright","degree"),
}

LAYOUTS = [("en-GB",GB),("es-MX",MX),("de-CH",CH),("fr-BE",BE),("fr-CA",CA)]

# settings block (rows 56-61), fr-FR style, identical for all 5 Latin variants
SETTINGS = {
 56:[None,("str","HIDE"),None,("num",50)],   # letter.hoffset
 57:[None,("str","HIDE"),None,("num",13)],   # letter.voffset
 58:[None,("num",28),None,("num",50)],       # num.hoffset
 59:[None,("num",0),None,("num",13)],        # num.voffset
 60:[None,("num",28),None,("num",50)],       # sym.hoffset
 61:[None,("num",0),None,("num",13)],        # sym.voffset
}

out = {}
for name, tbl in LAYOUTS:
    cols = {}
    for key,(b,s,a) in tbl.items():
        r = ROW[key]
        cb, cs, ca_ = cell(b), cell(s), cell(a)
        # CAPS: letters -> NULL (capslock uppercases); digits/symbols -> base
        if is_letter(b):
            cc = None
        else:
            cc = cb
        cols[r] = [cb, cs, cc, ca_]
    for r,v in SETTINGS.items():
        cols[r] = v
    out[name] = cols

json.dump(out, sys.stdout, indent=1)
