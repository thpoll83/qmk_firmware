// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "oled_helper.h"

#include "state.h"
#include "side.h"
#include "base/com.h"
#include "base/disp_array.h"
#include "base/fw_staging.h"
#ifdef POLYKYBD_DOOM
#include "doom/doom_mode.h"
#include "doom/doom_logo_oled.h"
#endif

#include QMK_KEYBOARD_H
#include "quantum.h"

#include <stdio.h>

// _Mid_ (10px) utility font: defined in util_font.h, owned by poly_keymap.c's
// translation unit. Reference it via extern rather than re-including the header
// (which would duplicate its PROGMEM tables at link time), matching status_oled.c.
extern const GFXfont NotoSans_Regular_Mid_10pt7b;

// Render `value` as a char32 (U"...") display string into `buffer`. The display
// pipeline is 32-bit (kdisp_write_gfx_text takes const uint32_t*), so each digit
// glyph is one uint32_t codepoint. `buffer_len` is the byte size of the buffer.
static inline void digits_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value, uint8_t base) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (value >= base * base && i < cap) out[i++] = U'0' + (value / (base * base)) % base;
    if (value >= base       && i < cap) out[i++] = U'0' + (value / base) % base;
    if (i < cap) out[i++] = U'0' + (value % base);
    if (i < cap) out[i] = 0;
}

void num_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    digits_to_u32_string(buffer, buffer_len, value, 10);
}

// Widen an ASCII C string into the 32-bit codepoint string the kdisp text pipeline
// expects (kdisp_write_gfx_text takes const uint32_t*). NUL-terminated, never
// overruns `buffer_len`. Used for the font-pack bundle name on the flash screen.
void ascii_to_u32_string(char* buffer, uint8_t buffer_len, const char* s) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (s) {
        for (; s[i] && (i + 1u) < cap; ++i) out[i] = (uint32_t)(uint8_t)s[i];
    }
    if (i < cap) out[i] = 0;
}

void hex_to_u32_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (value >= 16 && i < cap) { uint8_t hi = value / 16; out[i++] = (hi < 10 ? U'0' + hi : U'A' + hi - 10); }
    if (i < cap) { uint8_t lo = value % 16; out[i++] = (lo < 10 ? U'0' + lo : U'A' + lo - 10); }
    if (i < cap) out[i] = 0;
}

void oled_draw_layout_name(const GFXfont* const* font, int8_t x, int8_t y, uint8_t def_layer) {
    // Indexed by def_layer (the _L0.._L4 default-layout enum in each variant's
    // keymaps/default/layers.h). Keep in sync with the KC_L0..KC_L4 selectors in
    // poly_keymap.c; an out-of-range value falls back to "Unknown".
    static const uint32_t* const names[] = {
        U"Qwerty", U"Qwerty Stag!", U"Colemak DH", U"Neo", U"Workman",
    };
    const uint32_t* name = (def_layer < ARRAY_SIZE(names)) ? names[def_layer] : U"Unknown";
    kdisp_write_gfx_text(font, 1, x, y, name);
}

void oled_status_screen(void) {
    const poly_sync_t* local_state = get_local_state();
    if ((local_state->flags & STATUS_DISP_ON) == 0) {
        oled_off();
        return;
    } else if ((local_state->flags & STATUS_DISP_ON) != 0) {
        oled_on();
    }
    oled_update_buffer();
    // No oled_clear() here: oled_update_buffer() already composes a full 1024-byte
    // frame into the scratch buffer (it starts with kdisp_set_buffer(0), so the
    // background is black), and oled_write_raw() diffs byte-for-byte and marks only
    // the blocks that actually changed dirty. Calling oled_clear() first forced ALL
    // 16 framebuffer blocks dirty every 66 ms tick, so the whole panel was re-pushed
    // over I2C band-by-band even when only the WPM digit / brightness bar moved —
    // that is the "updates in multiple passes" flicker. Diffing keeps a static screen
    // silent and shrinks an incremental change to the one or two blocks it touches.
    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    // Push the changed blocks in ONE pass (see oled_fw_update_screen for the full
    // rationale): the stock per-iteration oled_render() flushes only one block per
    // main-loop pass, so a status change landing during a busy window (e.g. an
    // overlay burst on an app switch) could tear top-first. This is a no-op when
    // nothing changed, so a static screen still costs nothing on the bus.
    oled_render_dirty(true);
}

void oled_render_logos(void) {
    if (is_left_side()) {
        oled_draw_poly();
        oled_scroll_right();
    } else {
        oled_draw_kybd();
        oled_scroll_left();
    }
}

// Progress bar drawn into the kdisp scratch buffer (call from
// oled_update_buffer_fw_update before the blit).
void oled_fw_update_progress_bar(int8_t top_y, int8_t bottom_y, uint8_t pct) {
    if (bottom_y <= top_y) return;   // guard: inverted bounds would wrap the unsigned height
    if (pct > 100) pct = 100;
    uint8_t fill = (uint8_t)((uint16_t)pct * 127u / 100u);   // 0..127 across the display
    uint8_t height = (uint8_t)(bottom_y - top_y);
    if (fill) kdisp_fill_rect(0, top_y, fill, height);
}

// Draw `pct` (0..100) as digits RIGHT-ALIGNED so the number's right edge ends a
// couple px before `pct_sign_x` — the fixed x where the caller then draws the
// "%" sign. The number grows leftward as it gains digits, so a 2- or 3-digit
// value no longer overruns the "%" (the old left-anchored draw overwrote it).
void oled_fw_update_percent(const GFXfont *const *font, int8_t pct_sign_x, int8_t y, uint8_t pct) {
    uint32_t buf[6];
    num_to_u32_string((char*) buf, sizeof(buf), pct);
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(font, 1, buf, &lo, &hi);   // pixel extents at draw-origin 0
    int8_t x = (int8_t)(pct_sign_x - 2 - hi);        // right edge ~2 px before the "%"
    if (x < 0) x = 0;
    kdisp_write_gfx_text(font, 1, x, y, buf);
}

// Shared 0..100 progress of the in-flight flash (current bundle's bytes).
uint8_t fw_update_percent(void) {
    uint32_t total = fw_staging_image_size();
    uint32_t done  = fw_staging_next_offset();
    return total ? (uint8_t)(((uint64_t)done * 100) / total) : 0;
}

// Shown on both halves while a font-pack / firmware flash is in progress, so the
// user knows the keyboard is busy updating (it can't service keys meanwhile) and
// must not be unplugged. Forced on regardless of the display-off state. The status
// fonts are resident, so this renders even while the font pack is mid-flash.
void oled_fw_update_screen(void) {
    oled_on();
    oled_update_buffer_fw_update();
    // Same diff-only compose as the status screen (no oled_clear() — the scratch
    // is a full 1024-byte frame with a black background from kdisp_set_buffer(0)).
    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    // Then push the changed blocks synchronously in ONE pass. During a flash the
    // main loop is saturated feeding HID chunks / driving the deferred sector
    // erase, so the normal per-iteration oled_render() (1 block per call) can't
    // keep up — the status->update transition dribbled out top-first and left the
    // bottom rows still showing the old status screen for a visible moment. A full
    // flush here lands the whole frame on the first tick it is drawn; afterwards
    // the master's screen is static (slave's progress bar is the only churn), so
    // diffing keeps this to just the bar's blocks.
    oled_render_dirty(true);
}

// Small clockwise "refresh" arrow (⟳), 16x16, row-major MSB-first. The mid status
// font is ASCII-only (0x20..0x7E), so this glyph is drawn as a bitmap next to the
// word on the firmware-apply screen.
static const uint8_t s_apply_spinner[] = {
    0x00, 0x1C, 0x00, 0x3E, 0x00, 0x1E, 0x0E, 0x1E, 0x1E, 0x38, 0x38, 0x1C,
    0x30, 0x0C, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x30, 0x0C, 0x30, 0x0C,
    0x38, 0x1C, 0x1F, 0xF8, 0x0F, 0xF0, 0x00, 0x00,
};
#define APPLY_SPINNER_W 16
#define APPLY_SPINNER_H 16

// Shown on BOTH halves the instant a staged firmware image is applied (reboot
// imminent). Reads across the two status OLEDs as "⟳Applying  Firmware⟳": the
// LEFT half shows "⟳Applying", the RIGHT half "Firmware⟳", each centered. It is
// fully flushed synchronously (oled_render_dirty(true)) so the screen is complete
// before fw_staging_apply_and_reboot()'s blocking self-flash + hard reset (which
// never returns) — the last thing the user sees is a finished, un-torn notice.
void oled_fw_apply_screen(void) {
    const GFXfont*  small[]   = { &NotoSans_Regular_Mid_10pt7b };
    const uint32_t* word      = is_left_side() ? U"Applying" : U"Firmware";
    const bool      icon_left = is_left_side();   // "⟳Applying" vs "Firmware⟳"
    const int8_t    gap       = 3;                // px between icon and word

    oled_on();
    kdisp_set_buffer(0);   // clear the scratch to black

    // Measure the word so the icon+word group is centered as one unit.
    int8_t tmin = 0, tmax = 0;
    kdisp_gfx_text_bounds(small, 1, word, &tmin, &tmax);
    const int8_t  text_w  = (int8_t)(tmax - tmin + 1);
    const int16_t group_w = (int16_t)APPLY_SPINNER_W + gap + text_w;
    int16_t       gx      = (int16_t)((OLED_DISPLAY_WIDTH - group_w) / 2);
    if (gx < 0) gx = 0;

    // Vertically centre: a baseline for the word, a top-left y for the icon.
    const int8_t baseline = (int8_t)(OLED_DISPLAY_HEIGHT / 2 + 7);
    const int8_t icon_y   = (int8_t)((OLED_DISPLAY_HEIGHT - APPLY_SPINNER_H) / 2);

    if (icon_left) {
        kdisp_draw_bitmap((int8_t)gx, icon_y, s_apply_spinner, APPLY_SPINNER_W, APPLY_SPINNER_H);
        kdisp_write_gfx_text(small, 1, (int8_t)(gx + APPLY_SPINNER_W + gap - tmin), baseline, word);
    } else {
        kdisp_write_gfx_text(small, 1, (int8_t)(gx - tmin), baseline, word);
        kdisp_draw_bitmap((int8_t)(gx + text_w + gap), icon_y, s_apply_spinner, APPLY_SPINNER_W, APPLY_SPINNER_H);
    }

    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    oled_render_dirty(true);   // one synchronous full flush before the reboot
}

bool oled_task_user(void) {
    if (fw_staging_fw_up_active()) {
        oled_scroll_off();
        oled_fw_update_screen();
#ifdef POLYKYBD_DOOM
    } else if (doom_mode_active() || get_local_state()->doom_ctl) {
        // Game mode status OLED — master directly, slave via the synced
        // control-pad flag. In a level the MASTER shows the doomguy face
        // (redrawn only when the face index changes); otherwise the DOOM
        // logo, whose HARDWARE scroll runs only during the attract (the
        // driver activates scroll once the buffer is clean, and repeated
        // identical writes stay non-dirty — zero traffic while scrolling).
        int face = doom_status_face_render((uint8_t *)get_scratch_buffer());
        if (face == 2) {
            oled_scroll_off();
            oled_write_raw((char *)get_scratch_buffer(), get_scratch_buffer_size());
        } else if (face == 0) {
            oled_write_raw((const char *)DOOM_LOGO_OLED, sizeof(DOOM_LOGO_OLED));
            if (doom_status_scroll()) {
                oled_scroll_left();
            } else {
                oled_scroll_off();
            }
        }
        // face == 1: the panel already shows the current face — leave it be.
#endif
    } else if ((get_local_state()->flags & DISP_IDLE) != 0) {
        oled_render_logos();
    } else {
        oled_scroll_off();
        oled_status_screen();
    }
    return false;
}
