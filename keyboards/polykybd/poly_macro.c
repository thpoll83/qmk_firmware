// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later

#include "poly_macro.h"

#include "quantum.h"
#include "eeprom.h"
#include "dynamic_keymap.h"
#include "nvm_eeprom_eeconfig_internal.h"   // EECONFIG_BASE_SIZE, via the keymap base
#include "send_string.h"

#include "base/macro_decode.h"
#include "split_sync.h"      // POLY_KEYMAP_OP_MACRO_LABEL, sync_succeeded()
#include "bridge_helper.h"   // send_to_bridge()
#include <transactions.h>    // USER_SYNC_DYNAMIC_KEYMAP_DATA
#include "polymod_crc32.h"

// The label array must not overlap the bodies, and both must stay inside the region
// QMK sized. If a later edit to POLY_MACRO_LABEL_LEN or the count breaks either, the
// build stops here rather than the labels quietly eating the last macro.
_Static_assert(POLY_MACRO_LABEL_ADDR == DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE,
               "label array must start exactly where the body buffer ends");
_Static_assert(POLY_MACRO_LABEL_ADDR + POLY_MACRO_LABEL_BYTES - 1 <= DYNAMIC_KEYMAP_EEPROM_MAX_ADDR,
               "label array runs past the end of EEPROM");
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE > 256,
               "body buffer has shrunk to the point where the feature is not worth shipping");
_Static_assert(POLY_MACRO_COUNT <= POLY_MACRO_NONE,
               "POLY_MACRO_NONE must not collide with a real macro id");

// ---------------------------------------------------------------------------
// EEPROM plumbing

static uint8_t body_read(uint16_t offset, void *ctx) {
    (void)ctx;
    return eeprom_read_byte((const uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset));
}

uint16_t poly_macro_capacity(void) {
    return (uint16_t)DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE;
}

uint16_t poly_macro_bytes_used(void) {
    return poly_macro_used(body_read, NULL, poly_macro_capacity());
}

void poly_macro_read(uint16_t offset, uint16_t size, uint8_t *out) {
    const uint16_t cap = poly_macro_capacity();
    for (uint16_t i = 0; i < size; i++) {
        out[i] = (offset + i < cap) ? body_read(offset + i, NULL) : 0;
    }
}

void poly_macro_write(uint16_t offset, uint16_t size, const uint8_t *data) {
    const uint16_t cap = poly_macro_capacity();
    if (cap == 0 || size == 0) return;

    // A window that does NOT carry the buffer's final byte invalidates it first, so a
    // stream that stops early reads as not-intact and poly_macro_start() refuses it.
    // Without this, an interrupted upload leaves a playable splice of the new text and
    // whatever preceded it -- which can promote the tail of a former macro (a password,
    // say) into a macro of its own.
    //
    // ⚠️ The HOST also raises a marker before it streams, and that is NOT redundant: it
    // is the tested half (a mocked write cannot exercise EEPROM), while this half is
    // what makes the guarantee hold for a host that never learned to. The firmware must
    // not depend on the host to arm its own integrity guard.
    //
    // The consequence to know: a deliberate PREFIX write leaves the buffer unplayable
    // until something writes the tail. That is correct -- a partially rewritten buffer
    // is exactly what must not play -- but it means a caller probing a prefix has to
    // restore the final byte too.
    if ((uint32_t)offset + size < cap) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + cap - 1),
                           POLY_MACRO_INCOMPLETE);
    }

    for (uint16_t i = 0; i < size; i++) {
        if (offset + i >= cap) return;
        eeprom_update_byte((uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset + i), data[i]);
    }
}

// ---------------------------------------------------------------------------
// Labels

// Push ONE macro's whole look to the slave over the dynamic-keymap transaction.
//
// The style, the icon and the caption travel together, so the slave can never render a
// caption in a style the master has already replaced -- that is the reason they are one
// record rather than three.
//
// Classified with sync_succeeded() rather than bool-tested: send_to_bridge returns the
// byte the slave SAID, and every possible return is non-zero, so `if(!send_to_bridge())`
// is dead code (the 2026-06-18 stuck-slave bug). Returning the verdict lets the caller
// keep the look queued and re-send, which is what makes the dirty mask a retry queue.
bool poly_macro_look_bridge(uint8_t id, const poly_macro_look_t *look) {
    dynamic_keymap_sync_t msg = {0};
    msg.commands[0] = POLY_KEYMAP_OP_MACRO_LABEL;
    msg.commands[1] = id;
    msg.commands[2] = look->style;
    for (uint8_t n = 0; n < POLY_MACRO_ICON_LEN; n++) {
        msg.commands[3 + n] = (uint8_t)((look->icon >> (8 * n)) & 0xFFu);
    }
    uint8_t n = 0;
    for (; n < POLY_MACRO_LABEL_LEN && look->text[n] != '\0'; n++) {
        msg.commands[2 + POLY_MACRO_LOOK_LEN - POLY_MACRO_LABEL_LEN + n] = (uint8_t)look->text[n];
    }
    // 2 header bytes + the full stride, so the payload size is constant and a shorter
    // caption cannot leave stale bytes from a previous send in the tail.
    const uint8_t payload = (uint8_t)(sizeof(uint32_t) + 2 + POLY_MACRO_LOOK_LEN);
    msg.crc32 = crc32_1byte(msg.commands, (uint8_t)(payload - sizeof(uint32_t)), 0);
    return sync_succeeded(send_to_bridge(USER_SYNC_DYNAMIC_KEYMAP_DATA, &msg, payload, 3));
}


// RAM cache, on both halves. It buys two things: the render path never touches EEPROM
// (it runs for every macro keycap on every refresh), and the slave -- whose own EEPROM
// never sees a macro -- has somewhere to put what the master pushes it.
//
// The caption, the style and the icon are ONE record rather than three arrays, so a
// render can never compose a caption with a style that was written at a different
// moment, and the whole appearance crosses the split link in a single message.
static poly_macro_look_t s_looks[POLY_MACRO_COUNT];
static uint16_t          s_label_dirty;   // one bit per macro, master -> slave queue

_Static_assert(POLY_MACRO_COUNT <= 16, "the dirty mask is 16 bits wide");

static uint16_t label_addr(uint8_t id) {
    return (uint16_t)(POLY_MACRO_LABEL_ADDR + (uint16_t)id * POLY_MACRO_LOOK_LEN);
}

// A style this build does not know draws as INDEX rather than as nothing. The index is
// the one style that needs neither a font pack nor a stored icon, so it is the safe
// floor -- the same reasoning as the glyph script falling back to the normal legend.
static uint8_t style_or_default(uint8_t style) {
    return style < POLY_MACRO_STYLE_COUNT ? style : (uint8_t)POLY_MACRO_STYLE_INDEX;
}

void poly_macro_labels_load(void) {
    for (uint8_t id = 0; id < POLY_MACRO_COUNT; id++) {
        const uint16_t base = label_addr(id);
        uint8_t raw[POLY_MACRO_LOOK_LEN];
        for (uint8_t n = 0; n < POLY_MACRO_LOOK_LEN; n++) {
            raw[n] = eeprom_read_byte((const uint8_t *)(uintptr_t)(base + n));
        }
        s_looks[id].style = style_or_default(raw[0]);
        // Little-endian, matching the wire. An unwritten record reads as all-zero
        // (QMK's wear levelling normalises a cleared byte to 0, NOT 0xFF -- the fact
        // that made latin_assign read as "every key hosts 'a'"), which is exactly the
        // default look, so no migration sentinel is needed here.
        s_looks[id].icon = (uint32_t)raw[1] | ((uint32_t)raw[2] << 8)
                         | ((uint32_t)raw[3] << 16) | ((uint32_t)raw[4] << 24);
        for (uint8_t n = 0; n < POLY_MACRO_LABEL_LEN; n++) {
            s_looks[id].text[n] = (char)raw[1 + POLY_MACRO_ICON_LEN + n];
        }
        s_looks[id].text[POLY_MACRO_LABEL_LEN] = '\0';
    }
}

void poly_macro_look_get(uint8_t id, poly_macro_look_t *out) {
    if (out == NULL) return;
    if (id >= POLY_MACRO_COUNT) {
        out->style = POLY_MACRO_STYLE_INDEX;
        out->icon  = 0;
        out->text[0] = '\0';
        return;
    }
    *out = s_looks[id];
    out->text[POLY_MACRO_LABEL_LEN] = '\0';
}

void poly_macro_label_get(uint8_t id, char *out) {
    poly_macro_look_t look;
    poly_macro_look_get(id, &look);
    uint8_t n = 0;
    for (; n < POLY_MACRO_LABEL_LEN && look.text[n] != '\0'; n++) {
        out[n] = look.text[n];
    }
    out[n] = '\0';
}

// Normalises into the cache: ASCII only, NUL-padded to the full stride, style bounded.
// Shared by the master (which then persists) and the slave (which does not), so the two
// halves can never disagree about what a given macro looks like.
static void look_store(uint8_t id, const poly_macro_look_t *look) {
    s_looks[id].style = style_or_default(look != NULL ? look->style
                                                      : (uint8_t)POLY_MACRO_STYLE_INDEX);
    s_looks[id].icon  = look != NULL ? look->icon : 0u;
    uint8_t n = 0;
    if (look != NULL) {
        for (const char *p = look->text; *p && n < POLY_MACRO_LABEL_LEN; ++p) {
            // The _Nano_ face is 0x20..0x7E. Anything else draws nothing, which on a
            // keycap is indistinguishable from a bug -- so it never gets stored.
            if ((uint8_t)*p < 0x20 || (uint8_t)*p > 0x7E) continue;
            s_looks[id].text[n++] = *p;
        }
    }
    for (; n <= POLY_MACRO_LABEL_LEN; n++) {
        s_looks[id].text[n] = '\0';
    }
}

void poly_macro_look_set(uint8_t id, const poly_macro_look_t *look) {
    if (id >= POLY_MACRO_COUNT) return;
    look_store(id, look);
    const uint16_t base = label_addr(id);
    const uint32_t icon = s_looks[id].icon;
    eeprom_update_byte((uint8_t *)(uintptr_t)(base + 0), s_looks[id].style);
    for (uint8_t n = 0; n < POLY_MACRO_ICON_LEN; n++) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(base + 1 + n),
                           (uint8_t)((icon >> (8 * n)) & 0xFFu));
    }
    for (uint8_t n = 0; n < POLY_MACRO_LABEL_LEN; n++) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(base + 1 + POLY_MACRO_ICON_LEN + n),
                           (uint8_t)s_looks[id].text[n]);
    }
    s_label_dirty |= (uint16_t)1u << id;
}

void poly_macro_look_adopt(uint8_t id, const poly_macro_look_t *look) {
    if (id >= POLY_MACRO_COUNT) return;
    look_store(id, look);
}

void poly_macro_labels_mark_all_dirty(void) {
    s_label_dirty = (uint16_t)((1u << POLY_MACRO_COUNT) - 1u);
}

bool poly_macro_label_sync_tick(void) {
    if (s_label_dirty == 0) return false;
    for (uint8_t id = 0; id < POLY_MACRO_COUNT; id++) {
        const uint16_t bit = (uint16_t)1u << id;
        if (!(s_label_dirty & bit)) continue;
        poly_macro_look_t look;
        poly_macro_look_get(id, &look);
        // Clear the bit only on a real ACK: send_to_bridge returns what the slave SAID,
        // and every return value is non-zero, so it must be classified rather than
        // bool-tested. On a give-up the bit stays set and the next pass re-sends -- the
        // dirty mask IS the retry queue, the same shape the state diff uses.
        if (poly_macro_look_bridge(id, &look)) {
            s_label_dirty &= (uint16_t)~bit;
        }
        return true;   // at most one bridge per housekeeping pass
    }
    return false;
}

void poly_macro_reset_all(void) {
    poly_macro_abort();
    for (uint16_t i = 0; i < (uint16_t)DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; i++) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + i), 0);
    }
    for (uint16_t i = 0; i < (uint16_t)POLY_MACRO_LABEL_BYTES; i++) {
        eeprom_update_byte((uint8_t *)(uintptr_t)(POLY_MACRO_LABEL_ADDR + i), 0);
    }
    // The EEPROM is only half of it: both halves RENDER from the RAM cache, so leaving
    // it populated makes poly_macro_look_get() and render_macro_key() keep drawing the
    // look of a macro that no longer exists -- and the pending dirty bits would then
    // push those stale looks to the slave. Marking all dirty is what sends the cleared
    // ones across.
    memset(s_looks, 0, sizeof(s_looks));
    poly_macro_labels_mark_all_dirty();
}

// ---------------------------------------------------------------------------
// Playback

static bool     s_active;
static uint16_t s_cursor;
static uint32_t s_resume_ref;
static uint16_t s_resume_ms;

bool poly_macro_active(void) {
    return s_active;
}

void poly_macro_abort(void) {
    if (!s_active) return;
    s_active = false;
    // Same rule as the firmware-confirm prompt and doom_begin(): anything that stops
    // producing key events while a key may be registered has to clear the report, or
    // the host keeps the keycode down and auto-repeats it until USB drops.
    clear_keyboard();
}

bool poly_macro_start(uint8_t id) {
    poly_macro_abort();
    if (id >= POLY_MACRO_COUNT) return false;

    const uint16_t cap = poly_macro_capacity();
    // A buffer whose last byte is not NUL was cut short mid-write. Playing it would
    // type an arbitrary prefix of whatever was being uploaded.
    if (!poly_macro_buffer_intact(body_read, NULL, cap)) return false;

    uint16_t start = poly_macro_find(body_read, NULL, id, cap);
    if (start >= cap) return false;
    if (body_read(start, NULL) == 0) return false; // empty macro: nothing to play

    s_active     = true;
    s_cursor     = start;
    s_resume_ref = timer_read32();
    s_resume_ms  = 0;
    return true;
}

void poly_macro_tick(void) {
    if (!s_active) return;
    if (timer_elapsed32(s_resume_ref) < s_resume_ms) return;

    poly_macro_step_t step = poly_macro_decode(body_read, NULL, s_cursor, poly_macro_capacity());

    switch (step.kind) {
        case POLY_MACRO_STEP_CHAR: {
            // send_char() is the same translation table dynamic_keymap_macro_send()
            // would use, so a body typed here and a body played by QMK agree.
            char s[2] = {(char)step.code, '\0'};
            send_string(s);
            break;
        }
        case POLY_MACRO_STEP_TAP:
            tap_code(step.code);
            break;
        case POLY_MACRO_STEP_DOWN:
            register_code(step.code);
            break;
        case POLY_MACRO_STEP_UP:
            unregister_code(step.code);
            break;
        case POLY_MACRO_STEP_DELAY:
            // The whole reason this is a state machine: the wait is a deadline, not a
            // busy loop, so the main loop keeps scanning while it runs.
            s_cursor     = step.next;
            s_resume_ref = timer_read32();
            s_resume_ms  = step.ms;
            return;
        case POLY_MACRO_STEP_END:
        default:
            // Reached the terminator, or the body is malformed from here on. Either
            // way stop, and release anything a DOWN step left registered.
            poly_macro_abort();
            return;
    }

    s_cursor     = step.next;
    s_resume_ref = timer_read32();
    s_resume_ms  = POLY_MACRO_STEP_MS;
}
