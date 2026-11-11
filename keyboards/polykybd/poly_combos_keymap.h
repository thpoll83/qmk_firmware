// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Included by each variant's keymap.c. QMK compiles keymap.c inside
// quantum/keymap_introspection.c, and that file needs key_combos[] and
// tap_dance_actions[] to exist when COMBO_ENABLE / TAP_DANCE_ENABLE are on. The
// tables QMK actually reads come from poly_combos.c through the combo_get() /
// tap_dance_get() hooks, so these one-entry arrays are never used.

#pragma once

#ifdef POLY_RUNTIME_COMBOS
const uint16_t PROGMEM poly_combo_stub_keys[] = {COMBO_END};
combo_t key_combos[] = {COMBO(poly_combo_stub_keys, KC_NO)};
tap_dance_action_t tap_dance_actions[] = {{.fn = {NULL, NULL, NULL, NULL}, .user_data = NULL}};
#endif
