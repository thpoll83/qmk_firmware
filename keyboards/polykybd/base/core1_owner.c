// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// core1 ownership logic. Hardware access goes through the core1_hw_* seam in
// core1_owner.h, so this file is pure and unit-tested
// (make test:polykybd_core1_owner).
#include "core1_owner.h"

static uint8_t        s_holds;
static core1_tenant_t s_tenant = CORE1_TENANT_SERVICE;
// One launch handshake at a time. A launch runs outside the lock (it can take
// 100 ms), so the slave's split thread can preempt it and do a whole hold and
// release. That release must not start a second handshake against the first:
// it marks the running launch as disturbed instead, and the launcher relaunches.
static bool s_launching;
static bool s_relaunch;

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

// Reset core1 and claim the launch, unless a hold is outstanding or another
// launch is running. The check and the reset share one critical section: a
// hold taken on another thread can then land only before the check (seen,
// nothing released) or after the reset (core1 is forced off again, and the
// launch that follows times out).
static bool claim_launch(void) {
    core1_hw_lock();
    const bool ok = s_holds == 0 && !s_launching;
    if (ok) {
        core1_hw_force_off();
        core1_hw_release();
        s_launching = true;
    }
    core1_hw_unlock();
    return ok;
}

// End a claimed launch. Returns true when a hold and its release landed during
// it: core1 was reset under the handshake, so whatever it started is not what
// runs now. Sets the tenant only for an undisturbed launch that answered.
static bool end_launch(bool ok, core1_tenant_t tenant) {
    core1_hw_lock();
    const bool disturbed = s_relaunch;
    s_launching          = false;
    s_relaunch           = false;
    if (ok && !disturbed) {
        s_tenant = tenant;
    }
    core1_hw_unlock();
    return disturbed;
}

// Launch the service on a launch already claimed. A disturbed launch is redone
// (bounded); a timeout is not, because it means a hold landed and that hold's
// release relaunches.
static bool launch_service_claimed(const char *what) {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        const bool ok = core1_hw_launch_service();
        if (!end_launch(ok, CORE1_TENANT_SERVICE)) {
            if (!ok) {
                core1_hw_report(what);
            }
            return ok;
        }
        // Out of attempts: leave core1 as the last handshake left it. Claiming
        // here would reset a service that handshake may just have started, and
        // nothing would launch it again.
        if (attempt + 1u == 3u) {
            break;
        }
        if (!claim_launch()) {
            return false; // held again, or another launch runs: theirs
        }
    }
    core1_hw_report(what);
    return false;
}

void core1_release(void) {
    bool launch = false;
    core1_hw_lock();
    if (s_holds > 0 && --s_holds == 0) {
        // Whatever ran before the hold was killed by it; the service is what
        // comes back. A custom tenant (the Doom engine) is restarted only by
        // its owner, through core1_run().
        s_tenant = CORE1_TENANT_SERVICE;
        core1_hw_release();
        if (s_launching) {
            s_relaunch = true; // the running launcher redoes it
        } else {
            s_launching = true;
            launch      = true;
        }
    }
    core1_hw_unlock();
    if (launch) {
        (void)launch_service_claimed("release: RLE service relaunch timed out");
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

bool core1_run(void (*entry)(void), uint32_t *stack_bottom, size_t stack_bytes) {
    if (!claim_launch()) {
        return false;
    }
    const bool ok = core1_hw_launch(entry, stack_bottom, stack_bytes);
    if (end_launch(ok, CORE1_TENANT_CUSTOM)) {
        // The hold killed the entry and its release left the relaunch to us.
        core1_hw_report("run: a hold and release landed during the launch");
        (void)core1_restore_service();
        return false;
    }
    if (!ok) {
        core1_hw_report(s_holds > 0 ? "run: a hold landed during the launch" : "run: launch timed out");
        return false;
    }
    return true;
}

bool core1_restore_service(void) {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (!claim_launch()) {
            // Held: the last release relaunches the service. Launching: the
            // other launcher does.
            s_tenant = CORE1_TENANT_SERVICE;
            return false;
        }
        const bool ok = core1_hw_launch_service();
        if (!end_launch(ok, CORE1_TENANT_SERVICE) && ok) {
            return true;
        }
    }
    core1_hw_report("restore: RLE service relaunch failed 3 times");
    s_tenant = CORE1_TENANT_SERVICE;
    return false;
}
