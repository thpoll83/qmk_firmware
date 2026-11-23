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

// ---- the Intl keys in process_record_user() ----------------------------------------
// Moved from poly_keymap.c. Each returns true when it decided the event, with the value
// process_record_user() must return in *ret, and false to let the event go on.

// True only while the Ctrl the picker is riding on was registered BY the latch, so
// dropping it on layer exit can never release a Ctrl the user is really holding.
static bool s_picker_latched = false;
bool latin_picker_latched(void) {
    return s_picker_latched;
}

// Drop the picker's Ctrl latch, if WE hold it. Gated on ownership, never on the key:
// releasing a Ctrl the user is really holding would leave it stuck (docs/features/intl-layer.md).
void latin_picker_latch_release(void) {
    if (s_picker_latched) {
        unregister_mods(MOD_MASK_CTRL);
        s_picker_latched = false;
    }
}

// The remap prompt, the remap key, and the swallowed release of the Ctrl tap that
// armed the latch. process_record_user() calls this before its settings gate.
bool latin_picker_record_early(uint16_t keycode, keyrecord_t *record, bool addlang, bool *ret) {
    // --- Intl letter remap ----------------------------------------------------
    // Runs BEFORE the letter / picker handling below, because in remap mode a
    // letter press means "this one", not "emit your variation".
    if(addlang && access_local_layer()->remap_mode != LATIN_REMAP_OFF) {
        // ⚠️ Modifiers and LAYER keys must fall through — this block used to
        // `return false` for every release, which swallowed the release of
        // MO(_ADDLANG1) ITSELF.  QMK therefore never unregistered the layer: Intl
        // went down and never came back up, so releasing it did not leave the
        // prompt and there was no way out of the mode (field).  A held Shift was
        // stuck the same way.  This is the picker's documented "gate the swallow on
        // OWNERSHIP, not on the keycode" rule, which this block ignored.
        if(IS_MODIFIER_KEYCODE(keycode) || IS_QK_MOMENTARY(keycode) || IS_QK_TO(keycode)) {
            { *ret = true; return true; }
        }
        if(!record->event.pressed) {
            { *ret = false; return true; }                   // swallow the release of whatever we consumed
        }
        const int8_t slot = latin_target_slot(keycode);
        if(keycode == KC_LAT_REMAP) {
            latin_remap_cancel();           // tap the remap key again to back out
        } else if(slot >= 0) {
            poly_layer_t* ll = access_local_layer();
            if(ll->remap_mode == LATIN_REMAP_PICKKEY) {
                ll->remap_target = (uint8_t)slot;
                ll->remap_mode   = LATIN_REMAP_PICKLTR;
            } else if((uint8_t)slot == ll->remap_target || slot < LATIN_LETTER_TARGETS) {
                // Only a LETTER can be a source, so a punctuation key is inert as a
                // choice here (and renders blank) — with one exception: the TARGET
                // itself, which is how a key is cleared. For a letter that is
                // "re-pick your own letter"; for a punctuation key, which has no own
                // letter, it is the only per-key way back to plain typing. Both land
                // on LATIN_ASSIGN_NONE inside latin_remap_apply().
                latin_remap_apply(ll->remap_target, (uint8_t)slot);
                latin_remap_cancel();
            }
            request_disp_refresh();
        }
        // Everything else is inert while the prompt is up — the board is a dialog.
        { *ret = false; return true; }
    }
    if(keycode == KC_LAT_REMAP) {
        if(record->event.pressed && addlang) {
            // Shift+remap clears EVERY assignment. Once the board is remapped the
            // Intl legends no longer match the printed letters, so there has to be
            // one way back that does not depend on remembering what was changed.
            // (The per-key reset needs no gesture of its own: re-assigning a key to
            // its own letter is the same thing, and latin_remap_apply stores
            // LATIN_ASSIGN_NONE for it.)
            if((get_mods() & MOD_MASK_SHIFT) != 0) {
                latin_remap_reset_all();
                { *ret = false; return true; }
            }
            // Held with Intl: open remap mode. The Intl layer is the only place the
            // key exists, so there is no "outside the layer" case to guard.
            poly_layer_t* ll = access_local_layer();
            ll->remap_mode   = LATIN_REMAP_PICKKEY;
            ll->remap_target = 0;
            // ⚠️ The prompt swallows every non-modifier release from here on, so a key
            // QMK had ALREADY registered when the mode opened would never be
            // unregistered — the host would hold it down and auto-repeat until USB
            // dropped. Reachable: a letter with no variation falls through to QMK on
            // this layer, so it can genuinely be down at this moment. Same remedy the
            // firmware-confirm prompt and doom_begin() use for the same reason.
            clear_keyboard();
            // Drop the variation picker if it was open: the two prompts would
            // otherwise both claim the keycaps.
            latin_picker_latch_release();
            latin_picker_reset_page();
            request_disp_refresh();
        }
        { *ret = false; return true; }
    }
    // The latch toggles on the PRESS, so the release of the tap that ARMED it must
    // be swallowed too — otherwise QMK unregisters the modifier we just registered
    // and the latch is over before the finger lifts.
    //
    // ⚠️ Gated on s_picker_latched, i.e. on us owning the modifier — NOT on the key
    // alone. A Ctrl held down before Intl was pressed is registered by QMK, and
    // swallowing that release would leave it registered forever, turning every
    // later keystroke into Ctrl+key. The other two cases fall through harmlessly:
    // the toggle-OFF press already unregistered, so QMK's release handler just
    // unregisters nothing.
    if(addlang && !record->event.pressed && s_picker_latched &&
       (keycode == KC_LEFT_CTRL || keycode == KC_RIGHT_CTRL)) {
        { *ret = false; return true; }
    }
    return false;
}

// A press: the letters' variations, the picker latch toggle and the open picker.
// process_record_user() calls this after its QK_BOOTLOADER / QK_REBOOT switch.
bool latin_picker_record_pressed(uint16_t keycode, bool addlang, bool *ret) {
    const poly_layer_t* global_layer = get_global_layer();
    switch (keycode) {
        case KC_A ... KC_Z:
        case KC_MINUS ... KC_SLASH:
            // A punctuation key is a target too, but ONLY once it has been
            // mapped. Until then it must behave exactly as it does everywhere
            // else — never become the picker's subject, never swallow its own
            // keystroke — so it leaves here before touching any of that. The
            // guard is a no-op for letters, which always host themselves.
            if(!latin_has_row(keycode)) {
                break;
            }
            // A different letter has a different variation count, so the page
            // that was showing may not exist on it.  Reset here rather than in
            // the picker-open branch below: this case runs on EVERY letter press,
            // picker open or not, so it cannot be bypassed.
            if(keycode != get_local_last_latin_keycode()) {
                latin_picker_reset_page();
            }
            set_local_last_latin_keycode(keycode);
            if((get_mods() & LATIN_PICKER_MOD) == 0 && addlang) {
                const bool lshift = get_mods() == MOD_BIT(KC_LEFT_SHIFT);
                const bool rshift = get_mods() == MOD_BIT(KC_RIGHT_SHIFT);
                const bool upper_case = lshift || rshift || global_layer->led_state.caps_lock;
                const uint32_t* variation = latin_variation(keycode, upper_case);
                if(variation!=NULL) {
                    //this is a work-around (at least for I-Bus on Linux we need to remove the shift, otherwise the Unicode sequence will not be recognized!)
                    if(lshift) unregister_code16(KC_LEFT_SHIFT);
                    if(rshift) unregister_code16(KC_RIGHT_SHIFT);
                    register_unicode(variation[0]);
                    if(lshift) register_code16(KC_LEFT_SHIFT);
                    if(rshift) register_code16(KC_RIGHT_SHIFT);
                    { *ret = false; return true; }
                }
            }
        default:
            break;
    }
    uint16_t last_latin_keycode = get_local_last_latin_keycode();

    // The picker LATCHES: tap Ctrl to open it, tap again to close, and it also
    // closes as soon as a variation is chosen. Holding Intl+Ctrl+Shift+digit is
    // four keys at once, which is what made it awkward in practice.
    //
    // It latches by registering the REAL modifier rather than a private flag,
    // because poly_layer_t.mods is already synced to the other half — the slave
    // draws the picker digits on ITS keys from that same bit, and both flag
    // bytes in base/com.h are full, so a private flag would have nowhere to
    // ride. s_picker_latched only records that WE registered it, so leaving the
    // layer never unregisters a Ctrl the user is genuinely holding.
    if(addlang && (keycode == KC_LEFT_CTRL || keycode == KC_RIGHT_CTRL)) {
        if(s_picker_latched) {
            unregister_mods(MOD_MASK_CTRL);
            s_picker_latched = false;
        } else if((get_mods() & LATIN_PICKER_MOD) == 0) {
            register_mods(MOD_BIT(KC_LEFT_CTRL));
            s_picker_latched = true;
        }
        request_disp_refresh();
        { *ret = false; return true; }   // swallow: the tap is the toggle, not a modifier press
    }

    if((get_mods() & LATIN_PICKER_MOD) != 0 && addlang) {
        // A modifier must still reach QMK while the picker is open.  This
        // block used to `return false` for EVERY press, so the Shift PRESS
        // was swallowed, get_mods() never gained the bit, and pick_upper
        // below could only ever be true if Shift had been held BEFORE the
        // picker was opened — "press Shift first or you cannot choose the
        // capital".  Letting it through also flips the keycaps to the
        // upper-case row for free: `mods` is part of poly_layer_t, so the
        // change shows up as a layer_diff and housekeeping redraws.
        if(IS_MODIFIER_KEYCODE(keycode)) {
            { *ret = true; return true; }
        }
        // ⚠️ NOT a `case KC_LAT0 ... KC_LAT11` range — KC_LAT10/11 are appended
        // elsewhere in the enum, so the picker slots are resolved by helper.
        const int8_t pick_slot = latin_picker_slot(keycode);
        if(pick_slot >= 0) {
            if( latin_has_row(last_latin_keycode)) {
                const bool pick_upper = ((get_mods() & MOD_MASK_SHIFT) != 0) || global_layer->led_state.caps_lock;
                const uint8_t pick_row = latin_picker_row(last_latin_keycode, pick_upper);
                // Only the slots that exist are drawn on the picker keycaps
                // (render_key returns false for the rest), so a press on an
                // empty one is a press on a blank key.  Ignore it — storing
                // the index would leave ex[] pointing at a NULL cell, which
                // the letter then "emits" as whatever lies at address 0.
                // On a paged row this also covers the tail slots of the last
                // page, which are blank for exactly the same reason.
                const int8_t pick_idx = latin_picker_index(pick_row, get_local_layer()->picker_page, pick_slot);
                if(pick_idx >= 0) {
                    latin_sync_t* global_latin_table = access_global_latin_table();
                    // The ABSOLUTE variation index is what gets stored, not the
                    // slot — so a pick made on page 1 still reads back correctly,
                    // and on a variant with a different LATIN_PICKER_SLOTS.
                    // ⚠️ Stored against the KEY's field, read from the HOSTED
                    // row (pick_row above). Writing it at pick_row would work
                    // only while nothing is remapped, then silently overwrite
                    // the pick of whichever key owns that letter.
                    const int8_t pick_target = latin_target_slot(last_latin_keycode);
                    latin_pick_set(global_latin_table->ex, latin_pick_field(pick_target, pick_upper), (uint8_t)pick_idx);
                    latin_publish();

                    // "or an alternative character has been selected"
                    latin_picker_latch_release();
                    latin_picker_reset_page();
                    mark_latin_dirty();
                    request_disp_refresh();
                }
            }
            { *ret = false; return true; }
        }
        switch(keycode) {
            case KC_LAT_PAGE_PREV:
            case KC_LAT_PAGE_NEXT:
                if( latin_has_row(last_latin_keycode)) {
                    const bool pg_upper = ((get_mods() & MOD_MASK_SHIFT) != 0) || global_layer->led_state.caps_lock;
                    const uint8_t pages = latin_page_count(latin_picker_row(last_latin_keycode, pg_upper));
                    if(pages > 1) {
                        // Wrap both ways, like the language/emoji layers: with at
                        // most two pages on the current table, "next" and "prev"
                        // are the same gesture and a dead end at either edge would
                        // just be a key that sometimes does nothing.
                        poly_layer_t* lyr = access_local_layer();
                        uint8_t page = lyr->picker_page;
                        // Same clamp as latin_picker_index(): the case may have
                        // changed since the page was set, so start the wrap from a
                        // page that exists on THIS row.
                        if(page >= pages) {
                            page = (uint8_t)(pages - 1);
                        }
                        page = (keycode==KC_LAT_PAGE_NEXT) ? (uint8_t)((page + 1) % pages)
                                                           : (uint8_t)((page + pages - 1) % pages);
                        lyr->picker_page = page;
                        request_disp_refresh();
                    }
                }
                break;
            case KC_A ... KC_Z:
            case KC_MINUS ... KC_SLASH:
                // Switching the picker's subject: redraw so the slots show the
                // new row. Punctuation is included because a mapped one is a
                // subject like any letter; an unmapped one already left the
                // outer switch without becoming one, so this is just a repaint.
                request_disp_refresh();
                break;
            default:
                break;
        }

        { *ret = false; return true; }
    } else {
        set_local_last_latin_keycode(0);
    }
    return false;
}
