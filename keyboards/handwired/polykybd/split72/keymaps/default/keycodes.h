#pragma once

#include QMK_KEYBOARD_H

/*[[[cog
import cog
import os
from textwrap import wrap
from openpyxl import load_workbook
wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang", "lang_lut.xlsx"))
sheet = wb['key_lut']

languages = []
lang_index = 0
lang_key = sheet["B1"].value
while lang_key:
    languages.append(lang_key)
    if lang_index==0:
        cog.outl(f"#define INIT_LANG {lang_key}")
    lang_index = lang_index + 1
    lang_key = sheet.cell(row = 1, column = 2 + lang_index*3).value
]]]*/
#define INIT_LANG LANG_EN
//[[[end]]]

enum my_keycodes {
    KC_LANG = QK_KB_0,
    KC_DMIN,
    KC_DMAX,
    KC_DDIM,
    KC_DBRI,
    KC_DHLF,
    KC_D1Q,
    KC_D3Q,
    KC_BASE,
    KC_L0,
    KC_L1,
    KC_L2,
    KC_L3,
    KC_L4,
    KC_DEADKEY,
    KC_TOGMODS,
    KC_TOGTEXT,
    KC_RGB_TOG,
    /*[[[cog
        for lang in languages:
            if lang == "LANG_EN":
                cog.out(f"KC_LANG_EN = SAFE_RANGE, ")
            else:
                cog.out(f"KC_{lang}, ")
    ]]]*/
    KC_LANG_EN = SAFE_RANGE, KC_LANG_DE, KC_LANG_FR, KC_LANG_ES, KC_LANG_PT, KC_LANG_IT, KC_LANG_TR, KC_LANG_KO, KC_LANG_JA, KC_LANG_AR, KC_LANG_GR, 
    //[[[end]]]
    /*[[[cog
      for idx in range(10):
          cog.out(f"KC_LAT{idx}, ")
    ]]]*/
    KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3, KC_LAT4, KC_LAT5, KC_LAT6, KC_LAT7, KC_LAT8, KC_LAT9, 
    //[[[end]]]
    //Lables, no functionality:
    LBL_TEXT
};
static_assert((int)KC_RGB_TOG<=(int)QK_KB_31, "Too many custom QK key codes");
static_assert((int)LBL_TEXT<=(int)QK_USER_31, "Too many user custom key codes");
