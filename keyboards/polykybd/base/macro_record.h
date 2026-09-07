// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "macro_decode.h"

// Recording a macro ON THE KEYBOARD: the encoder that turns live key events into a
// dynamic-macro body, and the splice that puts that body into the shared buffer.
//
// Pure by construction -- a byte reader/writer callback, a caller-supplied clock, no
// quantum.h, no EEPROM, no timer -- so both link against a RAM buffer in a unit test.
// Same seam as base/macro_decode.c and base/fw_up_verdict.c, and for the same reason:
// the arithmetic is the part with a bug future, and it is only unreachable while it
// shares a function with the I/O.
//
// The output is QMK's own send-string encoding (see macro_decode.h), unchanged, so a
// recorded body is still playable by dynamic_keymap_macro_send(). Design notes and the
// decisions behind the gesture live in ../MACRO_RECORD_DESIGN.md.

// ---------------------------------------------------------------------------
// Encoder

// A gap shorter than this is not worth a step: replay already spaces steps by
// POLY_MACRO_STEP_MS, and recording every real gap roughly triples the size of a body
// for no benefit. Above it the pause was deliberate (waiting for a dialog, a menu),
// which is exactly what a macro has to reproduce.
#ifndef POLY_MACRO_REC_GAP_MS
#    define POLY_MACRO_REC_GAP_MS 150
#endif
// Quantise, so a 214 ms and a 219 ms pause encode as the same two digits.
#define POLY_MACRO_REC_GAP_ROUND_MS 10
// A pause longer than this was the user thinking, not the macro waiting.
#define POLY_MACRO_REC_GAP_MAX_MS 5000

// A press-and-release with nothing between them, held no longer than this, encodes as
// one TAP instead of DOWN + UP -- three bytes rather than six for the common case.
// A LONGER hold keeps DOWN + delay + UP: holding a key to make the host auto-repeat is
// a real thing to record, and collapsing it would silently drop the hold.
#define POLY_MACRO_REC_TAP_MS 500

// How many keys can be down at once and still be released by the stop-time flush.
// Above any reachable chord (a 6KRO report carries six keys plus eight modifiers).
#define POLY_MACRO_REC_HELD_MAX 12

// Bytes kept free so the flush can always close every open DOWN. Reserving up front is
// what makes "the buffer filled up" a clean stop rather than a body that ends holding
// Ctrl down on the host forever.
#define POLY_MACRO_REC_RESERVE (3 * POLY_MACRO_REC_HELD_MAX)

typedef struct {
    uint8_t *buf;
    uint16_t cap;
    uint16_t len;
    uint32_t last_ms;  // when the previous event was recorded
    bool     any;      // something has been recorded (so no leading delay)
    bool     full;     // hit the reserve; further events are refused, not truncated
    // When the most recent DOWN happened. Whether that DOWN is still collapsible is a
    // question about the BYTES (is the last step a DOWN of this key), so only the
    // clock needs remembering; rewriting its op byte in place is what makes a tap free.
    uint32_t tap_ms;
} poly_macro_rec_t;

// `buf`/`cap` are the caller's RAM staging buffer. Nothing is written to EEPROM while
// recording -- a write per keystroke is a wear-levelling journal append, and the
// consolidation erase it eventually triggers lands in the middle of live typing.
void poly_macro_rec_begin(poly_macro_rec_t *r, uint8_t *buf, uint16_t cap);

// Record one key event. `code` is a basic keycode (0x04..0xFF, modifiers 0xE0..0xE7);
// the caller is what turns a report into these. Returns false when the event did not
// fit, which is also when full() starts returning true.
bool poly_macro_rec_key(poly_macro_rec_t *r, uint8_t code, bool pressed, uint32_t now_ms);

bool poly_macro_rec_full(const poly_macro_rec_t *r);

// Close the recording: append an UP for every key still down, in reverse press order,
// and return the body length. Derived by WALKING the body through poly_macro_decode()
// rather than from a parallel "held" list kept during capture -- the guarantee that
// matters is "every DOWN in this body has an UP", and reading it back is what proves
// it. Idempotent: calling it twice adds nothing the second time.
//
// No trailing delay: a release at the end of a recording is the user stopping, not
// the macro waiting, so the flush takes no clock at all.
uint16_t poly_macro_rec_finish(poly_macro_rec_t *r);

// ---------------------------------------------------------------------------
// Splice

// Writes one byte of the macro region. The mirror of poly_macro_read_fn.
typedef void (*poly_macro_write_fn)(uint16_t offset, uint8_t value, void *ctx);

typedef enum {
    POLY_MACRO_SPLICE_OK = 0,
    POLY_MACRO_SPLICE_BAD_ID,   // the buffer is too small to hold that many slots
    POLY_MACRO_SPLICE_NO_ROOM,  // the new body does not fit alongside the others
} poly_macro_splice_result_t;

typedef enum {
    POLY_MACRO_COMMIT_MARK = 0,  // raise POLY_MACRO_INCOMPLETE
    POLY_MACRO_COMMIT_MOVE,      // shift the tail
    POLY_MACRO_COMMIT_ZERO,      // clear what a shrink vacated
    POLY_MACRO_COMMIT_BODY,      // write the new body + its terminator
    POLY_MACRO_COMMIT_UNMARK,    // the buffer is whole again
    POLY_MACRO_COMMIT_DONE,
} poly_macro_commit_phase_t;

// A splice in progress. Pumped in bounded chunks because the move is up to the whole
// body region: doing it in one go is a bulk EEPROM operation on the main loop, which
// is what stalls the split UART and makes the other half look dead.
typedef struct {
    poly_macro_read_fn  rd;
    poly_macro_write_fn wr;
    void               *ctx;
    const uint8_t      *body;
    uint16_t            len;
    uint16_t            end;        // one past the last byte of the macro region
    uint16_t            start;      // where this macro's bytes begin
    uint16_t            tail_from;  // first byte after the old body's terminator
    uint16_t            tail_len;
    uint16_t            zero_from;  // shrink only: first byte the move vacated
    uint16_t            zero_len;
    int32_t             delta;      // new size - old size, in bytes
    uint16_t            cursor;     // progress within the current phase
    uint8_t             phase;
} poly_macro_commit_t;

// Plans the splice and refuses BEFORE writing anything -- an over-capacity body must
// not half-apply. On OK the caller pumps poly_macro_commit_step() until it returns
// false. `end` is the size of the body region (poly_macro_capacity()).
poly_macro_splice_result_t poly_macro_commit_begin(poly_macro_commit_t *c,
                                                   poly_macro_read_fn rd,
                                                   poly_macro_write_fn wr, void *ctx,
                                                   uint8_t id, const uint8_t *body,
                                                   uint16_t len, uint16_t end);

// Does at most `budget` byte writes. Returns true while work remains.
//
// ⚠️ The buffer reads as NOT intact from the first step until the last one, on purpose.
// Interrupting a splice -- a power cut, a reset -- must leave something poly_macro_start()
// refuses, or the tail of a former macro is promoted into a macro of its own and a
// keypress types a fragment of whatever it used to hold.
bool poly_macro_commit_step(poly_macro_commit_t *c, uint16_t budget);

bool poly_macro_commit_done(const poly_macro_commit_t *c);
