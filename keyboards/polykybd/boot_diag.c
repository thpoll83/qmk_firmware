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
#include "base/update.h"       // enum refresh_mode / ALL_AT_ONCE
#include "base/disp_array.h"   // GFXfont type
// Only the single splash font is needed. Don't pull in gfx_used_fonts.h — the
// generated category headers it aggregates have external linkage and may be
// included by exactly one TU (poly_keymap.c). FreeSansBold24pt7b.h is
// self-contained (static const), so this TU gets its own copy.
#include "base/fonts/FreeSansBold24pt7b.h"   // FreeSansBold24pt7b
#include "hardware/clocks.h"                          // clock_get_hz()
#include "hardware/structs/vreg_and_chip_reset.h"     // core-voltage select

#include "boot_diag.h"
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
    uprintf("   idle: style=%s (%u) fade_out=%ums fade=%ums turn_off=%ums\n",
            idle_style_name(style), (unsigned int)style,
            (unsigned int)FADE_OUT_TIME, (unsigned int)FADE_TRANSITION_TIME,
            (unsigned int)TURN_OFF_TIME);
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

    clear_all_displays();
    display_message_progressive(1, 1, r1_word, &FreeSansBold24pt7b, 0, solid_count);
    display_message_progressive(r2_row, 1, r2_word, &FreeSansBold24pt7b, r1_vis, solid_count);

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
        update_displays(ALL_AT_ONCE);
    }
}
