// Boot diagnostics — HID-console identification banner + boot-splash progress.
// Extracted from poly_keymap.c so the keymap file stays focused on keymap logic;
// both features are self-contained boot instrumentation with no keymap state.
#include QMK_KEYBOARD_H

#include "print.h"
#include "version.h"
#include "split_util.h"   // is_transport_connected()
#include "side.h"         // is_left_side()
#include "poly_util.h"    // clear_all_displays(), display_message()
#include "base/update.h"       // enum refresh_mode / ALL_AT_ONCE
#include "base/disp_array.h"   // GFXfont type
// Only the single splash font is needed. Don't pull in gfx_used_fonts.h — the
// generated category headers it aggregates have external linkage and may be
// included by exactly one TU (poly_keymap.c). FreeSansBold24pt7b.h is
// self-contained (static const), so this TU gets its own copy.
#include "base/fonts/FreeSansBold24pt7b.h"   // FreeSansBold24pt7b

#include "boot_diag.h"

// update_displays() is defined in poly_keymap.c; the SPLASH_DONE tail calls it to
// hand the keycaps over to the real legends.
void update_displays(enum refresh_mode mode);

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
        wait_ms(300);
    }
    if (final) {
        // Boot complete: dwell on the finished splash, then hand the keycaps
        // over to the real legends — the same tail show_splash_screen() always
        // ran, now deferred to the end of boot so the reveal is meaningful.
        wait_ms(400);
        update_displays(ALL_AT_ONCE);
    }
}
