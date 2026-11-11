// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/ai_events.c: the AI-mode event ring (cmd 45 op 2), the key-index
// mapping, and the checks on op 1 and cmd 46.

#include "gtest/gtest.h"

extern "C" {
#include "ai_events.h"
}

namespace {

TEST(AiRing, EmptyRingHasSeqZeroAndNoEvents) {
    ai_ring_t r;
    ai_ring_init(&r);
    ai_event_t out[AI_RING_SIZE];
    EXPECT_EQ(r.seq, 0);
    EXPECT_EQ(ai_ring_read(&r, 0, out), 0);
    EXPECT_EQ(ai_ring_read(&r, 7, out), 0);
}

TEST(AiRing, FirstEventIsSeqOne) {
    ai_ring_t r;
    ai_ring_init(&r);
    EXPECT_EQ(ai_ring_push(&r, 3), 1);
    ai_event_t out[AI_RING_SIZE];
    ASSERT_EQ(ai_ring_read(&r, 0, out), 1);
    EXPECT_EQ(out[0].seq, 1);
    EXPECT_EQ(out[0].key, 3);
}

TEST(AiRing, ReadReturnsOnlyNewerEventsOldestFirst) {
    ai_ring_t r;
    ai_ring_init(&r);
    for (uint8_t k = 0; k < 5; ++k) ai_ring_push(&r, k);   // seq 1..5
    ai_event_t out[AI_RING_SIZE];
    ASSERT_EQ(ai_ring_read(&r, 3, out), 2);
    EXPECT_EQ(out[0].seq, 4);
    EXPECT_EQ(out[0].key, 3);
    EXPECT_EQ(out[1].seq, 5);
    EXPECT_EQ(out[1].key, 4);
    EXPECT_EQ(ai_ring_read(&r, 5, out), 0);   // up to date
}

TEST(AiRing, ReadDoesNotConsume) {
    ai_ring_t r;
    ai_ring_init(&r);
    ai_ring_push(&r, 1);
    ai_ring_push(&r, 2);
    ai_event_t out[AI_RING_SIZE];
    EXPECT_EQ(ai_ring_read(&r, 0, out), 2);
    EXPECT_EQ(ai_ring_read(&r, 0, out), 2);
}

TEST(AiRing, OverflowKeepsTheNewestEight) {
    ai_ring_t r;
    ai_ring_init(&r);
    for (uint8_t i = 0; i < 12; ++i) ai_ring_push(&r, (uint8_t)(i % 11));   // seq 1..12
    ai_event_t out[AI_RING_SIZE];
    // seq 2 fell out of the ring: everything held comes back.
    ASSERT_EQ(ai_ring_read(&r, 2, out), AI_RING_SIZE);
    EXPECT_EQ(out[0].seq, 5);
    EXPECT_EQ(out[7].seq, 12);
    // seq 9 is held: only 10..12.
    ASSERT_EQ(ai_ring_read(&r, 9, out), 3);
    EXPECT_EQ(out[0].seq, 10);
    EXPECT_EQ(out[2].seq, 12);
}

TEST(AiRing, SeqWrapsTo1NeverZero) {
    ai_ring_t r;
    ai_ring_init(&r);
    for (int i = 0; i < 255; ++i) ai_ring_push(&r, 0);
    EXPECT_EQ(r.seq, 255);
    EXPECT_EQ(ai_ring_push(&r, 4), 1);
    EXPECT_EQ(ai_ring_push(&r, 5), 2);
    ai_event_t out[AI_RING_SIZE];
    // Ring order, not numeric order: 255 is older than 1.
    ASSERT_EQ(ai_ring_read(&r, 255, out), 2);
    EXPECT_EQ(out[0].seq, 1);
    EXPECT_EQ(out[0].key, 4);
    EXPECT_EQ(out[1].seq, 2);
}

TEST(AiKeys, NumberRowAndEsc) {
    EXPECT_EQ(ai_key_index(0x1E), 0);    // KC_1
    EXPECT_EQ(ai_key_index(0x26), 8);    // KC_9
    EXPECT_EQ(ai_key_index(0x27), 9);    // KC_0
    EXPECT_EQ(ai_key_index(0x29), AI_KEY_ESC);
    EXPECT_EQ(ai_key_index(0x1D), AI_KEY_NONE);   // KC_Z
    EXPECT_EQ(ai_key_index(0x28), AI_KEY_NONE);   // KC_ENTER, between 0 and ESC
    EXPECT_EQ(ai_key_index(0x591E), AI_KEY_NONE); // a keycode whose low byte is KC_1
    EXPECT_EQ(ai_key_index(0x2D), AI_KEY_MINUS);  // KC_MINUS: dictation
    EXPECT_EQ(ai_key_index(0x2E), AI_KEY_PLUS);   // KC_EQUAL: next waiting agent
    EXPECT_EQ(ai_key_index(0x2F), AI_KEY_NONE);   // KC_LBRC, after KC_EQUAL
}

TEST(AiKeys, CardOfKey) {
    EXPECT_EQ(ai_card_of_key(0), 0);
    EXPECT_EQ(ai_card_of_key(9), 9);
    EXPECT_EQ(ai_card_of_key(AI_KEY_ESC), AI_KEY_NONE);   // ESC draws its own text
    EXPECT_EQ(ai_card_of_key(AI_KEY_MINUS), AI_CARD_MINUS);
    EXPECT_EQ(ai_card_of_key(AI_KEY_PLUS), AI_CARD_PLUS);
    EXPECT_EQ(ai_card_of_key(AI_KEY_NONE), AI_KEY_NONE);
}

static void make_payload(uint8_t *p, uint8_t state, uint8_t esc, uint16_t slot) {
    for (unsigned i = 0; i < AI_NUM_KEYS; ++i) p[i] = state;
    p[AI_NUM_KEYS] = esc;
    for (unsigned i = 0; i < AI_NUM_CARDS; ++i) {
        p[AI_NUM_KEYS + 1 + 2 * i]     = (uint8_t)(slot & 0xFF);
        p[AI_NUM_KEYS + 1 + 2 * i + 1] = (uint8_t)(slot >> 8);
    }
}

TEST(AiSetKeys, AcceptsTheFullRange) {
    uint8_t p[AI_SET_KEYS_LEN];
    make_payload(p, AI_STATE_STALE, AI_FLAGS_VALID, 599);
    EXPECT_TRUE(ai_set_keys_valid(p, 600));
    make_payload(p, AI_STATE_EMPTY, 0, AI_SLOT_NONE);
    EXPECT_TRUE(ai_set_keys_valid(p, 600));
}

TEST(AiSetKeys, RejectsEachBadField) {
    uint8_t p[AI_SET_KEYS_LEN];
    make_payload(p, 0, 0, AI_SLOT_NONE);
    p[9] = AI_STATE_COUNT;
    EXPECT_FALSE(ai_set_keys_valid(p, 600));

    make_payload(p, 0, 0x10, AI_SLOT_NONE);   // a flag outside AI_FLAGS_VALID
    EXPECT_FALSE(ai_set_keys_valid(p, 600));

    make_payload(p, 0, 0, AI_SLOT_NONE);
    p[AI_NUM_KEYS + 1 + 2 * AI_CARD_PLUS]     = 0x58;   // the '+' card at 600
    p[AI_NUM_KEYS + 1 + 2 * AI_CARD_PLUS + 1] = 0x02;
    EXPECT_FALSE(ai_set_keys_valid(p, 600));

    make_payload(p, 0, 0, AI_SLOT_NONE);
    p[AI_NUM_KEYS + 1 + 2 * 9]     = 0x58;   // 600, little-endian
    p[AI_NUM_KEYS + 1 + 2 * 9 + 1] = 0x02;
    EXPECT_FALSE(ai_set_keys_valid(p, 600));

    make_payload(p, 0, 0, 0xFFFE);
    EXPECT_FALSE(ai_set_keys_valid(p, 600));
}

TEST(AiSetKeys, DecodeIsLittleEndianPerKey) {
    uint8_t p[AI_SET_KEYS_LEN];
    make_payload(p, 0, AI_FLAG_MORE_PAGES, AI_SLOT_NONE);
    p[0]                  = AI_STATE_WAITING;
    p[AI_NUM_KEYS + 1]     = 0x34;
    p[AI_NUM_KEYS + 1 + 1] = 0x01;   // key 0 -> slot 0x134
    p[AI_NUM_KEYS + 1 + 2 * AI_CARD_MINUS]     = 0x56;
    p[AI_NUM_KEYS + 1 + 2 * AI_CARD_MINUS + 1] = 0x02;   // '-' -> slot 598
    uint8_t  state[AI_NUM_KEYS];
    uint8_t  flags = 0;
    uint16_t slot[AI_NUM_CARDS];
    ai_set_keys_decode(p, state, &flags, slot);
    EXPECT_EQ(state[0], AI_STATE_WAITING);
    EXPECT_EQ(state[1], AI_STATE_EMPTY);
    EXPECT_EQ(flags, AI_FLAG_MORE_PAGES);
    EXPECT_EQ(slot[0], 0x134);
    EXPECT_EQ(slot[9], AI_SLOT_NONE);
    EXPECT_EQ(slot[AI_CARD_MINUS], 598);
    EXPECT_EQ(slot[AI_CARD_PLUS], AI_SLOT_NONE);
}

TEST(AiCard, HeaderFields) {
    uint16_t slot = 0;
    uint8_t  seg  = 0;
    EXPECT_TRUE(ai_card_header((uint16_t)(599 | (5 << 12)), 600, &slot, &seg));
    EXPECT_EQ(slot, 599);
    EXPECT_EQ(seg, 5);
    EXPECT_FALSE(ai_card_header(600, 600, &slot, &seg));
    EXPECT_FALSE(ai_card_header((uint16_t)(6 << 12), 600, &slot, &seg));
    EXPECT_FALSE(ai_card_header((uint16_t)(7 << 12), 600, &slot, &seg));
}

}  // namespace
