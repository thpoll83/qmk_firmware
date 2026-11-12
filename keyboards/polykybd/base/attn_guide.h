// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pure part of the key-attention GUIDE (HID cmd 47 op 2, protocol v34): the focus
// ripple walks the user along a path of layer keys to a target layer, for example
// "hold FN" and then "press the MIDI key". Each step is a key and the layer that key is
// on. The ripple is on the step whose layer is the active one, so holding FN moves it
// to the next key and letting go moves it back. The guide ends when the target layer
// becomes active. No quantum.h, so it is unit-tested on the host
// (make test:polykybd_attn_guide). The firmware binding is anim/key_attention.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ATTN_GUIDE_OP          2u
#define ATTN_GUIDE_MAX_STEPS   4u
// Byte offsets in the report: data[0] report id, data[1] the command.
#define ATTN_GUIDE_IDX_COUNT   3u   // ripples in total, 0 = until done or cancelled
#define ATTN_GUIDE_IDX_PERIOD  4u   // 100 ms units, the same closed range as START
#define ATTN_GUIDE_IDX_TARGET  5u   // the layer that ends the guide
#define ATTN_GUIDE_IDX_NSTEPS  6u   // 1..ATTN_GUIDE_MAX_STEPS
#define ATTN_GUIDE_IDX_STEP0   7u   // then {row, col, layer} per step

// attn_guide_select() results that are not a step index.
#define ATTN_GUIDE_DONE        0xFEu   // the target layer is active
#define ATTN_GUIDE_IDLE        0xFFu   // no step is on the active layer: no ripple

typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t layer;   // the layer this key is on; the step shows while it is active
} attn_guide_step_t;

typedef struct {
    uint8_t           count;
    uint8_t           period;
    uint8_t           target;
    uint8_t           nsteps;
    attn_guide_step_t step[ATTN_GUIDE_MAX_STEPS];
} attn_guide_t;

// Parses op 2. Checks the period range, the step count, every row, col and layer
// against the limits given, that no two steps share a layer and that no step is on
// the target layer (either would make the active step ambiguous). Whether a key has
// a display is the caller's check. Returns false and leaves *out alone on any error.
bool attn_guide_parse(const uint8_t *data, uint8_t length, uint8_t rows, uint8_t cols,
                      uint8_t layers, uint8_t period_min, uint8_t period_max,
                      attn_guide_t *out);

// The step to ring for this active layer (the highest of the layer and default layer
// states), or ATTN_GUIDE_DONE / ATTN_GUIDE_IDLE.
uint8_t attn_guide_select(const attn_guide_t *g, uint8_t active_layer);
