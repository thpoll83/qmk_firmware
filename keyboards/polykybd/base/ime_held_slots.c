// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ime_held_slots.h"

// A slot is LIVE only while its key is physically down. An early gate in
// process_record_user (the macro picker, a confirm prompt) can swallow a KC_IME
// release after clear_keyboard() has already emptied the report; the slot it leaves
// behind must not count as "another key still holds this usage", or every later
// release skips its unregister and Right Alt stays down (Greptile on #355).
static bool slot_live(const ime_slots_t* t, uint8_t i, const ime_slot_io_t* io) {
    return t->slot[i].usage != 0 && io->key_down(t->slot[i].row, t->slot[i].col);
}

// Drop slot i AND let go of its usage, unless a live slot still holds the same one
// (the report carries a usage once, so releasing it would drop it under the other
// finger). Retiring must release, not just forget: QMK updates the whole matrix
// before it delivers a scan's events in row/column order, so a slot can read as dead
// while its own release event is still queued behind this press. Forgetting it there
// left that release nothing to undo, and a usage registered before a language switch
// stayed down (Greptile on #355). Unregistering a usage the report no longer carries
// is a no-op.
static void slot_retire(ime_slots_t* t, uint8_t i, const ime_slot_io_t* io) {
    const uint8_t usage = t->slot[i].usage;
    t->slot[i].usage = 0;
    for (uint8_t j = 0; j < IME_HELD_SLOTS; ++j) {
        if (t->slot[j].usage == usage && slot_live(t, j, io)) return;
    }
    io->release(usage);
}

void ime_slots_forget_cleared(ime_slots_t* t, const ime_slot_io_t* io) {
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) {
        if (t->slot[i].usage != 0 && !io->in_report(t->slot[i].usage)) {
            t->slot[i].usage = 0;
        }
    }
}

void ime_slots_press(ime_slots_t* t, uint8_t row, uint8_t col, uint8_t usage, const ime_slot_io_t* io) {
    // Retire slots whose key is up (a swallowed release, or one still queued in this
    // scan), so they neither block a free slot nor keep a usage held for a key that is
    // up. A slot at THIS key's position is stale by definition: a key cannot be
    // pressed twice without a release in between.
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) {
        const bool here = t->slot[i].row == row && t->slot[i].col == col;
        if (t->slot[i].usage != 0 && (here || !slot_live(t, i, io))) slot_retire(t, i, io);
    }
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) {
        if (t->slot[i].usage == 0) {
            t->slot[i].row   = row;
            t->slot[i].col   = col;
            t->slot[i].usage = usage;
            io->press(usage);
            return;
        }
    }
    // More KC_IME keys down at once than there are slots: a held key nobody could
    // release would be stuck, so this press gets a tap instead -- unless a slot
    // already holds the same usage, whose release the tap would steal from the keys
    // still down (Greptile on #355).
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) {
        if (t->slot[i].usage == usage) return;
    }
    io->tap(usage);
}

void ime_slots_release(ime_slots_t* t, uint8_t row, uint8_t col, const ime_slot_io_t* io) {
    for (uint8_t i = 0; i < IME_HELD_SLOTS; ++i) {
        if (t->slot[i].usage != 0 && t->slot[i].row == row && t->slot[i].col == col) {
            slot_retire(t, i, io);
            return;
        }
    }
}
