// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

//File content partially generated!
//Execute the following command to do so (via cogapp):
//cog -r lang_lut.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "named_glyphs.h"

// Pack a 4-char language code ("enUS") into one big-endian uint32 — the form
// decode_lang() answers with and the HID change-language command compares
// against. These live HERE because they are the lang-code encoding, not a HID
// concern: they used to sit in hid_com.h, which forced this cog-generated
// module to reach sideways into the HID layer for one macro (the include graph's
// only upward edge out of lang/).
#define LANG_TO_UI32(a,b,c,d) (((uint32_t)(a))<<24 | ((uint32_t)(b))<<16 | ((uint32_t)(c))<<8 | (d))
#define LANG_TO_UI32_ARR(arr) (((uint32_t)(arr[0]))<<24 | ((uint32_t)(arr[1]))<<16 | ((uint32_t)(arr[2]))<<8 | (arr[3]))

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
    LANG_HRHR,
    LANG_SKSK,
    LANG_LTLT,
    LANG_LVLV,
    LANG_ETEE,
    LANG_PTBR,
    LANG_SRRS,
    LANG_MKMK,
    LANG_FAIR,
    LANG_HIIN,
    LANG_MRIN,
    LANG_NENP,
    LANG_MNMN,
    LANG_URPK,
    LANG_ENGB,
    LANG_ESMX,
    LANG_DECH,
    LANG_FRBE,
    LANG_FRCA,
    LANG_THTH,
    LANG_BNIN,
    LANG_TEIN,
    LANG_TAIN,
    LANG_ZHTW,
    LANG_KAGE,
    LANG_HYAM,
    LANG_IDID,
    LANG_AZAZ,
    LANG_ISIS,
    LANG_VIVN,
    LANG_ZHHK,
    LANG_ENAU,
    LANG_ENNZ,
    LANG_MINZ,
    LANG_SMWS,
    LANG_FJFJ,
    LANG_TLPH,
    LANG_HWUS,
    LANG_ENZA,
    LANG_AFZA,
    LANG_AREG,
    LANG_SWKE,
    LANG_AMET,
    LANG_YONG,
    LANG_ENNG,
    LANG_ARMA,
    LANG_ARIQ,
    LANG_KUIQ,
    LANG_MSMY,
    LANG_UZUZ,
    LANG_ENCA,
    LANG_ESAR,
    LANG_ENPG,
    LANG_TYPF,
    LANG_ESCO,
    LANG_ESPE,
    LANG_ESVE,
    LANG_ESCL,
    LANG_ESEC,
    LANG_ESGT,
    LANG_ESDO,
    LANG_ESBO,
    LANG_ESPY,
    LANG_ESCR,
    LANG_ESSV,
    LANG_ESHN,
    LANG_ESPA,
    LANG_ESUY,
    LANG_ESNI,
    LANG_DEAT,
    LANG_NLBE,
    LANG_CAES,
    LANG_ENIE,
    LANG_BSBA,
    LANG_FRCH,
    LANG_SLSI,
    LANG_FOFO,
    LANG_ARAE,
    LANG_ARSY,
    LANG_ARJO,
    LANG_ARLB,
    LANG_ARYE,
    LANG_ARKW,
    LANG_AROM,
    LANG_ARPS,
    LANG_ARQA,
    LANG_ARBH,
    LANG_ARDZ,
    LANG_ARSD,
    LANG_ARTN,
    LANG_ARLY,
    LANG_FRCD,
    LANG_FRCI,
    LANG_FRCM,
    LANG_FRSN,
    LANG_FRMG,
    LANG_ENGH,
    LANG_ENUG,
    LANG_ENZM,
    LANG_SWTZ,
    LANG_PTAO,
    LANG_PTMZ,
    LANG_BNBD,
    LANG_ENIN,
    LANG_ENPK,
    LANG_ENPH,
    LANG_ENSG,
    LANG_ENLK,
    LANG_KYKG,
    LANG_TGTJ,
    LANG_ENGU,
    LANG_ENSB,
    LANG_ENVU,
    LANG_ENFM,
    LANG_FRNC,
    LANG_TOTO,
    LANG_EUES,
    LANG_GLES,
    LANG_RMCH,
    LANG_CYGB,
    LANG_GAIE,
    LANG_MTMT,
    LANG_LBLU,
    LANG_SENO,
    LANG_GNPY,
    LANG_QUPE,
    LANG_AYBO,
    LANG_NVUS,
    LANG_NHMX,
    LANG_PSAF,
    LANG_IUCA,
    LANG_CRCA,
    LANG_CKUS,
    //[[[end]]]
    NUM_LANG };

const uint32_t* translate_keycode_only_shift(uint8_t used_lang, uint16_t keycode);

const uint32_t* translate_keycode_only_altgr(uint8_t used_lang, uint16_t keycode);

const uint32_t* translate_keycode(uint8_t used_lang, uint16_t keycode, bool shift, bool caps_lock);

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
    SETTING_LETTER_ALTGRHALF,
    SETTING_NUM_ALTGRHALF,
    SETTING_SYM_ALTGRHALF,
    SETTING_LETTER_HELDHOFFSET,
    SETTING_LETTER_HELDVOFFSET,
    SETTING_NUM_HELDHOFFSET,
    SETTING_NUM_HELDVOFFSET,
    SETTING_SYM_HELDHOFFSET,
    SETTING_SYM_HELDVOFFSET,
    SETTINGS_NUM
    //[[[end]]]
};

enum settings_constants {
    HIDE_KEY = -128
};

int8_t get_setting(uint8_t setting, uint8_t lang, uint8_t variation);

uint32_t decode_lang(uint8_t index);

uint8_t translate_a_to_z(uint8_t keycode);

