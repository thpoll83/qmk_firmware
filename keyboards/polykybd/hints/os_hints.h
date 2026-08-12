// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "poly_os.h"

#include <stdint.h>

// The OS-aware shortcut-hint display list for `keycode` under the modifiers in
// `mods_raw`, or NULL when the chord has no hint.
//
// Pure — it reads no global state, which is what makes the table unit-testable.
//
//   mods_raw           poly_layer_t.mods: the raw QMK modifier bits, BOTH sides.
//                      Left/right are collapsed internally (bit0 Ctrl, bit1 Shift,
//                      bit2 Alt, bit3 GUI), and matches are on the EXACT collapsed
//                      set, so extra held modifiers disqualify a chord rather than
//                      leaking a subset match (Win+Ctrl+Shift+X must not show the
//                      Win+Ctrl+X hint).
//   active_os_packed   poly_sync_t.active_os, still carrying POLY_OS_AUTO_FLAG.
//                      Masked internally so a caller cannot forget to.
//
// Note there is deliberately no led_t parameter: keycode_to_disp_overlay() carried
// one for years and the body never read it.
const uint32_t* os_hint_for_keycode(uint16_t keycode, uint8_t mods_raw, uint8_t active_os_packed);
