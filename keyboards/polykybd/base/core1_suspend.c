// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core1_suspend.h"

core1_suspend_action_t core1_suspend_step(core1_suspend_t *s, bool eligible, bool owes, uint32_t now) {
    if (s->held) {
        return CORE1_SUSPEND_NONE;
    }
    if (!eligible) {
        s->waiting = false;
        return CORE1_SUSPEND_NONE;
    }
    if (!owes) {
        s->held    = true;
        s->waiting = false;
        s->waited  = 0;
        return CORE1_SUSPEND_HOLD;
    }
    if (!s->waiting) {
        s->waiting = true;
        s->since   = now;
    }
    // Unsigned difference: correct across a timer wrap.
    const uint32_t waited = now - s->since;
    if (waited < CORE1_SUSPEND_STALL_MS) {
        return CORE1_SUSPEND_NONE;
    }
    s->held    = true;
    s->waiting = false;
    s->waited  = waited;
    return CORE1_SUSPEND_HOLD_STALLED;
}

bool core1_resume_step(core1_suspend_t *s) {
    const bool release = s->held;
    s->held    = false;
    s->waiting = false;
    return release;
}
