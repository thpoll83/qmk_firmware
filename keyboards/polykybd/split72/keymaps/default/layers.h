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
    _FL0,
    _FL1,
    _NL,
    _UL,
    _SL,
    _LL,
    _ADDLANG1,
    _EMJ,
    // One-shot "Codex-style" macropad demo layer (split72 only). Forced active at
    // boot in keyboard_post_init_user(); the twelve cluster keys carry display-only
    // KC_CDX_* keycodes, every other key is KC_TRNS so the base legends still show.
    _CODEX };
