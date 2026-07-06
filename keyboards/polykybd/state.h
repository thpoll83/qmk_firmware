// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include "quantum.h"
#include "mru.h"

// Idle (anti-burn-in) display style, persisted in poly_eeconf_t.idle_style and
// toggled over HID (cmd 28). PULSE is the legacy contrast-only breathing; JITTER
// adds a per-cycle relocation of the legend so the lit pixels migrate over time.
// DOOM runs the doom easter egg's attract demo as a screensaver instead of the
// pulse (doom/README.md) — chrome-free, dismissed by the first key press; on a
// build/board where the demo can't start (no POLYKYBD_DOOM, staging active) it
// falls back to PULSE at runtime, so the value is always safe to accept/persist.
// Values are append-only (persisted + on the wire in poly_sync_t.idle_style).
enum poly_idle_style {
    IDLE_STYLE_PULSE  = 0,
    IDLE_STYLE_JITTER = 1,
    IDLE_STYLE_IDDQD  = 2,   // doom attract-demo screensaver (host: IdleStyle.IDDQD)
    IDLE_STYLE_COUNT
};

// Glyph-script override (HID cmd 30, protocol v9+). Replaces the language-layer
// letter/digit legends with an alternative script's glyphs while leaving overlays
// and OS-hints untouched. GLYPH_STD is the normal (language) legends. Values are
// append-only: persisted in poly_eeconf_t.glyph_script and carried on the wire
// (poly_sync_t.glyph_script), so never reorder or reuse — add new scripts at the end.
enum poly_glyph_script {
    GLYPH_STD     = 0,   // normal language legends (no override)
    GLYPH_TENGWAR = 1,   // Tengwar (Alcarin, CSUR mapping) — "fantasy" font-pack bundle
    GLYPH_SCRIPT_COUNT
};

// Active host-OS identity — a FIRST-CLASS state, deliberately DECOUPLED from
// unicode_mode (which stays a "how do I type codepoints" concern, host cmd 20).
// active_os drives the modifier-legend swap (Cmd/Opt vs Ctrl/Alt), the OS icon,
// and the semantic action keys (Lock/Search/Copy/… resolve per-OS at press time).
// Values are append-only: persisted in poly_eeconf_t.os_state and carried on the
// wire (HID cmd 29), so never reorder or reuse — add new OSes at the end.
// Sources are reconciled by resolve/get_active_os(): a manual pin wins; otherwise
// host push (cmd 29) beats firmware USB detection (QMK os_detection). Android can
// only ever come from a manual pin — QMK detection reports Android (and ChromeOS)
// as Linux.
enum poly_os {
    POLY_OS_UNKNOWN = 0,
    POLY_OS_WINDOWS = 1,
    POLY_OS_MACOS   = 2,
    POLY_OS_LINUX   = 3,
    POLY_OS_ANDROID = 4,
    // 5 = reserved, **currently not used**. Was POLY_OS_IOS; folded into macOS
    // (identical Cmd/Opt behaviour, and the host never runs on iOS). The slot is
    // kept and never reused, to honour the append-only wire contract (cmd 29 /
    // persisted os_state — a protocol-7 keyboard may still hold this value) and to
    // keep the GNOME/KDE values below pinned at 6/7, matching the host.
    POLY_OS_IOS     = 5,   // reserved / not used — folded into POLY_OS_MACOS
    // Host-detected Linux desktop environments (from XDG_CURRENT_DESKTOP, pushed
    // over cmd 29). They refine the Super-key shortcut hints — GNOME and KDE bind
    // the launcher/window-switcher differently — and otherwise behave as Linux.
    // Not manually pinnable (the KC_OS_SET_* keys stop at Android); a manual Linux
    // pin or USB detection yields the generic POLY_OS_LINUX.
    POLY_OS_LINUX_GNOME = 6,
    POLY_OS_LINUX_KDE   = 7,
    POLY_OS_COUNT
};

// poly_sync_t.active_os packs the resolved OS in the low bits plus the auto-mode
// flag in bit7, so the slave can render the OS *and* the auto/pin badge from the
// one synced byte. Readers that want the bare OS (e.g. emit_os_action) mask with
// POLY_OS_VALUE_MASK; the icon decodes POLY_OS_AUTO_FLAG for the badge.
#define POLY_OS_VALUE_MASK 0x7Fu
#define POLY_OS_AUTO_FLAG  0x80u

typedef struct _poly_layer_t {
    uint32_t      crc32;
    layer_state_t layer;
    layer_state_t def_layer;
    led_t         led_state;
    uint8_t       mods;
} poly_layer_t;

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
} poly_sync_t;

typedef struct _poly_last_t {
    uint32_t crc32;
    uint16_t latin_kc;
} poly_last_t;

typedef struct _latin_sync_t {
    uint32_t crc32;
    uint8_t  ex[26];
} latin_sync_t;

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
    // Growing EECONFIG_USER_DATA_SIZE 64->65 stays within POLY_EECONFIG_USER_RESERVED
    // (128), so the dynamic keymap does NOT relocate — no user EEPROM reset needed.
    uint8_t  glyph_script;
} poly_eeconf_t;


static_assert(sizeof(poly_eeconf_t) == EECONFIG_USER_DATA_SIZE, "Mismatch in keyboard EECONFIG stored data");
// The user datablock must fit inside the budget reserved ahead of the pinned
// dynamic-keymap base (see DYNAMIC_KEYMAP_EEPROM_ADDR in config.h); otherwise it
// would overlap the stored keymap. Raise POLY_EECONFIG_USER_RESERVED (a one-time
// keymap relocation/reset) if this trips.
static_assert(EECONFIG_USER_DATA_SIZE <= POLY_EECONFIG_USER_RESERVED, "poly_eeconf_t exceeds POLY_EECONFIG_USER_RESERVED — bump the reservation (one-time reset)");

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

// ---- Glyph-script override (enum poly_glyph_script) — see enum comment above. ----

// The active glyph-script override (GLYPH_STD = normal language legends).
uint8_t get_glyph_script(void);

// Sets the glyph script and marks it dirty (deferred EEPROM write). Out-of-range
// values are ignored. Used by the HID command (cmd 30).
void set_glyph_script(uint8_t script);

// Records the glyph script without marking dirty (boot-time EEPROM load); an
// out-of-range (uninitialised-EEPROM) value falls back to GLYPH_STD.
void note_glyph_script(uint8_t script);

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

