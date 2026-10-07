// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for base/fw_sig_policy.h, the decision behind fw_sig_verify(), which
// both the firmware image (FW-2) and the DOOM pack (FW-9) go through. The case
// that matters is the all-zero placeholder key: it is a low-order point, so a
// crafted signature can pass the crypto check, and the policy must refuse it
// WITHOUT asking the check. The stub below always says "valid" to prove that.

#include "gtest/gtest.h"

extern "C" {
#include "fw_sig_policy.h"
}

#include <cstring>

namespace {

int g_calls;
int g_result;

int stub_check(const uint8_t *, const uint8_t *, const uint8_t *, size_t) {
    ++g_calls;
    return g_result;
}

const uint8_t kSig[64] = {1};
const uint8_t kMsg[4]  = {'P', 'l', 'y', 'X'};

} // namespace

TEST(FwSigPolicy, PlaceholderKeyIsRefusedEvenIfTheCheckPasses) {
    uint8_t key[FW_SIG_KEY_LEN] = {0};
    g_calls                     = 0;
    g_result                    = 0; // the crypto would accept
    EXPECT_FALSE(fw_sig_key_provisioned(key));
    EXPECT_FALSE(fw_sig_verify_with(key, stub_check, kSig, kMsg, sizeof(kMsg)));
    EXPECT_EQ(g_calls, 0) << "the check must not run for the placeholder key";
}

TEST(FwSigPolicy, AnyNonZeroByteCountsAsProvisioned) {
    for (size_t i = 0; i < FW_SIG_KEY_LEN; ++i) {
        uint8_t key[FW_SIG_KEY_LEN] = {0};
        key[i]                      = 0x01;
        EXPECT_TRUE(fw_sig_key_provisioned(key)) << "byte " << i;
    }
}

TEST(FwSigPolicy, ProvisionedKeyFollowsTheCheck) {
    uint8_t key[FW_SIG_KEY_LEN];
    memset(key, 0xA5, sizeof(key));
    g_calls  = 0;
    g_result = 0;
    EXPECT_TRUE(fw_sig_verify_with(key, stub_check, kSig, kMsg, sizeof(kMsg)));
    g_result = -1;
    EXPECT_FALSE(fw_sig_verify_with(key, stub_check, kSig, kMsg, sizeof(kMsg)));
    EXPECT_EQ(g_calls, 2);
}
