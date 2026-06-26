// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "os_actions.h"
#include "quantum.h"
#include "keycode_helper.h"   // KC_OS_* enum
#include "state.h"            // enum poly_os, POLY_OS_COUNT

// One key chord = a left-modifier mask (QMK MOD_L*) + a basic keycode to tap.
// kc == KC_NO means "this action has no binding on this OS" (emit nothing).
typedef struct {
    uint8_t mods;
    uint8_t kc;
} os_key_t;

#define NA   {0, KC_NO}
#define CTL  MOD_LCTL
#define SFT  MOD_LSFT
#define ALT  MOD_LALT
#define GUI  MOD_LGUI

// Rows follow the KC_OS_* enum order (keycode_helper.h); columns follow enum
// poly_os: [UNKNOWN, WINDOWS, MACOS, LINUX, ANDROID, IOS]. UNKNOWN mirrors the
// Windows/Linux Ctrl convention so an unresolved OS still does the sensible thing.
// macOS/iOS share the Cmd(GUI)-based editing chords; system actions that don't
// exist on the mobile OSes (lock/screenshot/launcher/emoji) are NA there.
static const os_key_t os_action_table[][POLY_OS_COUNT] = {
    // UNKNOWN          WINDOWS          MACOS              LINUX            ANDROID          IOS
    [KC_OS_COPY       - KC_OS_ACTION_BASE] = {{CTL,KC_C},   {CTL,KC_C},     {GUI,KC_C},       {CTL,KC_C},      {CTL,KC_C},      {GUI,KC_C}},
    [KC_OS_CUT        - KC_OS_ACTION_BASE] = {{CTL,KC_X},   {CTL,KC_X},     {GUI,KC_X},       {CTL,KC_X},      {CTL,KC_X},      {GUI,KC_X}},
    [KC_OS_PASTE      - KC_OS_ACTION_BASE] = {{CTL,KC_V},   {CTL,KC_V},     {GUI,KC_V},       {CTL,KC_V},      {CTL,KC_V},      {GUI,KC_V}},
    [KC_OS_UNDO       - KC_OS_ACTION_BASE] = {{CTL,KC_Z},   {CTL,KC_Z},     {GUI,KC_Z},       {CTL,KC_Z},      {CTL,KC_Z},      {GUI,KC_Z}},
    [KC_OS_REDO       - KC_OS_ACTION_BASE] = {{CTL,KC_Y},   {CTL,KC_Y},     {GUI|SFT,KC_Z},   {CTL|SFT,KC_Z},  {CTL,KC_Y},      {GUI|SFT,KC_Z}},
    [KC_OS_SELALL     - KC_OS_ACTION_BASE] = {{CTL,KC_A},   {CTL,KC_A},     {GUI,KC_A},       {CTL,KC_A},      {CTL,KC_A},      {GUI,KC_A}},
    [KC_OS_FIND       - KC_OS_ACTION_BASE] = {{CTL,KC_F},   {CTL,KC_F},     {GUI,KC_F},       {CTL,KC_F},      {CTL,KC_F},      {GUI,KC_F}},
    // Lock screen: Win+L / Ctrl+Cmd+Q (macOS) / Super+L (GNOME default).
    [KC_OS_LOCK       - KC_OS_ACTION_BASE] = {{GUI,KC_L},   {GUI,KC_L},     {CTL|GUI,KC_Q},   {GUI,KC_L},      NA,              NA},
    // Region screenshot: Win+Shift+S / Cmd+Shift+4 / Shift+PrtSc (GNOME area).
    [KC_OS_SCRSHOT    - KC_OS_ACTION_BASE] = {{GUI|SFT,KC_S},{GUI|SFT,KC_S},{GUI|SFT,KC_4},   {SFT,KC_PSCR},   NA,              NA},
    // Launcher / search: tap Win (Start) / Cmd+Space (Spotlight) / tap Super.
    [KC_OS_SEARCH     - KC_OS_ACTION_BASE] = {{0,KC_LGUI},  {0,KC_LGUI},    {GUI,KC_SPACE},   {0,KC_LGUI},     NA,              NA},
    // App switcher: Alt+Tab / Cmd+Tab.
    [KC_OS_APP_SWITCH - KC_OS_ACTION_BASE] = {{ALT,KC_TAB}, {ALT,KC_TAB},   {GUI,KC_TAB},     {ALT,KC_TAB},    {ALT,KC_TAB},    {GUI,KC_TAB}},
    // Window switcher (within app on macOS = Cmd+`): Win+Tab (Task View) elsewhere.
    [KC_OS_WIN_SWITCH - KC_OS_ACTION_BASE] = {{GUI,KC_TAB}, {GUI,KC_TAB},   {GUI,KC_GRV},     {ALT,KC_GRV},    NA,              NA},
    // Emoji picker: Win+. / Ctrl+Cmd+Space / Ctrl+. (GTK).
    [KC_OS_EMOJI      - KC_OS_ACTION_BASE] = {{GUI,KC_DOT}, {GUI,KC_DOT},   {CTL|GUI,KC_SPACE},{CTL,KC_DOT},   NA,              NA},
    // Word-wise navigation: Ctrl+arrow / Option(Alt)+arrow on macOS/iOS.
    [KC_OS_WORD_LEFT  - KC_OS_ACTION_BASE] = {{CTL,KC_LEFT},{CTL,KC_LEFT},  {ALT,KC_LEFT},    {CTL,KC_LEFT},   {CTL,KC_LEFT},   {ALT,KC_LEFT}},
    [KC_OS_WORD_RIGHT - KC_OS_ACTION_BASE] = {{CTL,KC_RIGHT},{CTL,KC_RIGHT},{ALT,KC_RIGHT},   {CTL,KC_RIGHT},  {CTL,KC_RIGHT},  {ALT,KC_RIGHT}},
    // Line start/end: Home/End / Cmd+arrow on macOS/iOS.
    [KC_OS_LINE_HOME  - KC_OS_ACTION_BASE] = {{0,KC_HOME},  {0,KC_HOME},    {GUI,KC_LEFT},    {0,KC_HOME},     {0,KC_HOME},     {GUI,KC_LEFT}},
    [KC_OS_LINE_END   - KC_OS_ACTION_BASE] = {{0,KC_END},   {0,KC_END},     {GUI,KC_RIGHT},   {0,KC_END},      {0,KC_END},      {GUI,KC_RIGHT}},
};

#define OS_ACTION_COUNT (sizeof(os_action_table) / sizeof(os_action_table[0]))

// Keep the table in lockstep with the KC_OS_* keycode range.
_Static_assert(OS_ACTION_COUNT == (KC_OS_ACTION_END - KC_OS_ACTION_BASE),
               "os_action_table rows must match the KC_OS_* keycode count");

uint16_t os_action_count(void) {
    return (uint16_t)OS_ACTION_COUNT;
}

void emit_os_action(uint16_t action_idx, uint8_t os) {
    if (action_idx >= OS_ACTION_COUNT) {
        return;
    }
    if (os >= POLY_OS_COUNT) {
        os = POLY_OS_UNKNOWN;
    }
    os_key_t k = os_action_table[action_idx][os];
    if (k.kc == KC_NO) {
        return;   // no binding for this action on this OS
    }
    // Tap the chord without leaking any modifiers the user happens to be holding.
    uint8_t saved = get_mods();
    clear_mods();
    register_mods(k.mods);
    tap_code(k.kc);
    unregister_mods(k.mods);
    set_mods(saved);
}
