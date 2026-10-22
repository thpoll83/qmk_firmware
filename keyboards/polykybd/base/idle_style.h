// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The idle STYLES (HID cmd 28): what the keycaps do once the idle timeout has
// elapsed. Each style's behaviour is a row in one table (idle_style.c) instead of
// `if (style == ...)` branches spread through poly_keymap.c, so adding a style is
// one row plus its renderer, and the decisions below are unit-tested
// (make test:polykybd_idle_style). Header-only enum, pure table: no quantum.h.
//
// The ⚠️ notes on split42 and EDEN, and on POLY_DEFAULT_IDLE_STYLE, are in state.h
// beside the default.

// Values are append-only (persisted + on the wire in poly_sync_t.idle_style).
enum poly_idle_style {
    IDLE_STYLE_PULSE  = 0,
    IDLE_STYLE_JITTER = 1,
    IDLE_STYLE_IDDQD  = 2, // doom attract-demo screensaver (host: IdleStyle.IDDQD)
    IDLE_STYLE_EDEN   = 3, // looping "Eden" boot animation screensaver (host: IdleStyle.EDEN)
    IDLE_STYLE_COUNT
};

// Eden idle screensaver runs DIM (anti-burn-in + it's a sleeping-keyboard ambience,
// not a legend you need to read). This is the OLED contrast register value, not a
// brightness level — a small number is a faint glow.
#define EDEN_IDLE_BRIGHTNESS 4

// How a style takes over the keycaps when the board goes idle.
typedef enum {
    // Blank, set DISP_IDLE + IDLE_TRANSITION, then pulse the contrast while idle.
    IDLE_ENTER_PULSE = 0,
    // Try the Doom attract demo at the user brightness, without DISP_IDLE. When it
    // cannot start (no doom build, a flash in progress) fall back to the pulse.
    IDLE_ENTER_DOOM,
    // Set DISP_IDLE and hold steady_contrast; the style's own renderer owns the
    // keycaps (Eden's loop in eden_idle_tick()).
    IDLE_ENTER_STEADY,
} idle_enter_t;

typedef struct {
    const char  *name; // console log name
    idle_enter_t enter;
    uint8_t      steady_contrast; // IDLE_ENTER_STEADY only: contrast held while idle
    bool         jitter;          // pulse relocates each legend while it is dark
    bool         owns_keycaps;    // kdisp_idle() stands down: the style draws itself
    bool         in_key_cycle;    // KC_IDLE_STYLE may land on it
} idle_style_desc_t;

// The descriptor for a style. An unknown value (a newer host, a corrupt byte) gets
// the PULSE row, which is what the idle path has always done with one.
const idle_style_desc_t *idle_style_desc(uint8_t style);

// The style KC_IDLE_STYLE moves to from `style`: the next one in enum order whose
// in_key_cycle is set. IDDQD is not in the cycle (it is the doom easter egg, armed
// by typing IDDQD), but a board already on it still cycles out of it.
uint8_t idle_style_next_in_cycle(uint8_t style);
