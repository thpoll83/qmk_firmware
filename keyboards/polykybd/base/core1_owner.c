// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// core1 ownership logic. Hardware access goes through the core1_hw_* seam in
// core1_owner.h, so this file is pure and unit-tested
// (make test:polykybd_core1_owner).
#include "core1_owner.h"

static uint8_t        s_holds;
static core1_tenant_t s_tenant = CORE1_TENANT_SERVICE;

void core1_hold(void) {
    core1_hw_lock();
    if (s_holds == 0) {
        core1_hw_force_off();
    }
    if (s_holds < UINT8_MAX) {
        s_holds++;
    }
    core1_hw_unlock();
}

void core1_release(void) {
    bool relaunch = false;
    core1_hw_lock();
    if (s_holds > 0 && --s_holds == 0) {
        // Whatever ran before the hold was killed by it; the service is what
        // comes back. A custom tenant (the Doom engine) is restarted only by
        // its owner, through core1_run().
        s_tenant = CORE1_TENANT_SERVICE;
        core1_hw_release();
        relaunch = true;
    }
    core1_hw_unlock();
    // The handshake runs outside the lock (it can take 100 ms). A hold that
    // lands during it forces core1 off again; the launch then times out and
    // the next release relaunches the service.
    if (relaunch && !core1_hw_launch_service()) {
        core1_hw_report("release: RLE service relaunch timed out");
    }
}

bool core1_held(void) {
    return s_holds > 0;
}

uint8_t core1_hold_count(void) {
    return s_holds;
}

core1_tenant_t core1_tenant(void) {
    return s_tenant;
}

// Reset core1 unless a hold is outstanding. The check and the reset share one
// critical section: a hold taken on another thread can then land only before
// the check (seen, nothing released) or after the reset (core1 is forced off
// again, and the launch that follows times out).
static bool reset_unless_held(void) {
    core1_hw_lock();
    const bool held = s_holds > 0;
    if (!held) {
        core1_hw_force_off();
        core1_hw_release();
    }
    core1_hw_unlock();
    return !held;
}

bool core1_run(void (*entry)(void), uint32_t *stack_bottom, size_t stack_bytes) {
    if (!reset_unless_held()) {
        return false;
    }
    if (!core1_hw_launch(entry, stack_bottom, stack_bytes)) {
        core1_hw_report(s_holds > 0 ? "run: a hold landed during the launch" : "run: launch timed out");
        return false;
    }
    s_tenant = CORE1_TENANT_CUSTOM;
    return true;
}

bool core1_restore_service(void) {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (!reset_unless_held()) {
            // The last release relaunches the service.
            s_tenant = CORE1_TENANT_SERVICE;
            return false;
        }
        if (core1_hw_launch_service()) {
            s_tenant = CORE1_TENANT_SERVICE;
            return true;
        }
    }
    core1_hw_report("restore: RLE service relaunch failed 3 times");
    s_tenant = CORE1_TENANT_SERVICE;
    return false;
}
