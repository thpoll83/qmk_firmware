// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// HOST TUTORIAL (HID cmd 49, protocol v35): the pure part. The host runs a tutorial
// script (PolyKybdHost services/tutorials) and uses this command to put two lines of
// text on the status displays, to darken every key except a set of keys (the
// spotlight), to light the board with the reveal wave, and to ask the master to watch
// for a key press, a layer or a keycode. The host polls the matches; a host that stops
// polling ends the session after HOST_TUT_WATCHDOG_MS.
//
// No quantum.h: parsing, validation, matching and the watchdog are unit-tested on the
// host (make test:polykybd_host_tutorial_plan). The firmware binding, the relay to the
// slave and the drawing are anim/host_tutorial.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HOST_TUT_CMD 49u

// data[2]: the operation.
#define HOST_TUT_OP_INFO      0u   // reply [3] op bitmask, [4] max conditions, [5] watchdog s, [6] text bytes
#define HOST_TUT_OP_TEXT      1u   // [3] len0, [4..] line 0, then len1, line 1 (ASCII 0x20..0x7E)
#define HOST_TUT_OP_SPOTLIGHT 2u   // [3] flags (bit 0 on), [4..13] matrix bit mask, bit = row * cols + col
#define HOST_TUT_OP_REVEAL    3u   // [3] row, [4] col: the wave from that key lights every key
#define HOST_TUT_OP_ARM       4u   // [3] count 0..HOST_TUT_MAX_CONDS, then {kind, a, b} each
#define HOST_TUT_OP_POLL      5u   // reply [3] flags, [4] armed count, [5] matched bits, [6] layer
#define HOST_TUT_OP_END       6u   // clear text, spotlight and conditions
#define HOST_TUT_OP_COUNT     7u
// Master -> slave only, never from the host (the master's handler NACKs it as unknown):
// [3..6] a host_tut_state_crc() of the master's text and spotlight, little endian. The
// slave refuses it when its own state differs, and the master then resends TEXT and
// SPOTLIGHT. This catches a slave that restarted while the link never read as down.
#define HOST_TUT_RELAY_VERIFY 0x80u
// How often the master checks the slave's copy while a tutorial shows text or a
// spotlight (no reveal running, nothing pending).
#define HOST_TUT_VERIFY_MS    2000u

// Byte offsets in the report: data[0] 'P', data[1] the command, data[2] the op.
#define HOST_TUT_IDX_ARG      3u
// A raw HID report is 64 bytes; ops read at most this many.
#define HOST_TUT_REPORT_BYTES 64u
// TEXT: two length bytes and both lines must fit after the op byte.
#define HOST_TUT_TEXT_BYTES   (HOST_TUT_REPORT_BYTES - HOST_TUT_IDX_ARG - 2u)

#define HOST_TUT_SPOT_FLAG_ON 0x01u
// 80 bits: split72 has a 10 x 8 matrix, split42 8 x 6.
#define HOST_TUT_MASK_BYTES   10u

#define HOST_TUT_MAX_CONDS    4u
#define HOST_TUT_COND_KEY     1u   // a = row, b = col: that key is pressed
#define HOST_TUT_COND_LAYER   2u   // a = layer: that layer is on
#define HOST_TUT_COND_KEYCODE 3u   // a = low byte, b = high byte: a key with it is pressed

// POLL reply flags.
#define HOST_TUT_POLL_TEXT    0x01u
#define HOST_TUT_POLL_SPOT    0x02u
#define HOST_TUT_POLL_ARMED   0x04u

// The master ends the session when no cmd 49 report arrived for this long, so a host
// that crashed does not leave the board dark (maintainer's choice, 2026-11-15).
#define HOST_TUT_WATCHDOG_MS  10000u

// Both lines in one buffer: line 0 at buf[0], line 1 right after it. Together they
// never exceed HOST_TUT_TEXT_BYTES, because both came from one report.
typedef struct {
    uint8_t len[2];
    char    buf[HOST_TUT_TEXT_BYTES];
} host_tut_text_t;

typedef struct {
    uint8_t kind;
    uint8_t a;
    uint8_t b;
} host_tut_cond_t;

typedef struct {
    uint8_t         count;
    uint8_t         matched;   // bit i: condition i matched since ARM (sticky)
    host_tut_cond_t cond[HOST_TUT_MAX_CONDS];
} host_tut_conds_t;

// TEXT. Both lengths 0 clears the text. Refuses a length that runs past the report and
// any byte outside 0x20..0x7E (the status face covers no more). Leaves *out alone on
// any error.
bool host_tut_parse_text(const uint8_t *data, uint8_t length, host_tut_text_t *out);
bool host_tut_text_empty(const host_tut_text_t *t);
// Line i (0 or 1) and its length; not NUL-terminated.
const char *host_tut_text_line(const host_tut_text_t *t, uint8_t i);

// SPOTLIGHT. *on is the flag; mask gets the 10 mask bytes. Refuses a set bit at or past
// rows * cols, and an unknown flag bit.
bool host_tut_parse_spotlight(const uint8_t *data, uint8_t length, uint8_t rows, uint8_t cols,
                              bool *on, uint8_t mask[HOST_TUT_MASK_BYTES]);
bool host_tut_mask_has(const uint8_t mask[HOST_TUT_MASK_BYTES], uint8_t cols, uint8_t row,
                       uint8_t col);

// ARM. Checks the count, the kinds and the row, col and layer ranges. matched starts 0.
bool host_tut_parse_arm(const uint8_t *data, uint8_t length, uint8_t rows, uint8_t cols,
                        uint8_t layers, host_tut_conds_t *out);

// A key event on the master. Marks KEY and KEYCODE conditions on a press only.
void host_tut_note_key(host_tut_conds_t *c, uint8_t row, uint8_t col, uint16_t keycode,
                       bool pressed);
// The layer state on the master (layer_state | default_layer_state). Marks LAYER
// conditions whose layer bit is set.
void host_tut_note_layers(host_tut_conds_t *c, uint32_t layers_on);

// The watchdog: true when `last_ms` (the last cmd 49 report) is HOST_TUT_WATCHDOG_MS or
// more before `now_ms`. Wrap-safe.
bool host_tut_expired(uint32_t last_ms, uint32_t now_ms);

// What the master still has to send the slave (anim/host_tutorial.c), sent in this order.
#define HOST_TUT_DIRTY_END  0x01u
#define HOST_TUT_DIRTY_TEXT 0x02u
#define HOST_TUT_DIRTY_SPOT 0x04u
// The dirty bits that may go out now. A SPOTLIGHT is withheld while a reveal runs: on the
// slave it would cancel the reveal, and the reveal's end queues the spotlight-off anyway.
uint8_t host_tut_sendable(uint8_t dirty, bool reveal_on);

// A 32-bit FNV-1a checksum of the text (both lengths and the used bytes), the spotlight
// flag and the mask, as the VERIFY relay compares them. Bytes past the used text do not
// count, so two copies of the same lines always match.
uint32_t host_tut_state_crc(const host_tut_text_t *text, bool spot_on,
                            const uint8_t mask[HOST_TUT_MASK_BYTES]);

// The reveal front's progress 0..255 after `elapsed_ms` of a run of `run_ms`.
uint8_t host_tut_reveal_progress(uint32_t elapsed_ms, uint32_t run_ms);
