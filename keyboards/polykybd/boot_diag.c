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

// EEPROM boot-diag globals (poly_keymap.c) — printed here so a pasted log shows, per
// half, exactly what EEPROM state it booted with (SPLIT42_LINK_STATUS.md).
extern volatile uint8_t  g_eedbg_hands_enabled;
extern volatile uint8_t  g_eedbg_hands_raw;
extern volatile uint32_t g_eedbg_hands_ms;
extern volatile uint8_t  g_eedbg_post_enabled;
extern volatile uint32_t g_eedbg_load_ms;
extern volatile uint8_t  g_eedbg_bright;
extern volatile uint8_t  g_eedbg_lang;
extern volatile uint8_t  g_eedbg_init_ran;

void emit_boot_banner(void) {
    // PRODUCT is the QMK-generated keyboard_name from keyboard.json
    // ("PolyKybd Split72" / "PolyKybd Split42"), so the banner names the variant
    // with no extra per-variant define.
    uprintf("== " PRODUCT " " FW_VERSION " P%d HW0x%04X | %s %s | build %s ==\n",
            (int)PROTOCOL_VERSION, (unsigned int)DEVICE_VER,
            is_keyboard_left() ? "left" : "right",
            is_keyboard_master() ? "master" : "slave",
            QMK_GIT_HASH);
    // EEPROM state this half booted with. hands_enabled=0 or init_ran=1 => the EEPROM
    // was invalid/(re)formatted; hands_ms or load_ms large => a blocking wear-leveling
    // consolidation stalled boot (a strong split-link-establishment suspect).
    uprintf("   eeprom: hands_en=%d hands_raw=%d hands_ms=%lu post_en=%d load_ms=%lu init_ran=%d bright=%d lang=%d\n",
            (int)g_eedbg_hands_enabled, (int)g_eedbg_hands_raw, (unsigned long)g_eedbg_hands_ms,
            (int)g_eedbg_post_enabled, (unsigned long)g_eedbg_load_ms, (int)g_eedbg_init_ran,
            (int)g_eedbg_bright, (int)g_eedbg_lang);
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
// Each step advances the splash by one distinguishable frame, so a hang freezes
// it at the exact milestone it reached (see readme.md "Boot splash progress"):
//
//   step  left "POLY KYBD"   right "SPLIT 72"
//   ----  ----------------   ----------------
//    1    P                  S           <- pre_init: split/USB-init hang here =
//    2    PO                 SP             the fw-apply "slave not rebooted" case
//    3    POL                SPL
//    4    POLY               SPLI
//    5    POLY K             SPLII       <- right "types in" its last letter over
//    6    POLY KY            SPLIT          two steps (SPLII -> SPLIT) so the
//    7    POLY KYB           SPLIT 7        leading space in " 7 2" doesn't cost
//   DONE  POLY KYBD          SPLIT 72       it a frame — both halves get 8 steps
//
// A frozen SINGLE letter ("P" / "S") is the split/USB-init hang; the more letters
// are lit, the later boot stalled; only a fully booted keyboard replaces the
// splash with real legends. is_left_side() must be resolved (set_side()) before
// any call — it is, at every call site.

static uint8_t utext_len(const uint32_t* s) {
    uint8_t n = 0;
    while (n < 15 && s[n] != 0) {
        n++;
    }
    return n;
}

// NUL-terminated copy of the first `n` glyphs of `src` into `dst` (>= 16 words).
static void utext_prefix(uint32_t* dst, const uint32_t* src, uint8_t n) {
    uint8_t i = 0;
    for (; i < n && i < 15 && src[i] != 0; ++i) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

void splash_progress(uint8_t step) {
    const bool      final   = (step == SPLASH_DONE);
    const bool      left    = is_left_side();
    const uint32_t* r1_word = left ? U"POLY" : POLY_SPLASH_R1;
    const uint32_t* r2_word = left ? U"KYBD" : POLY_SPLASH_R2;
    const uint8_t   r2_row  = left ? 2 : POLY_SPLASH_R2_ROW;
    const uint8_t   r1_len  = utext_len(r1_word);
    const uint8_t   r2_len  = utext_len(r2_word);

    uint32_t r1buf[16];
    uint32_t r2buf[16];
    r1buf[0] = 0;
    r2buf[0] = 0;

    if (final) {                                  // whole splash
        utext_prefix(r1buf, r1_word, r1_len);
        utext_prefix(r2buf, r2_word, r2_len);
    } else if (left) {
        // Left "POLY KYBD" = 8 glyphs, one revealed per step -> 8 clean frames.
        utext_prefix(r1buf, r1_word, step < r1_len ? step : r1_len);
        if (step > r1_len) {
            utext_prefix(r2buf, r2_word, step - r1_len);
        }
    } else {
        // Right "SPLIT 72": the leading space in " 7 2" would otherwise hide the
        // first row-2 reveal (two consecutive steps both showing "SPLIT"). So the
        // LAST row-1 letter is "typed in" over two steps — step r1_len shows a
        // placeholder ("SPLII", last glyph = the penultimate one), step r1_len+1
        // corrects it ("SPLIT") — reclaiming the lost frame so the right half also
        // gets 8 distinct steps.
        if (step < r1_len) {                      // S, SP, SPL, SPLI
            utext_prefix(r1buf, r1_word, step);
        } else if (step == r1_len) {              // SPLII (placeholder last glyph)
            utext_prefix(r1buf, r1_word, r1_len);
            if (r1_len >= 2) {
                r1buf[r1_len - 1] = r1_word[r1_len - 2];
            }
        } else {                                  // full row 1, then row 2
            utext_prefix(r1buf, r1_word, r1_len);
            uint8_t r2_show = step - r1_len - 1;  // 0 at the "SPLIT" correction step
            if (r2_show > 0) {
                // +1 so the leading space is included (the visible digit is next).
                utext_prefix(r2buf, r2_word, r2_show + 1);
            }
        }
    }

    clear_all_displays();
    display_message(1, 1, r1buf, &FreeSansBold24pt7b);
    if (r2buf[0] != 0) {
        display_message(r2_row, 1, r2buf, &FreeSansBold24pt7b);
    }
    if (final) {
        // Boot complete: dwell on the finished splash, then hand the keycaps
        // over to the real legends — the same tail show_splash_screen() always
        // ran, now deferred to the end of boot so the reveal is meaningful.
        wait_ms(400);
        update_displays(ALL_AT_ONCE);
    }
}
