// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fw_stage.h"

fw_stage_t fw_stage_next(fw_stage_t stage, fw_stage_event_t ev) {
    switch (ev) {
        case FW_EV_BEGIN_SYNC:
            return FW_STAGE_RECEIVING;
        case FW_EV_BEGIN_DEFERRED:
            // From any stage: a new BEGIN (or a re-erase of a dirty slot) restarts
            // the erase.
            return FW_STAGE_ERASING;
        case FW_EV_BEGIN_REFUSED:
            // ⚠️ Kept exactly as the two flags behaved: a refused BEGIN stops a
            // pending erase but does NOT end a stream already in progress, so
            // fw_up_active stays set (and fw_staging keeps its core1 hold) until
            // that stream is finalized.
            return stage == FW_STAGE_ERASING ? FW_STAGE_RECEIVING : stage;
        case FW_EV_ERASE_DONE:
            return stage == FW_STAGE_ERASING ? FW_STAGE_RECEIVING : stage;
        case FW_EV_FINALIZED:
            return FW_STAGE_IDLE;
    }
    return stage;
}
