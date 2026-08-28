// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for the HID cmd 35 (GET_LAYER_NAMES, protocol v14+) payload encoder in
// layer_names.c. The host decoder is mutation-tested on its side, but a host
// fixture can only catch the host MISREADING a payload — never this end
// emitting the wrong one (the same one-sidedness the font-pack COMMIT status
// had). This suite pins the emitting half: [total][count] then NUL-terminated
// names, total = whole payload length including itself, count = the write cap.

#include "gtest/gtest.h"

extern "C" {
#include "layer_names.h"
}

#include <string>
#include <vector>

namespace {

std::vector<uint8_t> build() {
    std::vector<uint8_t> payload(LAYER_NAMES_PAYLOAD_MAX, 0xEE);  // poison
    uint16_t used = poly_layer_names_payload(payload.data());
    EXPECT_LE(used, payload.size());
    payload.resize(LAYER_NAMES_PAYLOAD_MAX);
    payload.push_back((uint8_t)used);  // smuggle `used` out for the callers
    return payload;
}

// Decode the way the host does: read byte 0, take exactly that many bytes,
// split on NULs — termination is arithmetic, never a scan.
std::vector<std::string> decode(const std::vector<uint8_t>& payload) {
    std::vector<std::string> names;
    uint8_t     total = payload[0];
    std::string cur;
    for (uint16_t i = 2; i < total; ++i) {
        if (payload[i] == 0) {
            names.push_back(cur);
            cur.clear();
        } else {
            cur.push_back((char)payload[i]);
        }
    }
    EXPECT_TRUE(cur.empty()) << "payload did not end on a terminator";
    return names;
}

TEST(LayerNamesPayloadTest, TotalIsTheReturnedLengthAndFitsOneByte) {
    auto payload = build();
    uint16_t used = payload.back();
    EXPECT_EQ(payload[0], used);
    EXPECT_LE(used, 255);
    EXPECT_LE(used, LAYER_NAMES_PAYLOAD_MAX);
}

TEST(LayerNamesPayloadTest, CountIsTheHostRemappableWriteCap) {
    // The SAME constant id_dynamic_keymap_get_layer_count answers with — two
    // different counts would let the editor draw a tab it has no name for.
    auto payload = build();
    EXPECT_EQ(payload[1], DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT);
}

TEST(LayerNamesPayloadTest, EveryLayerDecodesToItsWireName) {
    auto payload = build();
    auto names   = decode(payload);
    ASSERT_EQ(names.size(), (size_t)DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT);
    for (uint8_t layer = 0; layer < DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT; ++layer) {
        const char* wire = poly_layer_name_wire(layer);
        std::string expect = wire ? std::string(wire).substr(0, POLY_LAYER_NAME_MAX) : "";
        EXPECT_EQ(names[layer], expect) << "layer " << (int)layer;
        EXPECT_LE(names[layer].size(), (size_t)POLY_LAYER_NAME_MAX);
    }
}

TEST(LayerNamesPayloadTest, TheBaseLayoutAndFixedLayersAreNamed) {
    // A literal spot check so a table swap cannot slip past the self-referential
    // comparison above: layer 0 is the Qwerty base and the write cap's last
    // layer is the utility layer.
    auto names = decode(build());
    EXPECT_EQ(names.front(), "Qwerty");
    EXPECT_EQ(names.back(), "Utility");
}

TEST(LayerNamesPayloadTest, NoNameNeedsMoreThanTheBudget) {
    // POLY_LAYER_NAME_MAX is the host tab label's budget; the emitter clamps
    // rather than trusting the table, so nothing may exceed it after decode.
    for (auto& n : decode(build())) {
        EXPECT_LE(n.size(), (size_t)POLY_LAYER_NAME_MAX);
    }
}

TEST(LayerNamesPayloadTest, ZeroFillPastTheTotalIsNeverReadAsARecord) {
    // The report's zero fill after the payload must not add phantom empty
    // names — decoding stops at `total` by arithmetic. Poison vs zero fill
    // beyond `used` must not change the decode.
    auto payload = build();
    uint16_t used = payload.back();
    auto a = decode(payload);
    for (size_t i = used; i < (size_t)LAYER_NAMES_PAYLOAD_MAX; ++i) payload[i] = 0x00;
    auto b = decode(payload);
    EXPECT_EQ(a, b);
}

}  // namespace
