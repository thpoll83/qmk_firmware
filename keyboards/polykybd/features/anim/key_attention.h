// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// KEY ATTENTION (HID cmd 47, protocol v32): the host asks for the Eden intro's focus
// ripple (focus_ring.h) on one key, a number of times or until cancelled. For a
// program tutorial ("press this key next"), the layout editor, or anything else that
// wants to point at a key. AI mode's waiting agents use the same ripple
// (ai_attention_tick), but only while no host request runs.
//
// The request is RAM only and travels to the slave in poly_sync_t (attn_*), so both
// halves start the ring from the same key and it crosses the whole board. split72
// only: split42 has no per-keycap ripple, so the command ACKs and draws nothing there.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define KEY_ATTN_CMD          47
#define KEY_ATTN_OP_START      0
#define KEY_ATTN_OP_CANCEL     1
// Op 2 GUIDE (protocol v34) is ATTN_GUIDE_OP in base/attn_guide.h: a path of layer
// keys to a target layer, rung on the step whose layer is active.
// Ripple period in units of 100 ms, a CLOSED range: one ripple lasts 2.2 s, so a
// shorter period would start the next one over it, and more than 10 s reads as gone.
#define KEY_ATTN_PERIOD_MIN   23
#define KEY_ATTN_PERIOD_MAX  100

// Cmd 47, master. data[2] op: 0 START, 1 CANCEL, 2 GUIDE (base/attn_guide.h).
//   START  data[3] row, data[4] col (matrix position; the key must have a display),
//          data[5] count of ripples (0 = until cancelled), data[6] period / 100 ms
//          (KEY_ATTN_PERIOD_MIN..MAX). A bad field NACKs and changes nothing; a
//          START replaces any running request.
//   CANCEL ends the request; a ripple already running finishes.
// Reply "P\x2F." or "P\x2F!".
void key_attention_hid(uint8_t *data, uint8_t length);

// Housekeeping, both halves: start the next ripple of the request when it is due.
void key_attention_tick(void);

// True while a request still has ripples to run (AI mode leaves the ring to it).
bool key_attention_pending(void);
