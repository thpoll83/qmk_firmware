// Copyright 2026 Thomas Pollak
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime combos and tap dance (HID cmd 48, protocol v33). The record format and
// the tap-dance decision are in base/combo_store.h; this file holds the flash
// sector, the RAM tables QMK's engines read, and the copy to the other half.
//
// Compiled into every build except the DOOM monolith (POLYKYBD_DOOM without the
// pack flavour, rules.mk): the monolith is a developer build with ~3.3 KB of RAM
// left, and the release workflow still has to link it.
//
// RAM: QMK's combo and tap-dance engines read their tables through the weak hooks
// combo_count()/combo_get() and tap_dance_count()/tap_dance_get(), and both keep
// state inside the entries, so each entry is a full QMK struct in RAM. Measured on
// split72 against the same build without it: 1.26 KB for 16 + 16 entries with both
// engines (engines ~216 B, the stored records 336 B, combo_t + key lists 352 B,
// tap_dance_action_t 320 B, per-dance state 64 B). The shipped pack flavour had
// 8132 B free before and 6876 B after (local gcc; CI's shows ~450 B less).
//
// ⚠️ If RAM gets tight, about 800 B of this can go, at the cost of more code paths:
//   - read the records from the memory-mapped flash sector (XIP) instead of the
//     s_store copy (-336 B); a set then has to write the sector every time, or
//     keep only the slots being edited in RAM until the commit;
//   - point combo_t.keys into that sector instead of s_combo_keys (-160 B); a
//     4-key combo then needs a 0 after its keys, i.e. a format with a terminator;
//   - share ONE tap_dance_action_t across all dances and pick the record by index
//     in the callbacks (-300 B). This relies on QMK never holding two action
//     pointers at once, which is not a documented contract -- check
//     process_tap_dance.c after every upstream merge if this is done.
// The sector is only rewritten from the main loop, the same loop the combo and
// tap-dance engines run in, so pointers into XIP never see a half-written sector.

#include "poly_combos.h"

#include "quantum.h"
#include "base/combo_store.h"
#include "base/fw_staging.h"     // FW_COMBO_STORE_OFFSET
#include "base/core1_owner.h"    // core1_hold()/core1_release() around the write
#include "base/crash_record.h"   // CRASH_PHASE_FLASH breadcrumb
#include "polymod_crc32.h"
#include "bridge_helper.h"       // send_to_bridge()
#include "split_sync.h"          // dynamic_keymap_sync_t, POLY_KEYMAP_OP_COMBO_*
#include "transactions.h"        // USER_SYNC_DYNAMIC_KEYMAP_DATA

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <stddef.h>
#include <string.h>

// ---- RAM tables ---------------------------------------------------------------

// The records as stored (all slots, empty ones included) -- what cmd 48 reads back.
static poly_combo_store_t s_store;

// What QMK's combo engine reads: the active combos only, compacted. A combo with an
// empty key list would confuse the engine's key counting, so empty slots are left
// out rather than passed as zero-key combos.
static uint16_t s_combo_keys[POLY_COMBO_SLOTS][POLY_COMBO_MAX_KEYS + 1];   // + COMBO_END
static combo_t  s_combos[POLY_COMBO_SLOTS];
static uint8_t  s_combo_count = 0;

// Tap dance: one QMK action per slot, its record in s_store, and the keycode a held
// dance pressed, so on_reset releases exactly that one.
typedef struct {
    uint8_t  index;
    uint16_t held_kc;
} poly_td_state_t;

static tap_dance_action_t s_td_actions[POLY_TAP_DANCE_SLOTS];
static poly_td_state_t    s_td_state[POLY_TAP_DANCE_SLOTS];

// ---- QMK hooks (weak in quantum/keymap_introspection.c) -------------------------

uint16_t combo_count(void) {
    return s_combo_count;
}

combo_t *combo_get(uint16_t combo_idx) {
    return combo_idx < s_combo_count ? &s_combos[combo_idx] : NULL;
}

uint16_t tap_dance_count(void) {
    return POLY_TAP_DANCE_SLOTS;
}

tap_dance_action_t *tap_dance_get(uint16_t tap_dance_idx) {
    return tap_dance_idx < POLY_TAP_DANCE_SLOTS ? &s_td_actions[tap_dance_idx] : NULL;
}

// The per-dance term, or 0 to use the global tapping term (poly_keymap.c).
uint16_t poly_tap_dance_term(uint16_t keycode) {
    if (!IS_QK_TAP_DANCE(keycode)) return 0;
    const uint8_t i = (uint8_t)(keycode - QK_TAP_DANCE);
    return i < POLY_TAP_DANCE_SLOTS ? s_store.tap_dances[i].term_ms : 0;
}

// ---- Tap dance: one generic handler for every slot ------------------------------

static void td_key_down(uint16_t kc) {
    if (kc >= POLY_TD_KC_MO_MIN && kc <= POLY_TD_KC_MO_MAX) {
        layer_on(QK_MOMENTARY_GET_LAYER(kc));
    } else if (kc >= POLY_TD_KC_TO_MIN && kc <= POLY_TD_KC_TO_MAX) {
        layer_move(QK_TO_GET_LAYER(kc));
    } else if (kc >= POLY_TD_KC_TG_MIN && kc <= POLY_TD_KC_TG_MAX) {
        layer_invert(QK_TOGGLE_LAYER_GET_LAYER(kc));
    } else if (kc != 0 && kc <= POLY_TD_KC_BASIC_MAX) {
        register_code16(kc);
    }
}

static void td_key_up(uint16_t kc) {
    if (kc >= POLY_TD_KC_MO_MIN && kc <= POLY_TD_KC_MO_MAX) {
        layer_off(QK_MOMENTARY_GET_LAYER(kc));
    } else if (kc != 0 && kc <= POLY_TD_KC_BASIC_MAX) {
        unregister_code16(kc);
    }
    // TO and TG act on the press only.
}

static void td_finished(tap_dance_state_t *state, void *user_data) {
    poly_td_state_t *st = (poly_td_state_t *)user_data;
    const poly_td_plan_t plan = poly_td_plan(&s_store.tap_dances[st->index], state->count,
                                             state->pressed && !state->interrupted);
    for (uint8_t i = 0; i < plan.taps; i++) {
        td_key_down(plan.tap_kc);
        td_key_up(plan.tap_kc);
    }
    st->held_kc = plan.hold_kc;
    td_key_down(plan.hold_kc);
}

static void td_reset(tap_dance_state_t *state, void *user_data) {
    poly_td_state_t *st = (poly_td_state_t *)user_data;
    td_key_up(st->held_kc);
    st->held_kc = 0;
}

// ---- Building the engine tables from s_store -------------------------------------

static void rebuild_tables(void) {
    // The combo engine buffers pressed keys and keeps per-combo state; drop both
    // before the table under it changes. combo_disable() clears the buffers.
    const bool was_enabled = is_combo_enabled();
    if (was_enabled) combo_disable();

    s_combo_count = 0;
    for (uint8_t i = 0; i < POLY_COMBO_SLOTS; i++) {
        const poly_combo_rec_t *r = &s_store.combos[i];
        if (!poly_combo_rec_active(r)) continue;
        uint16_t *keys = s_combo_keys[s_combo_count];
        const uint8_t n = poly_combo_key_count(r);
        for (uint8_t k = 0; k < n; k++) keys[k] = r->keys[k];
        keys[n] = COMBO_END;
        memset(&s_combos[s_combo_count], 0, sizeof(combo_t));
        s_combos[s_combo_count].keys    = keys;
        s_combos[s_combo_count].keycode = r->result;
        s_combo_count++;
    }
    if (was_enabled) combo_enable();

    for (uint8_t i = 0; i < POLY_TAP_DANCE_SLOTS; i++) {
        s_td_state[i].index = i;
        // held_kc is kept: a dance that is held right now still releases its key.
        s_td_actions[i].fn.on_each_tap       = NULL;
        s_td_actions[i].fn.on_dance_finished = td_finished;
        s_td_actions[i].fn.on_reset          = td_reset;
        s_td_actions[i].fn.on_each_release   = NULL;
        s_td_actions[i].user_data            = &s_td_state[i];
    }
}

// ---- The flash sector ------------------------------------------------------------

static uint32_t store_crc(const poly_combo_store_t *st) {
    return crc32_1byte(&st->combos, (uint16_t)(sizeof(*st) - offsetof(poly_combo_store_t, combos)), 0);
}

static bool store_valid(const poly_combo_store_t *st) {
    if (st->magic != POLY_COMBO_STORE_MAGIC || st->version != POLY_COMBO_STORE_VERSION
        || st->combo_slots != POLY_COMBO_SLOTS || st->tap_dance_slots != POLY_TAP_DANCE_SLOTS
        || st->crc != store_crc(st)) {
        return false;
    }
    for (uint8_t i = 0; i < POLY_COMBO_SLOTS; i++) {
        if (!poly_combo_rec_valid(&st->combos[i])) return false;
    }
    for (uint8_t i = 0; i < POLY_TAP_DANCE_SLOTS; i++) {
        if (!poly_td_rec_valid(&st->tap_dances[i])) return false;
    }
    return true;
}

static void load_from_flash(void) {
    const poly_combo_store_t *flash = (const poly_combo_store_t *)(XIP_BASE + FW_COMBO_STORE_OFFSET);
    if (store_valid(flash)) {
        memcpy(&s_store, flash, sizeof(s_store));
    } else {
        // Erased (0xFF), an older format or a torn write: no entries.
        memset(&s_store, 0, sizeof(s_store));
    }
}

// Same shape as hand_stamp.c's stamp_flash(): core1 runs from XIP (Eden, RLE
// decode), so it is held for the erase and the programs.
static void write_to_flash(void) {
    s_store.magic           = POLY_COMBO_STORE_MAGIC;
    s_store.version         = POLY_COMBO_STORE_VERSION;
    s_store.combo_slots     = POLY_COMBO_SLOTS;
    s_store.tap_dance_slots = POLY_TAP_DANCE_SLOTS;
    s_store.reserved        = 0;
    s_store.reserved2       = 0;
    s_store.crc             = store_crc(&s_store);

    // flash_range_program() takes whole 256-byte pages from RAM. On the stack, as in
    // hand_stamp.c: this runs from the main loop only.
    uint8_t page[FLASH_PAGE_SIZE];
    const uint32_t pages = (sizeof(s_store) + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;

    uint32_t tag = crash_phase_enter(CRASH_PHASE_FLASH, 2);
    core1_hold();
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(FW_COMBO_STORE_OFFSET, FLASH_SECTOR_SIZE);
    for (uint32_t p = 0; p < pages; p++) {
        const uint32_t off = p * FLASH_PAGE_SIZE;
        const uint32_t n   = (sizeof(s_store) - off) < FLASH_PAGE_SIZE ? (sizeof(s_store) - off) : FLASH_PAGE_SIZE;
        memset(page, 0xFF, sizeof(page));
        memcpy(page, (const uint8_t *)&s_store + off, n);
        flash_range_program(FW_COMBO_STORE_OFFSET + off, page, FLASH_PAGE_SIZE);
    }
    restore_interrupts(irq);
    core1_release();
    crash_phase_leave(tag);
}

_Static_assert(sizeof(poly_combo_store_t) <= 4096u, "the combo store fits its sector");

// ---- Public API (combo_store.h) --------------------------------------------------

void poly_combos_init(void) {
    load_from_flash();
    rebuild_tables();
}

bool poly_combo_record_get(uint8_t kind, uint8_t index, uint8_t out[POLY_COMBO_REC_BYTES]) {
    if (kind == POLY_COMBO_KIND_COMBO && index < POLY_COMBO_SLOTS) {
        poly_combo_words_put(out, (const uint16_t *)&s_store.combos[index]);
        return true;
    }
    if (kind == POLY_COMBO_KIND_TAP_DANCE && index < POLY_TAP_DANCE_SLOTS) {
        poly_combo_words_put(out, (const uint16_t *)&s_store.tap_dances[index]);
        return true;
    }
    return false;
}

bool poly_combo_record_set(uint8_t kind, uint8_t index, const uint8_t in[POLY_COMBO_REC_BYTES]) {
    uint16_t w[POLY_COMBO_REC_WORDS];
    poly_combo_words_get(in, w);
    if (kind == POLY_COMBO_KIND_COMBO && index < POLY_COMBO_SLOTS) {
        poly_combo_rec_t r;
        memcpy(&r, w, sizeof(r));
        if (!poly_combo_rec_valid(&r)) return false;
        s_store.combos[index] = r;
    } else if (kind == POLY_COMBO_KIND_TAP_DANCE && index < POLY_TAP_DANCE_SLOTS) {
        poly_tap_dance_rec_t r;
        memcpy(&r, w, sizeof(r));
        if (!poly_td_rec_valid(&r)) return false;
        s_store.tap_dances[index] = r;
    } else {
        return false;
    }
    rebuild_tables();
    return true;
}

// Sends every record, then the commit, to the slave. Returns false when the link
// dropped a message; the slave then keeps its previous sector.
static bool bridge_all(void) {
    dynamic_keymap_sync_t msg;
    for (uint8_t kind = 0; kind < 2; kind++) {
        const uint8_t slots = kind == POLY_COMBO_KIND_COMBO ? POLY_COMBO_SLOTS : POLY_TAP_DANCE_SLOTS;
        for (uint8_t i = 0; i < slots; i++) {
            memset(&msg, 0, sizeof(msg));
            msg.commands[0] = POLY_KEYMAP_OP_COMBO_REC;
            msg.commands[1] = kind;
            msg.commands[2] = i;
            poly_combo_record_get(kind, i, &msg.commands[3]);
            const uint8_t payload = (uint8_t)(sizeof(uint32_t) + 3 + POLY_COMBO_REC_BYTES);
            if (!sync_succeeded(send_to_bridge(USER_SYNC_DYNAMIC_KEYMAP_DATA, &msg, payload, 3))) {
                return false;
            }
        }
    }
    memset(&msg, 0, sizeof(msg));
    msg.commands[0] = POLY_KEYMAP_OP_COMBO_COMMIT;
    return sync_succeeded(send_to_bridge(USER_SYNC_DYNAMIC_KEYMAP_DATA, &msg, sizeof(uint32_t) + 1, 3));
}

bool poly_combos_commit(void) {
    write_to_flash();
    return bridge_all();
}

// ---- The slave side ---------------------------------------------------------------
//
// ⚠️ The split handler runs on the slave's split-protocol thread, concurrently with
// its main loop. It validates each record and writes it into s_store (the slave
// never reads s_store while it is the slave: it processes no keys), and only flags
// the commit; the flash write happens in housekeeping (poly_combos_drain_pending()).
// There is no second copy of the store for the incoming records, to save its 336 B.
// A link that drops before the commit leaves this half's RAM records new and its
// flash old; the flash is what the next boot loads.

static volatile bool s_commit_pending = false;

void poly_combos_slave_record(uint8_t kind, uint8_t index, const uint8_t rec[POLY_COMBO_REC_BYTES]) {
    uint16_t w[POLY_COMBO_REC_WORDS];
    poly_combo_words_get(rec, w);
    if (kind == POLY_COMBO_KIND_COMBO && index < POLY_COMBO_SLOTS) {
        poly_combo_rec_t r;
        memcpy(&r, w, sizeof(r));
        if (poly_combo_rec_valid(&r)) s_store.combos[index] = r;
    } else if (kind == POLY_COMBO_KIND_TAP_DANCE && index < POLY_TAP_DANCE_SLOTS) {
        poly_tap_dance_rec_t r;
        memcpy(&r, w, sizeof(r));
        if (poly_td_rec_valid(&r)) s_store.tap_dances[index] = r;
    }
}

void poly_combos_slave_commit(void) {
    s_commit_pending = true;
}

void poly_combos_drain_pending(void) {
    if (!s_commit_pending) return;
    s_commit_pending = false;
    rebuild_tables();
    write_to_flash();
}
