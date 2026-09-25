// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// Boot diagnostics — HID-console identification banner + boot-splash progress.
// Extracted from poly_keymap.c so the keymap file stays focused on keymap logic;
// both features are self-contained boot instrumentation with no keymap state.
#include QMK_KEYBOARD_H

#include <string.h>       // strcmp() — the picker verdict compares state strings
#include "print.h"
#include "version.h"
#include "split_util.h"   // is_transport_connected()
#include "side.h"         // is_left_side()
#include "poly_util.h"    // clear_all_displays(), display_message()
#include "state.h"        // get_idle_style(), idle_style_name()
#include "layers.h"       // _ADDLANG1 (Intl picker banner line)
#include "quantum/keymap_introspection.h"   // keycode_at_keymap_location_raw()
#include "base/fw_staging.h"   // fw_staging_apply_breadcrumb()
#include "base/crash_record.h" // crash_record_emit_lines()
#include "oled_helper.h"       // oled_boot_progress()
#include "base/update.h"       // enum refresh_mode / ALL_AT_ONCE
#include "base/disp_array.h"   // GFXfont type
// The splash font comes from poly_heavy_font() (poly_util.h), NOT from including
// FreeSansBold24pt7b.h here: that header defines its tables static, so every TU that
// included it linked its own ~10 KB copy — three in the image before this was merged.
// Don't pull in gfx_used_fonts.h either: the generated category headers it aggregates
// have external linkage and may be included by exactly one TU (poly_keymap.c).
#include "hardware/clocks.h"                          // clock_get_hz()
#include "hardware/structs/vreg_and_chip_reset.h"     // core-voltage select

#include "boot_diag.h"
#include "base/hand_stamp.h"
#include "poly_keymap.h"

// update_displays() is defined in poly_keymap.c; the SPLASH_DONE tail calls it to
// hand the keycaps over to the real legends.

// ---------------------------------------------------------------------------
// Boot identification banner
// ---------------------------------------------------------------------------
// Printed to the HID console so `qmk console` shows which board, firmware and
// role a half is, plus its split-link state. The one-shot print in
// keyboard_post_init_user fires before a console is usually attached, so
// housekeeping re-emits it a few times over the first ~half minute (bounded by
// BOOT_BANNER_REPEATS) to catch a console attached shortly after boot.
#ifndef BOOT_BANNER_REPEATS
#    define BOOT_BANNER_REPEATS 3
#endif
#ifndef BOOT_BANNER_INTERVAL_MS
#    define BOOT_BANNER_INTERVAL_MS 3000
#endif

// Report what the Intl layer actually resolves each modifier to, read out of the
// compiled keymap rather than restated from LATIN_PICKER_MOD — same reason the
// clock below is read back from the PLL instead of printed from SYS_CLK_KHZ: the
// define is what we asked for, this is what shipped.
//
// A modifier masked with KC_NO on _ADDLANG1 draws an EMPTY keycap and makes the
// variation picker silently unreachable, with nothing to tell that apart from a
// code bug — which is how the picker ended up on Alt in the first place, and how a
// board still running an older image looked identical to a regression (field,
// 2026-08). The verdict is the point of the line: Ctrl (the picker modifier) and
// Shift (which selects the case) must reach the base layer, Alt must not, and the
// base layer must actually carry an Intl key.
//
// ⚠️ It scans EVERY position of BOTH hands per role. An earlier version returned on
// the first left-hand hit and reported two split42 keymaps as healthy: Ctrl sits on
// a home-row pinky on one base and a thumb on the others, and Shift has a
// right-hand instance, and the masked ones were simply never looked at.
static const char* addlang_state(uint8_t base, uint16_t lk, uint16_t rk) {
    bool found = false, masked = false;
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            uint16_t kc = keycode_at_keymap_location_raw(base, r, c);
            if (kc != lk && (rk == KC_NO || kc != rk)) continue;
            found = true;
            uint16_t on_layer = keycode_at_keymap_location_raw(_ADDLANG1, r, c);
            if (on_layer != KC_TRANSPARENT && on_layer != kc) masked = true;
        }
    }
    if (!found)  return "absent";
    return masked ? "masked" : "pass";
}

static void emit_intl_picker_line(void) {
    // ⚠️ NOT get_highest_layer(default_layer_state). This codebase drives the base
    // layer as an INDEX through layer_clear()+layer_on() and deliberately never
    // calls default_layer_set() (see the KC_L0..KC_L4 handlers), so
    // default_layer_state stays at _L0 for the life of the board. Reading it made
    // the banner always report base=L0 — which also meant the _L3 "no Intl key"
    // case it exists to catch could never have fired. def_layer is loaded from
    // EEPROM in keyboard_post_init_user before this runs.
    const uint8_t base = get_local_layer()->def_layer;

    // Whether this base layer can even reach the Intl layer: _L3 carries no
    // MO(_ADDLANG1) at all, so without this the line would read OK on a base from
    // which the picker simply cannot be opened.
    bool intl = false;
    for (uint8_t r = 0; r < MATRIX_ROWS && !intl; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (keycode_at_keymap_location_raw(base, r, c) == MO(_ADDLANG1)) { intl = true; break; }
        }
    }

    // Both hands for all three. One masked instance is a dead key (split42 had
    // exactly that: right Shift masked while left Shift passed), and symmetrically
    // one UNmasked Alt is enough to send a bare Alt tap to the host. split42's only
    // Alt is a right-hand thumb, so a left-only check declared it clean.
    const char* ctrl  = addlang_state(base, KC_LEFT_CTRL,  KC_RIGHT_CTRL);   // must pass
    const char* shift = addlang_state(base, KC_LEFT_SHIFT, KC_RIGHT_SHIFT);  // must pass
    const char* alt   = addlang_state(base, KC_LEFT_ALT,   KC_RIGHT_ALT);    // must NOT pass

    const bool ok = intl && !strcmp(ctrl, "pass") && !strcmp(shift, "pass")
                         && strcmp(alt, "pass");
    uprintf("   intl: base=L%d intl=%s ctrl=%s shift=%s alt=%s -> picker %s\n",
            (int)base, intl ? "yes" : "NOT-ON-THIS-BASE", ctrl, shift, alt,
            ok ? "OK" : "BROKEN");
}

void emit_boot_banner(void) {
    // PRODUCT is the QMK-generated keyboard_name from keyboard.json
    // ("PolyKybd Split72" / "PolyKybd Split42"), so the banner names the variant
    // with no extra per-variant define.
    uprintf("== " PRODUCT " " FW_VERSION " P%d HW0x%04X | %s %s ==\n",
            (int)PROTOCOL_VERSION, (unsigned int)DEVICE_VER,
            is_keyboard_left() ? "left" : "right",
            is_keyboard_master() ? "master" : "slave");
    // Split-link role inputs — a dead bridge (both halves picking the same role,
    // so the full-duplex crossover never forms) shows up here: USB_VBUS_PIN (GP24)
    // is what stock master detection keys on, and transport_connected reports
    // whether this half currently sees the other over the split UART.
#ifdef USB_VBUS_PIN
    uprintf("   link: vbus_pin=%d transport_connected=%d\n",
            (int)gpio_read_pin(USB_VBUS_PIN), (int)is_transport_connected());
#else
    uprintf("   link: transport_connected=%d\n", (int)is_transport_connected());
#endif
    // Where this half's handedness came from. Stock EE_HANDS would keep it in the
    // emulated EEPROM, whose wear-levelling recovery clears the WHOLE store on a
    // torn write -- and a cleared handedness byte is not "unknown", it reads as a
    // valid `right`, so a half silently comes up on the wrong side (field,
    // 2026-09-07). base/hand_stamp.c keeps it in a flash sector of our own instead
    // and config.h drops EE_HANDS; this line says which source answered, so the
    // next report of a half on the wrong side is one line to diagnose instead of
    // a guess.
    {
        static const char *const src[] = {"flash stamp", "stamped from EEPROM", "EEPROM, UNSTAMPED"};
        // slot/count/writer are here so an experiment can prove a stamp WRITE landed.
        // Without them this line reads identically whether a freshly dragged UF2 was
        // applied or a previous record is still in the sector -- which is exactly how
        // a probe that never wrote anything was read as "the write is harmless".
        // A UF2 lands at slot 0 with count 1 (the bootrom erases the sector first) and
        // carries writer=0x55; stamp_write() appends at the first free page and leaves 0.
        uprintf("   hand: %s (%s) slot=%u/%u writer=0x%02X%s\n", is_keyboard_left() ? "LEFT" : "RIGHT",
                src[poly_hand_source()], (unsigned)poly_hand_stamp_slot(),
                (unsigned)poly_hand_stamp_count(), (unsigned)poly_hand_stamp_writer(),
                poly_hand_ee_repaired() ? " [EEPROM byte repaired from the stamp]" : "");
    }
    // Read the clock back from the hardware rather than printing SYS_CLK_KHZ:
    // the define is what we ASKED for, this is what the PLL actually landed on.
    // VSEL is the core-voltage select (0xB = 1.10 V default, 0xC = 1.15 V), the
    // pairing that decides whether the clock below is a certified operating
    // point — see POLYKYBD_SYS_CLK in rules.mk.
    uprintf("   clk: sys=%luHz vreg_vsel=0x%X\n",
            (unsigned long)clock_get_hz(clk_sys),
            (unsigned int)((vreg_and_chip_reset_hw->vreg & VREG_AND_CHIP_RESET_VREG_VSEL_BITS) >> VREG_AND_CHIP_RESET_VREG_VSEL_LSB));
    emit_intl_picker_line();
}

// The configured idle (anti-burn-in) style + the timings that drive the idle
// state machine. Emitted separately from the identity banner because the style
// is only known after the EEPROM config load, which happens well after the
// one-shot emit_boot_banner() call — so a console can no longer see a fade/pulse
// happen without also seeing which style was actually selected.
void emit_idle_config(void) {
    const uint8_t style = get_idle_style();
    // fade_out is the ACTIVE value (cmd 40 / poly_eeconf_t.idle_timeout), not a
    // compile-time constant any more — printing FADE_OUT_TIME here would report the
    // default on a board that has chosen something else, which is exactly the class
    // of console line that sends a reader looking in the wrong place.
    uprintf("   idle: style=%s (%u) fade_out=%ums (preset %u) fade=%ums turn_off=%ums\n",
            idle_style_name(style), (unsigned int)style,
            (unsigned int)get_idle_timeout_ms(), (unsigned int)get_idle_timeout(),
            (unsigned int)FADE_TRANSITION_TIME, (unsigned int)TURN_OFF_TIME);
}

// The stored dynamic-keymap format version and whether this boot had to discard the
// keymap because of it. Emitted on the banner tick for the same reason emit_idle_config()
// is: it is only known after the EEPROM config load, which happens long after the
// one-shot banner print, and the reset it reports is a once-per-upgrade event that
// blocks post_init while it rewrites a few kB of wear-levelled EEPROM. A console that
// cannot see it cannot tell "the board is still resetting its keymap" from "the board
// is dead".
static uint8_t  s_keymap_fmt_seen  = 0xFF;   // 0xFF = post_init has not run yet
static bool     s_keymap_reset_ran = false;
static uint32_t s_keymap_reset_ms  = 0;

void note_keymap_storage(uint8_t stored_fmt, bool reset_ran, uint32_t elapsed_ms) {
    s_keymap_fmt_seen  = stored_fmt;
    s_keymap_reset_ran = reset_ran;
    s_keymap_reset_ms  = elapsed_ms;
}

// What the previous self-apply achieved, if this boot followed one. Printed with the
// banner because that is the only channel: fw_staging_do_apply() runs with interrupts
// off and never returns, so it can leave a breadcrumb in the watchdog scratch and
// nothing else.
void emit_apply_breadcrumb_line(void) {
    uint32_t last_sector = 0, spins = 0;
    bool     completed   = false;
    uint32_t p_low = 0, p_high = 0, p_count = 0;
    if (fw_staging_apply_progress(&p_low, &p_high, &p_count)) {
        uprintf("   apply: copy log - %lu sectors done, sectors %lu..%lu\n",
                (unsigned long)p_count, (unsigned long)p_low, (unsigned long)p_high);
    } else if (fw_staging_apply_started()) {
        uprintf("   apply: copy log - STARTED but not one sector completed\n");
    }
    uint32_t m_stage = 0, m_addr = 0, m_erased = 0, m_progd = 0, m_srcw = 0;
    if (fw_staging_apply_marks(&m_stage, &m_addr, &m_erased, &m_progd, &m_srcw)) {
        static const char *const where[] = {"", "died IN the erase",
                                            "died reading the staging source",
                                            "died IN the program of the image",
                                            "first page written OK"};
        uprintf("   apply: first image sector 0x%lx - reached stage %lu (%s)\n",
                (unsigned long)m_addr, (unsigned long)m_stage,
                where[m_stage < 5 ? m_stage : 0]);
        if (m_stage >= 2) uprintf("   apply: word after erase   = %08lx (ffffffff = erase took)\n",
                                  (unsigned long)m_erased);
        if (m_stage >= 3) uprintf("   apply: staging source word = %08lx (read OK)\n",
                                  (unsigned long)m_srcw);
        if (m_stage >= 4) uprintf("   apply: word after program  = %08lx\n", (unsigned long)m_progd);
    }
    uint32_t d_sectors = 0, d_size = 0, d_spins = 0;
    if (fw_staging_last_apply_completed(&d_sectors, &d_size, &d_spins)) {
        // From the in-flash record, so this survives the power cycle that a BOOTSEL
        // recovery needs -- it describes the last apply that ran, not necessarily one
        // that preceded THIS boot.
        uprintf("   apply: last self-apply COMPLETED its copy (%lu sectors, %lu B, psm_spins=%lu)\n",
                (unsigned long)d_sectors, (unsigned long)d_size, (unsigned long)d_spins);
        uint32_t off = 0, got = 0, want = 0;
        if (fw_staging_last_apply_diff(&off, &got, &want)) {
            if (off == 0xFFFFFFFFu) {
                uprintf("   apply: written image MATCHED the staged source exactly\n");
            } else {
                uprintf("   apply: written image DIFFERS at offset %lu (0x%lx): got %08lx want %08lx\n",
                        (unsigned long)off, (unsigned long)off,
                        (unsigned long)got, (unsigned long)want);
            }
        }
    }
    if (!fw_staging_apply_breadcrumb(&last_sector, &completed, &spins)) return;
    uprintf("   apply: previous self-apply reached sector %lu, copy %s (psm_spins=%lu)\n",
            (unsigned long)last_sector, completed ? "COMPLETE" : "INCOMPLETE",
            (unsigned long)spins);
}

void emit_keymap_storage_line(void) {
    uprintf("   keymap: stored_fmt=0x%02X current=0x%02X reset=%s%s\n",
            (unsigned int)s_keymap_fmt_seen, (unsigned int)KEYMAP_STORAGE_CURRENT,
            s_keymap_reset_ran ? "YES" : "no",
            s_keymap_reset_ran ? "" : " (no discard needed)");
    if (s_keymap_reset_ran) {
        uprintf("   keymap: discard took %lums\n", (unsigned long)s_keymap_reset_ms);
    }
}

void boot_banner_housekeeping_tick(void) {
    // Re-emit the boot identification banner a few times after power-on so a
    // `qmk console` attached shortly after boot still catches it (the one-shot
    // print in keyboard_post_init_user fires before the console is usually up).
    static uint8_t  banner_repeats = 0;
    static uint32_t banner_timer   = 0;
    if (banner_repeats < BOOT_BANNER_REPEATS) {
        if (banner_timer == 0) {
            banner_timer = timer_read32();   // arm on the first housekeeping pass
        } else if (timer_elapsed32(banner_timer) >= BOOT_BANNER_INTERVAL_MS) {
            emit_boot_banner();
            emit_idle_config();
            emit_keymap_storage_line();
            emit_apply_breadcrumb_line();
            // Rides the same repeat as the rest: the table is only complete at
            // SPLASH_DONE, which is well before a console is usually attached, and
            // the console is lossy — a one-shot print of the thing we need in order
            // to size a watchdog is the wrong shape.
            emit_boot_timing_line();
            crash_record_emit_lines();   // the previous run's crash, if there was one
            banner_timer = timer_read32();
            banner_repeats++;
        }
    }
}

// ---------------------------------------------------------------------------
// Boot-splash progress
// ---------------------------------------------------------------------------
// splash_progress(step) draws the splash frame for boot milestone `step`
// (1..7); SPLASH_DONE draws the whole splash AND performs the final dwell +
// legend handoff. Call sites, in boot order:
//   step 1       keyboard_pre_init_user()          before QMK split/USB init
//   step 2       post_init, after set_side()        split/USB init PASSED
//   step 3       post_init, after emj/lang/mru init
//   step 4       post_init, before core1 launch
//   step 5       post_init, after core1 launch
//   step 6       post_init, after RPC registration
//   step 7       post_init, after EEPROM config load
//   SPLASH_DONE  post_init end                      boot complete -> legends
//
// Reveal model: the WHOLE word is drawn from step 1, but every letter starts
// DIM (scanline / half-density) and solidifies one-by-one as boot advances. So
// the splash appears instantly (no "slow" letter-by-letter typing wait — the
// user sees the full logo immediately) while still doubling as a boot-progress
// indicator: the number of SOLID letters is how far boot got. Step 1 solidifies
// none (whole word dim); each further step solidifies one more; DONE solidifies
// all. Because step 1 shows the full word, the reveal has 7 solidify frames
// (steps 2..DONE):
//
//   step  left "POLY KYBD" (8)   right "SPLIT 72" (7)   solid
//   ----  -------------------   -------------------   -----
//    1    (all dim)             (all dim)               0
//    2    P                     S                       1
//    3    PO                    SP                      2
//    4    POL                   SPL                     3
//    5    POLY                  SPLI                     4
//    6    POLY K                SPLIT                    5
//    7    POLY KY               SPLIT 7                  6
//   DONE  POLY KYBD             SPLIT 72              all (8 / 7)
//
// (letters shown solid; the remainder are present but dim.) The right half has
// exactly 7 visible glyphs, so it solidifies one per step with no placeholder
// trick — the old " 7 2" leading-space "SPLII -> SPLIT" two-step reveal is gone.
// The left half has 8, one more than the 7 solidify frames, so DONE solidifies
// its last TWO letters (B D) at once. Alignment spaces in " 7 2" occupy their
// keycap but never consume a solidify step. is_left_side() must be resolved
// (set_side()) before any call — it is, at every call site.

// Count of visible (non-space, non-NUL) glyphs in a splash word.
static uint8_t utext_visible_len(const uint32_t* s) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < 15 && s[i] != 0; ++i) {
        if (s[i] != U' ') {
            n++;
        }
    }
    return n;
}

// ── Sub-milestones and the boot timing table ────────────────────────────────
// Which milestone we are inside, so boot_substep() can draw the right percent and
// stamp the right high byte without the caller repeating itself.
static uint8_t  s_boot_step = 0;
// (tag, ms) per milestone, where tag is the SAME encoding the crash breadcrumb
// uses: a bare step, or step<<8 | sub. 24 entries covers 8 milestones plus room
// for sub-steps without a bounds worry.
#define BOOT_TIMING_MAX 24
static uint16_t s_boot_tag[BOOT_TIMING_MAX];
static uint16_t s_boot_ms[BOOT_TIMING_MAX];
static uint8_t  s_boot_n    = 0;
static uint32_t s_boot_last = 0;

// Record the gap since the previous milestone. Saturates rather than wrapping: a
// span longer than a minute is already the answer, and a wrapped uint16 would read
// as a short one.
static void boot_timing_mark(uint16_t tag) {
    const uint32_t now = timer_read32();
    if (s_boot_n < BOOT_TIMING_MAX) {
        const uint32_t d = s_boot_n ? (now - s_boot_last) : 0u;
        s_boot_tag[s_boot_n] = tag;
        s_boot_ms[s_boot_n]  = (d > 0xFFFFu) ? 0xFFFFu : (uint16_t)d;
        s_boot_n++;
    }
    s_boot_last = now;
}

void emit_boot_timing_line(void) {
    if (s_boot_n == 0) return;
    // One line, because the console is lossy and a table split over many prints is
    // a table with holes in it. Each entry is "tag=ms"; a sub-step shows as 0x0502.
    uprint("   boot: spans ms");
    for (uint8_t i = 0; i < s_boot_n; i++) {
        if (s_boot_tag[i] & 0xFF00u) {
            uprintf(" %u.%u=%u", (unsigned)(s_boot_tag[i] >> 8),
                    (unsigned)(s_boot_tag[i] & 0xFFu), (unsigned)s_boot_ms[i]);
        } else {
            uprintf(" %u=%u", (unsigned)s_boot_tag[i], (unsigned)s_boot_ms[i]);
        }
    }
    uprint("\n");
}

void boot_substep(uint8_t sub, uint8_t sub_total) {
    if (s_boot_step == 0 || sub == 0) return;   // no milestone open / nothing to say
    const uint16_t tag = (uint16_t)(((uint16_t)s_boot_step << 8) | sub);
    // Same breadcrumb the milestones write, so whatever reset finally happens
    // archives the SUB-step rather than only the step it was inside.
    (void)crash_phase_enter(CRASH_PHASE_BOOT, tag);
    boot_timing_mark(tag);
    // Percent line only. The keycap splash is untouched: its solidify count belongs
    // to the milestone, and repainting 72 displays per sub-step would itself be a
    // multi-hundred-ms span in the window we are trying to measure.
    oled_boot_progress(s_boot_step, POLY_SPLASH_STEPS, sub, sub_total, NULL);
}

// ── The final boot render: per-key breadcrumbs + a watchdog guard ───────────
// See boot_diag.h (boot_render_mark) for what this instruments and why that span
// has no other evidence.
static bool s_render_guard = false;   // the watchdog is armed for this render
// The breadcrumbs (panel sub-steps, phase stamps) run for EVERY final render, guard
// or not. ⚠️ They used to share s_render_guard, so a boot that followed a watchdog
// reset here skipped the marks too: the panel froze at "100%" with no sub-steps, the
// exact screen that says nothing about where it stopped, on the one boot most likely
// to wedge in the same place again (hardware, 2026-09-25).
static bool s_render_marks = false;

// The denominator the panel shows: every key update_displays() walks on this half,
// KC_NO holes included, because the mark is stamped before the keycode is looked at.
// 40 on split72 (5 x 8), 24 on split42 (4 x 6).
#define BOOT_RENDER_KEYS ((uint8_t)(MATRIX_ROWS_PER_SIDE * MATRIX_COLS))

// Skip the guard when the PREVIOUS boot already died under it. The record is
// archived by then, so a second reset adds nothing — and without this a board that
// hangs here on every boot would reboot-loop, because the crash-loop halt lives in
// the fault handler and a watchdog reset runs no code at all. One reset, one
// record, then the old wedge, which BOOTSEL still recovers.
static bool render_guard_already_fired(void) {
    poly_crash_record_t rec;
    if (!crash_record_fresh() || !crash_record_archived(&rec)) {
        return false;
    }
    return rec.kind == CRASH_KIND_WATCHDOG && rec.phase == CRASH_PHASE_BOOT &&
           (uint8_t)(rec.phase_arg >> 8) == POLY_SPLASH_STEPS;
}

static void boot_render_guard_begin(void) {
    s_render_marks = true;
    if (render_guard_already_fired()) {
        return;
    }
    s_render_guard = true;
    // ⚠️ crash_watchdog_arm(), NOT crash_watchdog_start(): post_init has not
    // finished, so the BOOT breadcrumb and the consecutive count must survive.
    crash_watchdog_arm();
}

static void boot_render_guard_end(void) {
    s_render_marks = false;
    s_render_guard = false;
    // The watchdog stays ARMED on purpose: crash_watchdog_start() is the next line
    // of keyboard_post_init_user(), and from there the main loop feeds it.
}

// ── The USB bus watch ───────────────────────────────────────────────────────
// Why this is here at all: the render walks 40 keycaps at roughly 2.5 ms each, and
// two MacBook cold boots wedged it 5 ms apart (after key 16 and after key 18). A bad
// glyph would stop at the SAME key every time, so what stops it is not the keycap —
// it is something arriving from outside at a repeatable moment. The Mac's boot-time
// USB sequence is exactly that kind of clock: EFI enumerates, the kernel takes over
// and resets the bus, and every one of those events lands while post_init is still
// running and NOTHING is draining the USB event queue (usb_event_queue_task() is a
// main-loop call, and the main loop has not started).
//
// ChibiOS's driver state is the cheapest possible witness: one volatile read per key,
// no hook, no instrumentation of the USB stack. USB_ACTIVE(4) is the steady state; a
// bus reset drops it to USB_READY(2), a suspend to USB_SUSPENDED(5).
//
// The panel is the only channel that can report it, because a board wedged here
// prints nothing (console_task() is a main-loop call too) — so the transition
// displaces the label line and STAYS there, to be read off a photograph:
//
//     USB 4>2 @18          <- the bus dropped to READY while key 18 was drawing
//     18 / 40
//     100%
//
// A trailing '*' means it was not the only transition; the one shown is the LAST,
// which is the one next to the stall. The count itself is left off because
// "USB 4>2 @40 x12" measures 125 px of 128 and this screen has been sent back for
// less.
static uint8_t  s_usb_prev  = 0xFF;   // 0xFF = not sampled yet
static uint8_t  s_usb_count = 0;
static uint32_t s_usb_note[16];
static bool     s_usb_seen  = false;

static void usb_watch(uint8_t key) {
    const uint8_t st = (uint8_t)USBD1.state;
    if (s_usb_prev == 0xFF) {          // first sample: the baseline, not an event
        s_usb_prev = st;
        return;
    }
    if (st == s_usb_prev) {
        return;
    }
    char txt[24];
    s_usb_count = (uint8_t)(s_usb_count < 0xFF ? s_usb_count + 1 : 0xFF);
    snprintf(txt, sizeof(txt), "USB %u>%u @%u%s", (unsigned)s_usb_prev, (unsigned)st,
             (unsigned)key, s_usb_count > 1 ? "*" : "");
    ascii_to_u32_string(s_usb_note, sizeof(s_usb_note), txt);
    s_usb_seen = true;
    s_usb_prev = st;
    // Paint it NOW, not at the next row: the event and the stall are milliseconds
    // apart, and the next row may never come.
    oled_boot_progress(POLY_SPLASH_STEPS, POLY_SPLASH_STEPS, key, BOOT_RENDER_KEYS,
                       s_usb_note);
}

void boot_render_mark(uint8_t row, uint8_t col) {
    if (!s_render_marks) {
        return;
    }
    const uint8_t key = (uint8_t)(row * MATRIX_COLS + col + 1);   // 1-based, of BOOT_RENDER_KEYS
    // A long render must not trip the guard; a stalled one must. Each key gets the
    // full CRASH_WATCHDOG_MS, so what the reset means is "one keycap took 8 s".
    if (s_render_guard) {
        crash_watchdog_feed();
    }
    (void)crash_phase_enter(CRASH_PHASE_BOOT,
                            (uint16_t)(((uint16_t)POLY_SPLASH_STEPS << 8) | key));
    usb_watch(key);
    if (col == 0) {
        // Once per ROW: five panel frames, not forty. Each one is itself I2C traffic
        // inside the window being measured, and the boot timing table is not fed
        // from here for the same reason (BOOT_TIMING_MAX would fill with keys and
        // drop the milestones). Safe mid-render: the status OLED is I2C, it touches
        // neither the keycap SPI nor the shift-register walk, and every per-key
        // branch re-initialises the shared scratch buffer with kdisp_set_buffer().
        oled_boot_progress(POLY_SPLASH_STEPS, POLY_SPLASH_STEPS, key, BOOT_RENDER_KEYS,
                           s_usb_seen ? s_usb_note : NULL);
    }
}

void splash_progress(uint8_t step) {
    const bool      final   = (step == SPLASH_DONE);
    const bool      left    = is_left_side();
    const uint32_t* r1_word = left ? U"POLY" : POLY_SPLASH_R1;
    const uint32_t* r2_word = left ? U"KYBD" : POLY_SPLASH_R2;
    const uint8_t   r2_row  = left ? 2 : POLY_SPLASH_R2_ROW;
    const uint8_t   r1_vis  = utext_visible_len(r1_word);
    const uint8_t   total_vis = r1_vis + utext_visible_len(r2_word);

    // How many visible glyphs render SOLID; the rest render dim (scanline). Step
    // 1 -> 0 (whole word present but dim), each further step +1, DONE -> all.
    uint8_t solid_count = final ? total_vis : (step >= 1 ? (uint8_t)(step - 1) : 0);
    if (solid_count > total_vis) {
        solid_count = total_vis;
    }

    // ⚠️ Breadcrumb the MILESTONE, not just "somewhere in boot". The whole of
    // post_init runs with the watchdog OFF on purpose — crash_watchdog_start() is the
    // LAST line of keyboard_post_init_user(), because the steps above it may block
    // for seconds — so a hang here is a PERMANENT hang: no reset, no record, and the
    // only evidence is how many splash letters went solid, which is a two-letter
    // guess read off a keycap. Stamping the step means that whenever a reset DOES
    // happen (a later fault, a RUN-pin reset, the next crash), the archived record
    // names how far this boot got instead of just CRASH_PHASE_BOOT.
    //
    // Deliberately not paired with crash_phase_leave(): boot is a straight line, each
    // milestone supersedes the last, and crash_watchdog_start() resets the phase to
    // CRASH_PHASE_LOOP when post_init completes.
    (void)crash_phase_enter(CRASH_PHASE_BOOT, step);
    // Open this milestone for boot_substep(), and time the span that just ended.
    // SPLASH_DONE is stamped as the step count so the table's last row reads "8".
    s_boot_step = final ? POLY_SPLASH_STEPS : step;
    boot_timing_mark(s_boot_step);

    // ...and put the same milestone somewhere a human can read off a wedged board.
    // Skipped for step 1: that one runs in keyboard_pre_init_user(), and QMK does not
    // call oled_init() until later in keyboard_init(), so there is no panel yet.
    if (step != 1) {
        oled_boot_progress(final ? POLY_SPLASH_STEPS : step, POLY_SPLASH_STEPS, 0, 0, NULL);
    }

    clear_all_displays();
    display_message_progressive(1, 1, r1_word, poly_heavy_font(), 0, solid_count);
    display_message_progressive(r2_row, 1, r2_word, poly_heavy_font(), r1_vis, solid_count);

    if (step == 1) {
        // Hold the all-dim preview briefly so the eye registers the whole logo
        // before letters begin solidifying — the reveal otherwise starts the
        // instant boot leaves pre_init, too quick to read the dim frame.
        wait_ms(400);
    }
    if (final) {
        // Boot complete: dwell on the finished splash, then hand the keycaps
        // over to the real legends — the same tail show_splash_screen() always
        // ran, now deferred to the end of boot so the reveal is meaningful.
        wait_ms(400);
        boot_render_guard_begin();
        update_displays(ALL_AT_ONCE);
        boot_render_guard_end();
    }
}
