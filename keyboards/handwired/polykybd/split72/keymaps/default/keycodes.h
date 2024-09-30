#pragma once

#include QMK_KEYBOARD_H
#include "polykybd.h"
#include "quantum/quantum_keycodes.h"

#include "base/com.h"
#include "lang/named_glyphs.h"

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
    languages.append(lang_key)
    if lang_index==0:
        cog.outl(f"#define INIT_LANG {lang_key}")
    lang_index = lang_index + 1
    lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value
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
      for idx in range(10):
          cog.out(f"KC_LAT{idx}, ")
    ]]]*/
    KC_LAT0, KC_LAT1, KC_LAT2, KC_LAT3, KC_LAT4, KC_LAT5, KC_LAT6, KC_LAT7, KC_LAT8, KC_LAT9, 
    //[[[end]]]
    /*[[[cog
        for lang in languages:
            if lang == "LANG_EN":
                cog.out(f"KC_LANG_EN = QK_USER_0, ")
            else:
                cog.out(f"KC_{lang}, ")
    ]]]*/
    KC_LANG_EN = QK_USER_0, KC_LANG_DE, KC_LANG_FR, KC_LANG_ES, KC_LANG_PT, KC_LANG_IT, KC_LANG_TR, KC_LANG_KO, KC_LANG_JA, KC_LANG_AR, KC_LANG_GR, KC_LANG_UA, KC_LANG_RU, KC_LANG_BE, KC_LANG_KZ, KC_LANG_BG, KC_LANG_PL, KC_LANG_RO, KC_LANG_ZH, KC_LANG_NL, KC_LANG_HE, 
    //[[[end]]]
        //Lables, no functionality:
    LBL_TEXT
};
static_assert((int)KC_LAT9 <= (int)QK_KB_31, "Too many custom QK key codes");
static_assert((int)KC_LAT9 < (int)KC_LANG_EN, "Overlap detected");
static_assert((int)LBL_TEXT <= (int)QK_USER_31, "Too many user custom key codes");

const uint16_t* keycode_to_static_text(uint16_t keycode, led_t state, uint8_t state_flags) {
    switch (keycode) {
        case SC_LCPO:                       return u"(   " CURRENCY_SIGN;
        case SC_RCPC:                       return u")   " CURRENCY_SIGN;
        case SC_LSPO:                       return u"(   " ICON_SHIFT;
        case SC_RSPC:                       return u")   " ICON_SHIFT;
        case SC_LAPO:                       return u"(    <\r     ~";
        case SC_RAPC:                       return u")    <\r     ~";
        case SC_SENT:                       return ARROWS_RETURN u"  " ICON_SHIFT;
        case TO(_EMJ0):                     return u" " PRIVATE_EMOJI_1F600 u"\v" ICON_LAYER;
        case TO(_EMJ1):                     return u" " PRIVATE_EMOJI_1F440 u"\v" ICON_LAYER;
        case KC_DEADKEY:                    return (state_flags & DEAD_KEY_ON_WAKEUP) == 0 ? u"WakeX\r\v" ICON_SWITCH_OFF : u"WakeX\r\v" ICON_SWITCH_ON;
        case LBL_TEXT:                      return u"Text:";
        case KC_TOGMODS:                    return (state_flags & MODS_AS_TEXT) == 0 ? u"Mods\r\v" ICON_SWITCH_OFF : u"Mods\r\v" ICON_SWITCH_ON;
        case KC_TOGTEXT:                    return (state_flags & MORE_TEXT) == 0 ? u"Cmds\r\v" ICON_SWITCH_OFF : u"Cmds\r\v" ICON_SWITCH_ON;
        case QK_LEAD:                       return u"Lead";
        case KC_HYPR:                       return (state_flags & MORE_TEXT) != 0 ? u"Hypr" : u" " PRIVATE_HYPER;
        case KC_MEH:                        return (state_flags & MORE_TEXT) != 0 ? u"Meh" : u" " PRIVATE_MEH;
        case KC_EXEC:                       return u"Exec";
        case KC_NUM_LOCK:                   return !state.num_lock ? u"Nm" ICON_NUMLOCK_OFF : u"Nm" ICON_NUMLOCK_ON;
        case KC_KP_SLASH:                   return u"/";
        case KC_KP_ASTERISK:
        case KC_KP_MINUS:                   return u"-";
        case KC_KP_7:                       return !state.num_lock ? ARROWS_LEFTSTOP : u"7";
        case KC_KP_8:                       return !state.num_lock ? u"   " ICON_UP : u"8";
        case KC_KP_9:                       return !state.num_lock ? ARROWS_UPSTOP : u"9";
        case KC_KP_PLUS:                    return u"+";
        case KC_KP_4:                       return !state.num_lock ? u"  " ICON_LEFT : u"4";
        case KC_KP_5:                       return !state.num_lock ? u"" : u"5";
        case KC_KP_6:                       return !state.num_lock ? u"  " ICON_RIGHT : u"6";
        case KC_KP_EQUAL:                   return u"=";
        case KC_KP_1:                       return !state.num_lock ? ARROWS_RIGHTSTOP : u"1";
        case KC_KP_2:                       return !state.num_lock ? u"   " ICON_DOWN : u"2";
        case KC_KP_3:                       return !state.num_lock ? ARROWS_DOWNSTOP : u"3";
        case KC_CALCULATOR:                 return u"   " PRIVATE_CALC;
        case KC_KP_0:                       return !state.num_lock ? u"Ins" : u"0";
        case KC_KP_DOT:                     return !state.num_lock ? u"Del" : u".";
        case KC_KP_ENTER:                   return u"Enter";
        case QK_BOOTLOADER:                 return u"Boot";
        case QK_DEBUG_TOGGLE:               return ((state_flags & DBG_ON) == 0) ? u"Dbg\r\v" ICON_SWITCH_OFF : u"Dbg\r\v" ICON_SWITCH_ON;
        case RGB_RMOD:
        case RM_PREV:                       return u" " ICON_LEFT PRIVATE_LIGHT;
        case KC_RGB_TOG:                    return u"  I/O";
        case RGB_MOD:
        case RM_NEXT:                       return PRIVATE_LIGHT ICON_RIGHT;
        case RGB_HUI:
        case RM_HUEU:                       return u"Hue+";
        case RGB_HUD:
        case RM_HUED:                       return u"Hue-";
        case RGB_SAI:
        case RM_SATU:                       return u"Sat+";
        case RGB_SAD:
        case RM_SATD:                       return u"Sat-";
        case RGB_VAI:
        case RM_VALU:                       return u"Bri+";
        case RGB_VAD:
        case RM_VALD:                       return u"Bri-";
        case RGB_SPI:
        case RM_SPDU:                       return u"Spd+";
        case RGB_SPD:
        case RM_SPDD:                       return u"Spd-";
        case RGB_MODE_PLAIN:                return u"Plan";
        case RGB_MODE_BREATHE:              return u"Brth";
        case RGB_MODE_SWIRL:                return u"Swrl";
        case RGB_MODE_RAINBOW:              return u"Rnbw";
        case KC_MEDIA_NEXT_TRACK:           return ICON_RIGHT ICON_RIGHT;
        case KC_MEDIA_PLAY_PAUSE:           return u"  " ICON_RIGHT;
        case KC_MEDIA_STOP:                 return u"Stop";
        case KC_MEDIA_PREV_TRACK:           return ICON_LEFT ICON_LEFT;
        case KC_MS_ACCEL0:                  return u">>";
        case KC_MS_ACCEL1:                  return u">>>";
        case KC_MS_ACCEL2:                  return u">>>>";
        case KC_BTN1:                       return u"  " ICON_LMB;
        case KC_BTN2:                       return u"  " ICON_RMB;
        case KC_BTN3:                       return u"  " ICON_MMB;
        case KC_MS_UP:                      return u"  " ICON_UP;
        case KC_MS_DOWN:                    return u"  " ICON_DOWN;
        case KC_MS_LEFT:                    return u"  " ICON_LEFT;
        case KC_MS_RIGHT:                   return u"  " ICON_RIGHT;
        case KC_AUDIO_MUTE:                 return u"  " PRIVATE_MUTE;
        case KC_AUDIO_VOL_DOWN:             return u"  " PRIVATE_VOL_DOWN;
        case KC_AUDIO_VOL_UP:               return u"  " PRIVATE_VOL_UP;
        case KC_PRINT_SCREEN:               return u"  " PRIVATE_IMAGE;
        case KC_SCROLL_LOCK:                return u"ScLk";
        case KC_PAUSE:                      return u"Paus";
        case KC_INSERT:                     return u"Ins";
        case KC_HOME:                       return ARROWS_LEFTSTOP;
        case KC_END:                        return u"   " ARROWS_RIGHTSTOP;
        case KC_PAGE_UP:                    return u"  " ARROWS_UPSTOP;
        case KC_PAGE_DOWN:                  return u"  " ARROWS_DOWNSTOP;
        case KC_DELETE:                     return (state_flags & MORE_TEXT) != 0 ? u"Del" : TECHNICAL_ERASERIGHT;
        case KC_MYCM:                       return u"  " PRIVATE_PC;
        case TO(_SL):                       return PRIVATE_SETTINGS u"\v" ICON_LAYER;
        case MO(_FL0):
        case MO(_FL1):                      return u"Fn\r\v\t" ICON_LAYER;
        case TO(_NL):                       return u"Nm\r\v\t" ICON_LAYER;
        case MO(_NL):                       return u"Nm!\r\v\t" ICON_LAYER;
        case KC_BASE:                       return u"Base\r\v\t" ICON_LAYER;
        case OSL(_UL):                      return u"Util*\r\v\t" ICON_LAYER;
        case TO(_UL):                       return u"Util\r\v\t" ICON_LAYER;
        case MO(_ADDLANG1):                 return u"Intl";
        case KC_F1:                         return u" F1";
        case KC_F2:                         return u" F2";
        case KC_F3:                         return u" F3";
        case KC_F4:                         return u" F4";
        case KC_F5:                         return u" F5";
        case KC_F6:                         return u" F6";
        case KC_F7:                         return u" F7";
        case KC_F8:                         return u" F8";
        case KC_F9:                         return u" F9";
        case KC_F10:                        return u" F10";
        case KC_F11:                        return u" F11";
        case KC_F12:                        return u" F12";
        case KC_F13:                        return u" F13";
        case KC_F14:                        return u" F14";
        case KC_F15:                        return u" F15";
        case KC_F16:                        return u" F16";
        case KC_F17:                        return u" F17";
        case KC_F18:                        return u" F18";
        case KC_F19:                        return u" F19";
        case KC_F20:                        return u" F20";
        case KC_F21:                        return u" F21";
        case KC_F22:                        return u" F22";
        case KC_F23:                        return u" F23";
        case KC_F24:                        return u" F24";
        case KC_LEFT_CTRL:
        case KC_RIGHT_CTRL:                 return (state_flags & MODS_AS_TEXT) != 0 ? u"Ctrl" : TECHNICAL_CONTROL;
        case KC_LEFT_ALT:                   return (state_flags & MODS_AS_TEXT) != 0 ? u"Alt" : TECHNICAL_OPTION;
        case KC_RIGHT_ALT:                  return (state_flags & MODS_AS_TEXT) != 0 ? u"RAlt" : TECHNICAL_OPTION;
        case KC_LGUI:
        case KC_RGUI:                       return (state_flags & MODS_AS_TEXT) != 0 ? u"GUI" : DINGBAT_BLACK_DIA_X;
        case KC_LEFT:                       return u"  " ICON_LEFT;
        case KC_RIGHT:                      return u"  " ICON_RIGHT;
        case KC_UP:                         return u"  " ICON_UP;
        case KC_DOWN:                       return u"  " ICON_DOWN;
        case KC_CAPS_LOCK:                  return state.caps_lock ? u"Cap" ICON_CAPSLOCK_ON : u"Cap" ICON_CAPSLOCK_OFF;
        case KC_LSFT:
        case KC_RSFT:                       return (state_flags & MODS_AS_TEXT) != 0 ? u"Shft" : u" " ICON_SHIFT;
        case KC_NO:                         return u"";
        case KC_DDIM:                       return u"  " ICON_LEFT;
        case KC_DBRI:                       return u"  " ICON_RIGHT;
        case KC_D1Q:                        return u"  " PRIVATE_DISP_DARKER;
        case KC_D3Q:                        return u"  " PRIVATE_DISP_BRIGHTER;
        case KC_DHLF:                       return u"  " PRIVATE_DISP_HALF;
        case KC_DMIN:                       return u"  " PRIVATE_DISP_DARK;
        case KC_DMAX:                       return u"  " PRIVATE_DISP_BRIGHT;
        case KC_LANG:                       return (state_flags & MORE_TEXT) != 0 ? u"Lang" : PRIVATE_WORLD;
        case SH_TOGG:                       return u"SwpH";
        case QK_MAKE:                       return u"Make";
        case EE_CLR:                        return u"ClrEE";
        case QK_REBOOT:                     return u" " ARROWS_CIRCLE;
        case KC_LNG1:                       return u"Han/Y";
        case KC_APP:                        return u" Ctx";
        case DE_GRV:                        return u"`"; //neo layout
        case KC_CUT:                        return u"Cut";
        case KC_COPY:                       return u"Copy";
        case KC_PASTE:                      return u"Paste";
        case KC_UNDO:                       return u"Undo";
        case KC_AGAIN:                      return u"Redo";
        case KC_FIND:                       return u"Find";
        case KC_SELECT:                     return u"Word\r\v   sel";
        case KC_EXSEL:                      return u"Line\r\v    sel";
        case KC_OPER:                       return u"Line\r\v    join";
        case KC_CRSEL:                      return u"Line\r\v    del";

        //The following entries will over-rule language specific entries in the follow language lookup table,
        //however with this we can control them by flags and so far those where not language specific anyway.
        case KC_ENTER:                      return (state_flags & MORE_TEXT) != 0 ? u"Enter" : ARROWS_RETURN;
        case KC_ESCAPE:	                    return (state_flags & MORE_TEXT) != 0 ? u"Esc" : TECHNICAL_ESCAPE;
        case KC_BACKSPACE:                  return (state_flags & MORE_TEXT) != 0 ? u"Bksp" : TECHNICAL_ERASELEFT;
        case KC_TAB:                        return (state_flags & MORE_TEXT) != 0 ? u"Tab" : ARROWS_TAB;
        case KC_SPACE:                      return (state_flags & MORE_TEXT) != 0 ? u"Space" : ICON_SPACE;

        default: return NULL;
    }
}
