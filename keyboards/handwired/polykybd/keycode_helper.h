// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "polykybd.h"
#include "quantum/quantum.h"
#include "quantum/quantum_keycodes.h"
#include "keymap_german.h"

#include "base/com.h"
#include "lang/named_glyphs.h"

#include "led.h"
#include "layers.h"

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
    lang_key = lang_key.replace("-", "").upper()
    languages.append(lang_key)
    if lang_index==0:
        cog.outl(f"#define INIT_LANG LANG_{lang_key}")
    lang_index = lang_index + 1
    lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value
]]]*/
#define INIT_LANG LANG_ENUS
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
      for idx in range(10):
          cog.out(f"KC_LAT{idx}, ")
    ]]]*/
    KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3, KC_LAT4, KC_LAT5, KC_LAT6, KC_LAT7, KC_LAT8, KC_LAT9, 
    //[[[end]]]
    /*[[[cog
        for lang in languages:
            if lang == "ENUS":
                cog.out(f"KCL_ENUS = QK_USER_0, ")
            else:
                cog.out(f"KCL_{lang}, ")
    ]]]*/
    KCL_ENUS = QK_USER_0, KCL_DEDE, KCL_FRFR, KCL_ESES, KCL_PTPT, KCL_ITIT, KCL_TRTR, KCL_KOKR, KCL_JAJP, KCL_ARSA, KCL_ELGR, KCL_UKUA, KCL_RURU, KCL_BEBY, KCL_KKKZ, KCL_BGBG, KCL_PLPL, KCL_RORO, KCL_ZHCN, KCL_NLNL, KCL_HEIL, KCL_SVSE, KCL_FIFI, KCL_NNNO, KCL_DADK, KCL_HUHU, KCL_CSCZ, KCL_HRHR, KCL_SKSK, KCL_LTLT, KCL_LVLV, KCL_ETEE, KCL_PTBR, KCL_SRRS, KCL_MKMK, KCL_FAIR, KCL_HIIN, KCL_MRIN, KCL_NENP, KCL_MNMN, 
    //[[[end]]]
        //Lables, no functionality:
    LBL_TEXT,

    // ── Emoji category layer keycodes ────────────────────────────────────────
    // Category tab keys: KC_EMJ_CAT_BASE + n  (n = 0 .. EMJ_NUM_CATEGORIES-1)
    KC_EMJ_CAT_BASE,
    // Page navigation (follow sequentially after CAT_BASE + 12 slots)
    KC_EMJ_PAGE_PREV = KC_EMJ_CAT_BASE + 12,
    KC_EMJ_PAGE_NEXT,
    // Emoji slot keys: KC_EMJ_SLOT_BASE + n  (n = 0 .. EMJ_SLOTS_PER_PAGE-1)
    KC_EMJ_SLOT_BASE,
    // Sentinel — must stay <= QK_USER_MAX (0x7FFF)
    KC_EMJ_END = KC_EMJ_SLOT_BASE + 50,
};
static_assert((int)KC_LAT9 <= (int)QK_KB_31, "Too many custom QK key codes");
static_assert((int)KC_LAT9 < (int)KCL_ENUS, "Overlap detected");
static_assert((int)KC_EMJ_END <= 0x7FFF, "Emoji keycodes exceed QK_USER_MAX");

// Convenience macros for the emoji category layer keymap entries.
#define KC_EMJ_CAT(n)  ((uint16_t)((uint16_t)KC_EMJ_CAT_BASE  + (uint16_t)(n)))
#define ESLOT(n)       ((uint16_t)((uint16_t)KC_EMJ_SLOT_BASE  + (uint16_t)(n)))

const uint32_t* keycode_to_static_text(uint16_t keycode, led_t state, uint8_t state_flags);
