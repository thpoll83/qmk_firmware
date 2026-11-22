// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Intl latin-variation picker, split out of poly_keymap.c. See latin_picker.h.
#include QMK_KEYBOARD_H

#include <string.h>

#include "latin_picker.h"
#include "poly_keymap_internal.h"   // latin_publish()
#include "keycode_helper.h"         // KC_LAT0..KC_LAT11
#include "lib/system/state.h"       // the latin table, access_local_layer()
#include "base/update.h"            // request_disp_refresh()
#include "base/font_lookup.h"       // kdisp_gfx_glyph()
#include "base/fontpack.h"          // g_all_fonts / g_all_font_count
#include "lang/lang_lut_ext.h"      // latin_ex_map, LATIN_EX_VARIATIONS

// ── Intl latin-variation picker: slots and pages ─────────────────────────────
//
// The picker row shows LATIN_PICKER_SLOTS variations at a time (12 on split72, 10
// on split42 — the variant headers) and pages through any beyond that.  What is
// STORED is always the absolute variation index, so the two page sizes cost
// nothing and a pick made on one variant reads back correctly on the other.

// ⚠️ KC_LAT0..KC_LAT9 are contiguous but KC_LAT10/KC_LAT11 are NOT — they were
// appended further down the enum so widening the cog range could not renumber
// KC_DAUTO/KC_IDDQD out from under the dynamic-keymap EEPROM (keycode_helper.h).
// So resolve a picker keycode HERE; never write `keycode - KC_LAT0` or a
// `KC_LAT0 ... KC_LAT11` case range.
int8_t latin_picker_slot(uint16_t keycode) {
    int8_t slot = -1;
    if (keycode >= KC_LAT0 && keycode <= KC_LAT9) slot = (int8_t)(keycode - KC_LAT0);
    else if (keycode == KC_LAT10)                 slot = 10;
    else if (keycode == KC_LAT11)                 slot = 11;
    // split42 wires only 10 slot keys, so 10/11 are unreachable there — but the
    // shared code is compiled for both, and a slot past the variant's row would
    // index the picker beyond what the keymap can ever show.
    return (slot < LATIN_PICKER_SLOTS) ? slot : -1;
}

// Rows are dense — the cog appends variations left to right and pads the tail with
// NULL — so the first NULL is the end.
uint8_t latin_variation_count(uint8_t row) {
    uint8_t n = 0;
    while (n < LATIN_EX_VARIATIONS && latin_ex_map[row][n] != NULL) {
        n++;
    }
    return n;
}

uint8_t latin_page_count(uint8_t row) {
    const uint8_t n = latin_variation_count(row);
    return (n <= LATIN_PICKER_SLOTS)
               ? 1
               : (uint8_t)((n + LATIN_PICKER_SLOTS - 1) / LATIN_PICKER_SLOTS);
}

// --- target slots and the assignment indirection ------------------------------
//
// A "target" is a key that can carry an Intl variation.  Today that is A..Z and
// slot == letter index, but the two are deliberately separate concepts: the slot
// addresses the STORAGE (which key), the letter addresses the TABLE (whose
// variations).  They diverge the moment a key is remapped -- which is the whole
// point, since French wants e, and two other keys, all showing forms of e.
int8_t latin_target_slot(uint16_t keycode) {
    if(keycode >= KC_A && keycode <= KC_Z) {
        return (int8_t)(keycode - KC_A);
    }
    // KC_MINUS .. KC_SLASH is a contiguous run of the twelve printable punctuation
    // keycodes, so no table. Whether a given board can actually REACH one is the
    // keymap's business: a position masked with KC_NO on _ADDLANG1 (split42's ')
    // or absent entirely (split42 has no [ ] `) simply never produces the keycode.
    if(keycode >= KC_MINUS && keycode <= KC_SLASH) {
        return (int8_t)(LATIN_LETTER_TARGETS + (keycode - KC_MINUS));
    }
    return -1;
}

// The base letter a target key hosts, or -1 for none. A letter defaults to itself;
// a punctuation key defaults to nothing, so it keeps typing its own symbol until
// it is deliberately mapped.
static int8_t latin_slot_letter(int8_t slot) {
    if(slot < 0) {
        return -1;
    }
    return latin_assign_get(get_global_latin_table()->assign, (uint8_t)slot);
}

// True when this key currently HOSTS a variation row.  Every letter does by
// default; a punctuation key only once it has deliberately been mapped, so an
// unmapped one keeps typing and drawing its own symbol on the Intl layer.  This
// is the gate the picker and the emit path use -- NOT `keycode >= KC_A`, which
// was the same test only while the letters were the only targets.
bool latin_has_row(uint16_t keycode) {
    return latin_slot_letter(latin_target_slot(keycode)) >= 0;
}

// The row the picker is currently showing, for the key last touched.  Both the
// render and the press path must agree on this, so it lives in one place.
// ⚠️ Precondition: latin_has_row(last_latin_keycode).  The clamp is only so a
// stray call can never index latin_ex_map out of bounds.
uint8_t latin_picker_row(uint16_t last_latin_keycode, bool upper_case) {
    const int8_t letter = latin_slot_letter(latin_target_slot(last_latin_keycode));
    return (uint8_t)((upper_case ? 0 : 26) + (letter < 0 ? 0 : letter));
}

// Absolute variation index for a picker slot on the currently-shown page, or -1
// when that slot is past the end of the row.
int8_t latin_picker_index(uint8_t row, uint8_t page, int8_t slot) {
    if (slot < 0) {
        return -1;
    }
    // ⚠️ Clamp, don't rely on the reset: the CASE selects a different row with its
    // own variation count and it can change while the picker is open (Shift/Caps),
    // which latin_picker_reset_page() does NOT cover — it only fires on a letter
    // change.  Shifting from a two-page row onto a one-page one then left the page
    // out of range, and every exit was blocked at once: every slot returned -1 (a
    // blank picker row), latin_page_count() said 1 so the arrows were blanked AND
    // the KC_LAT_PAGE_* handler's `pages > 1` guard did nothing.  Reachable today on
    // split42 (10 slots): lowercase y has 11 variations, uppercase Y has 10.
    // Clamping here rather than resetting on the case change keeps this the single
    // place the render path and the press path agree on — both go through it.
    const uint8_t pages = latin_page_count(row);
    if (page >= pages) {
        page = (uint8_t)(pages - 1);
    }
    const uint16_t idx = (uint16_t)page * LATIN_PICKER_SLOTS + (uint16_t)slot;
    return (idx < latin_variation_count(row)) ? (int8_t)idx : -1;
}

// Leave remap mode without changing anything.
void latin_remap_cancel(void) {
    poly_layer_t* ll = access_local_layer();
    ll->remap_mode   = LATIN_REMAP_OFF;
    ll->remap_target = 0;
    request_disp_refresh();
}

// Point `slot` at `letter`'s variation row, or back at its own letter when the two
// are the same — so re-picking a key's own letter is the per-key reset, with no
// separate gesture to learn.
void latin_remap_apply(uint8_t slot, uint8_t letter) {
    // ⚠️ The two bounds are deliberately DIFFERENT. Any target can be remapped, but
    // only a LETTER can be the source — a punctuation key hosts a letter's row,
    // never the reverse. The one exception is slot == letter, the "point this key
    // back at itself" clear, which is the only per-key way to unmap a punctuation
    // key (it has no own letter to re-pick).
    if(slot >= LATIN_TARGETS) {
        return;
    }
    if(letter >= LATIN_LETTER_TARGETS && letter != slot) {
        return;
    }
    latin_sync_t* table = access_global_latin_table();
    latin_assign_set(table->assign, slot, (slot == letter) ? LATIN_ASSIGN_NONE : letter);
    // The key now indexes a different row, so its stored pick is meaningless there
    // (and may be past the end of a shorter row). Start it at the first variation
    // rather than leaving a stale index for latin_variation() to fall back from.
    latin_pick_set(table->ex, latin_pick_field((int8_t)slot, true),  0);
    latin_pick_set(table->ex, latin_pick_field((int8_t)slot, false), 0);
    latin_publish();
    mark_latin_dirty();
}

// Clear every assignment. Reached by tapping the remap key on the Intl layer
// WITHOUT opening the mode — see process_record_user.
//
// ⚠️ Clears the picks of the REMAPPED slots only, not the whole ex[] array. A key
// that was never remapped still hosts its own letter, so its pick is still valid
// and is the user's own choice — wiping it would make "clear the assignments" also
// silently discard every accent anyone had ever chosen (caught in review, #206).
// A remapped slot's pick, by contrast, indexes the row it is losing and may be past
// the end of its own, so that one must go back to 0.
void latin_remap_reset_all(void) {
    latin_sync_t* table = access_global_latin_table();
    for(uint8_t slot = 0; slot < LATIN_TARGETS; slot++) {
        // The RAW field, not latin_assign_get(): the getter substitutes the key's
        // own letter for an unassigned slot, which is exactly the case to skip.
        if(latin_bits_get(table->assign, LATIN_ASSIGN_BYTES, slot) < LATIN_LETTER_TARGETS) {
            latin_pick_set(table->ex, latin_pick_field(slot, true),  0);
            latin_pick_set(table->ex, latin_pick_field(slot, false), 0);
        }
    }
    memset(table->assign, LATIN_ASSIGN_FILL, sizeof(table->assign));
    latin_publish();
    mark_latin_dirty();
    request_disp_refresh();
}

// Back to page 0.  Anything that makes the current page meaningless calls this:
// picking a variation, changing the letter (a one-page letter would otherwise show
// a whole row of blanks while page 1 was still selected), and leaving the layer.
void latin_picker_reset_page(void) {
    access_local_layer()->picker_page = 0;
}

// Resolve the latin variation a letter key should display/emit on the _ADDLANG1
// layer, honouring the persisted per-case pick in latin_sync_t.ex (high nibble =
// uppercase, low nibble = lowercase).
//
// Returns NULL when the letter has no variations at all.  A pick that lands on an
// empty slot falls back to variation 0 instead of handing back the NULL cell: the
// nibble can hold 0..15 while only LATIN_EX_VARIATIONS exist, and nothing
// re-validates a stored pick against a row that later shrank.  Feeding that NULL
// to kdisp_write_gfx_text() / register_unicode() reads address 0 — mapped boot ROM
// on RP2040, so no fault, just a garbage codepoint and junk on the keycap.  The
// picker now refuses to store an empty slot (see process_record_user), but a byte
// written before that guard — or a stale/garbage EEPROM — still arrives here.
const uint32_t* latin_variation(uint16_t keycode, bool upper_case) {
    const int8_t slot = latin_target_slot(keycode);
    if (slot < 0) {
        return NULL;
    }
    // ⚠️ Two DIFFERENT indices. The row comes from the letter this key HOSTS (its
    // own, unless reassigned); the pick comes from the KEY's own storage slot. They
    // must not be collapsed back into one: two keys assigned to the same letter
    // share a row but need independent picks -- that is the entire feature.
    const int8_t letter = latin_slot_letter(slot);
    if (letter < 0) {
        return NULL;                        // punctuation key, not mapped to anything
    }
    const uint8_t row = (uint8_t)((upper_case ? 0 : 26) + letter);
    if (latin_ex_map[row][0] == NULL) {
        return NULL;                        // this letter has no variations
    }
    const uint8_t   idx    = latin_pick_get(get_global_latin_table()->ex, latin_pick_field(slot, upper_case));
    const uint32_t* chosen = (idx < LATIN_EX_VARIATIONS) ? latin_ex_map[row][idx] : NULL;
    // Fall back when the pick names a slot this build does not have (a stale
    // EEPROM), and ALSO when it names a real variation whose GLYPH is missing --
    // the table is static but the font set is not, so a variation drawn from a
    // font-pack bundle disappears if that bundle is absent or older. Without this
    // the keycap renders blank and the key emits a character with no legend.
    if (chosen != NULL && kdisp_gfx_glyph(g_all_fonts, g_all_font_count, chosen[0]) == NULL) {
        chosen = NULL;
    }
    return (chosen != NULL) ? chosen : latin_ex_map[row][0];
}
