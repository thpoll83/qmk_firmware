// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "polymod_os_actions.h"

#ifdef OS_ACTIONS_UNIT_TEST
// The standalone googletest harness has no quantum.h; the shim declares the five
// mod/tap primitives (mocked by the suite) and the keycode/modifier constants
// the table below spells its chords with.
#    include "os_actions_test_shim.h"
#else
#    include "quantum.h"
#endif

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

// Rows follow enum polymod_os_action; columns follow enum polymod_os_action_os:
// [UNKNOWN, WINDOWS, MACOS, LINUX, ANDROID, IOS]. UNKNOWN mirrors the
// Windows/Linux Ctrl convention so an unresolved OS still does the sensible thing.
// macOS/iOS share the Cmd(GUI)-based editing chords; system actions that don't
// exist on the mobile OSes (lock/screenshot/launcher/emoji) are NA there.
static const os_key_t os_action_table[][OSA_OS_COUNT] = {
    // UNKNOWN          WINDOWS          MACOS              LINUX            ANDROID          IOS
    [OSA_COPY]       = {{CTL,KC_C},   {CTL,KC_C},     {GUI,KC_C},       {CTL,KC_C},      {CTL,KC_C},      {GUI,KC_C}},
    [OSA_CUT]        = {{CTL,KC_X},   {CTL,KC_X},     {GUI,KC_X},       {CTL,KC_X},      {CTL,KC_X},      {GUI,KC_X}},
    [OSA_PASTE]      = {{CTL,KC_V},   {CTL,KC_V},     {GUI,KC_V},       {CTL,KC_V},      {CTL,KC_V},      {GUI,KC_V}},
    [OSA_UNDO]       = {{CTL,KC_Z},   {CTL,KC_Z},     {GUI,KC_Z},       {CTL,KC_Z},      {CTL,KC_Z},      {GUI,KC_Z}},
    [OSA_REDO]       = {{CTL,KC_Y},   {CTL,KC_Y},     {GUI|SFT,KC_Z},   {CTL|SFT,KC_Z},  {CTL,KC_Y},      {GUI|SFT,KC_Z}},
    [OSA_SELALL]     = {{CTL,KC_A},   {CTL,KC_A},     {GUI,KC_A},       {CTL,KC_A},      {CTL,KC_A},      {GUI,KC_A}},
    [OSA_FIND]       = {{CTL,KC_F},   {CTL,KC_F},     {GUI,KC_F},       {CTL,KC_F},      {CTL,KC_F},      {GUI,KC_F}},
    // Lock screen: Win+L / Ctrl+Cmd+Q (macOS) / Super+L (GNOME default).
    [OSA_LOCK]       = {{GUI,KC_L},   {GUI,KC_L},     {CTL|GUI,KC_Q},   {GUI,KC_L},      NA,              NA},
    // Region screenshot: Win+Shift+S / Cmd+Shift+4 / Shift+PrtSc (GNOME area).
    [OSA_SCRSHOT]    = {{GUI|SFT,KC_S},{GUI|SFT,KC_S},{GUI|SFT,KC_4},   {SFT,KC_PSCR},   NA,              NA},
    // Launcher / search: tap Win (Start) / Cmd+Space (Spotlight) / tap Super.
    [OSA_SEARCH]     = {{0,KC_LGUI},  {0,KC_LGUI},    {GUI,KC_SPACE},   {0,KC_LGUI},     NA,              NA},
    // App switcher: Alt+Tab / Cmd+Tab.
    [OSA_APP_SWITCH] = {{ALT,KC_TAB}, {ALT,KC_TAB},   {GUI,KC_TAB},     {ALT,KC_TAB},    {ALT,KC_TAB},    {GUI,KC_TAB}},
    // Window switcher (within app on macOS = Cmd+`): Win+Tab (Task View) elsewhere.
    [OSA_WIN_SWITCH] = {{GUI,KC_TAB}, {GUI,KC_TAB},   {GUI,KC_GRV},     {ALT,KC_GRV},    NA,              NA},
    // Emoji picker: Win+. / Ctrl+Cmd+Space / Ctrl+. (GTK).
    [OSA_EMOJI]      = {{GUI,KC_DOT}, {GUI,KC_DOT},   {CTL|GUI,KC_SPACE},{CTL,KC_DOT},   NA,              NA},
    // Word-wise navigation: Ctrl+arrow / Option(Alt)+arrow on macOS/iOS.
    [OSA_WORD_LEFT]  = {{CTL,KC_LEFT},{CTL,KC_LEFT},  {ALT,KC_LEFT},    {CTL,KC_LEFT},   {CTL,KC_LEFT},   {ALT,KC_LEFT}},
    [OSA_WORD_RIGHT] = {{CTL,KC_RIGHT},{CTL,KC_RIGHT},{ALT,KC_RIGHT},   {CTL,KC_RIGHT},  {CTL,KC_RIGHT},  {ALT,KC_RIGHT}},
    // Line start/end: Home/End / Cmd+arrow on macOS/iOS.
    [OSA_LINE_HOME]  = {{0,KC_HOME},  {0,KC_HOME},    {GUI,KC_LEFT},    {0,KC_HOME},     {0,KC_HOME},     {GUI,KC_LEFT}},
    [OSA_LINE_END]   = {{0,KC_END},   {0,KC_END},     {GUI,KC_RIGHT},   {0,KC_END},      {0,KC_END},      {GUI,KC_RIGHT}},
};

#define OS_ACTION_TABLE_ROWS (sizeof(os_action_table) / sizeof(os_action_table[0]))

// Keep the table in lockstep with the row enum.
_Static_assert(OS_ACTION_TABLE_ROWS == OSA_ACTION_COUNT,
               "os_action_table rows must match enum polymod_os_action");

uint16_t os_action_count(void) {
    return (uint16_t)OS_ACTION_TABLE_ROWS;
}

void emit_os_action(uint16_t action_idx, uint8_t os) {
    if (action_idx >= OS_ACTION_TABLE_ROWS) {
        return;
    }
    if (os >= OSA_OS_COUNT) {
        os = OSA_OS_UNKNOWN;
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
