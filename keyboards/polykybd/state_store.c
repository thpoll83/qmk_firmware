// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The PERSISTENCE half of the state module: every function that knows the
// poly_eeconf_t EEPROM layout and calls eeconfig_* lives here, split out of
// state.c so the flash I/O and the RAM-state policy stop sharing one file.
// The policy half (state.c) owns the values and the dirty flags and reads
// nothing back from this file; this file reads the values to persist through
// state.h's public getters, so the two halves meet only at that seam.
//
// Behaviour-identical to the pre-split state.c: same write contents, same
// order, same offsets. The declarations stay in state.h, so consumers are
// unchanged.

#include "state.h"

#include <stddef.h>
#include "eeconfig.h"

// Writes only lang+brightness+idle_style+unused (4 bytes) to EEPROM.
void save_user_settings(void) {
    const poly_eeconf_t ee = { .lang = get_local_state()->lang, .brightness = (uint8_t)(~get_user_brightness()),
                               .idle_style = get_idle_style(), .auto_brightness = pack_auto_brightness() };
    eeconfig_update_user_datablock(&ee, 0, offsetof(poly_eeconf_t, latin_ex));
    // Stamp the idle-style format marker AFTER the block it describes, same rule as
    // the latin sentinels below: an interrupted write must never leave the byte
    // claiming a choice that was not stored. From here on load_user_eeconf() takes
    // idle_style verbatim, so a future default change cannot overwrite a real one.
    const uint8_t idle_marker = IDLE_STYLE_FMT_OK;
    eeconfig_update_user_datablock(&idle_marker, offsetof(poly_eeconf_t, idle_style_fmt),
                                   sizeof(idle_marker));
}

// Writes only the packed latin variation picks to EEPROM.
// ⚠️ These go to latin_ex_wide, NOT the legacy latin_ex: the pick table is 6-bit
// fields, so writing it at the old 26-byte offset would run straight into
// mru_emoji. The sentinel is stamped in the same breath, so a half-written
// migration cannot leave the wide block claiming to be converted.
//
// ⚠️ The RAM table is one flat array over all LATIN_TARGETS, but EEPROM keeps the
// letters and the punctuation in SEPARATE blocks — the letter block must keep the
// size and offset it shipped with, or the format byte behind it moves and can no
// longer be found (see state.h). So each write is explicitly bounded rather than
// sizeof(->ex): the letters go to the original block, the rest to the tail.
void save_user_latin(void) {
    const latin_sync_t* latin = get_global_latin_table();
    eeconfig_update_user_datablock(latin->ex, offsetof(poly_eeconf_t, latin_ex_wide),
                                   LATIN_PICK_BASE_BYTES);
    eeconfig_update_user_datablock(latin->assign, offsetof(poly_eeconf_t, latin_assign),
                                   LATIN_ASSIGN_BASE_BYTES);
    // Picks split on a byte boundary (39 = 52 fields x 6 bits), so the tail is a
    // plain slice of the same array. Assignments do NOT (26 x 6 = 156 bits), so the
    // punctuation ones are re-packed into their own array indexed from zero.
    eeconfig_update_user_datablock(latin->ex + LATIN_PICK_BASE_BYTES,
                                   offsetof(poly_eeconf_t, latin_ex_ext),
                                   LATIN_PICK_EXT_BYTES);
    uint8_t assign_ext[LATIN_ASSIGN_EXT_BYTES] = {0};
    for(uint8_t i = 0; i < LATIN_PUNCT_TARGETS; i++) {
        latin_bits_set(assign_ext, LATIN_ASSIGN_EXT_BYTES, i,
                       latin_bits_get(latin->assign, LATIN_ASSIGN_BYTES,
                                      (uint8_t)(LATIN_LETTER_TARGETS + i)));
    }
    eeconfig_update_user_datablock(assign_ext, offsetof(poly_eeconf_t, latin_assign_ext),
                                   sizeof(assign_ext));
    // Stamp the format versions LAST, so an interrupted write can never leave a
    // block claiming a layout it does not have.
    const uint8_t marker = LATIN_PICK_ASSIGN_OK;
    eeconfig_update_user_datablock(&marker, offsetof(poly_eeconf_t, latin_pick_migrated),
                                   sizeof(marker));
    const uint8_t ext_marker = LATIN_EXT_OK;
    eeconfig_update_user_datablock(&ext_marker, offsetof(poly_eeconf_t, latin_ext_fmt),
                                   sizeof(ext_marker));
}

// Record that the stored dynamic keymap now matches this build's layer enum. Written
// straight through rather than via the dirty-flag/suspend path: the keymap has already
// been reset by the time this is called, so losing the stamp to a power cut would cost
// the user a SECOND reset on the next boot.
void stamp_keymap_layers_fmt(void) {
    const uint8_t marker = KEYMAP_STORAGE_CURRENT;
    eeconfig_update_user_datablock(&marker, offsetof(poly_eeconf_t, keymap_layers_fmt),
                                   sizeof(marker));
}

// Saves both settings and latin table. Use save_user_settings() or save_user_latin() when only one part changed.
void save_user_eeconf(void) {
    save_user_settings();
    save_user_latin();
}

// Writes the MRU blob (emoji + lang recents) to EEPROM, but only when it changed
// since the last load/save. Keeps the write off the hot path so flash wear and
// the ~50 ms consolidation erase only ever happen on a real suspend.
void save_user_mru_if_dirty(void) {
    if (!mru_dirty()) {
        return;
    }
    uint8_t packed[MRU_EMOJI_PACKED];
    mru_emoji_pack(packed);
    eeconfig_update_user_datablock(packed, offsetof(poly_eeconf_t, mru_emoji), MRU_EMOJI_PACKED);
    uint8_t lang_packed[MRU_CAP];
    mru_lang_pack(lang_packed);
    eeconfig_update_user_datablock(lang_packed, offsetof(poly_eeconf_t, mru_lang),
                                   MRU_CAP * sizeof(uint8_t));
    mru_clear_dirty();
}

// Loads the persisted MRU lists from EEPROM into the RAM lists.
void load_user_mru(void) {
    poly_eeconf_t ee;
    eeconfig_read_user_datablock(&ee, 0, sizeof(ee));
    mru_load(ee.mru_emoji, ee.mru_lang);
}

// Loads user keyboard configuration from EEPROM with brightness validation against maximum.
poly_eeconf_t load_user_eeconf(void) {
    poly_eeconf_t ee;
    eeconfig_read_user_datablock(&ee, 0, sizeof(ee));
    ee.brightness = ~ee.brightness;
    if(ee.brightness>FULL_BRIGHT) {
        ee.brightness = FULL_BRIGHT;
    }
    // ⚠️ Gate the idle style on its format byte, NOT on the value. PULSE is 0 and an
    // unwritten byte reads back as 0 (wear levelling), so on a pre-sentinel EEPROM
    // "the user chose pulse" and "nobody ever chose" are indistinguishable — see the
    // idle_style_fmt comment in state.h. Substituting the board default for the whole
    // pre-sentinel population is therefore the only honest reading, and it is what
    // moves an existing board onto the new default exactly once.
    // Two ways to reach the board default, one remedy: the byte predates the
    // sentinel, or it is out of range.
    if(ee.idle_style_fmt != IDLE_STYLE_FMT_OK || ee.idle_style >= IDLE_STYLE_COUNT) {
        ee.idle_style = POLY_DEFAULT_IDLE_STYLE;
    }
    if(ee.glyph_script == 0xFF) {
        ee.glyph_script = GLYPH_STD;        // erased/uninitialised EEPROM byte -> normal legends
    }
    // Any other value is kept verbatim (a glyph-script INDEX). An index this
    // firmware doesn't know renders the normal legend; keeping it means the choice
    // survives a firmware/font-pack update that later adds that script.
    if(ee.glyph_size >= GLYPH_SIZE_COUNT) {
        ee.glyph_size = GLYPH_SIZE_S;       // unwritten/garbage EEPROM -> the original face
    }
    return ee;
}

// Writes the single os_state byte to EEPROM. Separate from save_user_settings()
// because os_state sits at the end of poly_eeconf_t (the former alignment byte),
// not in the contiguous lang/brightness/idle/auto settings block.
void save_user_os(void) {
    uint8_t packed = pack_os_state();
    eeconfig_update_user_datablock(&packed, offsetof(poly_eeconf_t, os_state), sizeof(packed));
}

// Writes the single glyph_script byte to EEPROM. Separate from save_user_settings()
// because glyph_script sits at the tail of poly_eeconf_t, not in the contiguous
// lang/brightness/idle/auto settings block.
void save_user_glyph_script(void) {
    const uint8_t script = get_glyph_script();
    eeconfig_update_user_datablock(&script, offsetof(poly_eeconf_t, glyph_script),
                                   sizeof(script));
}

// Writes the single glyph_size byte to EEPROM — same reasoning as
// save_user_glyph_script(): it is a tail byte, not part of the settings block.
void save_user_glyph_size(void) {
    const uint8_t size = get_glyph_size();
    eeconfig_update_user_datablock(&size, offsetof(poly_eeconf_t, glyph_size),
                                   sizeof(size));
}

// Writes just the boot-intro marker tail byte (like glyph_script, it sits at the
// tail of poly_eeconf_t, not in the contiguous settings block).
void save_user_boot_flags(void) {
    const uint8_t flags = get_boot_flags();
    eeconfig_update_user_datablock(&flags, offsetof(poly_eeconf_t, boot_flags),
                                   sizeof(flags));
}
