// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pure part of AI mode (ai_mode.h, HID cmd 45 / 46, protocol v30): the event ring
// that reports key presses to the host, the key-index mapping, and the checks on what
// the host sends. No quantum.h, no config.h, so it is unit-tested on the host
// (make test:polykybd_ai_events). The firmware binding is ai_mode.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Ring size. Op 2 (READ_EVENTS) returns at most this many events, as {seq, key} pairs
// after a 2-byte header, so it must stay below (62 - 2) / 2 = 30.
#define AI_RING_SIZE 8u
// The number keys 1..9, 0 are key 0..9; ESC (the pager) is key 10.
#define AI_NUM_KEYS 10u
#define AI_KEY_ESC  10u
#define AI_KEY_NONE 0xFFu

// Per-key state the host sets (op 1). A CLOSED range: a value past AI_STATE_STALE is
// NACKed.
enum ai_state {
    AI_STATE_EMPTY   = 0,
    AI_STATE_WORKING = 1,
    AI_STATE_WAITING = 2,
    AI_STATE_DONE    = 3,
    AI_STATE_IDLE    = 4,
    AI_STATE_STALE   = 5,
};
#define AI_STATE_COUNT 6u
// A key with no card (ai_slot).
#define AI_SLOT_NONE 0xFFFFu

typedef struct {
    uint8_t seq;
    uint8_t key;
} ai_event_t;

// The ring. seq 0 means "no event yet"; the first event is seq 1, and after 255 comes
// 1 again, so a host that has seen nothing can always send 0.
typedef struct {
    ai_event_t ev[AI_RING_SIZE];
    uint8_t    count;   // events held, 0..AI_RING_SIZE
    uint8_t    head;    // index of the next write
    uint8_t    seq;     // newest seq, 0 = none yet
} ai_ring_t;

void ai_ring_init(ai_ring_t *r);

// Writes one event and returns its seq (never 0).
uint8_t ai_ring_push(ai_ring_t *r, uint8_t key);

// Copies the events newer than `since`, OLDEST FIRST, into out[] (room for
// AI_RING_SIZE) and returns how many. "Newer" is ring order: the events after the one
// whose seq is `since`. If `since` is not in the ring (0, or older than the ring
// holds, or from before a reboot), every event held is returned.
uint8_t ai_ring_read(const ai_ring_t *r, uint8_t since, ai_event_t *out);

// The key index of a base-layer keycode: KC_1..KC_9 -> 0..8, KC_0 -> 9, KC_ESC -> 10,
// anything else AI_KEY_NONE. The HID usage values are passed in so this file needs no
// QMK header; ai_mode.c asserts them against QMK's.
#define AI_HID_KC_1   0x1Eu
#define AI_HID_KC_0   0x27u
#define AI_HID_KC_ESC 0x29u
uint8_t ai_key_index(uint16_t keycode);

// Op 1 (SET_KEYS) validation: every state < AI_STATE_COUNT, esc_attention 0 or 1, and
// every slot either AI_SLOT_NONE or below `num_slots`. `payload` is data[3..33] of the
// report: 10 states, 1 esc byte, 10 slots little-endian.
#define AI_SET_KEYS_LEN (AI_NUM_KEYS + 1u + 2u * AI_NUM_KEYS)
bool ai_set_keys_valid(const uint8_t *payload, uint16_t num_slots);

// Op 1 decode, after ai_set_keys_valid(): fills state[10], *esc and slot[10].
void ai_set_keys_decode(const uint8_t *payload, uint8_t *state, uint8_t *esc, uint16_t *slot);

// Cmd 46 header: bits 0..9 pool slot, bits 12..14 segment. False if the slot is not
// below num_slots or the segment is past 5.
bool ai_card_header(uint16_t header, uint16_t num_slots, uint16_t *slot, uint8_t *segment);
