// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include "quantum.h"
#include "mru.h"
#include "layers.h"

// Idle (anti-burn-in) display style, persisted in poly_eeconf_t.idle_style and
// toggled over HID (cmd 28). PULSE is the legacy contrast-only breathing; JITTER
// adds a per-cycle relocation of the legend so the lit pixels migrate over time.
// DOOM runs the doom easter egg's attract demo as a screensaver instead of the
// pulse (doom/README.md) — chrome-free, dismissed by the first key press; on a
// build/board where the demo can't start (no POLYKYBD_DOOM, staging active) it
// falls back to PULSE at runtime, so the value is always safe to accept/persist.
// EDEN loops the "Eden" boot animation as a screensaver (split72 only): it reuses
// the normal idle machinery (DISP_IDLE flag → wake on key + TURN_OFF suspend) but
// renders looping Eden frames on both halves instead of the pulse; on split42 the
// no-op anim stubs leave it behaving like PULSE.
// Values are append-only (persisted + on the wire in poly_sync_t.idle_style).
enum poly_idle_style {
    IDLE_STYLE_PULSE  = 0,
    IDLE_STYLE_JITTER = 1,
    IDLE_STYLE_IDDQD  = 2,   // doom attract-demo screensaver (host: IdleStyle.IDDQD)
    IDLE_STYLE_EDEN   = 3,   // looping "Eden" boot animation screensaver (host: IdleStyle.EDEN)
    IDLE_STYLE_COUNT
};

// Glyph-script override (HID cmd 30, protocol v9+). Replaces the language-layer
// letter/digit legends with an alternative script's glyphs while leaving overlays
// and OS-hints untouched. GLYPH_STD is the normal (language) legends. Values are
// append-only: persisted in poly_eeconf_t.glyph_script and carried on the wire
// (poly_sync_t.glyph_script), so never reorder or reuse — add new scripts at the end.
enum poly_glyph_script {
    GLYPH_STD      = 0,   // normal language legends (no override)
    GLYPH_TENGWAR  = 1,   // Tengwar (Alcarin, CSUR mapping) — "fantasy" font-pack bundle
    // 2026-07 expansion — 9 more scripts, all in the "fantasy" bundle. Each maps
    // the letter (and, where the script has them, digit) keys to a dense private
    // PUA block; scripts without native numerals leave the digit keys as normal
    // numerals. See glyph_script_codepoint() in poly_keymap.c for the base blocks.
    GLYPH_RUNES    = 2,   // Elder Futhark runes (Noto Sans Runic, transliteration)
    GLYPH_AUREBESH = 3,   // Aurebesh (Unifont CSUR) — letters only
    GLYPH_SGA      = 4,   // Standard Galactic Alphabet (CC0) — cipher, has digits
    GLYPH_CIRTH    = 5,   // Cirth / Angerthas (Unifont CSUR) — letters only
    GLYPH_IBMVGA   = 6,   // IBM VGA / CP437 (VileR PxPlus) — Latin restyle
    GLYPH_C64      = 7,   // Commodore 64 (Homecomputer Fonts) — Latin restyle
    GLYPH_AMIGA    = 8,   // Amiga Topaz (Homecomputer Fonts) — Latin restyle
    GLYPH_APL      = 9,   // APL (Unifont) — Dyalog/IBM keyboard symbol per key
    GLYPH_BRAILLE  = 10,  // Braille (Unifont) — Grade-1 letters + digits
    GLYPH_SCRIPT_COUNT
};

// Keycap legend SIZE (HID cmd 34, protocol v13+). Picks how large the key's MAIN
// legend is drawn — the single glyph the key produces. The shift / AltGr previews
// and every other kind of chrome (overlays, OS hints, tabs, flags) are deliberately
// NOT affected: they are secondary marks, and a keycap only has room for one big
// thing. GLYPH_SIZE_S is the size the keyboard has always drawn.
//
// The bigger faces are latin only and live in the `latinbig` font-pack bundle at
// relocated codepoints (see glyph_size_remap() in poly_keymap.c). A keyboard whose
// pack is missing that bundle — or a legend outside the latin repertoire, e.g. a
// CJK or Arabic keycap — silently falls back to GLYPH_SIZE_S rather than blanking.
//
// Values are append-only: persisted in poly_eeconf_t.glyph_size and carried on the
// wire (poly_sync_t.glyph_size), so never reorder or reuse.
enum poly_glyph_size {
    GLYPH_SIZE_S = 0,   // 27 px em / cap height 20 — the original, and the default
    GLYPH_SIZE_M = 1,   // 33 px em / cap height 24
    GLYPH_SIZE_L = 2,   // 39 px em / cap height 27
    GLYPH_SIZE_COUNT
};

// Active host-OS identity — a FIRST-CLASS state, deliberately DECOUPLED from
// unicode_mode (which stays a "how do I type codepoints" concern, host cmd 20).
// active_os drives the modifier-legend swap (Cmd/Opt vs Ctrl/Alt), the OS icon,
// and the semantic action keys (Lock/Search/Copy/… resolve per-OS at press time).
// Values are append-only: persisted in poly_eeconf_t.os_state and carried on the
// wire (HID cmd 29), so never reorder or reuse — add new OSes at the end.
#include "poly_os.h"   // enum poly_os, POLY_OS_VALUE_MASK / _AUTO_FLAG

typedef struct _poly_layer_t {
    uint32_t      crc32;
    layer_state_t layer;
    layer_state_t def_layer;
    led_t         led_state;
    uint8_t       mods;
    // Which page of the Intl latin-variation picker is showing.  This has to be
    // SYNCED, not a master-side static: the slave draws the picker slots that land
    // on its own keys and only ever sees poly_layer_t — the same reason the picker
    // latches by registering the real Ctrl instead of a private flag (both flag
    // bytes in base/com.h are full).  sync_and_refresh_displays() diffs this struct
    // with memcmp over sizeof(), so the field joins the existing sync for free.
    uint8_t       picker_page;
    // Intl letter-remap mode, synced for exactly the same reason as picker_page:
    // the slave draws the keys that land on its own half and only ever sees
    // poly_layer_t, and both flag bytes in base/com.h are full so there is no bit
    // to ride on.  See enum poly_latin_remap.
    uint8_t       remap_mode;
    // The target slot chosen in PICKKEY, so BOTH halves can invert the right keycap
    // — the target is often on the other side from the key that was pressed.
    uint8_t       remap_target;
} poly_layer_t;

// Intl letter-remap: a two-step gesture that reassigns which LETTER a key hosts,
// so several keys can carry variations of the same letter (French wants e, è, é
// and ê at once).  Distinct from the variation picker, which chooses another form
// of the letter a key ALREADY hosts.
enum poly_latin_remap {
    LATIN_REMAP_OFF     = 0,   // not remapping
    LATIN_REMAP_PICKKEY = 1,   // non-letters blanked; waiting for the target key
    LATIN_REMAP_PICKLTR = 2,   // target chosen (drawn inverted); waiting for the letter
};

typedef struct _poly_sync_t {
    uint32_t crc32;
    uint8_t  lang;
    uint8_t  contrast;
    uint8_t  flags;
    uint8_t  overlay_flags;
    uint8_t  unicode_mode;
    // Emoji layer state — synced so both halves show the same category/page.
    uint8_t  emj_category;
    uint8_t  emj_page;
    // Language layer page — synced so both halves show the same page of languages.
    uint8_t  lang_page;
    // Active idle (anti-burn-in) style, synced so the slave jitters in lockstep
    // with the master. In JITTER style each half relocates its own keys' legends
    // independently as they pulse dark (see kdisp_idle); there is no shared offset
    // to sync — only the style bit (and the pulse `contrast`) cross the link.
    uint8_t  idle_style;
    // Active host-OS identity (enum poly_os). Resolved on the master from the
    // manual pin / host push / USB detection (get_active_os) and synced here so the
    // slave's keycaps render the same legends and its semantic action keys emit the
    // same per-OS sequences. The master refreshes it every housekeeping pass.
    uint8_t  active_os;
    // Active glyph-script override (enum poly_glyph_script). Master-authoritative,
    // synced so the slave renders the same legends. See render_key / to_static_text.
    uint8_t  glyph_script;
    // Active keycap legend size (enum poly_glyph_size). Master-authoritative and
    // synced for the same reason as glyph_script: the slave draws its own keys and
    // only ever sees this struct. See render_key / glyph_size_remap.
    uint8_t  glyph_size;
    // Doom game mode active on the master (0/1). Synced so the SLAVE half turns
    // itself into a control pad: update_displays blanks every key that is not a
    // game control (doom_key_is_control). The master's keycaps are driven by the
    // game blitter directly and never see this flag (update_displays early-returns
    // there while the game runs).
    uint8_t  doom_ctl;
    // Doom weapon pad state (valid while doom_ctl): bit N-1 = number-key slot N
    // owned; ready = the slot of the weapon in hand (0 = none/unknown). The slave
    // renders its outer-column weapon pad from these.
    uint8_t  doom_wpn_owned;
    uint8_t  doom_wpn_ready;
    // Doom sound->RGB cue (valid while doom_ctl; doom_mode.c doom_rgb_task):
    // bits 0-3 = red base level 0..15 (degrading health), bits 4-5 = fire
    // pulse counter (player weapon fired -> yellow flash; wraps 1..3, 0 =
    // idle), bits 6-7 = world-sound pulse counter (monsters etc -> blue
    // flash, suppressed while firing). Both halves render it locally in
    // rgb_matrix_indicators_kb via doom_rgb_indicators().
    uint8_t  doom_rgb;
    // One-shot replay trigger for the startup ("Eden") animation. The master
    // bumps this on the HID replay command; the slave starts its own animation
    // when it sees the value change (see user_sync_poly_data_handler). It is a
    // nonce, not a state — any change triggers exactly one replay.
    uint8_t  anim_nonce;
    // FW-2 unsigned-image confirmation prompt active on the master (0/1). Synced so
    // BOTH halves turn their keycaps into the prompt: update_displays blanks every
    // key and draws A/ACCEPT (left half) or R/REJECT (right half) on the home-row
    // middle key. The answer comes back over the normal matrix pull — only the
    // master runs process_record, so it sees either half's press.
    uint8_t  fw_confirm;
    // The settings layer's advanced half is revealed (0/1) — see KC_SETTINGS_MORE.
    // Synced because the SLAVE draws its own half of that row and only ever sees
    // this struct; without it the two halves would disagree about what is visible.
    // Master-authoritative and per-visit: layer_state_set_user clears it on leaving
    // _SL, so it is never persisted and never survives a trip out of the layer.
    uint8_t  settings_more;
} poly_sync_t;

// Same reasoning as latin_sync_t's guard: transaction_rpc_exec() refuses a payload
// over the cap BEFORE sending anything, and every bulk send_to_bridge call site
// discards the ack — so outgrowing this would produce a master that applies a change
// and a slave that never hears it, with nothing in the log.
static_assert(sizeof(poly_sync_t) <= RPC_M2S_BUFFER_SIZE,
              "poly_sync_t exceeds RPC_M2S_BUFFER_SIZE — the split sync would be silently rejected");

typedef struct _poly_last_t {
    uint32_t crc32;
    uint16_t latin_kc;
} poly_last_t;

// --- Intl latin variations: which letter a key hosts, and which form it picks --
//
// Two 6-bit arrays, both indexed by TARGET SLOT (the remappable key), not by
// letter.  Today the targets are A..Z, so slot == letter index by default and the
// two readings coincide -- which is exactly why an existing EEPROM keeps working.
//
//   assign[slot]        the base LETTER whose variation row this key hosts.
//                       LATIN_ASSIGN_NONE = "this key's own natural behaviour",
//                       i.e. its own letter.  Shared across case: remapping is a
//                       property of the KEY, so Shift follows it (r -> e means
//                       Shift+r gives E's upper-case form, not <).
//   ex[slot][case]      the chosen variation WITHIN that row.  Per case, because
//                       the two rows are not parallel -- lower n has 12 variations
//                       and upper N has 11, so one shared index would be out of
//                       range for one of them.
//
// Six bits covers every letter+combining-mark form that exists in Unicode (the
// widest is O at 34 -- 16 single-mark plus 18 Vietnamese double-mark) and, for
// assign, the 26 letters plus the sentinel.  It also means BOTH arrays use the
// same accessor.
//
// ⚠️ NEVER infer "this map was never written" from its bytes -- gate it on the
// latin_pick_migrated format version instead.  An earlier version of this relied
// on LATIN_ASSIGN_NONE being all-bits-set and an unwritten EEPROM reading 0xFF.
// That is wrong here: QMK's wear-levelling normalises its backing store so cleared
// bytes arrive as ZERO (it memsets its cache to 0 and requires a 0xFF-based store
// to return the complement), so the map read back all-0x00 = "every key hosts
// letter 0" and the whole Intl layer showed variations of 'a' (field, first flash).
// LATIN_ASSIGN_NONE stays all-bits-set only because it must be a non-letter value.
//
// ⚠️ Go through latin_pick_get/set.  The old width was open-coded at the call site
// as `(pick << 4) | (cur & 0xf)`, which is precisely the shape that turns a width
// change into a silent corruption of the other case's pick.
#define LATIN_PICK_BITS    6
#define LATIN_PICK_MAX     (1u << LATIN_PICK_BITS)             /* 64 */
// Slots 0..25 are the LETTERS A..Z; 26..37 are the printable punctuation keys,
// KC_MINUS 0x2D .. KC_SLASH 0x38 — a CONTIGUOUS run of twelve, so the mapping is
// `kc - KC_MINUS + 26` with no table (see latin_target_slot()).  The run is taken
// whole rather than hand-filtered: a key that is masked or absent on a given board
// is simply unreachable there, which excludes `\` / non-US-hash by itself on the
// layouts that do not want them, and costs nothing on the ones that do.
// Only a letter can be a SOURCE — a punctuation key hosts a letter's row, never
// the other way round — so the two bounds are deliberately separate constants.
#define LATIN_LETTER_TARGETS 26
#define LATIN_PUNCT_TARGETS  12
#define LATIN_TARGETS      (LATIN_LETTER_TARGETS + LATIN_PUNCT_TARGETS)   /* 38 */
#define LATIN_PICK_FIELDS  (LATIN_TARGETS * 2)                 /* upper + lower  */
#define LATIN_PICK_BYTES   ((LATIN_PICK_FIELDS * LATIN_PICK_BITS + 7) / 8)  /* 57 */
#define LATIN_ASSIGN_BYTES ((LATIN_TARGETS * LATIN_PICK_BITS + 7) / 8)      /* 29 */

// ⚠️ EEPROM keeps the letters and the punctuation in SEPARATE blocks, and the
// letter block keeps its original offsets and size.  Growing the stored arrays in
// place would shift latin_pick_migrated, and a version byte that MOVES cannot be
// found — the load would read a pick byte as the version and could not tell a real
// format from a coincidence.  Appending instead means an old EEPROM reads the new
// block as zeros (wear-levelling, above), which is unambiguously "not written",
// and every existing pick and assignment survives untouched.
//
// The PICK split lands on a byte boundary, which is what makes it a plain memcpy:
// 52 letter fields x 6 bits = 312 = 39 bytes exactly, and the 24 punctuation
// fields x 6 = 144 = 18 bytes exactly, starting at byte 39.  The ASSIGN split does
// NOT (26 x 6 = 156 bits = 19.5 bytes), so the punctuation assignments live in
// their own array indexed FROM ZERO and are copied field-by-field, not blitted.
#define LATIN_PICK_BASE_BYTES   ((LATIN_LETTER_TARGETS * 2 * LATIN_PICK_BITS + 7) / 8) /* 39 */
#define LATIN_PICK_EXT_BYTES    (LATIN_PICK_BYTES - LATIN_PICK_BASE_BYTES)             /* 18 */
#define LATIN_ASSIGN_BASE_BYTES ((LATIN_LETTER_TARGETS * LATIN_PICK_BITS + 7) / 8)     /* 20 */
#define LATIN_ASSIGN_EXT_BYTES  ((LATIN_PUNCT_TARGETS * LATIN_PICK_BITS + 7) / 8)      /* 9  */
#define LATIN_ASSIGN_NONE  (uint8_t)(LATIN_PICK_MAX - 1u)      /* 0x3F -- see above */
// Byte fill that makes EVERY 6-bit field read LATIN_ASSIGN_NONE. 0x3F does not tile
// a byte (6 does not divide 8), so memset(map, LATIN_ASSIGN_NONE, n) would leave a
// mix of values -- it is 0xFF that fills every field with all-ones.
#define LATIN_ASSIGN_FILL  0xFFu

// Pick fields are CASE-INTERLEAVED (slot*2 + case), not case-blocked.  Growing
// LATIN_TARGETS then APPENDS fields instead of inserting a second block in the
// middle, so the existing letters keep their field indices.  Blocked layout was
// the original and is converted once at load (LATIN_PICK_INTERLEAVED).
#define LATIN_PICK_UPPER   0
#define LATIN_PICK_LOWER   1
#define latin_pick_field(slot, upper) (uint8_t)((slot) * 2u + ((upper) ? LATIN_PICK_UPPER : LATIN_PICK_LOWER))

typedef struct _latin_sync_t {
    uint32_t crc32;
    uint8_t  ex[LATIN_PICK_BYTES];
    uint8_t  assign[LATIN_ASSIGN_BYTES];
} latin_sync_t;

// ⚠️ The whole table crosses the split link in ONE RPC (USER_SYNC_LATIN_EX_DATA), and
// transaction_rpc_exec() refuses a payload larger than the buffer by returning false
// BEFORE sending anything -- while the bulk send_to_bridge() call sites discard the
// ack.  So outgrowing this cap does not fail loudly: the master applies the change,
// the slave never hears it, and half the board keeps the old legends with nothing in
// the log.  Adding targets grows this struct, so assert it rather than remember it.
static_assert(sizeof(latin_sync_t) <= RPC_M2S_BUFFER_SIZE,
              "latin_sync_t no longer fits one split RPC -- raise RPC_M2S_BUFFER_SIZE");

// A field can straddle a byte boundary (6 does not divide 8), so both helpers
// read/write a 16-bit window -- guarded by the array's own length, because the
// last field ends exactly on the final byte and buf[len] would be one past it.
// ⚠️ `len` is why these take the size rather than assuming LATIN_PICK_BYTES: the
// same accessor serves the 39-byte pick array and the 20-byte assignment map, and
// a hardcoded bound would read past the end of the shorter one.
static inline uint8_t latin_bits_get(const uint8_t* buf, uint8_t len, uint8_t field) {
    const uint16_t bit = (uint16_t)field * LATIN_PICK_BITS;
    const uint16_t by  = bit >> 3;
    const uint8_t  sh  = (uint8_t)(bit & 7u);
    uint16_t       w   = (uint16_t)buf[by];
    if (by + 1u < len) w |= (uint16_t)buf[by + 1u] << 8;
    return (uint8_t)((w >> sh) & (LATIN_PICK_MAX - 1u));
}

static inline void latin_bits_set(uint8_t* buf, uint8_t len, uint8_t field, uint8_t value) {
    const uint16_t bit  = (uint16_t)field * LATIN_PICK_BITS;
    const uint16_t by   = bit >> 3;
    const uint8_t  sh   = (uint8_t)(bit & 7u);
    const uint16_t mask = (uint16_t)(LATIN_PICK_MAX - 1u) << sh;
    const uint16_t val  = (uint16_t)(value & (LATIN_PICK_MAX - 1u)) << sh;
    buf[by]             = (uint8_t)((buf[by] & ~(uint8_t)mask) | (uint8_t)val);
    if (by + 1u < len) {
        buf[by + 1u] = (uint8_t)((buf[by + 1u] & ~(uint8_t)(mask >> 8)) | (uint8_t)(val >> 8));
    }
}

static inline uint8_t latin_pick_get(const uint8_t* ex, uint8_t field) {
    return latin_bits_get(ex, LATIN_PICK_BYTES, field);
}

static inline void latin_pick_set(uint8_t* ex, uint8_t field, uint8_t value) {
    latin_bits_set(ex, LATIN_PICK_BYTES, field, value);
}

// The letter whose variation row this key hosts, or -1 for "none".  Unassigned
// (or a stale value past the alphabet) falls back to the key's OWN letter, so a
// fresh EEPROM and a corrupt one both behave like the un-remapped keyboard — but
// only slots 0..25 have an own letter to fall back to.  An unassigned punctuation
// key has no row at all, which is what keeps `,` a comma until someone maps it.
static inline int8_t latin_assign_get(const uint8_t* assign, uint8_t slot) {
    const uint8_t v = latin_bits_get(assign, LATIN_ASSIGN_BYTES, slot);
    if (v < LATIN_LETTER_TARGETS) return (int8_t)v;
    return (slot < LATIN_LETTER_TARGETS) ? (int8_t)slot : -1;
}

static inline void latin_assign_set(uint8_t* assign, uint8_t slot, uint8_t letter) {
    latin_bits_set(assign, LATIN_ASSIGN_BYTES, slot, letter);
}

typedef struct _poly_eeconf_t {
    uint8_t lang;
    uint8_t brightness;
    uint8_t idle_style;   // enum poly_idle_style (carved out of the former uint16_t unused)
    uint8_t auto_brightness;  // host-auto state: bit7 = mode engaged, bit6 = host value known, bits0-5 = last auto value
    uint8_t latin_ex[26];
    // MRU recents for the emoji / language selection layers. Persisted only on a
    // power-suspension event (and the host save command), and only when dirty.
    // Emoji are bit-packed 14-bit category|offset codes; languages are LANG_* bytes.
    uint8_t  mru_emoji[MRU_EMOJI_PACKED];
    uint8_t  mru_lang[MRU_CAP];
    // Persisted active-OS state, packed by pack_os_state(): bit7 = manual pin
    // engaged (0 = auto mode, the default — so a fresh/zeroed EEPROM comes up in
    // auto), bit6 = a real OS value is known, bits0-5 = enum poly_os value (the
    // pin in manual mode, the last resolved OS in auto). Was the trailing
    // alignment `reserved` byte, so repurposing it keeps sizeof at
    // EECONFIG_USER_DATA_SIZE and leaves latin_ex/mru at their original offsets
    // (old EEPROMs read it as 0 = auto/unknown — a clean default migration).
    uint8_t  os_state;
    // Persisted glyph-script override (enum poly_glyph_script). Appended at the end
    // (like os_state) so latin_ex/mru offsets are unchanged; an old EEPROM reads an
    // uninitialised byte here, so load_user_eeconf() bounds-guards it to GLYPH_STD.
    // Growing EECONFIG_USER_DATA_SIZE 64->65 stayed within the reservation as it then
    // was (128), so the dynamic keymap did NOT relocate — no user EEPROM reset needed.
    // (The reservation is 256 now; the live bound is the static_assert at the end of
    // this header, not this note.)
    uint8_t  glyph_script;
    // First-boot marker for the one-time startup animation. Appended tail byte
    // (same pattern as os_state/glyph_script) so earlier offsets are unchanged.
    // A fresh/erased EEPROM reads 0xFF (or 0 after eeconfig_init) — anything other
    // than BOOT_INTRO_DONE means "not yet played", so the intro runs once and then
    // writes BOOT_INTRO_DONE. Growing EECONFIG_USER_DATA_SIZE 65->66 likewise stayed
    // within the then-128-byte reservation: no keymap relocation / user reset.
    uint8_t  boot_flags;
    // Widened Intl variation picks (6 bits per case -- see LATIN_PICK_* above).
    // APPENDED rather than widening latin_ex[] in place: that array sits ahead of
    // mru_emoji/mru_lang, so resizing it would shift every later field and an
    // existing EEPROM would read its MRU lists out of the shifted bytes.
    // ⚠️ Sized LATIN_PICK_BASE_BYTES, NOT LATIN_PICK_BYTES: this array must keep the
    // size it shipped with.  Growing it in place is what would shift the version
    // byte below, and a version byte that MOVES cannot be found -- the load would
    // read a pick byte where it expects the sentinel.  The extra targets go in
    // latin_ex_ext at the tail instead.
    uint8_t  latin_ex_wide[LATIN_PICK_BASE_BYTES];
    // FORMAT VERSION for latin_ex_wide, not a boolean. "Looks empty" is NOT
    // decidable here -- all-zero is a legitimate every-letter-on-its-first-variation
    // state and erased flash reads 0xFF -- so each conversion is gated on an
    // explicit sentinel instead of a guess. Anything unrecognised means the legacy
    // one-byte-per-letter nibble pairs in latin_ex[].
    //   LATIN_PICK_MIGRATED    6-bit fields, CASE-BLOCKED  (case*26 + letter)
    //   LATIN_PICK_INTERLEAVED 6-bit fields, CASE-INTERLEAVED (slot*2 + case)
    uint8_t  latin_pick_migrated;
    // Which base letter each target key hosts. Appended at the tail (like os_state
    // / glyph_script) so every earlier offset is untouched. It needs NO sentinel:
    // LATIN_ASSIGN_NONE is all-bits-set, so the 0xFF an old EEPROM reads here is
    // already "every key unassigned". ⚠️ That is also why eeconfig_init_user() has
    // to memset this to 0xFF rather than let the struct-wide {0} stand -- zeroed,
    // it would read as "every key assigned to A".
    uint8_t  latin_assign[LATIN_ASSIGN_BASE_BYTES];
    // Punctuation targets (slots 26..37), APPENDED rather than grown into the two
    // arrays above — see the byte-boundary note by LATIN_PICK_BASE_BYTES. An old
    // EEPROM reads this whole block as zeros, so latin_ext_fmt != LATIN_EXT_OK is
    // an unambiguous "never written" and the letters keep their offsets and data.
    uint8_t  latin_ex_ext[LATIN_PICK_EXT_BYTES];
    uint8_t  latin_assign_ext[LATIN_ASSIGN_EXT_BYTES];
    uint8_t  latin_ext_fmt;
    // Persisted keycap legend size (enum poly_glyph_size). Appended at the tail, so
    // every earlier offset is untouched. It needs NO sentinel and no migration: the
    // default GLYPH_SIZE_S is 0, and an EEPROM written before this field existed
    // reads back as 0 here — QMK's wear levelling normalises cleared bytes to ZERO,
    // not 0xFF (the trap that made latin_assign read as "every key hosts 'a'"), and
    // for once zero is exactly the value we want. load_user_eeconf() still bounds-
    // guards it so a stale byte outside the enum cannot select a missing face.
    uint8_t  glyph_size;
    // Which LAYER ENUM the stored dynamic keymap was written against. Unlike every
    // other tail byte here this is not a setting — it exists because the dynamic
    // keymap is indexed BY LAYER NUMBER, so removing a layer silently repoints every
    // stored layer above it (dropping _FL1 slid _NL 7->6, _UL 8->7, ...). Nothing in
    // QMK versions that block, so without a gate a board would come up running the
    // old _FL1 data as its numpad layer. Zero is the "written by an older build"
    // value: `poly_eeconf_t ee = {0}` in eeconfig_init_user() leaves it zero on a
    // fresh EEPROM too, and a fresh EEPROM wants the reset just as much.
    uint8_t  keymap_layers_fmt;
} poly_eeconf_t;

#define BOOT_INTRO_DONE     0x5A   // sentinel written after the startup animation has played
// Format versions for latin_ex_wide + latin_assign, oldest first. Anything
// unrecognised means the legacy one-byte-per-letter nibble pairs in latin_ex[].
#define LATIN_PICK_MIGRATED    0xA5 // 6-bit fields, case-BLOCKED; no assignment map
#define LATIN_PICK_INTERLEAVED 0xC3 // ...case-INTERLEAVED; assignment map UNTRUSTWORTHY
#define LATIN_PICK_ASSIGN_OK   0xD7 // ...and the assignment map is real (letters only)
#define LATIN_EXT_OK           0x6B // latin_*_ext hold the punctuation targets
// Layer-enum revision the stored dynamic keymap matches. Bump this ONLY when a layer
// is added, removed or reordered — not when a layer's CONTENTS change, which needs no
// reset. Anything else (0 included) means "written against a different enum, discard".
#define KEYMAP_LAYERS_FL_MERGED 0x71 // single _FL; _NL/_UL/_SL shifted down one
// ⚠️ 0xC3 is deliberately NOT the current version. The build that introduced it
// assumed an unwritten EEPROM reads 0xFF, so it copied the assignment map straight
// out of a block that had never held one — read back as all-zero, i.e. "every key
// hosts letter 0", and every Intl keycap rendered a variation of 'a' (field, first
// flash). Worse, the next suspend PERSISTED those zeros under 0xC3, so a version
// gate alone cannot tell them from a real map. Retiring the value is what heals an
// already-flashed board: 0xC3 now means "picks are fine, discard the map".


static_assert(sizeof(poly_eeconf_t) == EECONFIG_USER_DATA_SIZE, "Mismatch in keyboard EECONFIG stored data");
// The user datablock must fit inside the budget reserved ahead of the pinned
// dynamic-keymap base (see DYNAMIC_KEYMAP_EEPROM_ADDR in config.h); otherwise it
// would overlap the stored keymap. Raise POLY_EECONFIG_USER_RESERVED (a one-time
// keymap relocation/reset) if this trips.
static_assert(EECONFIG_USER_DATA_SIZE <= POLY_EECONFIG_USER_RESERVED, "poly_eeconf_t exceeds POLY_EECONFIG_USER_RESERVED — bump the reservation (one-time reset)");

// Pin the two dynamic-keymap counts to the layer enum itself. Both were hand-kept
// numbers that a layer add/remove silently invalidates, and the failure is quiet in
// the worst way: the keymap is stored BY LAYER INDEX, so a stale cap does not error,
// it just serves the previous occupant of every slot that moved. Collapsing
// _FL0/_FL1 is exactly that change, which is why these are asserts now rather than
// a comment asking the next person to remember.
//   * DYNAMIC_KEYMAP_LAYER_COUNT must cover every compiled layer (QMK's own rule).
//   * The write cap IS the first read-only layer: _SL and everything above it is
//     served from flash by poly_keycode_at(), so the host must not be told it can
//     write there. Anything that moves _SL moves the cap with it.
static_assert(_EMJ + 1 <= DYNAMIC_KEYMAP_LAYER_COUNT,
              "DYNAMIC_KEYMAP_LAYER_COUNT is below the number of compiled layers");
static_assert(_SL == DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT,
              "DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT must equal _SL — the first flash-served layer");

void reset_all_states_and_layers(void);

const poly_layer_t* get_local_layer(void);
poly_layer_t* access_local_layer(void);
void copy_local_layer(const poly_layer_t *value);
const poly_layer_t* get_global_layer(void);
void copy_global_layer(const poly_layer_t *value);

const poly_sync_t* get_local_state(void);
poly_sync_t* access_local_state(void);
void copy_local_state(const poly_sync_t* value);
const poly_sync_t* get_global_state(void);
poly_sync_t* access_global_state(void);
void copy_global_state(const poly_sync_t* value);

const poly_last_t* get_local_last_latin(void);
poly_last_t* access_local_last_latin(void);
void set_local_last_latin_keycode(uint16_t keycode);
uint16_t get_local_last_latin_keycode(void);
void copy_local_last_latin(const poly_last_t* value);

const poly_last_t* get_global_last_latin(void);
poly_last_t* access_global_last_latin(void);
uint16_t get_global_last_latin_keycode(void);
void copy_global_last_latin(const poly_last_t* value);

const latin_sync_t* get_global_latin_table(void);
latin_sync_t* access_global_latin_table(void);
void copy_global_latin_table(const latin_sync_t* value);


// Writes only lang+brightness (4 bytes) to EEPROM. Use when only settings changed, not the latin table.
void save_user_settings(void);

// Writes only the 26-byte latin extension table to EEPROM. Use when only latin mappings changed.
void save_user_latin(void);

// Writes the emoji/language MRU lists to EEPROM, but only if they changed since
// the last load/save (mru_dirty()). Called on both halves from suspend (and on
// the master from the host save command); each half persists its own copy.
void save_user_mru_if_dirty(void);

// Loads the persisted MRU lists from EEPROM into the RAM lists (mru_load()).
void load_user_mru(void);

// Saves both settings and latin table (convenience wrapper around the two above).
void stamp_keymap_layers_fmt(void);
void save_user_eeconf(void);

// Loads user keyboard configuration from EEPROM with brightness validation against maximum.
poly_eeconf_t load_user_eeconf(void);

// Increments brightness by BRIGHT_STEP with clamping to FULL_BRIGHT (deferred EEPROM write).
void inc_brightness(void);

// Decrements brightness by BRIGHT_STEP with clamping to MIN_BRIGHT (deferred EEPROM write).
void dec_brightness(void);

// Sets contrast to a deliberate user-chosen level and records it as the brightness
// to persist (deferred EEPROM write). Transient contrast changes (suspend, idle
// fade) must NOT use this — they write l_state.contrast directly.
void set_user_brightness(uint8_t value);

// Records the intended user brightness without marking settings dirty (boot-time
// load, slave adopting an awake master's synced contrast).
void note_user_brightness(uint8_t value);

// The deliberate user brightness — the value that gets persisted (tracks
// unflushed changes). Restore paths should use get_active_brightness() instead.
uint8_t get_user_brightness(void);

// Host-driven (daylight/auto) brightness mode (RAM-only, never persisted).
bool get_brightness_auto_mode(void);
void set_brightness_auto_mode(bool on);
void toggle_brightness_auto_mode(void);

// Apply a host auto/daylight brightness update: volatile, applied only while
// auto mode is engaged, leaves the deliberate brightness intact. The mode + last
// value ARE persisted (see pack/load_auto_brightness) so the keyboard restores
// host-auto brightness after a reboot instead of the stale manual value.
void set_auto_brightness_value(uint8_t value);

// Persist/restore the host-auto state across reboots, packed into the single
// poly_eeconf_t.auto_brightness byte (bit7 = mode engaged, bit6 = a real host
// value is known, bits0-5 = last auto value). load_auto_brightness() is called at
// boot; a reboot while host-auto was engaged then comes up at the last auto
// brightness (or the manual one if no host value had arrived yet), not the stale
// manual value.
uint8_t pack_auto_brightness(void);
void load_auto_brightness(uint8_t packed);

// The brightness currently in effect: auto ? last host auto value : the user
// brightness. Idle/suspend/fade restore paths use this.
uint8_t get_active_brightness(void);

// Marks settings (lang + brightness) as needing an EEPROM write at the next flush.
void mark_settings_dirty(void);

// The active idle (anti-burn-in) display style — see enum poly_idle_style.
uint8_t get_idle_style(void);

// Sets the idle style and marks settings dirty (deferred EEPROM write). Out-of-range
// values are ignored. Used by the HID toggle (cmd 28).
void set_idle_style(uint8_t style);

// Records the idle style without marking settings dirty (boot-time EEPROM load).
void note_idle_style(uint8_t style);

// Human-readable name of an idle style, for console logs ("pulse"/"jitter"/…).
// Never NULL — an unknown value reads as "?".
const char* idle_style_name(uint8_t style);

// ---- Glyph-script override (enum poly_glyph_script) — see enum comment above. ----

// The active glyph-script override (GLYPH_STD = normal language legends).
uint8_t get_glyph_script(void);

// Sets the glyph script and marks it dirty (deferred EEPROM write). Out-of-range
// values are ignored. Used by the HID command (cmd 30).
void set_glyph_script(uint8_t script);

// Records the glyph script without marking dirty (boot-time EEPROM load); an
// out-of-range (uninitialised-EEPROM) value falls back to GLYPH_STD.
void note_glyph_script(uint8_t script);

// ---- Keycap legend size (enum poly_glyph_size) — see the enum comment above. ----

// The active legend size (GLYPH_SIZE_S = the original 27 px face).
uint8_t get_glyph_size(void);

// Sets the legend size and marks it dirty (deferred EEPROM write). Used by the HID
// command (cmd 34) and the KC_GLYPH_SIZE_UP/_DOWN keys. Out-of-range values are ignored —
// unlike glyph_script this range is CLOSED, because a size is not an open-ended
// catalogue of faces: every value has to name a tier the render path knows how to
// place, so an unknown one is a bug, not a graceful degradation.
void set_glyph_size(uint8_t size);

// Records the legend size without marking dirty (boot-time EEPROM load); an
// out-of-range value falls back to GLYPH_SIZE_S.
void note_glyph_size(uint8_t size);

// Steps the legend size one tier (delta +1/-1), CLAMPED at small and large — the
// KC_GLYPH_SIZE_UP / _DOWN keys. WRAPPING, not clamping. That is a consequence of
// the layer now carrying ONE size key rather than a pair: a clamped end tier leaves
// that key looking dead until you remember to hold Shift, whereas the digit in its
// legend already tells you which tier you are on, so a cycle cannot get you lost.
// (The older per-direction pair wanted the opposite — a "bigger" keycap that jumped
// to small would have contradicted its own arrow.)
void step_glyph_size(int8_t delta);

// Human-readable name of a legend size, for console logs ("small"/"medium"/…).
// Never NULL — an unknown value reads as "?".
const char* glyph_size_name(uint8_t size);

// ---- First-boot startup animation marker (poly_eeconf_t.boot_flags) ----
// Records the boot_flags byte at EEPROM load (no dirty flag).
void note_boot_flags(uint8_t flags);
// True until BOOT_INTRO_DONE has been persisted — i.e. the intro hasn't played.
bool boot_intro_pending(void);
// Persist BOOT_INTRO_DONE (one-time tail-byte write) so the intro won't replay.
void mark_boot_intro_done(void);

// ---- Active host-OS (enum poly_os) — see the poly_os comment in this header. ----

// The OS currently in effect: the manual pin while pinned, else the last resolved
// (host/detection) OS in auto mode, else POLY_OS_UNKNOWN. Rendering/semantic-action
// code on the MASTER uses this; the slave reads the synced poly_sync_t.active_os.
uint8_t get_active_os(void);

// True while auto mode is engaged (detection + host pushes accepted). Manual mode
// (a pin) ignores both. Auto is the default for a fresh EEPROM.
bool get_os_auto_mode(void);

// Engage/leave auto mode without changing the pin/resolved value (deferred flush).
void set_os_auto_mode(bool on);

// Pin the OS explicitly (manual mode): wins over host + detection, survives
// reboots. The only path that can select Android. Deferred EEPROM flush.
void set_user_os(uint8_t os);

// Host-pushed OS (HID cmd 29): applied only in auto mode; beats detection for the
// rest of the session (set_detected_os yields once the host has spoken).
void set_host_os(uint8_t os);

// Firmware USB-detected OS (QMK os_detection callback): applied only in auto mode
// and only while the host has NOT pushed one this session. Deferred EEPROM flush.
void set_detected_os(uint8_t os);

// Persist/restore the active-OS state across reboots, packed into the single
// poly_eeconf_t.os_state byte (see pack/load below). load_os_state() runs at boot.
uint8_t pack_os_state(void);
void load_os_state(uint8_t packed);

// Queues a default-layer EEPROM write — safe from a sync handler. Written at the next flush.
void defer_default_layer_save(layer_state_t def_layer);

// Marks the latin extension table as needing an EEPROM write (deferred to the next flush).
void mark_latin_dirty(void);

// Flushes every dirty user-state block (settings, latin, default layer, MRU) to
// EEPROM at once. Call only at flush points (suspend / host shutdown / store key).
void save_all_dirty(void);

// Requests a deferred flush-all — safe from a sync handler (sets a flag only).
void request_eeprom_save(void);

// Performs the flush-all if one was requested. Call from housekeeping_task_user().
void save_all_if_requested(void);

