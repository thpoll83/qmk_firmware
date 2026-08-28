// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "macro_decode.h"

static inline bool is_ascii_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

uint16_t poly_macro_find(poly_macro_read_fn rd, void *ctx, uint8_t id, uint16_t end) {
    uint16_t offset = 0;
    while (id > 0) {
        if (offset >= end) return end;
        if (rd(offset, ctx) == 0) --id;
        ++offset;
    }
    return offset;
}

uint16_t poly_macro_used(poly_macro_read_fn rd, void *ctx, uint16_t end) {
    uint16_t used = 0;
    for (uint16_t offset = 0; offset < end; ++offset) {
        if (rd(offset, ctx) != 0) used = offset + 2; // the byte plus its terminator
    }
    return used > end ? end : used;
}

bool poly_macro_buffer_intact(poly_macro_read_fn rd, void *ctx, uint16_t end) {
    if (end == 0) return false;
    return rd(end - 1, ctx) == 0;
}

poly_macro_step_t poly_macro_decode(poly_macro_read_fn rd, void *ctx, uint16_t cursor, uint16_t end) {
    poly_macro_step_t step = {.kind = POLY_MACRO_STEP_END, .code = 0, .ms = 0, .next = cursor};

    if (cursor >= end) return step;

    uint8_t c = rd(cursor, ctx);
    if (c == 0) return step; // end of this macro

    if (c != POLY_MACRO_PREFIX) {
        step.kind = POLY_MACRO_STEP_CHAR;
        step.code = c;
        step.next = cursor + 1;
        return step;
    }

    // A prefix with nothing after it is a truncated buffer, not a step.
    if (cursor + 1 >= end) return step;
    uint8_t op = rd(cursor + 1, ctx);

    if (op == POLY_MACRO_OP_DELAY) {
        // ASCII digits, terminated by the first non-digit -- which is NOT consumed,
        // exactly as send_string_with_delay_impl re-reads it as the next step. Getting
        // that wrong swallows the byte after every delay.
        uint32_t ms  = 0;
        uint16_t pos = cursor + 2;
        while (pos < end) {
            uint8_t d = rd(pos, ctx);
            if (!is_ascii_digit(d)) break;
            ms = ms * 10 + (uint32_t)(d - '0');
            if (ms > 0xFFFFu) ms = 0xFFFFu; // clamp rather than wrap: a wrapped delay
                                            // reads as "no delay" and the macro races
            ++pos;
        }
        step.kind = POLY_MACRO_STEP_DELAY;
        step.ms   = (uint16_t)ms;
        step.next = pos;
        return step;
    }

    // Every other op takes exactly one keycode byte.
    if (cursor + 2 >= end) return step;
    uint8_t code = rd(cursor + 2, ctx);

    switch (op) {
        case POLY_MACRO_OP_TAP:
            step.kind = POLY_MACRO_STEP_TAP;
            break;
        case POLY_MACRO_OP_DOWN:
            step.kind = POLY_MACRO_STEP_DOWN;
            break;
        case POLY_MACRO_OP_UP:
            step.kind = POLY_MACRO_STEP_UP;
            break;
        default:
            // Unknown op. Stop rather than skip: the byte after it is not known to be
            // an argument, so resuming would type whatever the argument happened to be.
            return step;
    }
    step.code = code;
    step.next = cursor + 3;
    return step;
}
