// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "keycode_helper.h"
#include "state.h"

// The active OS (synced into poly_sync_t.active_os), used to pick OS-aware legends.
static inline uint8_t kc_active_os(void) {
    return get_local_state()->active_os & POLY_OS_VALUE_MASK;
}
// macOS and iOS use the Command(⌘)/Option(⌥) glyphs; the others use ❖/⎇.
static inline bool kc_os_is_apple(void) {
    uint8_t o = kc_active_os();
    return o == POLY_OS_MACOS || o == POLY_OS_IOS;
}
// GUI/Super key icon per active OS — a distinct resident glyph each:
// Windows 4-pane, Linux 🐧, Android head, ❖ when nothing is detected. macOS/iOS
// are handled by the Apple modifier swap below (the GUI key shows ⌥), so they
// never reach this; kept here as a safe fallback (⌘) if the swap is ever bypassed.
static inline const uint32_t* kc_os_gui_icon(void) {
    switch (kc_active_os()) {
        case POLY_OS_WINDOWS: return ICON_OS_WINDOWS;
        case POLY_OS_MACOS:   return TECHNICAL_COMMAND;
        case POLY_OS_LINUX:   return ICON_OS_LINUX;
        case POLY_OS_ANDROID: return ICON_OS_ANDROID;
        case POLY_OS_IOS:     return TECHNICAL_COMMAND;   // iOS collapsed into macOS
        default:              return DINGBAT_BLACK_DIA_X;
    }
}

const uint32_t* keycode_to_static_text(uint16_t keycode, led_t state, uint8_t state_flags) {
    switch (keycode) {
        case SC_LCPO:                       return U"(   " CURRENCY_SIGN;
        case SC_RCPC:                       return U")   " CURRENCY_SIGN;
        case SC_LSPO:                       return U"(   " ICON_SHIFT;
        case SC_RSPC:                       return U")   " ICON_SHIFT;
        case SC_LAPO:                       return U"(    <\r     ~";
        case SC_RAPC:                       return U")    <\r     ~";
        case SC_SENT:                       return ARROWS_RETURN U"  " ICON_SHIFT;
        case TO(_EMJ):                      return U" " PRIVATE_EMOJI_1F600 U"\v" ICON_LAYER;
        case KC_DEADKEY:                    return (state_flags & DEAD_KEY_ON_WAKEUP) == 0 ? U"WakeX\r\v" ICON_SWITCH_OFF : U"WakeX\r\v" ICON_SWITCH_ON;
        case LBL_TEXT:                      return U"Text:";
        case KC_TOGMODS:                    return (state_flags & MODS_AS_TEXT) == 0 ? U"Mods\r\v" ICON_SWITCH_OFF : U"Mods\r\v" ICON_SWITCH_ON;
        case KC_TOGTEXT:                    return (state_flags & MORE_TEXT) == 0 ? U"Cmds\r\v" ICON_SWITCH_OFF : U"Cmds\r\v" ICON_SWITCH_ON;
        case QK_LEAD:                       return U"Lead";
        case KC_HYPR:                       return (state_flags & MORE_TEXT) != 0 ? U"Hypr" : U" " PRIVATE_HYPER;
        case KC_MEH:                        return (state_flags & MORE_TEXT) != 0 ? U"Meh" : U" " PRIVATE_MEH;
        case KC_EXEC:                       return U"Exec";
        case KC_NUM_LOCK:                   return !state.num_lock ? U"Nm" ICON_NUMLOCK_OFF : U"Nm" ICON_NUMLOCK_ON;
        case KC_KP_SLASH:                   return U"/";
        case KC_KP_ASTERISK:                return U"*";
        case KC_KP_MINUS:                   return U"-";
        case KC_KP_7:                       return !state.num_lock ? ARROWS_LEFTSTOP : U"7";
        case KC_KP_8:                       return !state.num_lock ? U"   " ICON_UP : U"8";
        case KC_KP_9:                       return !state.num_lock ? ARROWS_UPSTOP : U"9";
        case KC_KP_PLUS:                    return U"+";
        case KC_KP_4:                       return !state.num_lock ? U"  " ICON_LEFT : U"4";
        case KC_KP_5:                       return !state.num_lock ? U"" : U"5";
        case KC_KP_6:                       return !state.num_lock ? U"  " ICON_RIGHT : U"6";
        case KC_KP_EQUAL:                   return U"=";
        case KC_KP_1:                       return !state.num_lock ? ARROWS_RIGHTSTOP : U"1";
        case KC_KP_2:                       return !state.num_lock ? U"   " ICON_DOWN : U"2";
        case KC_KP_3:                       return !state.num_lock ? ARROWS_DOWNSTOP : U"3";
        case KC_CALCULATOR:                 return U"   " PRIVATE_CALC;
        case KC_KP_0:                       return !state.num_lock ? U"Ins" : U"0";
        case KC_KP_DOT:                     return !state.num_lock ? U"Del" : U".";
        case KC_KP_ENTER:                   return U"Enter";
        case QK_BOOTLOADER:                 return U"Boot";
        case QK_DEBUG_TOGGLE:               return ((state_flags & DBG_ON) == 0) ? U"Dbg\r\v" ICON_SWITCH_OFF : U"Dbg\r\v" ICON_SWITCH_ON;
        case RM_PREV:                       return U" " ICON_LEFT PRIVATE_LIGHT;
        case KC_RGB_TOG:                    return (state_flags & RGB_ON) == 0 ? U"RGB\r\v" ICON_SWITCH_OFF : U"RGB\r\v" ICON_SWITCH_ON;
        case RM_NEXT:                       return PRIVATE_LIGHT ICON_RIGHT;
        case RM_HUEU:                       return U"Hue+";
        case RM_HUED:                       return U"Hue-";
        case RM_SATU:                       return U"Sat+";
        case RM_SATD:                       return U"Sat-";
        case RM_VALU:                       return U"Bri+";
        case RM_VALD:                       return U"Bri-";
        case RM_SPDU:                       return U"Spd+";
        case RM_SPDD:                       return U"Spd-";
        case RGB_MODE_PLAIN:                return U"Plan";
        case RGB_MODE_BREATHE:              return U"Brth";
        case RGB_MODE_SWIRL:                return U"Swrl";
        case RGB_MODE_RAINBOW:              return U"Rnbw";
        case KC_MEDIA_NEXT_TRACK:           return ICON_RIGHT ICON_RIGHT;
        case KC_MEDIA_PLAY_PAUSE:           return U"  " ICON_RIGHT;
        case KC_MEDIA_STOP:                 return U"Stop";
        case KC_MEDIA_PREV_TRACK:           return ICON_LEFT ICON_LEFT;
        case MS_ACL0:                       return U">>";
        case MS_ACL1:                       return U">>>";
        case MS_ACL2:                       return U">>>>";
        case MS_BTN1:                       return U"  " ICON_LMB;
        case MS_BTN2:                       return U"  " ICON_RMB;
        case MS_BTN3:                       return U"  " ICON_MMB;
        case MS_UP:                         return U"  " ICON_UP;
        case MS_DOWN:                       return U"  " ICON_DOWN;
        case MS_LEFT:                       return U"  " ICON_LEFT;
        case MS_RGHT:                       return U"  " ICON_RIGHT;
        case KC_AUDIO_MUTE:                 return U"  " PRIVATE_MUTE;
        case KC_AUDIO_VOL_DOWN:             return U"  " PRIVATE_VOL_DOWN;
        case KC_AUDIO_VOL_UP:               return U"  " PRIVATE_VOL_UP;
        case KC_PRINT_SCREEN:               return U"  " PRIVATE_IMAGE;
        case KC_SCROLL_LOCK:                return U"ScLk";
        case KC_PAUSE:                      return U"Paus";
        case KC_INSERT:                     return U"Ins";
        case KC_HOME:                       return ARROWS_LEFTSTOP;
        case KC_END:                        return U"   " ARROWS_RIGHTSTOP;
        case KC_PAGE_UP:                    return U"  " ARROWS_UPSTOP;
        case KC_PAGE_DOWN:                  return U"  " ARROWS_DOWNSTOP;
        case KC_DELETE:                     return (state_flags & MORE_TEXT) != 0 ? U"Del" : TECHNICAL_ERASERIGHT;
        case KC_MYCM:                       return U"  " PRIVATE_PC;
        case TO(_SL):                       return PRIVATE_SETTINGS U"\v" ICON_LAYER;
        case MO(_FL0):
        case MO(_FL1):                      return U"Fn\r\v\t" ICON_LAYER;
        case TO(_NL):                       return U"Nm\r\v\t" ICON_LAYER;
        case MO(_NL):                       return U"Nm!\r\v\t" ICON_LAYER;
        case KC_BASE:                       return U"Base\r\v\t" ICON_LAYER;
        case OSL(_UL):                      return U"Util*\r\v\t" ICON_LAYER;
        case TO(_UL):                       return U"Util\r\v\t" ICON_LAYER;
        case MO(_ADDLANG1):                 return U"Intl";
        case KC_F1:                         return U" F1";
        case KC_F2:                         return U" F2";
        case KC_F3:                         return U" F3";
        case KC_F4:                         return U" F4";
        case KC_F5:                         return U" F5";
        case KC_F6:                         return U" F6";
        case KC_F7:                         return U" F7";
        case KC_F8:                         return U" F8";
        case KC_F9:                         return U" F9";
        case KC_F10:                        return U" F10";
        case KC_F11:                        return U" F11";
        case KC_F12:                        return U" F12";
        case KC_F13:                        return U" F13";
        case KC_F14:                        return U" F14";
        case KC_F15:                        return U" F15";
        case KC_F16:                        return U" F16";
        case KC_F17:                        return U" F17";
        case KC_F18:                        return U" F18";
        case KC_F19:                        return U" F19";
        case KC_F20:                        return U" F20";
        case KC_F21:                        return U" F21";
        case KC_F22:                        return U" F22";
        case KC_F23:                        return U" F23";
        case KC_F24:                        return U" F24";
        case KC_LEFT_CTRL:
        case KC_RIGHT_CTRL:                 return (state_flags & MODS_AS_TEXT) != 0 ? U"Ctrl" : (kc_os_is_apple() ? ICON_MAC_CONTROL : TECHNICAL_CONTROL);
        // OS-aware modifier legends. On macOS/iOS the GUI and Alt keys are SWAPPED
        // (both legend and keycode — see process_record) so the physical row matches
        // a Mac's Ctrl–Option–Command order: the GUI key becomes Option ⌥ and the
        // Alt key becomes Command ⌘. Other OSes keep ⎇ Alt and the per-OS GUI icon.
        // All glyphs are resident (Technical2/GuiKey/IconsFont) — render with no pack.
        case KC_LEFT_ALT:                   return (state_flags & MODS_AS_TEXT) != 0 ? (kc_os_is_apple() ? U"Cmd"  : U"Alt")  : (kc_os_is_apple() ? TECHNICAL_COMMAND : TECHNICAL_ALTERNATIVE);
        case KC_RIGHT_ALT:                  return (state_flags & MODS_AS_TEXT) != 0 ? (kc_os_is_apple() ? U"Cmd"  : U"RAlt") : (kc_os_is_apple() ? TECHNICAL_COMMAND : TECHNICAL_ALTERNATIVE);
        case KC_LGUI:
        case KC_RGUI:                       return (state_flags & MODS_AS_TEXT) != 0 ? (kc_os_is_apple() ? U"Opt"  : U"GUI")  : (kc_os_is_apple() ? TECHNICAL_OPTION  : kc_os_gui_icon());
        case KC_LEFT:                       return U"  " ICON_LEFT;
        case KC_RIGHT:                      return U"  " ICON_RIGHT;
        case KC_UP:                         return U"  " ICON_UP;
        case KC_DOWN:                       return U"  " ICON_DOWN;
        case KC_CAPS_LOCK:                  return state.caps_lock ? U"Cap" ICON_CAPSLOCK_ON : U"Cap" ICON_CAPSLOCK_OFF;
        case KC_LSFT:
        case KC_RSFT:                       return (state_flags & MODS_AS_TEXT) != 0 ? U"Shft" : U" " ICON_SHIFT;
        case KC_NO:                         return U"";
        case KC_DDIM:                       return U"  " ICON_LEFT;
        case KC_DBRI:                       return U"  " ICON_RIGHT;
        case KC_D1Q:                        return U"  " PRIVATE_DISP_DARKER;
        case KC_D3Q:                        return U"  " PRIVATE_DISP_BRIGHTER;
        case KC_DHLF:                       return U"  " PRIVATE_DISP_HALF;
        case KC_DMIN:                       return U"  " PRIVATE_DISP_DARK;
        case KC_DMAX:                       return U"  " PRIVATE_DISP_BRIGHT;
        case KC_DAUTO:                      return U" Auto";
        case KC_LANG:                       return (state_flags & MORE_TEXT) != 0 ? U"Lang" : PRIVATE_WORLD;
        case SH_TOGG:                       return U"SwpH";
        case QK_MAKE:                       return U"Make";
        case EE_CLR:                        return U"ClrEE";
        case QK_REBOOT:                     return U" " ARROWS_CIRCLE;
        case KC_LNG1:                       return U"Han/Y";
        case KC_APP:                        return U" Ctx";
        case DE_GRV:                        return U"`"; //neo layout
        case KC_CUT:                        return U"Cut";
        case KC_COPY:                       return U"Copy";
        case KC_PASTE:                      return U"Paste";
        case KC_UNDO:                       return U"Undo";
        case KC_AGAIN:                      return U"Redo";
        case KC_FIND:                       return U"Find";
        case KC_SELECT:                     return U"Word\r\v   sel";
        case KC_EXSEL:                      return U"Line\r\v    sel";
        case KC_OPER:                       return U"Line\r\v    join";
        case KC_CRSEL:                      return U"Line\r\v    del";

        // OS-semantic action keys (KC_OS_*): plain labels for now; an OS-aware
        // legend (⌘ vs ⌃) follows with the modifier-legend swap.
        case KC_OS_COPY:                    return U"Copy";
        case KC_OS_CUT:                     return U"Cut";
        case KC_OS_PASTE:                   return U"Paste";
        case KC_OS_UNDO:                    return U"Undo";
        case KC_OS_REDO:                    return U"Redo";
        case KC_OS_SELALL:                  return U"SelAll";
        case KC_OS_FIND:                    return U"Find";
        case KC_OS_LOCK:                    return U"Lock";
        case KC_OS_SCRSHOT:                 return U"Snip";
        case KC_OS_SEARCH:                  return U"Srch";
        case KC_OS_APP_SWITCH:              return U"App\r\v  sw";
        case KC_OS_WIN_SWITCH:              return U"Win\r\v  sw";
        case KC_OS_EMOJI:                   return U"Emoji";
        case KC_OS_WORD_LEFT:               return U"Word" ICON_LEFT;
        case KC_OS_WORD_RIGHT:              return U"Word" ICON_RIGHT;
        case KC_OS_LINE_HOME:               return U"Line" ICON_LEFT;
        case KC_OS_LINE_END:                return U"Line" ICON_RIGHT;
        // OS status / control: top line = active OS, bottom = auto vs manual pin.
        case KC_OS_ICON: {
            const bool autom = (get_local_state()->active_os & POLY_OS_AUTO_FLAG) != 0;
            switch (get_local_state()->active_os & POLY_OS_VALUE_MASK) {
                case POLY_OS_WINDOWS: return autom ? U"Win\r\v auto" : U"Win\r\v  pin";
                case POLY_OS_MACOS:   return autom ? U"Mac\r\v auto" : U"Mac\r\v  pin";
                case POLY_OS_LINUX:   return autom ? U"Lnx\r\v auto" : U"Lnx\r\v  pin";
                case POLY_OS_ANDROID: return autom ? U"And\r\v auto" : U"And\r\v  pin";
                case POLY_OS_IOS:     return autom ? U"iOS\r\v auto" : U"iOS\r\v  pin";
                default:              return autom ? U"OS?\r\v auto" : U"OS?\r\v  pin";
            }
        }
        // OS selection keys: name on top, a toggle glyph below marking the active
        // choice (radio-style — exactly one is ON). AUTO is ON in auto mode; each
        // pin key is ON when that OS is the active pinned one.
        case KC_OS_SET_AUTO ... KC_OS_SET_END - 1: {
            const uint8_t sel   = (uint8_t)(keycode - KC_OS_SET_BASE);
            const bool    autom = (get_local_state()->active_os & POLY_OS_AUTO_FLAG) != 0;
            const uint8_t cur   = get_local_state()->active_os & POLY_OS_VALUE_MASK;
            const bool    on    = (sel == POLY_OS_UNKNOWN) ? autom : (!autom && sel == cur);
            switch (sel) {
                case POLY_OS_UNKNOWN: return on ? U"Auto\r\v" ICON_SWITCH_ON : U"Auto\r\v" ICON_SWITCH_OFF;
                case POLY_OS_WINDOWS: return on ? U"Win\r\v"  ICON_SWITCH_ON : U"Win\r\v"  ICON_SWITCH_OFF;
                case POLY_OS_MACOS:   return on ? U"Mac\r\v"  ICON_SWITCH_ON : U"Mac\r\v"  ICON_SWITCH_OFF;
                case POLY_OS_LINUX:   return on ? U"Lnx\r\v"  ICON_SWITCH_ON : U"Lnx\r\v"  ICON_SWITCH_OFF;
                case POLY_OS_ANDROID: return on ? U"And\r\v"  ICON_SWITCH_ON : U"And\r\v"  ICON_SWITCH_OFF;
                default:              return U"";   // iOS collapsed into macOS — not a pinnable selection
            }
        }

        //The following entries will over-rule language specific entries in the follow language lookup table,
        //however with this we can control them by flags and so far those where not language specific anyway.
        case KC_ENTER:                      return (state_flags & MORE_TEXT) != 0 ? U"Enter" : ARROWS_RETURN;
        case KC_ESCAPE:                     return (state_flags & MORE_TEXT) != 0 ? U"Esc" : TECHNICAL_ESCAPE;
        case KC_BACKSPACE:                  return (state_flags & MORE_TEXT) != 0 ? U"Bksp" : TECHNICAL_ERASELEFT;
        case KC_TAB:                        return (state_flags & MORE_TEXT) != 0 ? U"Tab" : ARROWS_TAB;
        case KC_SPACE:                      return (state_flags & MORE_TEXT) != 0 ? U"Space" : ICON_SPACE;

        default: return NULL;
    }
}
