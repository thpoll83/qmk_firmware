// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "poly_macro_record.h"

#include "quantum.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "nvm_eeprom_eeconfig_internal.h"   // EECONFIG_BASE_SIZE, via the keymap base
#include "host.h"
#include "host_driver.h"
#include "report.h"

#include "poly_macro.h"
#include "state.h"
#include "base/update.h"

// The report shapes this file diffs are QMK's, and base/macro_record.c takes plain
// bytes so it can be tested without them. If either ever stops matching, the diff
// would read past the end of a report -- so pin them here rather than trusting the
// two constants to stay in step by luck.
_Static_assert(KEYBOARD_REPORT_KEYS == POLY_MACRO_REC_KRO_KEYS,
               "the 6KRO key array changed size; base/macro_record.c must follow");
_Static_assert(NKRO_REPORT_BITS == POLY_MACRO_REC_NKRO_BYTES,
               "the NKRO bitmap changed size; base/macro_record.c must follow");

// How long "Saved Mn" stays on the status OLED. Long enough to read, short enough that
// nobody waits for it.
#define POLY_REC_SAVED_MS 1200
// Bytes spliced per housekeeping pass. The move is up to the whole ~2.2 KB body region
// and every byte is an eeprom_update_byte, so doing it in one go is a bulk EEPROM
// operation on the main loop -- which stalls the split UART and makes the other half
// look dead. This is the same reason the overlay-mapping repair drains 2 reports a tick.
#define POLY_REC_COMMIT_CHUNK 32

// ---------------------------------------------------------------------------
// State

static uint8_t             s_state = POLY_REC_IDLE;
static uint8_t             s_slot  = POLY_MACRO_NONE;
static uint32_t            s_started_ms;
static uint32_t            s_saved_ms;
static uint8_t             s_buf[POLY_MACRO_REC_BYTES];
static poly_macro_rec_t    s_rec;
static poly_macro_commit_t s_commit;
static uint16_t            s_body_len;

// The last report we saw, so the next one can be diffed against it. Zeroed at the
// start of every recording rather than kept across sessions: the first report of a
// recording must diff against "nothing held", or a modifier that was already down when
// REC was pressed is never recorded as pressed and its release closes a DOWN that does
// not exist.
static uint8_t s_prev_mods;
static uint8_t s_prev_keys[POLY_MACRO_REC_KRO_KEYS];
static uint8_t s_prev_bits[POLY_MACRO_REC_NKRO_BYTES];

enum poly_rec_state poly_macro_rec_state(void) { return (enum poly_rec_state)s_state; }
uint8_t             poly_macro_rec_slot(void)  { return s_slot; }
uint16_t            poly_macro_rec_bytes(void) { return s_rec.len; }

uint32_t poly_macro_rec_elapsed_ms(void) {
    return (s_state == POLY_REC_RECORDING) ? timer_elapsed32(s_started_ms) : 0;
}

// ---------------------------------------------------------------------------
// Capture shim

// The driver the USB stack installed, kept so every report is FORWARDED. The shim
// records and forwards, so it cannot lose a keystroke by construction -- that is the
// property that makes capturing at the report altitude safe at all.
static host_driver_t *s_real;
static host_driver_t  s_shim;

static void feed(uint8_t code, bool pressed, void *ctx) {
    (void)ctx;
    poly_macro_rec_key(&s_rec, code, pressed, timer_read32());
}

static uint8_t shim_leds(void) {
    return s_real->keyboard_leds();
}

static void shim_send_keyboard(report_keyboard_t *report) {
    if (s_state == POLY_REC_RECORDING && report != NULL) {
        poly_macro_rec_diff_6kro(s_prev_mods, s_prev_keys,
                                 report->mods, report->keys, feed, NULL);
        s_prev_mods = report->mods;
        memcpy(s_prev_keys, report->keys, sizeof(s_prev_keys));
    }
    s_real->send_keyboard(report);
}

static void shim_send_nkro(report_nkro_t *report) {
    if (s_state == POLY_REC_RECORDING && report != NULL) {
        poly_macro_rec_diff_nkro(s_prev_mods, s_prev_bits,
                                 report->mods, report->bits, feed, NULL);
        s_prev_mods = report->mods;
        memcpy(s_prev_bits, report->bits, sizeof(s_prev_bits));
    }
    s_real->send_nkro(report);
}

static void shim_send_mouse(report_mouse_t *report)  { s_real->send_mouse(report); }
static void shim_send_extra(report_extra_t *report)  { s_real->send_extra(report); }
#ifdef RAW_ENABLE
static void shim_send_raw_hid(uint8_t *data, uint8_t length) {
    s_real->send_raw_hid(data, length);
}
#endif

// Installed ONCE and left in place for the life of the board, rather than swapped in
// at the start of a recording and out at the end. Two reasons, and the second is the
// binding one: a report can be in flight across the swap, and a driver pointer that
// changes under the USB stack is a race for no benefit -- the shim costs one branch
// per report when idle. It also means there is exactly one moment the install can go
// wrong, instead of one per recording.
static void install_shim(void) {
    host_driver_t *cur = host_get_driver();
    if (cur == NULL || cur == &s_shim) return;   // not up yet, or already ours
    s_real            = cur;
    s_shim            = *cur;                    // copy, so anything we do not wrap is kept
    s_shim.keyboard_leds = shim_leds;
    s_shim.send_keyboard = shim_send_keyboard;
    s_shim.send_nkro     = shim_send_nkro;
    s_shim.send_mouse    = shim_send_mouse;
    s_shim.send_extra    = shim_send_extra;
#ifdef RAW_ENABLE
    s_shim.send_raw_hid  = shim_send_raw_hid;
#endif
    host_set_driver(&s_shim);
}

// ---------------------------------------------------------------------------
// EEPROM access for the splice

static uint8_t body_read(uint16_t offset, void *ctx) {
    (void)ctx;
    return eeprom_read_byte((const uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset));
}

static void body_write(uint16_t offset, uint8_t value, void *ctx) {
    (void)ctx;
    eeprom_update_byte((uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset), value);
}

// ---------------------------------------------------------------------------
// Gesture

static void enter_idle(void) {
    s_state = POLY_REC_IDLE;
    s_slot  = POLY_MACRO_NONE;
    request_disp_refresh();
}

void poly_macro_rec_cancel(void) {
    if (s_state == POLY_REC_IDLE) return;
    // A cancel during the SPLICE would leave the buffer marked incomplete and half
    // moved, so it is refused there -- the pump is bounded and finishes in a few
    // passes anyway.
    if (s_state == POLY_REC_SAVING) return;
    enter_idle();
}

void poly_macro_rec_toggle(void) {
    switch (s_state) {
        case POLY_REC_IDLE:
            s_state = POLY_REC_PICKING;
            s_slot  = POLY_MACRO_NONE;
            request_disp_refresh();
            break;
        case POLY_REC_PICKING:
            enter_idle();            // pressing REC again backs out of the picker
            break;
        case POLY_REC_RECORDING:
            poly_macro_rec_stop();
            break;
        default:
            break;                   // SAVING/SAVED own themselves
    }
}

void poly_macro_rec_pick(uint8_t id) {
    if (s_state != POLY_REC_PICKING || id >= POLY_MACRO_COUNT) return;

    // Start from "nothing held". The keys that opened the gesture are up by now (the
    // pick happens on a release edge), but a modifier the user is still holding would
    // otherwise never be seen going down and its release would close a DOWN that was
    // never recorded.
    s_prev_mods = 0;
    memset(s_prev_keys, 0, sizeof(s_prev_keys));
    memset(s_prev_bits, 0, sizeof(s_prev_bits));

    poly_macro_rec_begin(&s_rec, s_buf, sizeof(s_buf));
    s_slot       = id;
    s_started_ms = timer_read32();
    s_state      = POLY_REC_RECORDING;
    request_disp_refresh();
}

void poly_macro_rec_stop(void) {
    if (s_state != POLY_REC_RECORDING) return;
    s_body_len = poly_macro_rec_finish(&s_rec);

    if (s_body_len == 0) {
        // Nothing was typed. Storing an empty body would clear whatever was in the
        // slot, which is a destructive answer to an accident -- so a recording with no
        // keystrokes simply leaves the macro alone.
        enter_idle();
        return;
    }

    const poly_macro_splice_result_t plan =
        poly_macro_commit_begin(&s_commit, body_read, body_write, NULL,
                                s_slot, s_buf, s_body_len, poly_macro_capacity());
    if (plan != POLY_MACRO_SPLICE_OK) {
        // Refused before a byte was written -- the shared pool is full. The recording
        // is lost, which is the honest outcome: the alternative is evicting somebody
        // else's macro to make room.
        uprintf("macro rec: slot %u does not fit (%u B)\n", s_slot, s_body_len);
        enter_idle();
        return;
    }
    s_state = POLY_REC_SAVING;
    request_disp_refresh();
}

// ---------------------------------------------------------------------------
// Pump

void poly_macro_rec_tick(void) {
    install_shim();

    switch (s_state) {
        case POLY_REC_RECORDING:
            // Hold the idle timer off, exactly as the FW-2 prompt does: without this
            // the panel swaps to the logos and the keycaps fade mid-recording, and
            // update_displays early-returns once DISP_IDLE is set so neither comes
            // back until a keypress.
            update_performed();
            // A full buffer stops the recording rather than silently dropping the rest
            // of what is typed. The encoder reserved room to close every held key, so
            // the body is still valid.
            if (poly_macro_rec_full(&s_rec)) {
                uprintf("macro rec: buffer full, stopping at %u B\n", s_rec.len);
                poly_macro_rec_stop();
            }
            break;

        case POLY_REC_SAVING:
            update_performed();
            if (!poly_macro_commit_step(&s_commit, POLY_REC_COMMIT_CHUNK)) {
                // The host caches macros; nothing else would tell it the board just
                // wrote one. One counter on a reply it already polls every second.
                poly_state_touch();
                s_saved_ms = timer_read32();
                s_state    = POLY_REC_SAVED;
                request_disp_refresh();
            }
            break;

        case POLY_REC_SAVED:
            update_performed();
            if (timer_elapsed32(s_saved_ms) >= POLY_REC_SAVED_MS) enter_idle();
            break;

        case POLY_REC_PICKING:
            update_performed();
            break;

        default:
            break;
    }
}
