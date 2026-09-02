// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "poly_util.h"

#include "quantum.h"

#include "state.h"
#include "bridge_helper.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/shift_reg.h"
#include "base/helpers.h"
#include "base/fonts/FreeSansBold24pt7b.h"

#include <print.h>
#include <transactions.h>
#include "hardware/watchdog.h"
#include "poly_keymap.h"

// ---------------------------------------------------------------------------
// RP2040 warm-reset fix (QK_REBOOT / soft_reset_keyboard path).
//
// QMK's default RP2040 mcu_reset() (platforms/chibios/bootloaders/rp2040.c) is
// just NVIC_SystemReset(), which on RP2040 asserts only AIRCR.SYSRESETREQ — it
// resets the Cortex-M0+ core but leaves USB, the PLLs and most peripherals in
// their prior state.  That half-reset state intermittently hangs at early boot
// until the cable is replugged (observed both on the fw-up self-apply reboot and
// on the QK_REBOOT keycode).
//
// watchdog_reboot(0,0,0) instead programs PSM_WDSEL to reset *everything* except
// the ROSC/XOSC and triggers the watchdog → a clean full-chip reset equivalent to
// a power cycle.  mcu_reset() is a weak symbol in the QMK bootloader layer (other
// boards override it, e.g. zsa/voyager), so this strong override wins at link.
// ---------------------------------------------------------------------------
void mcu_reset(void) {
    watchdog_reboot(0, 0, 0);   // pc=0,sp=0 → normal flash boot; delay 0 → immediate
    while (1) { /* wait for the watchdog to fire */ }
}

const uint8_t* get_key_disp_bitmask(uint8_t index);
uint8_t get_disp_bitmask_size(void);


void select_all_displays(void) {
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);
}

void clear_all_displays(void) {
    select_all_displays();
    kdisp_set_buffer(0x00);
    kdisp_send_buffer();
}

void display_bootloader_message(void) {
    #ifdef RGB_MATRIX_ENABLE
        rgb_matrix_enable_noeeprom();
        rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
        rgb_matrix_set_color_all(0, 5, 3);
        rgb_matrix_update_pwm_buffers();
    #endif

    clear_all_displays();
    // Contrast 10 → SETCONTRAST 9/255; contrast 1 tested unreadable.
    set_displays(20, false);
    display_message(1, 1, U"BOOT-",   &FreeSansBold24pt7b);
    display_message(3, 0, U"LOADER!", &FreeSansBold24pt7b);
}

// Announce an imminent bootloader jump: flag it, sync the slave half so it
// shows the bootloader message too, then show the message locally. The caller
// resets afterwards — via QMK for the QK_BOOTLOADER keymap path, or via
// reset_keyboard() for the host-triggered HID command (hid_com.c case 23).
void poly_announce_bootloader(void) {
    uprintf("Bootloader entered. Please copy new Firmware.\n");
    // Sync slave before the reset — QMK resets before housekeeping runs.
    access_local_state()->overlay_flags |= BOOTLOADER_DISPLAY;
    uint8_t ack = send_to_bridge(USER_SYNC_POLY_DATA, (void *)access_local_state(), sizeof(poly_sync_t), 10);
    uprintf("Master: BOOTLOADER_DISPLAY sync ack=%d\n", ack);
    display_bootloader_message();
}

void display_message(uint8_t row, uint8_t col, const uint32_t* message, const GFXfont* font) {

    const GFXfont* displayFont[] = { font };
    uint8_t index = 0;
    for (uint8_t c = 0; c < MATRIX_COLS; ++c) {

        uint8_t disp_idx = LAYOUT_TO_INDEX(row, c);

        if (disp_idx != 255) {

            sr_shift_out_buffer_latch(get_key_disp_bitmask(disp_idx), get_disp_bitmask_size());
            kdisp_set_buffer(0x00);

            if (c >= col && message[index] != 0) {
                const uint32_t text[2] = { message[index], 0 };
                kdisp_write_gfx_text(displayFont, 1, 49, 38, text);
                index++;
            }
            kdisp_send_buffer();
        }
    }
}

// Like display_message(), but each visible glyph is drawn either SOLID or DIM
// (scanline, half-density) depending on its position in the reveal sequence:
// the first `solid_count` visible glyphs (counting from `base_visible`, i.e.
// including glyphs already drawn on a previous row) render solid, the rest
// render scanline-dimmed. Spaces occupy their keycap (drawn as nothing) but do
// not advance the visible counter, so alignment spaces never cost a reveal step.
// Used by the progressive boot splash: the whole word is present from the first
// frame (dim), then solidifies letter-by-letter as boot advances.
void display_message_progressive(uint8_t row, uint8_t col, const uint32_t* message,
                                 const GFXfont* font, uint8_t base_visible, uint8_t solid_count) {

    const GFXfont* displayFont[] = { font };
    uint8_t index   = 0;
    uint8_t visible = base_visible;
    for (uint8_t c = 0; c < MATRIX_COLS; ++c) {

        uint8_t disp_idx = LAYOUT_TO_INDEX(row, c);

        if (disp_idx != 255) {

            sr_shift_out_buffer_latch(get_key_disp_bitmask(disp_idx), get_disp_bitmask_size());
            kdisp_set_buffer(0x00);

            if (c >= col && message[index] != 0) {
                const uint32_t cp = message[index];
                if (cp != U' ') {
                    const uint32_t text[2] = { cp, 0 };
                    // Not-yet-solid letters render dim via the coarse 2-on/2-off
                    // scanline — cleaner than the fine 1-on/1-off on the big
                    // 24pt splash glyphs, which flickered.
                    // Phase 0: the splash is a few seconds at boot, so a fixed
                    // band alignment costs nothing and a rolling one would read as
                    // shimmer across the reveal.
                    kdisp_set_gfx_scanline2(visible >= solid_count, 0);
                    kdisp_write_gfx_text(displayFont, 1, 49, 38, text);
                    kdisp_set_gfx_scanline2(false, 0);
                    visible++;
                }
                index++;
            }
            kdisp_send_buffer();
        }
    }
}
