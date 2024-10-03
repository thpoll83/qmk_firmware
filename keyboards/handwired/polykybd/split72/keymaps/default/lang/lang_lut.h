//File content partially generated!
//Execute the following command to do so (via cogapp):
//cog -r lang_lut.h
#pragma once
#include "named_glyphs.h"

enum {ALPHA = 26, NUM = 10, ADDITIONAL = 18};
enum variation_index{VAR_SMALL = 0, VAR_SHIFT, VAR_CAPS, VAR_ALTGR};
enum lang_layer {
    /*[[[cog
    import cog
    import os
    from openpyxl import load_workbook
    wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang_lut.xlsx"), data_only=True)
    sheet = wb['key_lut']

    lang_index = 0
    lang_key = sheet["B1"].value
    while lang_key:
        lang_key = lang_key.replace("-", "").upper()
        if lang_index==0:
            cog.outl(f"\tLANG_{lang_key} = 0,")
        else:
            cog.outl(f"\tLANG_{lang_key},")
        lang_index = lang_index + 1
        lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value
    ]]]*/
    LANG_ENUS = 0,
    LANG_DEDE,
    LANG_FRFR,
    LANG_ESES,
    LANG_PTPT,
    LANG_ITIT,
    LANG_TRTR,
    LANG_KOKR,
    LANG_JAJP,
    LANG_ARSA,
    LANG_ELGR,
    LANG_UKUA,
    LANG_RURU,
    LANG_BEBY,
    LANG_KKKZ,
    LANG_BGBG,
    LANG_PLPL,
    LANG_RORO,
    LANG_ZHCN,
    LANG_NLNL,
    LANG_HEIL,
    LANG_SVSE,
    LANG_FIFI,
    LANG_NNNO,
    LANG_DADK,
    LANG_HUHU,
    LANG_CSCZ,
    //[[[end]]]
    NUM_LANG };

const uint16_t* translate_keycode_only_shift(uint8_t used_lang, uint16_t keycode);

const uint16_t* translate_keycode_only_altgr(uint8_t used_lang, uint16_t keycode);

const uint16_t* translate_keycode(uint8_t used_lang, uint16_t keycode, bool shift, bool caps_lock);

enum settings_keys {
    /*[[[cog
    #skip all letters
    key_index = 2
    key_name = sheet["A2"].value
    glyph_code = sheet["D2"].value
    while not key_name.startswith("{"):
        key_index = key_index + 1
        key_name = sheet.cell(row = key_index, column = 1).value

    #collect all settings keys
    all_keys = []
    last_key = key_name
    while key_name:
        all_keys.append("SETTING_" + key_name.strip('{}').replace('.','_').upper())
        key_index = key_index + 1
        last_key = key_name
        key_name = sheet.cell(row = key_index, column = 1).value
    cog.outl(',\n'.join(str_key + ( " = 0" if str_key==all_keys[0] else "") for str_key in all_keys) + ',')
    cog.outl("SETTINGS_NUM")
    ]]]*/
    SETTING_LETTER_HOFFSET = 0,
    SETTING_LETTER_VOFFSET,
    SETTING_NUM_HOFFSET,
    SETTING_NUM_VOFFSET,
    SETTING_SYM_HOFFSET,
    SETTING_SYM_VOFFSET,
    SETTINGS_NUM
    //[[[end]]]
};

enum settings_constants {
    HIDE_KEY = -128
};

int8_t get_setting(uint8_t setting, uint8_t lang, uint8_t variation);

