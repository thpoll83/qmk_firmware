// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Board init: keyboard_pre_init_user(), keyboard_post_init_user() and its
// post_init_*() steps, eeconfig_init_kb() / eeconfig_init_user() and oled_init_user().
// The steps run in boot order; the splash steps and boot sub-steps they report are
// the ones a hang report names. Moved out of keys_com/poly_keymap.c.

#include QMK_KEYBOARD_H

#include <string.h>
#include <transactions.h>

#include "print.h"

#include "poly_keymap.h"
#include "base/com.h"
#include "base/fontpack.h"
#include "base/hand_stamp.h"
#include "base/overlay.h"
#include "dev/diag/crash_test.h"
#include "dev/diag/usb_stress.h"
#include "features/anim/startup_anim.h"
#include "features/emoji/emoji_layer.h"
#include "lang/lang_lut.h"
#include "lang/lang_lut_ext.h"
#include "lib/display/multicore_exec.h"
#include "lib/drivers/crash_record.h"
#include "lib/drivers/disp_array.h"
#include "lib/drivers/e2prom.h"
#include "lib/drivers/fw_staging.h"
#include "lib/drivers/helpers.h"
#include "lib/drivers/hw_uid.h"
#include "lib/fw_update/split_fw_up.h"
#include "lib/split_link/bridge_helper.h"
#include "lib/split_link/side.h"
#include "lib/split_link/slave_data.h"
#include "lib/split_link/split_sync.h"
#include "lib/system/boot_diag.h"
#include "lib/system/poly_util.h"
#include "lib/system/state.h"
#include "polymod_core1.h"
#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
#    include "polymod_ltr559.h"
#endif
#include "keys_com/layers.h"
#include "keycode_helper.h"
#include "lang_layer.h"
#include "mru.h"
#include "poly_combos.h"
#include "poly_macro.h"
#include "poly_keymap_internal.h"

static enum lang_layer g_lang_init = INIT_LANG;

#ifdef POLY_DUMMY_TXN_TEST
// Root-cause experiment: a no-op split-transaction handler. Registering three of these
// grows NUM_TOTAL_TRANSACTIONS by 3 (matching the working pointing build) without the
// pointing device, to test whether split42's dead split link is transaction-count
// dependent. Never actually invoked (the master never execs these ids).
static void user_sync_dummy_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    (void)in_len; (void)in_data; (void)out_len; (void)out_data;
}
#endif

#ifdef FW_UP_BOOT_TRACE
// Diagnostic only (build with -DFW_UP_BOOT_TRACE): overwrite the keycaps with a
// single digit at boot milestones so a hang in early boot is visible — the last
// digit shown on each half tells us how far that half got before it stopped.
static void boot_trace(const uint32_t* digit) {
    clear_all_displays();
    display_message(1, 1, digit, poly_heavy_font());
}
#endif

// ---- keyboard_post_init_user(), in the order it runs -----------------------------
// Each step is called once, from keyboard_post_init_user() below. The order is the
// boot order; the splash and sub-step numbers in the steps are what a hang report names.

// The tables that come from flash and EEPROM before anything draws: MIDI octave,
// handedness repair, combos, macro labels.
static void post_init_tables(void) {
#ifdef MIDI_ENABLE
    // QMK starts at octave index 4 (OCTAVE_2), where MI_C plays MIDI note 48, C3. One
    // index higher puts MI_C on middle C (60, C4), which is what the MIDI layer's home
    // row is drawn as. setup_midi() already ran from protocol_pre_init().
    midi_config.octave = QK_MIDI_OCTAVE_3 - MIDI_OCTAVE_MIN;
#endif
    // (The previous run's crash record was captured and archived at the top of
    // keyboard_pre_init_user(); the boot banner reports it.)
    // Put the EEPROM handedness byte back when the flash stamp outvoted it.
    // Nothing in this build READS that byte (config.h drops EE_HANDS), so this is
    // purely so a downgrade to firmware predating the stamp still comes up on the
    // right side. Deliberately not in pre_init: quantum_init() runs between the
    // two and erases the whole store when the magic reads invalid, which is
    // exactly the state a wipe leaves behind. Ahead of emit_boot_banner() below,
    // so the banner can report the repair.
    poly_hand_post_init();
    poly_combos_init();   // cmd 48: this half's flash sector into QMK's tables
    // Labels live in RAM on both halves (the render path reads one per macro keycap per
    // refresh). Each half loads its own EEPROM copy, then the master overwrites the
    // slave's over the link -- so a role swap makes whichever half the host talks to
    // the authority, with no handedness bookkeeping.
    poly_macro_labels_load();
    // Give the slots nothing has claimed their stock look, so a keyboard that has never
    // met the host app still shows sixteen tellable-apart macro keycaps rather than
    // sixteen "M<n>" ones. Writes EEPROM only on the boot that finds a slot empty.
    poly_macro_seed_defaults();
    // Queue them all. Nothing detects "the link is up" here and nothing needs to: the
    // sync tick only clears a label's bit on a real ACK, so the queue simply drains
    // once the slave starts answering. Same shape as the state diff being its own
    // retry queue.
    poly_macro_labels_mark_all_dirty();
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"1");
#endif
}

// QMK debug flags (debug_enable: POLYKYBD_DEBUG builds start with it on) and the
// pointing device CPI.
static void post_init_debug_flags(void) {
    // Customise these values to desired behaviour
    // SECURITY: default OFF. debug_enable now gates keystroke logging to the HID
    // console (process_record_user) and the host-driven active-control HID commands
    // (cmd 14 key injection, cmd 23 bootloader entry). Enable at runtime with the
    // DB_TOGG key (physical access required) when you actually want demo/debug.
    // A POLYKYBD_DEBUG build (rules.mk, test builds only) starts with it on.
#ifdef POLYKYBD_DEBUG
    debug_enable = true;
#else
    debug_enable = false;
#endif
    debug_matrix = false;
    debug_keyboard = false;
    debug_mouse = false;

    //pointing_device_set_cpi(20000);
#if defined(POINTING_DEVICE_ENABLE)
    pointing_device_set_cpi(650);
#endif
    //pimoroni_trackball_set_rgbw(0,0,255,100);
}

// The default layer, this half's role and side, the boot banner (splash step 2) and
// the LTR-559 report.
static void post_init_layers_and_side(void) {
    layer_state_t default_layer = persistent_default_layer_get();
    access_local_layer()->def_layer = default_layer;
    access_local_state()->unicode_mode = get_unicode_input_mode();
    layer_reset_to(default_layer);
    g_force_layer_resync = true;   // push this boot's default layer to the slave
    g_force_resync_tries = FORCE_LAYER_RESYNC_TRIES;  // (re-arm the bounded budget)

    //set these values, they will never change
    set_com_state(is_keyboard_master() ? USB_HOST : BRIDGE);
    set_side(is_keyboard_left() ? LEFT_SIDE : RIGHT_SIDE);

    emit_boot_banner();   // one-shot at boot; housekeeping re-emits for a late console
    crash_record_emit_lines();   // `crash: side=master ...` when this boot followed a crash
    crash_test_announce();       // TEST BUILDS ONLY: list the deliberate-crash chords

    // Boot-splash progress: reaching post_init proves QMK's split/USB init got
    // past the point where the fw-apply "slave not rebooted" hang stalls. Reveal
    // the next glyph here (right after set_side, so is_left_side() is valid).
    splash_progress(2);

#ifdef COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE
    // The module's keyboard_post_init hook has already probed the sensor — QMK
    // runs keyboard_post_init_modules() before keyboard_post_init_kb()/_user(),
    // so ltr559_available() is answerable here. It probes on BOTH halves (the
    // sensor can be soldered to either half's expansion port); the half that
    // finds it uses it, the other stays disabled after bounded retries.
    if (ltr559_available()) {
        uprint("LTR-559 sensor detected.\n");
    }
#endif
}

// Encoder pins, the emoji/language/MRU tables, the overlay pool, and core1 (splash
// steps 3 to 5).
static void post_init_peripherals_and_core1(void) {
    //encoder pins
    gpio_set_pin_input_high(GP25);
    gpio_set_pin_input_high(GP29);

    //srand(halGetCounterValue());

    emj_init();
    lang_init();
    mru_init();
    splash_progress(3);                 // language/emoji/MRU init done

    reset_overlay_pool();
    clear_display_has_overlay();
    //standard mapping is 1:1
    reset_display_to_pool();

#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"2");
#endif
    splash_progress(4);                 // before core1 launch
#ifdef USE_CORE1
    g_core1_entered = 0u;
    multicore_launch_core1();
#endif
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"3");
#endif
    splash_progress(5);                 // core1 up
}

// The split-link RPCs and fw-staging (boot sub-steps 1 to 4, splash step 6).
static void post_init_split_rpcs(void) {
    // ⚠️ Sub-steps, because 63% -> 75% is the gap a boot hang has repeatedly landed
    // in and "somewhere in these four calls" is as far as the milestone can narrow
    // it. Everything here is cheap on paper — array writes and XIP reads — which is
    // exactly why the hang is interesting: it points at core1, launched immediately
    // above, rather than at the calls themselves. The sub-step is what turns the
    // next occurrence into a name instead of a photograph of a percentage.
    boot_substep(1, 4);                    // core1 launched, about to register RPCs
    transaction_register_rpc(USER_SYNC_POLY_DATA,           user_sync_poly_data_handler);
    transaction_register_rpc(USER_SYNC_LAYER_DATA,          user_sync_layer_data_handler);
    transaction_register_rpc(USER_SYNC_LASTKEY_DATA,        user_sync_lastkey_data_handler);
    transaction_register_rpc(USER_SYNC_LATIN_EX_DATA,       user_sync_latin_ex_data_handler);
    transaction_register_rpc(USER_SYNC_OVERLAY_DATA,        user_sync_overlay_data_handler);
    transaction_register_rpc(USER_SYNC_COMPRESSED_DATA,     user_sync_compressed_overlay_data_handler);
    transaction_register_rpc(USER_SYNC_ROI_DATA,            user_sync_roi_data_handler);
    transaction_register_rpc(USER_SYNC_DYNAMIC_KEYMAP_DATA, user_sync_dynamic_keymap_data_handler);
    transaction_register_rpc(USER_SYNC_OVERLAY_MAP_DATA,    user_sync_overlay_map_data_handler);
    transaction_register_rpc(USER_SYNC_FLASH_STAGE,         user_sync_flash_stage_handler);
    transaction_register_rpc(USER_SYNC_RESET,               user_sync_reset_handler);
    boot_substep(2, 4);         // the 11 poly RPCs are registered
    slave_data_register();   // USER_SYNC_SLAVE_DATA: LTR-559 sensor pull + the slave crash record
#ifdef POLY_DUMMY_TXN_TEST
    // Root-cause experiment: register 3 no-op transactions so NUM_TOTAL_TRANSACTIONS
    // grows by 3 (matching the working pointing build) without the pointing device.
    // They are never executed — only their existence changes the transaction count.
    transaction_register_rpc(USER_SYNC_DUMMY1, user_sync_dummy_handler);
    transaction_register_rpc(USER_SYNC_DUMMY2, user_sync_dummy_handler);
    transaction_register_rpc(USER_SYNC_DUMMY3, user_sync_dummy_handler);
#endif

    boot_substep(3, 4);                    // slave_data_register() returned
    fw_staging_init();
    boot_substep(4, 4);                    // fw_staging_init() returned (apply-log + done-record read)
    splash_progress(6);                 // split RPCs registered, fw-staging up
}

// The persisted settings from the user EEPROM block: language, brightness, idle,
// tap terms, Auto Shift, glyph script and size, emoji search mode.
static void post_init_load_settings(poly_eeconf_t* ee) {
    poly_sync_t* local_state = access_local_state();
    local_state->lang = ee->lang;
    local_state->contrast = ee->brightness;
    note_user_brightness(ee->brightness);
    // If host-auto brightness was engaged before the reboot, come up in auto mode
    // at the last auto value instead of the deliberate manual brightness (which the
    // host never refreshes, so a stale low manual value would otherwise show until
    // the host re-engages). Overrides local_state->contrast when auto was on.
    load_auto_brightness(ee->auto_brightness);
    note_idle_style(ee->idle_style);
    note_idle_timeout(ee->idle_timeout);   // BEFORE the banner below, which prints it
    // The tail bytes (tapping_term..emj_search_mode) may hold old keymap data on a
    // board older than 2026-08-13 (state.h, tail_fmt): use their "never chosen"
    // value and stamp the marker, once.
    if (ee->tail_fmt != TAIL_FMT_OK) {
        uprintf("Settings tail unmarked (tail_fmt 0x%02X): tap terms and emoji search mode reset to defaults\n",
                (unsigned)ee->tail_fmt);
        ee->tapping_term    = 0;
        ee->flow_tap_term   = 0;
        ee->emj_search_mode = 0;
        stamp_tail_fmt();
    }
    note_poly_tap_terms(tapping_term_unpack(ee->tapping_term), flow_tap_term_unpack(ee->flow_tap_term));
    // auto_shift is after tail_fmt, so it has its own marker (state.h, tail2_fmt).
    if (ee->tail2_fmt != TAIL2_FMT_OK) {
        ee->auto_shift = 0;
        stamp_tail2_fmt();
    }
    note_poly_auto_shift(auto_shift_unpack_on(ee->auto_shift), auto_shift_unpack_ms(ee->auto_shift));
    emit_idle_config();   // the style is only known here — the banner tick re-emits it
    note_glyph_script(ee->glyph_script);
    note_glyph_size(ee->glyph_size);
    note_glyph_place(ee->glyph_place);
    note_emj_search_mode(ee->emj_search_mode);
}

// The stored keymap's format check and its one-time migrations.
static void post_init_keymap_storage(poly_eeconf_t* ee) {
    // The dynamic keymap is stored BY LAYER INDEX and QMK does not version it, so a
    // build whose layer enum has shifted would read the previous occupant of every
    // slot above the change. Discard it once, then stamp the revision so the next
    // boot is a no-op. Done here rather than in eeconfig_init_user() because the
    // user datablock and the keymap are separate blocks with separate lifetimes —
    // the keymap survives a user-data re-init, which is exactly the case that would
    // otherwise slip through.
    // Timed and reported on the banner tick (note_keymap_storage) as well as printed
    // here: this discard rewrites the capped keymap, the encoder map and the whole
    // macro region -- a few kB of wear-levelled EEPROM -- inside post_init. USB is
    // already connected by then (protocol_pre_init() runs before keyboard_init()), so
    // the host is enumerating while this writes; a wear-levelling erase here masks
    // the USB interrupt for tens of ms (docs/build/upstream-patches.md -> "ChibiOS-Contrib RP2040
    // USB driver"). No console is attached yet either: the one-shot uprintf below is
    // usually lost, so a board that spends a long time here (or never leaves) looks
    // simply dead.
    const uint8_t  stored_fmt = ee->keymap_layers_fmt;
    const bool     need_reset = (stored_fmt != KEYMAP_STORAGE_CURRENT);
    const uint32_t reset_t0   = timer_read32();
    if (need_reset) {
        uprintf("Keymap layer enum changed (fmt %u) - resetting dynamic keymap\n",
                (unsigned)stored_fmt);
        // The late-boot watchdog guard is armed here (boot_diag.c, from step 5): give
        // this one-time rewrite of a few kB of EEPROM the full CRASH_WATCHDOG_MS.
        crash_watchdog_feed();
        dynamic_keymap_reset_poly();
        stamp_keymap_layers_fmt();
    }
    note_keymap_storage(stored_fmt, need_reset,
                        need_reset ? timer_elapsed32(reset_t0) : 0);
    // The split72 default layouts have the AI key where Hyper was. A stored keymap
    // takes priority over the defaults, so a board that stored one gets the change
    // once here: only keys that still hold Hyper at those positions change, the rest
    // of the keymap and the macros stay (state.h, keymap_mig).
    if (ee->keymap_mig < KEYMAP_MIG_AI_KEY) {
        const uint8_t moved = dynamic_keymap_migrate_poly(KC_HYPR, KC_AI);
        stamp_keymap_mig(KEYMAP_MIG_AI_KEY);
        note_keymap_migration(KEYMAP_MIG_AI_KEY, moved);   // printed on the banner tick
    }
    // Fn + the key above the right Shift opens the MIDI layer (split72 default, the
    // key the host's MIDI guide points at). A stored Fn layer still has it
    // transparent: change it there once, and only where it is still transparent.
    // Only on _FL: the utility layer has TO(_ML) too, and a transparent key a user
    // put there stays.
    if (ee->keymap_mig < KEYMAP_MIG_FN_MIDI) {
        const uint8_t moved = dynamic_keymap_migrate_layers_poly(_FL, _FL + 1, KC_TRNS, TO(_ML));
        stamp_keymap_mig(KEYMAP_MIG_FN_MIDI);
        note_keymap_migration(KEYMAP_MIG_FN_MIDI, moved);
    }
}

// The OS state and the synced fields the first render reads.
static void post_init_seed_state(poly_eeconf_t* ee) {
    poly_sync_t* local_state = access_local_state();
    // Restore the active-OS state (auto/manual + last known OS). Auto by default, so
    // a fresh EEPROM re-resolves per host via detection / host push; a manual pin
    // (e.g. Android) sticks. Seed local_state->active_os so the first render before
    // housekeeping runs already reflects the restored OS.
    load_os_state(ee->os_state);
    // Seed with the same OS|auto-flag encoding housekeeping uses, so the first
    // render/sync before housekeeping runs shows the correct auto/pin badge.
    local_state->active_os = (uint8_t)(get_active_os() | (get_os_auto_mode() ? POLY_OS_AUTO_FLAG : 0));
    // Seed the glyph-script override so the first render reflects the persisted script.
    local_state->glyph_script = get_glyph_script();
    local_state->idle_style   = get_idle_style();
    local_state->idle_timeout = get_idle_timeout();
    // Same for the legend size, so the first render is already at the chosen size.
    local_state->glyph_size = get_glyph_size();
    local_state->glyph_place = get_glyph_place();
#ifdef RGB_MATRIX_ENABLE
    local_state->flags = set_flag(STATUS_DISP_ON, RGB_ON, rgb_matrix_is_enabled());
#else
    local_state->flags = STATUS_DISP_ON;   // no RGB on this variant
#endif
}

// The persisted Intl latin picks and assignments: migrate and normalise.
static void post_init_latin_table(poly_eeconf_t* ee) {
    // Normalise the persisted latin-variation picks.  Each nibble can hold 0..15
    // while only LATIN_EX_VARIATIONS slots exist, and a row may have lost entries
    // since the byte was written, so a stale EEPROM can name a slot that is empty
    // or off the end.  latin_variation() falls back to slot 0 at read time either
    // way; rewriting the byte here stops a dead index in one case's nibble being
    // carried forward every time the other case is re-picked.
    latin_sync_t* latin_table = access_global_latin_table();
    bool latin_normalised = false;
    // ⚠️ The assignment map MUST be gated on the version byte — do NOT infer "never
    // written" from the bytes themselves. An earlier version of this relied on
    // LATIN_ASSIGN_NONE being all-bits-set and an unwritten EEPROM reading 0xFF;
    // that is wrong for this backend. QMK's wear-levelling normalises its backing
    // store so cleared bytes arrive as ZERO (quantum/wear_leveling/wear_leveling.c
    // clears the cache with memset(...,0) and requires a 0xFF store to return the
    // complement), so the map read back as all 0x00 = "every key hosts letter 0",
    // and every keycap on the Intl layer showed a variation of 'a' (field, first
    // flash). LATIN_PICK_INTERLEAVED is only ever stamped by firmware that writes
    // this field in the same breath, so it is the one trustworthy witness that the
    // bytes mean anything.
    // ⚠️ The letter block is copied at LATIN_*_BASE_BYTES, not sizeof(): EEPROM
    // keeps the letters and the punctuation apart so the letter block can keep the
    // size and offset it shipped with (see state.h). The punctuation half of both
    // arrays is filled from the ext block further down — unconditionally, because
    // the ASSIGN split is not byte-aligned (26 x 6 = 156 bits), so the 20th base
    // byte carries four bits of the first punctuation field with it.
    if(ee->latin_pick_migrated == LATIN_PICK_ASSIGN_OK) {
        memcpy(latin_table->assign, ee->latin_assign, LATIN_ASSIGN_BASE_BYTES);
    } else {
        // Includes LATIN_PICK_INTERLEAVED, whose stored map is the persisted
        // all-zero garbage described in state.h — discarding it is the recovery.
        memset(latin_table->assign, LATIN_ASSIGN_FILL, sizeof(latin_table->assign));
        latin_normalised = true;           // re-stamp at the next flush
    }
    if(ee->latin_pick_migrated == LATIN_PICK_ASSIGN_OK ||
       ee->latin_pick_migrated == LATIN_PICK_INTERLEAVED) {
        memcpy(latin_table->ex, ee->latin_ex_wide, LATIN_PICK_BASE_BYTES);
    } else if(ee->latin_pick_migrated == LATIN_PICK_MIGRATED) {
        // Re-index case-BLOCKED (case*26 + letter) to case-INTERLEAVED (slot*2 +
        // case). Same 52 values, same width -- only the field numbering moves, so
        // every pick is preserved.
        memset(latin_table->ex, 0, sizeof(latin_table->ex));
        for(uint8_t i = 0; i < 26; i++) {
            latin_pick_set(latin_table->ex, latin_pick_field(i, true),  latin_bits_get(ee->latin_ex_wide, LATIN_PICK_BASE_BYTES, i));
            latin_pick_set(latin_table->ex, latin_pick_field(i, false), latin_bits_get(ee->latin_ex_wide, LATIN_PICK_BASE_BYTES, (uint8_t)(26 + i)));
        }
        latin_normalised = true;           // force the flush that stamps the new version
    } else {
        // One-time widening of the legacy nibble pairs (one byte per letter, high
        // nibble = uppercase) into the 6-bit fields. Every legacy value is < 16 so
        // it lands in the wider field unchanged and the user keeps their picks.
        memset(latin_table->ex, 0, sizeof(latin_table->ex));
        for(uint8_t i = 0; i < 26; i++) {
            latin_pick_set(latin_table->ex, latin_pick_field(i, true),  (uint8_t)(ee->latin_ex[i] >> 4));
            latin_pick_set(latin_table->ex, latin_pick_field(i, false), (uint8_t)(ee->latin_ex[i] & 0x0F));
        }
        latin_normalised = true;
    }
    // The punctuation targets, from their own appended block. An EEPROM written
    // before they existed reads this as zeros (wear-levelling clears to 0, NOT to
    // 0xFF — see state.h), which is why "never written" is decided by the format
    // byte and not by the bytes: zeroed assignments would mean "every punctuation
    // key hosts A", the exact failure the letter map already shipped once.
    if(ee->latin_ext_fmt == LATIN_EXT_OK) {
        memcpy(latin_table->ex + LATIN_PICK_BASE_BYTES, ee->latin_ex_ext, LATIN_PICK_EXT_BYTES);
        for(uint8_t i = 0; i < LATIN_PUNCT_TARGETS; i++) {
            latin_assign_set(latin_table->assign, (uint8_t)(LATIN_LETTER_TARGETS + i),
                             latin_bits_get(ee->latin_assign_ext, LATIN_ASSIGN_EXT_BYTES, i));
        }
    } else {
        memset(latin_table->ex + LATIN_PICK_BASE_BYTES, 0, LATIN_PICK_EXT_BYTES);
        for(uint8_t i = 0; i < LATIN_PUNCT_TARGETS; i++) {
            latin_assign_set(latin_table->assign, (uint8_t)(LATIN_LETTER_TARGETS + i), LATIN_ASSIGN_NONE);
        }
        latin_normalised = true;           // stamp the ext block at the next flush
    }
    // ⚠️ Validate each pick against the row its key actually HOSTS, not against the
    // field index. Those are the same number only while every key is unassigned;
    // once a key is remapped, checking latin_ex_map[field] would validate the pick
    // against the wrong letter and zero perfectly good choices.
    for(uint8_t slot = 0; slot < LATIN_TARGETS; slot++) {
        const int8_t letter = latin_assign_get(latin_table->assign, slot);
        for(uint8_t c = 0; c < 2; c++) {
            const bool    upper = (c == 0);
            const uint8_t field = latin_pick_field(slot, upper);
            const uint8_t idx   = latin_pick_get(latin_table->ex, field);
            // An unassigned punctuation key hosts no row at all, so there is nothing
            // to validate against — its pick must simply be 0, ready for whatever it
            // is later mapped to.
            const bool bad = (letter < 0)
                                 ? (idx != 0)
                                 : (idx >= LATIN_EX_VARIATIONS ||
                                    latin_ex_map[(uint8_t)((upper ? 0 : 26) + letter)][idx] == NULL);
            if(bad) {
                latin_pick_set(latin_table->ex, field, 0);
                latin_normalised = true;
            }
        }
    }
    if(latin_normalised) {
        // access_global_latin_table() returns &g_latin, i.e. the very buffer
        // save_user_latin() writes, so the normalised bytes are already in place --
        // they only need scheduling for the next flush point.  Gate it on an actual
        // change: marking latin dirty unconditionally would rewrite the 26-byte
        // block on EVERY boot->suspend cycle with nothing to correct, and EEPROM
        // write frequency is load-bearing here (the wear-levelling consolidation
        // erase blocking a split transaction is what made the slave go
        // unresponsive -- see the readme).  A corrupt byte thus converges once.
        mark_latin_dirty();
    }
}

// QMK calls this once after reset. It runs the post_init_*() steps above in boot
// order, then the displays, the first-run intro and the watchdog.
void keyboard_post_init_user(void) {
    post_init_tables();
    post_init_debug_flags();
    post_init_layers_and_side();
    post_init_peripherals_and_core1();
    post_init_split_rpcs();
    poly_eeconf_t ee = load_user_eeconf();
    post_init_load_settings(&ee);
    post_init_keymap_storage(&ee);
    post_init_seed_state(&ee);
    post_init_latin_table(&ee);
    poly_sync_t* local_state = access_local_state();
    // Restore the MRU recents and schedule a one-time push to the slave half.
    mru_load(ee.mru_emoji, ee.mru_lang);
    splash_progress(7);                 // EEPROM config (brightness/lang/OS/MRU) loaded

    set_displays(local_state->contrast, false);   // active brightness (auto value if restored, else manual)
    // …and bring the status panel up on the same scale (local_state->contrast was
    // just loaded from EEPROM above). QMK's oled_init() programs the contrast
    // register to OLED_BRIGHTNESS unconditionally, so without this a board that
    // booted with a stored brightness of 2 would light its status OLED at full
    // until the first contrast change moved it.
    oled_set_brightness(status_oled_level(false));

    // One-time startup animation: on the very first boot (fresh EEPROM), play the
    // procedural intro once, then persist BOOT_INTRO_DONE (in the housekeeping
    // finish edge). Each half reads its own flag and animates its own keycaps.
    note_boot_flags(ee.boot_flags);
    // The boot-time auto-play of Eden that used to wedge a half is the first-run
    // trigger below; see the note there for what the hang was and why it is fixed.
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"4");
#endif
    splash_progress(SPLASH_DONE);       // boot complete: full splash, dwell, then legends
    // First-run experience: Eden, then the tutorial. boot_intro_pending() reads the
    // EEPROM marker — which is stamped only once the TUTORIAL has been seen or skipped,
    // so an interrupted first run replays both and a completed one never returns. The
    // marker survives a firmware flash: EEPROM is the last 8 KB of the chip
    // (0x7FE000..0x800000), outside every region any flash path writes.
    //
    // ⚠️ boot_intro_pending() had NO callers before this — the marker, the pending check
    // and the finish edge all existed, but nothing ever started the animation at boot.
    //
    // ON BY DEFAULT since 1.0.0 (`-e POLYKYBD_BOOT_INTRO=no` opts out; HIL images
    // default it off, see rules.mk). It was off for a long time because boot auto-play
    // had wedged a half and the hang was never understood. It is understood now: the
    // master bumps anim_nonce at boot, and the SLAVE's poly-sync handler started Eden
    // RIGHT THERE — on the split-protocol thread (serial_protocol.c's SlaveThread,
    // HIGHPRIO), concurrently with the slave's own post_init. Both wrote the keycap SPI
    // bus at once and the slave parked forever in a spiSend() with no timeout, status
    // panel frozen at "100%" with no render sub-steps (hardware, 2026-09-25). The
    // handler now only records the request and housekeeping starts it
    // (split_sync_drain_anim_replay()). The rounds of tutorial testing since have run
    // this exact path on every reset (POLYKYBD_TUTORIAL_TEST) without a recurrence.
    // What runs HERE is cheap — arming, and startup_anim_start()'s contrast write; the
    // frames render from housekeeping, after crash_watchdog_start() below.
#if defined(POLYKYBD_BOOT_INTRO) || defined(POLYKYBD_TUTORIAL_TEST)
    // ⚠️ The TEST build respects the marker too. It used to force this true on every
    // reset, and a tester who had FINISHED the lesson saw it again after a restart. The
    // test build's marker is keyed to the build instead (boot_done_value() in state.c),
    // so each newly flashed image plays once and a finished lesson stays finished; RESET
    // Eden replays it by hand. It still uses the Eden hand-off (ARMED is set right here,
    // before the first sync), never a direct start from each half's own timer, which
    // raced and left the slave on the default layer.
    const bool first_run = boot_intro_pending();
#    ifdef POLYKYBD_TUTORIAL_TEST
    uprintf("Tutorial TEST build: first-run %s\n", first_run ? "pending" : "already played");
#    endif
    if (first_run) {
        arm_tutorial_after_intro();
        startup_anim_start();
        // ⚠️ The master's marker decides for BOTH halves. RESET Eden runs on the
        // master only and so clears only the master's marker; a slave that already
        // saw the tutorial reads DONE and would sit out the first run. The nonce
        // replays Eden there exactly as KC_EDEN does (split_sync.c's anim_replay), and
        // is a no-op on a slave already animating from its own pending marker.
        // is_keyboard_master(), not is_usb_host_side(): the bridge role is not known
        // yet this early, while QMK's split role is.
        if (is_keyboard_master()) local_state->anim_nonce++;
    }
#endif
    // LAST: arm the hardware watchdog. Everything above may block for seconds
    // (the keymap discard, the splash dwell); from here on housekeeping feeds it
    // every pass and a hang becomes a reset with a `kind=watchdog` crash record.
    crash_watchdog_start();
}

// Pre-initialization setup: initializes display hardware, loads EEPROM config, shows splash screen.
void keyboard_pre_init_user(void) {
    // FIRST, before any of our own init and before QMK's (matrix, split link and
    // status OLED all run between here and post_init): did the previous run end
    // in a crash? Captures the NOLOAD record + reset cause into RAM and stamps
    // the phase BOOT, so a fault anywhere in this boot is recorded as one and
    // the previous record is already archived if it recurs. The flash write
    // runs here without the core1 lockout, which is only safe because core1 has
    // not been launched yet (see crash_record.h) -- keep this ahead of
    // post_init's multicore_launch_core1().
    crash_record_init();

    // Resolve handedness while the EEPROM's own verdict is still readable. It has
    // to be HERE, in keyboard_setup(), because keyboard_init() later runs
    // quantum_init()'s `if (!eeconfig_is_enabled()) eeconfig_init()` -- an erase of
    // the whole store -- and after that point "the store was wiped" and "the store
    // is fine" look identical, which is the one input poly_hand_decide() needs.
    // Same core1 rule as crash_record_init() above: the migration write takes no
    // lockout because core1 has not been launched yet.
    // Provisioning build (-e POLYKYBD_FORCE_HAND=left|right): stamp the side from
    // the image, BEFORE the resolve below, so poly_hand_boot_init() reads it back
    // and reports SRC_STAMP and drives the EEPROM repair through the ordinary path.
    // Written as two plain #ifs rather than IS_DEFINED(), which needs the macro to
    // expand to 0/1 -- OPT_DEFS passes these with no value at all.
#if defined(POLYKYBD_FORCE_HAND_LEFT)
    poly_hand_force_stamp(true);
#elif defined(POLYKYBD_FORCE_HAND_RIGHT)
    poly_hand_force_stamp(false);
#endif
    poly_hand_boot_init();

    // The flash unique ID for cmd 50. A flash command, so it runs here, before
    // core1 exists (lib/drivers/hw_uid.h).
    poly_hw_uid_boot_init();

    // Load the external-flash font pack and assemble g_all_fonts = resident ++
    // pack BEFORE the first render (show_splash_screen() below draws keycaps).
    // No valid pack (erased/corrupt/ABI mismatch) -> resident-only fonts.
    fontpack_init(RESIDENT_FONTS, resident_font_count);

    kdisp_hw_setup();
    kdisp_init(NUM_SHIFT_REGISTERS);
    peripherals_reset();
    kdisp_setup(true);

    select_all_displays();
    kdisp_scroll_vlines(47);
    kdisp_scroll_modehv(true, 3, 1);
    kdisp_scroll(false);

    reset_all_states_and_layers();

    set_displays(50, false);
    set_local_last_latin_keycode(0);
    // Resolve the side BEFORE the splash so each half shows its own logo
    // (left = "POLY KYBD", right = "SPLIT 72") instead of both showing the
    // right-side text.  set_side() otherwise runs only in post_init, after the
    // splash, so the splash always saw side == UNDECIDED → both rendered "SPLIT 72".
    //
    // Resolve through poly_hand_is_left(), which poly_hand_boot_init() above has
    // already settled from the flash stamp — so this is a cached read that touches
    // no EEPROM at all.  That matters here specifically: any route through
    // eeconfig this early — right after eeprom_driver_init() in keyboard_setup,
    // before the wear-leveling store is validated — can see eeconfig as "not
    // enabled" and run eeconfig_init() → nvm_eeconfig_erase() →
    // eeprom_driver_format(), wiping the *entire* emulated EEPROM.  That is what
    // the old eeconfig_read_handedness() call here had to be careful about, and
    // what dropping EE_HANDS removed from split_pre_init() as well.
    set_side(poly_hand_is_left() ? LEFT_SIDE : RIGHT_SIDE);
    show_splash_screen();
#ifdef FW_UP_BOOT_TRACE
    boot_trace(U"0");
#endif

    gpio_set_pin_input_high(I2C1_SDA_PIN);

    // Test builds only (-e POLYKYBD_USB_STRESS=yes): last in pre_init, so its
    // interrupt-masked windows start just before protocol_pre_init() connects USB
    // and never overlap the flash writes above.
    usb_stress_start();
}

// Runs immediately after QMK's own dynamic_keymap_reset() in eeconfig_init_quantum().
//
// That call is the ONE remaining reachable use of the unbounded reset, and with the
// encoder/macro regions rebased on DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT (config.h) it
// writes layers 9..12 straight over both of them. It cannot be prevented from here --
// eeconfig.c is upstream and we deliberately do not patch it for this -- but it CAN be
// repaired, because eeconfig_init_quantum() calls us three lines later, unconditionally,
// in the same function. dynamic_keymap_reset_poly() rewrites layers 0..8 and the capped
// encoder map from flash and zeroes the macro buffer, which is exactly the state a fresh
// EEPROM should be in, so this is the normal initialisation rather than a fix-up that
// happens to also repair.
//
// Nearly free despite running second: eeprom_update_byte() only writes a byte that
// actually changed, and QMK's pass has already put the correct values in layers 0..8.
void eeconfig_init_kb(void) {
    // Replicate what the weak default does before adding the repair -- overriding it
    // replaces the whole body, and dropping either half here would be silent
    // (eeconfig.c, EECONFIG_KB_DATA_SIZE == 0 branch).
    eeconfig_update_kb(0);
    dynamic_keymap_reset_poly();
    eeconfig_init_user();
}

// Initializes EEPROM configuration with default language, brightness, and latin extension settings.
void eeconfig_init_user(void) {
    uprint("Init EE config\n");
    // Zero the WHOLE struct: the write below is a full-struct write, but not every
    // field is assigned here (os_state, glyph_script, boot_flags are not), so an
    // uninitialised local would persist stack garbage into a fresh EEPROM for those.
    poly_eeconf_t ee = {0};
    ee.lang = g_lang_init;
    ee.brightness = ~FULL_BRIGHT;
    ee.auto_brightness = 0;   // host-auto off on a fresh EEPROM
    // Born with the board default already recorded as a real choice, rather than
    // leaning on load_user_eeconf()'s pre-sentinel substitution. Same reasoning as
    // latin_ex_wide being born widened: a fresh EEPROM should never have to be
    // migrated, and the zero that {0} leaves here means PULSE, not "unset".
    ee.idle_style     = POLY_DEFAULT_IDLE_STYLE;
    ee.idle_style_fmt = IDLE_STYLE_FMT_OK;
    // Likewise born with a real choice recorded. ⚠️ Through idle_timeout_pack(), not
    // raw: the stored form is biased by one, and the zero {0} leaves here is the
    // "never written" value — writing the enum directly would store IDLE_TIMEOUT_15S
    // as a 0 that the loader then reads back as unset. Same class of trap as the
    // latin_assign memset two blocks down, opposite direction.
    ee.idle_timeout   = idle_timeout_pack(POLY_DEFAULT_IDLE_TIMEOUT);
    // Biased like idle_timeout, for the same reason (base/tap_terms.h).
    ee.tapping_term   = tap_term_pack(POLY_DEFAULT_TAPPING_TERM_MS);
    ee.flow_tap_term  = tap_term_pack(POLY_DEFAULT_FLOW_TAP_TERM_MS);
    ee.tail_fmt       = TAIL_FMT_OK;
    ee.auto_shift     = auto_shift_pack(false, POLY_DEFAULT_AUTO_SHIFT_MS);
    ee.tail2_fmt      = TAIL2_FMT_OK;
    ee.keymap_mig     = KEYMAP_MIG_FN_MIDI;  // the keymap was just written from the defaults
    memset(ee.latin_ex, 0, sizeof(ee.latin_ex));
    // A fresh EEPROM is born already widened: zeroed picks (every letter on its
    // first variation) plus the sentinel, so it never runs the legacy conversion.
    memset(ee.latin_ex_wide, 0, sizeof(ee.latin_ex_wide));
    ee.latin_pick_migrated = LATIN_PICK_ASSIGN_OK;
    // ⚠️ LATIN_ASSIGN_FILL, NOT the struct-wide {0}: a zeroed map reads as "every
    // key assigned to A" and the whole Intl layer shows A's variations.
    memset(ee.latin_assign, LATIN_ASSIGN_FILL, sizeof(ee.latin_assign));
    // Same for the punctuation block, and for the same reason — plus its own format
    // byte, so a fresh EEPROM is born with the extension already valid.
    memset(ee.latin_ex_ext, 0, sizeof(ee.latin_ex_ext));
    memset(ee.latin_assign_ext, LATIN_ASSIGN_FILL, sizeof(ee.latin_assign_ext));
    ee.latin_ext_fmt = LATIN_EXT_OK;
    // Empty MRU recents: the serialised form uses 0 == empty for both lists, so
    // a zeroed block reads back as "no recent" (no stray category-0 / lang-0).
    memset(ee.mru_emoji, 0, sizeof(ee.mru_emoji));
    memset(ee.mru_lang, 0, sizeof(ee.mru_lang));
    eeconfig_update_user_datablock(&ee, 0, sizeof(ee));
}


// Initializes OLED display: turns off, clears buffer, sets the scroll speed (the DOOM
// attract logo's hardware scroll), then enables. The scrolling Poly/Kybd logos once
// drawn here were removed: this runs before the driver is initialised, so they only
// ever filled RAM and were never seen at boot.
oled_rotation_t oled_init_user(oled_rotation_t rotation){
    oled_off();
    oled_clear();
    oled_render();
    oled_scroll_set_speed(0);
    oled_on();
    return rotation;
}
