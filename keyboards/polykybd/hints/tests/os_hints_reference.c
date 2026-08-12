// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

// GOLDEN REFERENCE: the OS shortcut-hint table exactly as it stood in
// poly_keymap.c on the base branch, before it was extracted into
// hints/os_hints.c. Copied verbatim from
//   git show origin/PolyKybd:keyboards/polykybd/poly_keymap.c
// with only the two global-state reads turned into the parameters the extracted
// version takes (asserted at generation time — see tests/README.md).
//
// It exists so os_hints_tests.cpp can prove the extraction was behaviour-
// preserving by EXHAUSTION rather than by sampling: the two implementations are
// compiled into one binary and compared over every (keycode, mods, os) triple.
//
// Do NOT "fix" or reformat this file, and do not let it drift toward the live
// table — the moment it stops being the old code it stops being evidence. When a
// future change intentionally alters a hint, that is the point at which this file
// and the exhaustive test are retired or regenerated deliberately.

#include "poly_os.h"

#include "quantum/quantum_keycodes.h"
#include "quantum/modifiers.h"

#include "lang/named_glyphs.h"

#include <stddef.h>
#include <stdint.h>

const uint32_t* os_hint_reference(uint16_t keycode, uint8_t mods_raw, uint8_t active_os_packed);

const uint32_t* os_hint_reference(uint16_t keycode, uint8_t mods_raw, uint8_t active_os_packed) {
    switch (keycode)
    {
        case KC_F2: return U"      " PRIVATE_NOTE;
        case KC_F5: return U"     " ARROWS_CIRCLE;
        default: break;
    }

    uint8_t local_mods = mods_raw;
    // OS-aware shortcut-preview icons. The "editing" shortcuts (copy/paste/undo/…)
    // hang off the OS's primary command modifier: Cmd (GUI) on macOS, Ctrl
    // everywhere else — so on a Mac these show under Cmd, not Ctrl (where Ctrl+C
    // does not copy). The "window-management" shortcuts (lock/show-desktop/display/
    // maximize/minimize) hang off the GUI/Super key on Windows & Linux desktops; on
    // macOS Cmd is already the editing modifier (so e.g. Cmd+L is NOT lock and shows
    // nothing here), and on Android the Search key is not a window manager.
    const uint8_t active_os = active_os_packed & POLY_OS_VALUE_MASK;
    const bool apple = (active_os == POLY_OS_MACOS);
    // Collapse left/right modifier sides into one logical set (bit0 Ctrl, bit1 Shift,
    // bit2 Alt, bit3 GUI — the MOD_L* bit values) so every hint below matches the EXACT
    // set of modifiers held. Extra modifiers now disqualify a chord instead of leaking a
    // subset match (Win+Ctrl+Shift+X no longer shows the Win+Ctrl+X hint, Win+Ctrl+C no
    // longer falls through to plain Ctrl+C, etc.). Side (L/R) is intentionally ignored.
    const uint8_t mods_now = (uint8_t)((local_mods | (local_mods >> 4)) & 0x0F);
    if (apple) {
        // macOS: editing lives on Cmd (GUI). Each block is an exact modifier set.
        if (mods_now == (MOD_LGUI | MOD_LCTL)) {
            switch(keycode) {
                case KC_Q: return U"    " PRIVATE_LOCK;       // Ctrl+Cmd+Q = lock screen
                case KC_F: return U"     " PRIVATE_MAXIMIZE;  // Ctrl+Cmd+F = fullscreen
                default: break;
            }
        } else if (mods_now == MOD_LALT) {
            // Word nav on macOS is Option(Alt)+arrows (line nav is Cmd+arrows, below).
            switch(keycode) {
                case KC_LEFT:  return U"    " ICON_WORD_LEFT;
                case KC_RIGHT: return U"    " ICON_WORD_RIGHT;
                default: break;
            }
        } else if (mods_now == (MOD_LGUI | MOD_LSFT)) {
            // Cmd+Shift+Z = redo (mac has no Cmd+Y redo).
            switch(keycode) {
                case KC_Z: return U"      " ARROWS_REDO;
                default: break;
            }
        } else if (mods_now == MOD_LGUI) {
            switch(keycode) {
                case KC_A: return U"      " BOX_WITH_CHECK_MARK;
                case KC_C: return U"     " CLIPBOARD_COPY;
                case KC_F: return U"    " PRIVATE_FIND;
                case KC_X: return U"\t\b\b" CLIPBOARD_CUT;
                case KC_V: return U"     " CLIPBOARD_PASTE;
                case KC_S: return U"\t" PRIVATE_FLOPPY;
                case KC_O: return U"\t" FILE_OPEN;
                case KC_P: return U"\t" PRIVATE_PRINTER;
                case KC_M: return U"     " PRIVATE_WINDOW;    // Cmd+M = minimize
                case KC_Z: return U"      " ARROWS_UNDO;      // Cmd+Z = undo (Cmd+Shift+Z redo above)
                // OS-aware shortcut hints (wave B). Tab uses the narrow ARROWS_TAB
                // base legend, so 4 spaces clear it; Space gets 3.
                case KC_TAB:   return U"    " ICON_APP_SWITCH;    // Cmd+Tab app switcher
                case KC_SPACE: return U"   "  ICON_LAUNCHER;      // Cmd+Space (Spotlight)
                case KC_W:     return U"    " ICON_CLOSE;         // Cmd+W close
                case KC_Q:     return U"    " ICON_CLOSE;         // Cmd+Q quit
                case KC_GRV:   return U"    " ICON_WINDOW_SWITCH; // Cmd+` window switcher
                case KC_LEFT:  return U"    " ARROWS_LEFTSTOP;    // Cmd+Left  line start
                case KC_RIGHT: return U"    " ARROWS_RIGHTSTOP;   // Cmd+Right line end
                default: break;
            }
        }
    } else {
    // Windows / Linux / Android / undetected: editing on Ctrl, window-mgmt on GUI.
    // The two host-detected Linux desktops (GNOME/KDE) behave as Linux here, but a
    // few Super-key hints differ between them — see the Super (GUI) switch below.
    const bool gnome = (active_os == POLY_OS_LINUX_GNOME);
    const bool win_or_unknown = (active_os == POLY_OS_WINDOWS || active_os == POLY_OS_UNKNOWN);
    const bool linux_any = (active_os == POLY_OS_LINUX
                            || active_os == POLY_OS_LINUX_GNOME
                            || active_os == POLY_OS_LINUX_KDE);
    const bool wm = (win_or_unknown || linux_any);   // OSes whose window-mgmt hangs off GUI/Super
    // Windows multi-modifier Super chords (wave D), each on its EXACT modifier set. An
    // unmatched key returns nothing (no fall-through to the Ctrl/Alt editing hints) —
    // Win+Ctrl+C is a different chord from Ctrl+C, so it no longer previews "copy".
    if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LCTL | MOD_LSFT)) {
        switch(keycode) {
            // Win+Ctrl+Shift+B restart graphics: monitor 🖵, then MOVE to the screen
            // cavity and HALF-draw the reload 🗘 into it.
            case KC_B: return U"    " ICON_GFX_RESTART HINT_MOVE(HINT_POS_SCREEN) HINT_HALF ICON_GFX_RELOAD;
            default: break;
        }
    } else if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LCTL)) {
        switch(keycode) {
            // Virtual-desktop chords: a compact monitor glyph (ICON_DESKTOP_SMALL)
            // composed with +/←/→/x so the action reads next to the screen.
            case KC_D:     return U"  " PRIVATE_SCREEN U"+";             // Win+Ctrl+D new virtual desktop
            case KC_LEFT:  return U"  " ICON_LEFT PRIVATE_SCREEN;        // Win+Ctrl+Left  previous desktop
            case KC_RIGHT: return U"  " PRIVATE_SCREEN ICON_RIGHT;       // Win+Ctrl+Right next desktop
            case KC_F4:    return U"  " PRIVATE_SCREEN U"x";             // Win+Ctrl+F4 close desktop
            case KC_F:     return U"    " ICON_NET;                       // Win+Ctrl+F search network computers (🖧 pack glyph)
            case KC_V:     return U"   "  ICON_VOLUME_MIXER;              // Win+Ctrl+V volume mixer (🔊 pack glyph; mixer flyout on Win 11 24H2+)
            case KC_N:     return U"    "  ICON_NARRATOR;                 // Win+Ctrl+N Narrator settings (👂 pack glyph)
            case KC_Q:     return U"   "   ICON_QUICK_ASSIST;             // Win+Ctrl+Q Quick Assist (🤝 pack glyph)
            case KC_S:     return U"   "   ICON_SPEECH_REC;               // Win+Ctrl+S Speech Recognition (🎤 pack glyph)
            default: break;
        }
    } else if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LALT)) {
        switch(keycode) {
            case KC_R: return U"   " ICON_SCREEN_RECORD;       // Win+Alt+R start/stop screen recording
            default: break;
        }
    }
    if (mods_now == (MOD_LCTL | MOD_LSFT)) {
        switch(keycode) {
            case KC_Z: return U"      " ARROWS_REDO;        // Ctrl+Shift+Z redo (Linux/cross-app)
            default: break;
        }
    } else if (mods_now == MOD_LCTL) {
        switch(keycode) {
            case KC_A: return U"      " BOX_WITH_CHECK_MARK;
            case KC_C: return U"     " CLIPBOARD_COPY;
            case KC_D: return U"\t " PRIVATE_DELETE;
            case KC_F: return U"    " PRIVATE_FIND;
            case KC_X: return U"\t\b\b" CLIPBOARD_CUT;
            case KC_V: return U"     " CLIPBOARD_PASTE;
            case KC_S: return U"\t" PRIVATE_FLOPPY;
            case KC_O: return U"\t" FILE_OPEN;
            case KC_P: return U"\t" PRIVATE_PRINTER;
            case KC_Y: return U"      " ARROWS_REDO;         // Ctrl+Y redo (Windows)
            case KC_Z: return U"      " ARROWS_UNDO;         // Ctrl+Z undo (Ctrl+Shift+Z redo above)
            // OS-aware shortcut hints (wave B): word nav + close on Ctrl.
            case KC_LEFT:  return U"    " ICON_WORD_LEFT;   // Ctrl+Left  word left
            case KC_RIGHT: return U"    " ICON_WORD_RIGHT;  // Ctrl+Right word right
            case KC_W:     return U"    " ICON_CLOSE;       // Ctrl+W close
            default: break;
        }
    } else if (mods_now == MOD_LALT) {
        switch(keycode) {
            case KC_TAB: return U"    " ICON_APP_SWITCH;    // Alt+Tab app switcher
            case KC_F4:  return U"    " ICON_CLOSE;         // Alt+F4 close
            default: break;
        }
    } else if (win_or_unknown && mods_now == (MOD_LGUI | MOD_LSFT)) {
        switch(keycode) {
            case KC_S: return U"   " ICON_SNIP;             // Win+Shift+S Snipping Tool (region capture)
            default: break;
        }
    } else if (wm && mods_now == MOD_LGUI) {
        switch(keycode) {
            case KC_D:
                // Show desktop: Win+D and KDE Super+D. GNOME has no default
                // show-desktop chord, so don't show it there.
                if (!gnome) return U"    " PRIVATE_PC;
                break;
            case KC_L:      return U"    " PRIVATE_LOCK;       // Win/Super+L lock
            case KC_P:      return U"    " PRIVATE_SCREEN;     // Win/Super+P display
            case KC_UP:     return U"     " PRIVATE_MAXIMIZE;  // Super+Up maximize
            case KC_DOWN:   return U"     " PRIVATE_WINDOW;    // Super+Down minimize
            // Super+Tab switches: Windows (Task View) and GNOME (switch apps). On
            // KDE / generic Linux the switcher is Alt+Tab (shown via the Alt branch),
            // and Super+Tab is unbound — so don't show it there.
            case KC_TAB:
                if (win_or_unknown || gnome) return U"    " ICON_WINDOW_SWITCH;
                break;
            // Launcher/search on a Super chord is Windows-only (Win+S). GNOME uses the
            // Super overview and KDE a Super-tap / Alt+Space — neither binds Super+S — so
            // show it only on Windows (and the unknown default). Win+Shift+S (Snipping
            // Tool) is handled in its own block above.
            case KC_S:
                if (win_or_unknown) return U"   " ICON_LAUNCHER;
                break;
            // Windows-only Super-chords (wave C). These have no standard GNOME/KDE
            // equivalent, so they are gated on win_or_unknown only. Dictation (Win+H)
            // is Windows-specific: macOS triggers it with a double-tap Fn/Ctrl (not a
            // GUI+letter chord the hint engine can preview), and Linux/Android bind no
            // standard dictation chord.
            // Leading-space counts tuned per glyph (oled_preview) so each wide emoji
            // glyph sits as far right as it fits without clipping the 72 px window —
            // matching the existing hints' placement. M reuses the 5-space minimize
            // legend (= Super+Down); X's narrower glyph takes 4.
            case KC_H:
                if (win_or_unknown) return U"   "   ICON_DICTATION;     // Win+H dictation
                break;
            case KC_I:
                if (win_or_unknown) return U"   "   ICON_SETTINGS;      // Win+I settings (⚙ pack glyph)
                break;
            case KC_M:
                if (win_or_unknown) return U"      " PRIVATE_MINIMIZE;  // Win+M minimize all (🗕)
                break;
            case KC_R:
                // Win+R run dialog: draw the run-dialog FRAME at its top-left, reset the
                // cursor to the origin, then draw the base-font ">_" (4 spaces,
                // right-of-centre) inside it.
                if (win_or_unknown) return HINT_MOVE(HINT_POS_RUNBOX) HINT_FRAME(HINT_SZ_RUNBOX) HINT_RESET U"    >_";
                break;
            case KC_T:
                if (win_or_unknown) return U"   "   ICON_TASK_CYCLE;    // Win+T cycle taskbar
                break;
            case KC_K:
                if (win_or_unknown) return U"   "   ICON_CAST;          // Win+K cast (📶 pack glyph)
                break;
            case KC_V:
                if (win_or_unknown) return U"   "   ICON_CLIP_HISTORY;  // Win+V clipboard history
                break;
            case KC_X:
                if (win_or_unknown) return U"    "  ICON_QUICK_MENU;    // Win+X quick-link menu
                break;
            case KC_COMMA:
                if (win_or_unknown) return U"   "   ICON_PEEK;          // Win+, peek desktop
                break;
            case KC_DOT:
                if (win_or_unknown) return U"   "   PRIVATE_EMOJI_1F600; // Win+. emoji panel
                break;
            // More Windows-only Super-chords (wave D). Leading-space counts tuned per
            // glyph (hint_preview) so each sits as far right as it fits without
            // clipping the 72 px window, matching the existing hints' placement.
            case KC_A:
                if (win_or_unknown) return U"      " ICON_LIGHTNING;    // Win+A Action Center/Quick Settings
                break;
            case KC_E:
                if (win_or_unknown) return U"    "  ICON_EXPLORER;      // Win+E File Explorer (folder pixmap)
                break;
            case KC_U:
                if (win_or_unknown) return U"      " ICON_ACCESSIBILITY;// Win+U Accessibility settings
                break;
            case KC_B:
                if (win_or_unknown) return U"   "   ICON_MAC_CONTROL;   // Win+B focus system tray (⌃ mac-control caret / show-hidden-icons chevron)
                break;
            case KC_HOME:
                if (win_or_unknown) return U"     " ICON_FOCUS_WINDOW;  // Win+Home minimize all but active
                break;
            case KC_LEFT:
                if (win_or_unknown) return U"     " ICON_SNAP_LEFT;     // Win+Left snap window left (⍇ pack glyph)
                break;
            case KC_RIGHT:
                if (win_or_unknown) return U"     " ICON_SNAP_RIGHT;    // Win+Right snap window right (⍈ pack glyph)
                break;
            case KC_SCLN:
                if (win_or_unknown) return U"   "   ICON_GIF;           // Win+; GIF / emoji panel
                break;
            case KC_PAUSE:
                if (win_or_unknown) return U"    " ICON_SLIDERS;        // Win+Pause System Properties (🎛 knobs, pack)
                break;
            case KC_PSCR:
                if (win_or_unknown) return U"   "   ICON_SCREENSHOT;    // Win+PrtScn full-screen screenshot
                break;
            // Magnifier zoom: '+' keys (= and numpad +) zoom in, '-' keys zoom out. Both
            // draw the pack magnifier 🔍, then MOVE the cursor so a plain base-font '+'/'-'
            // lands centred in the lens.
            case KC_EQL:
            case KC_KP_PLUS:
                if (win_or_unknown) return U"   " ICON_MAGNIFIER HINT_MOVE(HINT_POS_ZOOMIN) U"+";  // Win + '+' zoom in
                break;
            case KC_MINS:
            case KC_KP_MINUS:
                if (win_or_unknown) return U"   " ICON_MAGNIFIER HINT_MOVE(HINT_POS_ZOOMOUT) U"-"; // Win + '-' zoom out
                break;
            // Wave E — more Windows-only Super chords.
            case KC_Q:
                if (win_or_unknown) return U"   "   ICON_TEXT_RECOG;   // Win+Q Click to Do — text recognition (🔤 pack glyph)
                break;
            case KC_G:
                if (win_or_unknown) return U"   "   ICON_GAME_BAR;     // Win+G Xbox Game Bar (🎮 pack glyph)
                break;
            case KC_F:
                if (win_or_unknown) return U"   "   ICON_FEEDBACK;     // Win+F Feedback Hub (📣 pack glyph)
                break;
            case KC_C:
                if (win_or_unknown) return U"   "   ICON_COPILOT;      // Win+C Copilot (🤖 pack glyph)
                break;
            default: break;
        }
    }
    }

    if(IS_QK_MOD_TAP(keycode)) {
        uint8_t mods = QK_MOD_TAP_GET_MODS(keycode);
        if((mods & MOD_MASK_CSAG) == MOD_MASK_CSAG) {
            return U"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SAG) == MOD_MASK_SAG) {
            return U"    " ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CAG) == MOD_MASK_CAG) {
            return U"    " CURRENCY_SIGN NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSG) == MOD_MASK_CSG) {
            return U"    " CURRENCY_SIGN ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSA) == MOD_MASK_CSA) {
            return U"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN;
        } else if((mods & MOD_MASK_AG) == MOD_MASK_AG) {
            return U"    " NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SG) == MOD_MASK_SG) {
            return U"    " ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SA) == MOD_MASK_SA) {
            return U"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CG) == MOD_MASK_CG) {
            return U"    " CURRENCY_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CA) == MOD_MASK_CA) {
            return U"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CS) == MOD_MASK_CS) {
            return U"    " CURRENCY_SIGN ICON_SHIFT;
        } else if(mods & MOD_MASK_CTRL) {
            return U"    " CURRENCY_SIGN;
        } else if(mods & MOD_MASK_ALT) {
            return U"    " NOT_SIGN;
        } else if (mods & MOD_MASK_SHIFT) {
            return U"    " ICON_SHIFT;
        } else {
            return U"   " KATAKANA_MIDDLE_DOT;
        }
    }

    return NULL;
}
