// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "state.h"

#include <string.h>
#include "eeconfig.h"
#include "base/com.h"   // enum poly_flag — the display-state bits guarding the auto push
#include "base/mode_byte.h"

static poly_layer_t l_layer;
static poly_layer_t g_layer;

static poly_sync_t l_state;
static poly_sync_t g_state;

static bool g_brightness_dirty = false;   // lang + brightness need a flush

// The brightness the user actually chose — the value that gets persisted.
// l_state.contrast also carries transient values (DISP_OFF during suspend,
// faded/pulsing levels during idle), and the flush points (suspend/shutdown)
// run exactly when such a transient is live, so the save path must read this
// snapshot instead of the live contrast.
static uint8_t g_user_brightness = FULL_BRIGHT;

// Active idle (anti-burn-in) display style — persisted alongside lang+brightness.
static uint8_t g_idle_style = POLY_DEFAULT_IDLE_STYLE;

// Active glyph-script override (enum poly_glyph_script). Persisted at the tail of
// poly_eeconf_t (its own byte, like os_state) and flushed via g_glyph_dirty.
static uint8_t g_glyph_script = GLYPH_STD;
static bool    g_glyph_dirty  = false;

// Active keycap legend size (enum poly_glyph_size). Persisted at the tail of
// poly_eeconf_t (its own byte, like glyph_script) and flushed via g_gsize_dirty.
static uint8_t g_glyph_size  = GLYPH_SIZE_S;
static bool    g_gsize_dirty = false;
// First-boot startup-animation marker (poly_eeconf_t.boot_flags). 0xFF/0 (fresh
// EEPROM) => intro pending; BOOT_INTRO_DONE => already played.
static uint8_t g_boot_flags = 0xFF;
static bool    g_boot_dirty = false;

// Host-driven (daylight/auto) brightness mode. While engaged, the keyboard
// applies the host's VOLATILE brightness updates and restores to the last auto
// value after idle/wake — but that value is NEVER persisted to EEPROM. Any
// deliberate set (keyboard brightness keys, or a host explicit/non-volatile
// set) leaves auto mode so the manual choice wins until auto is re-engaged.
// RAM-only: defaults off at boot; the host re-asserts it on connect from its
// daylight setting.
static bool    g_auto_brightness      = false;
static uint8_t g_last_auto_brightness = FULL_BRIGHT;
// True once the host has pushed at least one auto/daylight value this session.
// Until then, engaging auto must NOT jump to the FULL_BRIGHT default above —
// fall back to the user's own brightness instead (see get_active_brightness).
static bool    g_auto_value_known     = false;

// ---- Active host-OS (enum poly_os). Mirrors the brightness auto/manual model. ----
// g_user_os is the manual PIN (only set at deliberate set-points); g_resolved_os is
// the last value learned from host/detection while in auto mode. The mode + the
// relevant value are persisted in poly_eeconf_t.os_state (pack/load_os_state) so a
// reboot restores them — a daily host-switcher stays in auto (re-resolves per plug),
// an Android/locked-down user pins manually and it sticks.
static uint8_t g_user_os      = POLY_OS_UNKNOWN;  // the manual pin
static uint8_t g_resolved_os  = POLY_OS_UNKNOWN;  // last host/detection result (auto mode)
static bool    g_os_auto      = true;             // default: auto (accept host + detection)
static bool    g_os_known     = false;            // a real OS has been resolved/pinned
// Host has pushed an OS this session -> firmware USB detection yields to it (host
// wins). RAM-only, so it resets on every boot/re-enumeration, which is correct:
// detection then fills in again until/unless the host re-pushes.
static bool    g_host_os_seen = false;
static bool    g_os_dirty     = false;

static bool          g_def_layer_dirty = false;
static layer_state_t g_def_layer_pending = 0;

// Latin extension table needs a flush (set on edit / on a sync from the master).
static bool g_latin_dirty = false;

// One-shot "flush all user state" request, raised by the store key (locally on
// the master, via the SAVE_EEPROM sync flag on the slave) and drained from
// housekeeping so the actual flash write never happens inside a sync handler.
static bool g_save_pending = false;

static poly_last_t l_last;
static poly_last_t g_last;

static latin_sync_t g_latin;

void reset_all_states_and_layers(void) {
    memset(&l_layer, 0, sizeof(l_layer));
    memset(&g_layer, 0, sizeof(g_layer));
    memset(&l_state, 0, sizeof(l_state));
    memset(&g_state, 0, sizeof(g_state));
}

// Getters and setters for l_layer
const poly_layer_t* get_local_layer(void) {
    return &l_layer;
}

poly_layer_t* access_local_layer(void) {
    return &l_layer;
}

void copy_local_layer(const poly_layer_t *value) {
    memcpy(&l_layer, value, sizeof(poly_layer_t));
}

// Getters and setters for g_layer
const poly_layer_t* get_global_layer(void) {
    return &g_layer;
}

void copy_global_layer(const poly_layer_t *value) {
    memcpy(&g_layer, value, sizeof(poly_layer_t));
}


// Getters and setters for l_state
const poly_sync_t* get_local_state(void) {
    return &l_state;
}

poly_sync_t* access_local_state(void) {
    return &l_state;
}

void copy_local_state(const poly_sync_t* value) {
    memcpy(&l_state, value, sizeof(poly_sync_t));
}

// Getters and setters for g_state
const poly_sync_t* get_global_state(void) {
    return &g_state;
}

poly_sync_t* access_global_state(void) {
    return &g_state;
}

void copy_global_state(const poly_sync_t* value) {
    memcpy(&g_state, value, sizeof(poly_sync_t));
}


// Getters and setters for l_last
const poly_last_t* get_local_last_latin(void) {
    return &l_last;
}

poly_last_t* access_local_last_latin(void) {
    return &l_last;
}

uint16_t get_local_last_latin_keycode(void) {
    return l_last.latin_kc;
}

void set_local_last_latin_keycode(uint16_t keycode) {
    l_last.latin_kc = keycode;
}

void copy_local_last_latin(const poly_last_t* value) {
    memcpy(&l_last, value, sizeof(poly_last_t));
}

const poly_last_t* get_global_last_latin(void) {
    return &g_last;
}

poly_last_t* access_global_last_latin_keycode(void) {
    return &g_last;
}

void copy_global_last_latin(const poly_last_t* value) {
    memcpy(&g_last, value, sizeof(poly_last_t));
}

// Getters and setters for g_latin
const latin_sync_t* get_global_latin_table(void) {
    return &g_latin;
}

latin_sync_t* access_global_latin_table(void) {
    return &g_latin;
}

void copy_global_latin_table(const latin_sync_t* value) {
    memcpy(&g_latin, value, sizeof(latin_sync_t));
}


// The EEPROM block I/O — everything that knows the poly_eeconf_t offsets and
// calls eeconfig_* — lives in state_store.c (the persistence half of this
// module); this file owns the RAM state, the policy and the dirty flags, and
// the store reads the values it persists through the public getters.

// Increments brightness by BRIGHT_STEP with clamping to FULL_BRIGHT.
// EEPROM write is deferred to the next flush (suspend / store key) via the dirty flag.
// Global variables: l_state
void inc_brightness(void) {
    if (l_state.contrast < FULL_BRIGHT) {
        l_state.contrast += BRIGHT_STEP;
    }
    if (l_state.contrast > FULL_BRIGHT) {
        l_state.contrast = FULL_BRIGHT;
    }
    g_user_brightness  = l_state.contrast;
    g_auto_brightness  = false;   // deliberate change — leave host-auto mode
    g_brightness_dirty = true;
}

// Decrements brightness by BRIGHT_STEP with clamping to MIN_BRIGHT.
// EEPROM write is deferred to the next flush (suspend / store key) via the dirty flag.
// Global variables: l_state
void dec_brightness(void) {
    if (l_state.contrast > (MIN_BRIGHT + BRIGHT_STEP)) {
        l_state.contrast -= BRIGHT_STEP;
    } else {
        l_state.contrast = MIN_BRIGHT;
    }
    g_user_brightness  = l_state.contrast;
    g_auto_brightness  = false;   // deliberate change — leave host-auto mode
    g_brightness_dirty = true;
}

// Sets the display contrast to a deliberate user-chosen level and records it as
// the brightness to persist at the next flush. Use for the preset keys and the
// host's set-brightness command; transient contrast changes (suspend, idle fade)
// must write l_state.contrast directly so they are never persisted.
void set_user_brightness(uint8_t value) {
    l_state.contrast   = value;
    g_user_brightness  = value;
    g_auto_brightness  = false;   // deliberate change — leave host-auto mode
    g_brightness_dirty = true;
}

// True while host-driven (daylight/auto) brightness is engaged.
bool get_brightness_auto_mode(void) {
    return g_auto_brightness;
}

// True only while the panels are actually showing the awake brightness. False
// once they are dark (USB suspend, or the TURN_OFF_TIME branch) and while the
// idle fade/pulse owns the contrast.
//
// ⚠️ The host-auto (daylight) and LTR-559 pushes below are UNATTENDED — they fire
// on a timer with no user in the loop — so they must not write l_state.contrast in
// those states. Writing it after TURN_OFF_TIME is what re-lit the whole keyboard:
// the turn-off branch calls disable_idle_tracking(), so the housekeeping idle
// ladder no longer runs, and the contrast diff switched every keycap OLED back on
// with nothing left to ever switch them off again (field 2026-07-30: 10-min
// daylight push six minutes after "Turning off" → both halves lit until the next
// keypress). The value is still remembered in g_last_auto_brightness, and every
// wake path restores through get_active_brightness(), so it lands on the next
// real wake instead.
static bool displays_awake(void) {
    return l_state.contrast != DISP_OFF &&
           (l_state.flags & (uint8_t)STATUS_DISP_ON) != 0 &&
           (l_state.flags & ((uint8_t)DISP_IDLE | (uint8_t)IDLE_TRANSITION)) == 0;
}

// Engage/disengage host-driven brightness, applying the resulting active
// brightness to the display. The mode (and last value, below) is persisted so a
// reboot in auto mode restores the auto brightness, not the stale manual value.
void set_brightness_auto_mode(bool on) {
    g_auto_brightness = on;
    if (displays_awake()) {
        l_state.contrast = get_active_brightness();
    }
    g_brightness_dirty = true;   // persist the new auto-mode state (flushed at suspend/store)
}

// Toggle for the KC_DAUTO key.
void toggle_brightness_auto_mode(void) {
    set_brightness_auto_mode(!g_auto_brightness);
}

// Apply a host auto/daylight brightness update. Remembered as the restore
// target and applied only while auto mode is engaged. NEVER persisted, and it
// does not change the deliberate user brightness or the auto mode itself.
void set_auto_brightness_value(uint8_t value) {
    g_last_auto_brightness = value;
    g_auto_value_known     = true;
    if (g_auto_brightness) {
        if (displays_awake()) {      // never light dark/idling panels — see displays_awake()
            l_state.contrast = value;
        }
        g_brightness_dirty = true;   // remember the latest auto value for the next boot
    }
}

// The brightness currently in effect: the last host auto value while auto mode
// is engaged AND the host has actually pushed one, otherwise the deliberate user
// brightness. Idle/suspend/fade restore paths use this so they restore whatever
// is actually driving. The `g_auto_value_known` guard stops a just-engaged auto
// mode (e.g. the KC_DAUTO key) from snapping to FULL_BRIGHT before the host's
// first daylight value arrives — it holds the user's brightness until then.
uint8_t get_active_brightness(void) {
    return (g_auto_brightness && g_auto_value_known) ? g_last_auto_brightness
                                                     : g_user_brightness;
}

// Records the intended user brightness without marking settings dirty (boot-time
// load, slave adopting an awake master's synced contrast).
void note_user_brightness(uint8_t value) {
    g_user_brightness = value;
}

// Pack the host-auto state into one EEPROM byte: bit7 = mode engaged, bit6 = a
// real host value is known, bits0-5 = last auto value (0..FULL_BRIGHT < 64). The
// known bit matters: engaging auto before the host pushes a value must NOT persist
// the default g_last_auto_brightness as if real — otherwise the next boot would
// treat it as known and snap to it (the very FULL_BRIGHT jump get_active_brightness
// guards against at runtime). 0 when auto is off.
uint8_t pack_auto_brightness(void) {
    if (!g_auto_brightness) return 0u;        // auto off (bit7 clear = off here)
    return mode_byte_pack(true, g_auto_value_known,
                          g_auto_value_known ? g_last_auto_brightness : 0u);
}

// Restore the host-auto state at boot from the packed byte. If auto was engaged
// AND a real host value was stored, come up in auto mode at that value
// (g_auto_value_known so get_active_brightness uses it) — the host refreshes it on
// connect. If auto was engaged with no host value yet, come up in auto mode but
// fall back to the manual brightness (g_auto_value_known stays false). Off-state
// leaves the manual brightness (l_state.contrast = ee.brightness) untouched.
void load_auto_brightness(uint8_t packed) {
    bool on = mode_byte_flag(packed);
    g_auto_brightness = on;
    if (on) {
        if (mode_byte_known(packed)) {         // a real host auto value was persisted
            uint8_t value = mode_byte_value(packed);
            if (value > FULL_BRIGHT) value = FULL_BRIGHT;
            g_last_auto_brightness = value;
            g_auto_value_known     = true;
        }
        // else: auto engaged but no host value yet -> g_auto_value_known stays false,
        // so get_active_brightness() returns the manual brightness until the host pushes.
        l_state.contrast = get_active_brightness();
    }
}

// The brightness to restore after idle/suspend. Unlike a load_user_eeconf()
// round-trip this tracks changes that have not been flushed to EEPROM yet.
uint8_t get_user_brightness(void) {
    return g_user_brightness;
}

// Marks settings (lang + brightness) as needing an EEPROM write at the next flush.
void mark_settings_dirty(void) {
    g_brightness_dirty = true;
}

// The active idle (anti-burn-in) display style.
uint8_t get_idle_style(void) {
    return g_idle_style;
}

// Sets the idle style and marks the settings block dirty (flushed at the next
// suspend / store). Out-of-range values are ignored.
void set_idle_style(uint8_t style) {
    if (style >= IDLE_STYLE_COUNT) {
        return;
    }
    g_idle_style = style;
    g_brightness_dirty = true;
}

// Records the idle style without marking settings dirty (boot-time EEPROM load).
void note_idle_style(uint8_t style) {
    g_idle_style = (style < IDLE_STYLE_COUNT) ? style : POLY_DEFAULT_IDLE_STYLE;
}

// Console-log name for an idle style. Keep in sync with enum poly_idle_style.
const char* idle_style_name(uint8_t style) {
    switch (style) {
        case IDLE_STYLE_PULSE:  return "pulse";
        case IDLE_STYLE_JITTER: return "jitter";
        case IDLE_STYLE_IDDQD:  return "iddqd";
        case IDLE_STYLE_EDEN:   return "eden";
        default:                return "?";
    }
}

// The active glyph-script override (GLYPH_STD = normal language legends).
uint8_t get_glyph_script(void) {
    return g_glyph_script;
}

// Sets the glyph script by INDEX and marks it dirty (flushed at next suspend/store).
// Any index 0..0xFE is accepted; an index this firmware can't render just falls back
// to the normal legend (see glyph_script_codepoint) — this is what lets the host add
// scripts without a protocol bump. 0xFF is the query sentinel and is never stored.
// The awake re-render is driven from housekeeping (the master syncs glyph_script +
// calls request_disp_refresh on change).
void set_glyph_script(uint8_t script) {
    if (script == 0xFF || script == g_glyph_script) {
        return;   // query sentinel or no-op: don't mark dirty / churn the split sync
    }
    g_glyph_script = script;
    g_glyph_dirty  = true;
}

// Records the glyph script without marking dirty (boot-time EEPROM load). Keeps any
// stored index verbatim; only the erased-EEPROM 0xFF maps to normal legends (an index
// this firmware can't render degrades at draw time, not here).
void note_glyph_script(uint8_t script) {
    g_glyph_script = (script == 0xFF) ? GLYPH_STD : script;
}

// The active keycap legend size (GLYPH_SIZE_S = the original 27 px face).
uint8_t get_glyph_size(void) {
    return g_glyph_size;
}

// Sets the legend size and marks it dirty (flushed at next suspend/store). Unlike
// set_glyph_script the range is CLOSED: a size names a tier that render_key has to
// know how to relocate and place, so an unknown value is rejected rather than kept
// verbatim. The awake re-render is driven from housekeeping (the master syncs
// glyph_size + calls request_disp_refresh on change).
void set_glyph_size(uint8_t size) {
    if (size >= GLYPH_SIZE_COUNT || size == g_glyph_size) {
        return;   // out of range or no-op: don't mark dirty / churn the split sync
    }
    g_glyph_size  = size;
    g_gsize_dirty = true;
}

// Records the legend size without marking dirty (boot-time EEPROM load).
void note_glyph_size(uint8_t size) {
    g_glyph_size = (size < GLYPH_SIZE_COUNT) ? size : GLYPH_SIZE_S;
}

// One tier up or down, WRAPPING at the ends — the KC_GLYPH_SIZE_UP / _DOWN keys.
// One key drives both directions (Shift reverses it), so an end tier must still
// lead somewhere: clamping would make the key look dead at 1 and at 3.
void step_glyph_size(int8_t delta) {
    int8_t next = (int8_t)g_glyph_size + delta;
    while (next < 0) {
        next += (int8_t)GLYPH_SIZE_COUNT;
    }
    while (next >= (int8_t)GLYPH_SIZE_COUNT) {
        next -= (int8_t)GLYPH_SIZE_COUNT;
    }
    set_glyph_size((uint8_t)next);
}

// Console-log name for a legend size. Keep in sync with enum poly_glyph_size.
const char* glyph_size_name(uint8_t size) {
    switch (size) {
        case GLYPH_SIZE_S: return "small";
        case GLYPH_SIZE_M: return "medium";
        case GLYPH_SIZE_L: return "large";
        default:           return "?";
    }
}

// ---- First-boot startup animation marker (poly_eeconf_t.boot_flags) ----
void note_boot_flags(uint8_t flags) {
    g_boot_flags = flags;
}
bool boot_intro_pending(void) {
    return g_boot_flags != BOOT_INTRO_DONE;
}
// Marks the boot-intro-played tail byte dirty once the intro has finished. The
// actual EEPROM write is deferred to the next centralized flush (save_all_dirty at
// suspend/shutdown/store) — never a direct write here, since this runs from the
// housekeeping task. Worst case on power loss before a flush: the one-time intro
// replays next boot (cosmetic, harmless).
void mark_boot_intro_done(void) {
    if (g_boot_flags == BOOT_INTRO_DONE) return;
    g_boot_flags = BOOT_INTRO_DONE;
    g_boot_dirty = true;
}

// The raw boot_flags byte, for the persistence half (save_user_boot_flags).
uint8_t get_boot_flags(void) {
    return g_boot_flags;
}

// ---- Active host-OS (enum poly_os) ----

// The OS in effect: the manual pin while pinned, else the last resolved OS in auto
// mode (only once one is actually known — UNKNOWN until host/detection speaks).
uint8_t get_active_os(void) {
    if (!g_os_auto) return g_user_os;
    return g_os_known ? g_resolved_os : (uint8_t)POLY_OS_UNKNOWN;
}

bool get_os_auto_mode(void) {
    return g_os_auto;
}

// Engage/leave auto mode without disturbing the stored pin/resolved value.
void set_os_auto_mode(bool on) {
    if (g_os_auto == on) return;
    g_os_auto   = on;
    g_os_dirty  = true;
}

// Pin the OS explicitly — manual mode. Wins over host + detection and survives
// reboots; the only way to select Android (USB detection can't tell it from Linux).
void set_user_os(uint8_t os) {
    if (os >= POLY_OS_COUNT) return;
    g_user_os  = os;
    g_os_auto  = false;
    g_os_known = true;
    g_os_dirty = true;
}

// Host-pushed OS (HID cmd 29). Applied only in auto mode; marks the host as having
// spoken so firmware detection yields to it for the rest of the session.
void set_host_os(uint8_t os) {
    if (os >= POLY_OS_COUNT) return;
    if (!g_os_auto) return;                 // a manual pin is in charge — ignore the push entirely
    g_host_os_seen = true;                  // only mark "host spoke" once we actually accept it
    if (g_resolved_os == os && g_os_known) return;
    g_resolved_os = os;
    g_os_known    = true;
    g_os_dirty    = true;
}

// Firmware USB-detected OS (QMK os_detection). Applied only in auto mode and only
// until the host pushes one (host wins). Detection re-runs on each re-enumeration.
void set_detected_os(uint8_t os) {
    if (os >= POLY_OS_COUNT) return;
    if (!g_os_auto || g_host_os_seen) return;
    if (g_resolved_os == os && g_os_known) return;
    g_resolved_os = os;
    g_os_known    = true;
    g_os_dirty    = true;
}

// Pack the active-OS state into one EEPROM byte. bit7 = manual pin engaged (so a
// zeroed/fresh EEPROM = auto, the default), bit6 = a real value is known, bits0-5 =
// the value (the pin in manual mode, the last resolved OS in auto — stored so a
// reboot shows the right OS immediately, before host/detection re-confirm).
uint8_t pack_os_state(void) {
    // bit7 here means "manual pin engaged" (the opposite polarity from
    // pack_auto_brightness) so a zeroed byte still reads as this setting's default.
    return mode_byte_pack(!g_os_auto, g_os_known,
                          g_os_auto ? g_resolved_os : g_user_os);
}

// Restore the active-OS state at boot from the packed byte (0 from an old EEPROM =
// auto, unknown — the desired default).
void load_os_state(uint8_t packed) {
    g_os_auto  = !mode_byte_flag(packed);   // bit7 set => manual pin
    g_os_known = mode_byte_known(packed);
    uint8_t val = mode_byte_value(packed);
    if (val >= POLY_OS_COUNT) { val = POLY_OS_UNKNOWN; g_os_known = false; }
    // Migrate the retired POLY_OS_IOS (5): a protocol-7 EEPROM may still hold it
    // (pinned or last-detected). iOS is now folded into macOS, so normalise it here
    // — the single load point — so runtime active_os is never the reserved 5 and the
    // Apple legends / modifier swap (which test == POLY_OS_MACOS) stay correct.
    if (val == POLY_OS_IOS) { val = POLY_OS_MACOS; }
    if (g_os_auto) g_resolved_os = g_os_known ? val : (uint8_t)POLY_OS_UNKNOWN;
    else           g_user_os     = g_os_known ? val : (uint8_t)POLY_OS_UNKNOWN;
}

// Defers a default-layer EEPROM write — safe to call from split sync handlers.
// The actual write happens at the next flush (save_all_dirty).
void defer_default_layer_save(layer_state_t def_layer) {
    g_def_layer_pending = def_layer;
    g_def_layer_dirty   = true;
}

// Marks the latin extension table as needing an EEPROM write. Used in place of a
// direct save_user_latin() on both the edit path and the slave's sync handler,
// so the flash write is deferred out of the UART transaction callback.
void mark_latin_dirty(void) {
    g_latin_dirty = true;
}

// Flushes every dirty user-state block to EEPROM in one go (settings, latin,
// default layer, MRU). Each block is dirty-gated, so untouched ones are skipped.
// Call only at flush points (suspend, host shutdown signal, the store key),
// never on the typing hot path.
void save_all_dirty(void) {
    if (g_brightness_dirty) { save_user_settings(); g_brightness_dirty = false; }
    if (g_os_dirty)         { save_user_os();       g_os_dirty = false; }
    if (g_glyph_dirty)      { save_user_glyph_script(); g_glyph_dirty = false; }
    if (g_gsize_dirty)      { save_user_glyph_size();   g_gsize_dirty = false; }
    if (g_boot_dirty)       { save_user_boot_flags(); g_boot_dirty = false; }
    if (g_latin_dirty)      { save_user_latin();    g_latin_dirty = false; }
    if (g_def_layer_dirty)  { eeconfig_update_default_layer(g_def_layer_pending); g_def_layer_dirty = false; }
    save_user_mru_if_dirty();
}

// Requests a deferred flush-all. Safe to call from a sync handler — it only sets
// a flag; save_all_if_requested() does the actual write from housekeeping.
void request_eeprom_save(void) {
    g_save_pending = true;
}

// Drains a pending store request by flushing all dirty state. Call from
// housekeeping_task_user() on both sides.
void save_all_if_requested(void) {
    if (g_save_pending) {
        save_all_dirty();
        g_save_pending = false;
    }
}
