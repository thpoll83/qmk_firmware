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
//
// yield_to_service: a refused claim because of a hold records the service as
// the tenant, in the same critical section. The hold's last release relaunches
// the service. Written after the unlock instead, a release and a core1_run()
// on another thread could land in between, and the write would mark the
// running engine as the service.
static bool claim_launch_ex(bool yield_to_service) {
    core1_hw_lock();
    const bool ok = s_holds == 0 && !s_launching;
    if (ok) {
        core1_hw_force_off();
        core1_hw_release();
        s_launching = true;
    } else if (yield_to_service && s_holds > 0) {
        s_tenant = CORE1_TENANT_SERVICE;
    }
    core1_hw_unlock();
    return ok;
}

static bool claim_launch(void) {
    return claim_launch_ex(false);
}

// How a claimed launch ended, decided under the lock.
typedef enum {
    LAUNCH_CLEAN,     // nothing touched core1 during it
    LAUNCH_HELD,      // a hold is outstanding: core1 is in reset whatever answered,
                      // and that hold's last release relaunches the service
    LAUNCH_DISTURBED, // a hold and its release both landed: core1 was reset under
                      // the handshake and the release left the relaunch to us
} launch_end_t;

// End a claimed launch. Sets the tenant only for a clean launch that answered. A
// hold that lands after the handshake answered but before this lock still counts:
// otherwise the launch would read as a success while core1 sits in reset.
static launch_end_t end_launch(bool ok, core1_tenant_t tenant) {
    core1_hw_lock();
    const launch_end_t end = s_relaunch ? LAUNCH_DISTURBED : (s_holds > 0 ? LAUNCH_HELD : LAUNCH_CLEAN);
    s_launching            = false;
    s_relaunch             = false;
    if (ok && end == LAUNCH_CLEAN) {
        s_tenant = tenant;
    } else if (end == LAUNCH_HELD) {
        s_tenant = CORE1_TENANT_SERVICE; // what the hold's last release relaunches
    }
    core1_hw_unlock();
    return end;
}

// Launch the service on a launch already claimed. A disturbed launch is redone
// (bounded); a timeout is not, because it means a hold landed and that hold's
// release relaunches.
static bool launch_service_claimed(const char *what) {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        const bool         ok  = core1_hw_launch_service();
        const launch_end_t end = end_launch(ok, CORE1_TENANT_SERVICE);
        if (end == LAUNCH_HELD) {
            return true; // that hold's last release relaunches the service
        }
        if (end == LAUNCH_CLEAN) {
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

bool core1_release(void) {
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
    if (!launch) {
        return true;
    }
    return launch_service_claimed("release: RLE service relaunch timed out");
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
    const bool         ok  = core1_hw_launch(entry, stack_bottom, stack_bytes);
    const launch_end_t end = end_launch(ok, CORE1_TENANT_CUSTOM);
    if (end != LAUNCH_CLEAN) {
        // A hold killed the entry. Hand core1 back to the service: restore launches
        // it now if the hold is already released, or leaves it to the last release.
        core1_hw_report(end == LAUNCH_HELD ? "run: a hold landed during the launch" : "run: a hold and release landed during the launch");
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
        if (!claim_launch_ex(true)) {
            // Held: the last release relaunches the service. Launching: the
            // other launcher sets the tenant it launched.
            return false;
        }
        const bool         ok  = core1_hw_launch_service();
        const launch_end_t end = end_launch(ok, CORE1_TENANT_SERVICE);
        if (end == LAUNCH_HELD) {
            return false; // held: the last release relaunches the service
        }
        if (end == LAUNCH_CLEAN && ok) {
            return true;
        }
    }
    core1_hw_report("restore: RLE service relaunch failed 3 times");
    core1_hw_lock();
    if (!s_launching) {
        s_tenant = CORE1_TENANT_SERVICE; // the engine is gone either way
    }
    core1_hw_unlock();
    return false;
}
