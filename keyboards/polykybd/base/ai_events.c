// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pure part of AI mode, see ai_events.h.
#include "ai_events.h"

#include <string.h>

void ai_ring_init(ai_ring_t *r) {
    memset(r, 0, sizeof(*r));
}

uint8_t ai_ring_push(ai_ring_t *r, uint8_t key) {
    r->seq = (r->seq == 0xFFu) ? 1u : (uint8_t)(r->seq + 1u);
    r->ev[r->head].seq = r->seq;
    r->ev[r->head].key = key;
    r->head = (uint8_t)((r->head + 1u) % AI_RING_SIZE);
    if (r->count < AI_RING_SIZE) ++r->count;
    return r->seq;
}

uint8_t ai_ring_read(const ai_ring_t *r, uint8_t since, ai_event_t *out) {
    // The oldest event is `count` places behind the head.
    const uint8_t first = (uint8_t)((r->head + AI_RING_SIZE - r->count) % AI_RING_SIZE);
    uint8_t start = 0;   // position (oldest = 0) of the first event to return
    if (since != 0) {
        for (uint8_t i = 0; i < r->count; ++i) {
            if (r->ev[(first + i) % AI_RING_SIZE].seq == since) {
                start = (uint8_t)(i + 1u);
                break;
            }
        }
    }
    uint8_t n = 0;
    for (uint8_t i = start; i < r->count; ++i) {
        out[n++] = r->ev[(first + i) % AI_RING_SIZE];
    }
    return n;
}

uint8_t ai_key_index(uint16_t keycode) {
    if (keycode >= AI_HID_KC_1 && keycode <= AI_HID_KC_0) return (uint8_t)(keycode - AI_HID_KC_1);
    if (keycode == AI_HID_KC_ESC) return AI_KEY_ESC;
    if (keycode == AI_HID_KC_MINUS) return AI_KEY_MINUS;
    if (keycode == AI_HID_KC_EQUAL) return AI_KEY_PLUS;
    return AI_KEY_NONE;
}

uint8_t ai_card_of_key(uint8_t key) {
    if (key < AI_NUM_KEYS) return key;
    if (key == AI_KEY_MINUS) return AI_CARD_MINUS;
    if (key == AI_KEY_PLUS) return AI_CARD_PLUS;
    return AI_KEY_NONE;
}

static uint16_t slot_at(const uint8_t *payload, uint8_t i) {
    const uint8_t *p = &payload[AI_NUM_KEYS + 1u + 2u * i];
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

bool ai_set_keys_valid(const uint8_t *payload, uint16_t num_slots) {
    for (uint8_t i = 0; i < AI_NUM_KEYS; ++i) {
        if (payload[i] >= AI_STATE_COUNT) return false;
    }
    if (payload[AI_NUM_KEYS] & (uint8_t)~AI_FLAGS_VALID) return false;
    for (uint8_t i = 0; i < AI_NUM_CARDS; ++i) {
        const uint16_t s = slot_at(payload, i);
        if (s != AI_SLOT_NONE && s >= num_slots) return false;
    }
    return true;
}

void ai_set_keys_decode(const uint8_t *payload, uint8_t *state, uint8_t *flags, uint16_t *slot) {
    memcpy(state, payload, AI_NUM_KEYS);
    *flags = payload[AI_NUM_KEYS];
    for (uint8_t i = 0; i < AI_NUM_CARDS; ++i) slot[i] = slot_at(payload, i);
}

bool ai_card_header(uint16_t header, uint16_t num_slots, uint16_t *slot, uint8_t *segment) {
    const uint16_t s   = (uint16_t)(header & 0x03FFu);
    const uint8_t  seg = (uint8_t)((header >> 12) & 0x07u);
    if (s >= num_slots || seg > 5u) return false;
    *slot    = s;
    *segment = seg;
    return true;
}
