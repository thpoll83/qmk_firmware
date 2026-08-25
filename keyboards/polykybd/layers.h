// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

enum kb_layers {
    _BL = 0x00,
    _L0 = _BL,
    _L1,
    _L2,
    _L3,
    _L4,
    // ONE function layer. The F-keys are resolved from the ACTIVE BASE LAYOUT at
    // runtime (KC_FKEY -> the F-key matching the digit that key types), so a second
    // hardcoded arrangement is no longer needed.
    _FL,
    // ⚠️ TOMBSTONE - do NOT reuse or delete. This was _FL1. Layers 0..8 are the
    // host-remappable dynamic keymap stored in EEPROM and 9..12 are served from
    // flash (see DYNAMIC_KEYMAP_LAYER_COUNT in <variant>/config.h), so removing an
    // index shifts _SL into the dynamic range AND invalidates every stored user
    // keymap. Keeping the slot costs nothing but a name.
    _FL_UNUSED,
    _NL,
    _UL,
    _SL,
    _LL,
    _ADDLANG1,
    _EMJ };
