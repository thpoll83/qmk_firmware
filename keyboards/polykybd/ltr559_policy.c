// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ltr559_policy.h"

#ifdef POLYKYBD_LTR559_DRIVE

#    include "quantum.h"
#    include <transactions.h>
#    include <string.h>

#    include "print.h"
#    include "state.h"
#    include "bridge_helper.h"    // is_usb_host_side()
#    include "poly_keymap.h"      // reset_idle_jitter()
#    include "base/com.h"         // STATUS_DISP_ON / DISP_IDLE flags
#    include "base/update.h"      // update_performed() / request_disp_refresh()
#    include "doom/doom_mode.h"   // screensaver teardown (inline no-ops without POLYKYBD_DOOM)
#    include "slave_data.h"       // SLAVE_DATA_SENSOR (the pull channel lives there)

#    ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
#        include "polymod_ltr559.h"
#    endif

// --- LTR-559 auto-brightness + idle-inhibit (opt-in; needs hardware tuning) -----
//
// The sensor is just another SOURCE feeding the existing auto-brightness path:
// the 5 s-average lux is mapped to a contrast and pushed through
// set_auto_brightness_value() — exactly the volatile/host-auto channel the PC
// host uses — so the resulting contrast reaches the slave over the existing
// poly_sync_t brightness transport.
//
// Brightness and idle are master-authoritative, so the decision runs on the
// master. The sensor lives on the RIGHT half, so:
//   * master IS the right half  -> read the sensor locally
//   * master is the LEFT half   -> pull {avg lux, proximity} from the slave over
//     the generic USER_SYNC_SLAVE_DATA channel (kind = SLAVE_DATA_SENSOR) — the
//     split slot freed by the FW_UP transaction consolidation.
// Either way driving works regardless of which half USB is plugged into.
// All tunables below are first-cut guesses to be dialled in against the OLED.
// Proximity (0..2047) that counts as "close" -> wake / inhibit idle. Measured on
// hardware: hand at ~5 cm 400, ~1 cm 1000, hole fully covered ~2000 (saturated).
// The resting (nothing near) baseline depends on the housing: ~129 on the open
// bench, ~325 once mounted in the enclosure (its walls reflect IR back). 350 was
// kept as the wake point (wakes as a hand comes within ~5-6 cm). NOTE: with the
// housed ~325 baseline the margin is only ~25 counts — raise toward 400 if the
// mounted sensor ever self-triggers; lower toward the baseline for an earlier wake.
#    define LTR559_NEAR_THRESHOLD 350
#    define LTR559_DRIVE_MS 500         // how often the master samples + applies
#    define LTR559_LUX_FULL_REF 100     // avg lux mapped to FULL_BRIGHT (ceiling)
                                        // Tuned on hardware: a ~28 lux office reads
                                        // B≈26 (was B≈19 at 200), matching the level
                                        // the user set by hand. Curve (sqrt): ~4 in a
                                        // dark room, 26 @ 28 lux, 35 @ 50 lux, full @ 100+.
#    define LTR559_MIN_CONTRAST 4       // auto-brightness floor — the sensor never
                                        // drives below this. 4 = a dim but visible
                                        // night level; still clear of the near-off
                                        // B=1/DISP_OFF. (The power-on dark-screen was a
                                        // separate boot transient, fixed by the
                                        // don't-engage-until-first-reading guard below.)

// The slave->master pull channel itself (USER_SYNC_SLAVE_DATA, op-dispatched on a
// `kind` byte) lives in slave_data.c; this file only supplies the SENSOR payload.
typedef struct {
    uint16_t lux;   // 5 s-average lux
    uint16_t prox;  // latest raw proximity (0..2047)
} ltr559_sync_t;

// Slave side: fill the reply for SLAVE_DATA_SENSOR, bounded by out_len.
void poly_ltr559_slave_sensor_reply(void* out_data, uint8_t out_len) {
    ltr559_sync_t s = { ltr559_avg_lux(), ltr559_prox() };
    if (out_len >= sizeof(s)) {
        memcpy(out_data, &s, sizeof(s));
    }
}

// Master-side: mirror the HID cmd-15 stop-idle path to force the displays awake.
// Logged because this is the ONLY wake with no user-visible trigger — a proximity
// wake used to restart the whole idle countdown silently, so the console showed a
// second "Transition to idle" with nothing explaining the first one ending.
static void poly_force_wake(void) {
    poly_sync_t* local_state = access_local_state();
    // The DOOM attract screensaver (IDLE_STYLE_IDDQD) runs with STATUS_DISP_ON SET
    // and DISP_IDLE CLEARED — it owns the keycaps at active brightness via
    // doom_tick(), not through the DISP_IDLE pulse path. So it matches NEITHER
    // branch below, and proximity would be a silent no-op over it while pulse /
    // jitter / eden (all DISP_IDLE) and full suspend wake normally — the reported
    // "doom idle doesn't react to the proximity sensor, the other idle modes do".
    // Tear it down exactly like a keypress does (doom_exit restores the legends and
    // stamps a fresh last_update); only the screensaver, never an active game.
    if (doom_mode_screensaver()) {
        uprint("Wake by proximity (from doom screensaver)\n");
        doom_screensaver_stop();
        update_performed();
        return;
    }
    if ((local_state->flags & (STATUS_DISP_ON | DISP_IDLE)) == 0) {
        uprint("Wake by proximity (from suspend)\n");
        suspend_wakeup_init_kb();   // fully suspended -> full wake
    } else if (local_state->flags & DISP_IDLE) {
        uprint("Wake by proximity (from idle)\n");
        local_state->contrast = get_active_brightness();
        local_state->flags &= ~((uint8_t)DISP_IDLE);
        local_state->flags |= STATUS_DISP_ON;
        reset_idle_jitter();
        request_disp_refresh();
        update_performed();
    }
}

static uint32_t isqrt32(uint32_t x) {
    uint32_t r = 0, b = 1UL << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return r;
}

static uint8_t lux_to_contrast(uint16_t lux) {
    // Perceptual (sqrt) curve: brightness rises quickly out of the dark and eases
    // toward the ceiling, so ordinary indoor light already gives a usable level
    // instead of the near-off B=2 a linear map produced. LTR559_LUX_FULL_REF is
    // the lux that reaches FULL_BRIGHT. (×100 before the sqrt for resolution.)
    uint32_t sref = isqrt32((uint32_t)LTR559_LUX_FULL_REF * 100u);
    uint32_t slux = isqrt32((uint32_t)lux * 100u);
    if (slux >= sref) {
        return FULL_BRIGHT;
    }
    uint32_t c = MIN_BRIGHT + ((uint32_t)(FULL_BRIGHT - MIN_BRIGHT) * slux) / sref;
    if (c < LTR559_MIN_CONTRAST) c = LTR559_MIN_CONTRAST;  // never near-off
    if (c > FULL_BRIGHT) c = FULL_BRIGHT;
    return (uint8_t)c;
}

void poly_ltr559_drive(void) {
    static uint32_t last        = 0;
    static bool     engaged     = false;
    static uint8_t  last_logged = 0xFF;   // last brightness announced to the console
    if (!is_usb_host_side()) {
        return;   // decisions are master-only (the slave just serves reads)
    }
    if (timer_elapsed32(last) < LTR559_DRIVE_MS) {
        return;
    }
    last = timer_read32();

    // Brightness/idle decisions are master-only; the sensor is auto-detected on
    // whichever half it's soldered to.
    uint16_t lux, prox;
    if (ltr559_available()) {
        // The master itself has the sensor — read locally.
        lux  = ltr559_avg_lux();
        prox = ltr559_prox();
    } else {
        // Sensor is on the slave (right) half — pull its latest values up over the
        // generic slave->master channel (kind = SLAVE_DATA_SENSOR), so driving
        // works in either USB orientation.
        uint8_t       kind = SLAVE_DATA_SENSOR;
        ltr559_sync_t s;
        if (!transaction_rpc_exec(USER_SYNC_SLAVE_DATA, sizeof(kind), &kind, sizeof(s), &s)) {
            return;   // slave busy this round; try again next tick
        }
        lux  = s.lux;
        prox = s.prox;
    }

    // Auto-brightness from the 5 s average lux, via the same volatile/host-auto
    // path the host uses (keeps the manual brightness untouched).
    //
    // Don't engage until the sensor has produced a real reading: for the first
    // ~1 s after boot the 5 s average is still 0 (no samples), and engaging then
    // would yank the displays down to the floor. Hold at the manual/restored
    // brightness until the first non-zero average, then engage. Once engaged we
    // keep applying — a genuine dark-room 0 is floored by lux_to_contrast (never
    // off), so a momentary 0 can't blank the keys.
    if (!engaged) {
        if (lux == 0) {
            return;
        }
        set_brightness_auto_mode(true);
        engaged = true;
        uprintf("LTR-559: auto-brightness engaged (avg lux %u)\n", lux);
    }

    // Announce every brightness the sensor drives — but only when it actually
    // changes: this runs every LTR559_DRIVE_MS (500 ms), so an unconditional print
    // would drown the console. Without it a sensor-driven brightness move is
    // indistinguishable from a key/host change (or from a bug) in the log.
    const uint8_t target = lux_to_contrast(lux);
    if (target != last_logged) {
        last_logged = target;
        uprintf("LTR-559: auto brightness -> %u (avg lux %u)%s\n", target, lux,
                get_brightness_auto_mode() ? "" : " [auto off, not applied]");
    }
    set_auto_brightness_value(target);

    // Proximity: something is close -> defer idle (and wake if already idle).
    if (prox > LTR559_NEAR_THRESHOLD) {
        poly_force_wake();
        update_performed();
    }
}
#endif  // POLYKYBD_LTR559_DRIVE
