// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Pure decoding for the dynamic-macro buffer: where macro N starts, how much of the
// buffer is in use, and what the next step is. Deliberately dependency-free -- no
// quantum.h, no EEPROM, no timer -- so it links against a RAM buffer in a unit test.
//
// This is the same split as base/fw_up_verdict.c: the part with a bug history is the
// arithmetic, and the arithmetic is the part that was unreachable while it lived
// inside a function that also did I/O.
//
// WIRE FORMAT -- QMK's own send-string encoding, NOT Vial's extension of it.
// A macro is a run of bytes terminated by NUL; macro N is found by skipping N NULs.
//
//   <printable>              type that character
//   0x01 0x01 <kc>           tap keycode
//   0x01 0x02 <kc>           press keycode
//   0x01 0x03 <kc>           release keycode
//   0x01 0x04 <ascii digits> wait N ms, terminated by the first non-digit (which is
//                            re-read as the next step, exactly as send_string does)
//
// Keycodes are 8-bit, i.e. basic keycodes only. That covers every modifier
// (KC_LCTL..KC_RGUI are 0xE0..0xE7), so chords are expressible; mod-taps and layer
// keycodes are not, and a 16-bit extension opcode can be added if that ever bites.
// Staying on the base encoding rather than Vial's means QMK's own
// dynamic_keymap_macro_send() can still play the very same buffer, which is a real
// cross-check rather than a theoretical one.

#define POLY_MACRO_PREFIX    0x01
#define POLY_MACRO_OP_TAP    0x01
#define POLY_MACRO_OP_DOWN   0x02
#define POLY_MACRO_OP_UP     0x03
#define POLY_MACRO_OP_DELAY  0x04

typedef enum {
    POLY_MACRO_STEP_END = 0, // NUL, or the cursor ran past the buffer
    POLY_MACRO_STEP_CHAR,    // type `code` as a character
    POLY_MACRO_STEP_TAP,
    POLY_MACRO_STEP_DOWN,
    POLY_MACRO_STEP_UP,
    POLY_MACRO_STEP_DELAY,   // wait `ms`
} poly_macro_step_kind_t;

typedef struct {
    poly_macro_step_kind_t kind;
    uint8_t                code; // CHAR / TAP / DOWN / UP
    uint16_t               ms;   // DELAY
    uint16_t               next; // cursor to resume from
} poly_macro_step_t;

// Reads one byte of the macro region. `end` is one past the last readable offset;
// the decoder never calls this with an offset >= end.
typedef uint8_t (*poly_macro_read_fn)(uint16_t offset, void *ctx);

// Byte offset where macro `id` starts, or `end` when the buffer holds fewer than
// id+1 macros. Skipping N NULs is how QMK addresses them, so an empty macro is a
// bare NUL and still occupies a slot.
uint16_t poly_macro_find(poly_macro_read_fn rd, void *ctx, uint8_t id, uint16_t end);

// Bytes in use = the offset just past the terminator of the last non-empty macro.
// Trailing empty slots cost nothing, so this is what a budget readout should show.
uint16_t poly_macro_used(poly_macro_read_fn rd, void *ctx, uint16_t end);

// Decode the step at `cursor`. Always returns a step whose `next` is > cursor for
// anything other than END, so a caller cannot spin.
poly_macro_step_t poly_macro_decode(poly_macro_read_fn rd, void *ctx, uint16_t cursor, uint16_t end);

// True when the buffer looks mid-write: the last byte is not NUL, so a stream was cut
// short. QMK refuses playback in that state and so do we -- a half-written macro can
// otherwise type an arbitrary prefix of someone's password into the wrong window.
bool poly_macro_buffer_intact(poly_macro_read_fn rd, void *ctx, uint16_t end);
