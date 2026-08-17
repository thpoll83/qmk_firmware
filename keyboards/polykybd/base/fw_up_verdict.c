// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "fw_up_verdict.h"

#include "polymod_crc32.h"

bool fw_up_commit_ack_is_self_describing(uint8_t slave_ack) {
    return slave_ack == SYNC_NACK_REFUSED;
}

enum fw_up_commit_verdict fw_up_classify_commit_failure(uint8_t slave_ack, bool status_ok,
                                                       uint8_t recorded_ack) {
    // An explicit refusal is final. It must NOT be overridden by the probe: a
    // snapshot can be stale, corrupt or unobtainable, and none of that makes the
    // refusal we already received less true.
    if (fw_up_commit_ack_is_self_describing(slave_ack)) {
        return FW_UP_COMMIT_REFUSED;
    }
    // No trustworthy snapshot — fall back to the conservative reading. This is also
    // the path for a slave too old to record a verdict, and it is never worse than
    // the pre-probe behaviour: a retry of a genuinely bad image just fails again,
    // whereas a wrong "refused" costs a full re-stream.
    if (!status_ok) {
        return FW_UP_COMMIT_LINK_FAILURE;
    }
    // The refusal ack itself was lost, but the slave wrote down what it answered.
    if (recorded_ack == SYNC_NACK_REFUSED) {
        return FW_UP_COMMIT_REFUSED;
    }
    // Anything else — an unset verdict (0, cleared at the start of this attempt) or
    // a recorded SYNC_ACK (the slave committed and the ACK was lost) — is a link
    // failure, and a link failure is free to retry.
    return FW_UP_COMMIT_LINK_FAILURE;
}

bool fw_up_status_crc_ok(const fw_staging_status_t *status, uint32_t claimed_crc) {
    if (!status) return false;
    return crc32_1byte((const uint8_t *)status, sizeof(*status), 0) == claimed_crc;
}
