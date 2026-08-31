// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "oled_helper.h"
#include "layer_names.h"

#include "state.h"
#include "side.h"
#include "bridge_helper.h"   // is_usb_host_side() + the split-link health counters
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

// Status-OLED fonts owned by poly_keymap.c's translation unit (via
// util_font.h / gfx_used_fonts.h). Reference them via extern rather than
// re-including the headers (which would duplicate their PROGMEM tables at link
// time), matching status_oled.c. The Arrows font is a resident symbol font
// (RESIDENT_FONTS) — it carries the circular "refresh" arrow U+2B6F used on the
// firmware-apply screen, so no pack and no custom bitmap are needed.
extern const GFXfont NotoSans_Regular_Mid_19px7b;
extern const GFXfont NotoSansSymbols2_Regular_Arrows_20pt16b;
// Defined in <variant>/status_oled.c's translation unit — extern here for the
// same reason (its PROGMEM tables must not be duplicated at link time).
extern const GFXfont NotoSans_Regular_Small_15px7b;

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

// 16-bit decimal, no leading zeros (0 renders as "0"). digits_to_u32_string above is
// uint8_t-only; the hue-in-degrees readout needs three digits up to 359.
void num16_to_u32_string(char* buffer, uint8_t buffer_len, uint16_t value) {
    uint32_t* out = (uint32_t*)buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    uint16_t  div = 10000;
    while (div > 1 && value < div) div /= 10;
    for (; div > 0; div /= 10) {
        if (i < cap) out[i++] = U'0' + (uint32_t)((value / div) % 10);
    }
    if (i < cap) out[i] = 0;
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
    // The table lives in layer_names.c, which also feeds split42's short forms and
    // HID cmd 35 -- see the header for why all three widths share one record.
    kdisp_write_gfx_text(font, 1, x, y, poly_layout_name(def_layer));
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

// Draw `text` so its rightmost lit pixel lands on `right_x`. A variable-width number
// then grows leftward instead of running into whatever is anchored to its right, which
// is what fixed-position numbers on the status screen keep getting wrong.
void oled_draw_text_right(const GFXfont *const *font, int8_t right_x, int8_t y, const uint32_t *text) {
    int8_t lo = 0, hi = 0;
    kdisp_gfx_text_bounds(font, 1, text, &lo, &hi);   // pixel extents at draw-origin 0
    int8_t x = (int8_t)(right_x - hi);
    if (x < 0) x = 0;
    kdisp_write_gfx_text(font, 1, x, y, text);
}

// Draw `value` right-aligned so it ends on `right_x`.
void oled_draw_num_right(const GFXfont *const *font, int8_t right_x, int8_t y, uint8_t value) {
    uint32_t buf[6];
    num_to_u32_string((char*) buf, sizeof(buf), value);
    oled_draw_text_right(font, right_x, y, buf);
}

void oled_draw_num16_right(const GFXfont *const *font, int8_t right_x, int8_t y, uint16_t value) {
    uint32_t buf[8];
    num16_to_u32_string((char*) buf, sizeof(buf), value);
    oled_draw_text_right(font, right_x, y, buf);
}

// Draw `pct` (0..100) as digits ending a couple px before `pct_sign_x` — the fixed x
// where the caller then draws the "%" sign.
void oled_fw_update_percent(const GFXfont *const *font, int8_t pct_sign_x, int8_t y, uint8_t pct) {
    oled_draw_num_right(font, (int8_t)(pct_sign_x - 2), y, pct);
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

// FW-2: the question behind the A/R keycaps. The keycaps alone say WHICH key does
// what but not WHY the board went modal, and a user who has never seen this before
// has no way to find out — the host is stuck at "verifying" and the console is not
// something anyone has open. So the status OLED states it in words, on both halves
// (poly_sync_t.fw_confirm is synced), each naming its OWN half's key.
//
// Deliberately landscape on split42 too, matching the flash/apply screens there —
// the portrait rework of those is still deferred.
void oled_fw_confirm_screen(void) {
    const GFXfont*  small = &NotoSans_Regular_Small_15px7b;
    const GFXfont*  fonts[] = { small };
    // Three lines on the 64px panel; the 32px one only has room for the verdict
    // and the key, so it drops the "firmware!" continuation.
    const bool      tall  = OLED_DISPLAY_HEIGHT >= 64;
    const uint32_t* l0    = tall ? U"Unsigned" : U"Unsigned!";
    const uint32_t* l1    = tall ? U"firmware!" : NULL;
    const uint32_t* l2    = is_left_side() ? U"A = ACCEPT" : U"R = REJECT";

    oled_on();
    kdisp_set_buffer(0);   // clear the scratch to black

    const uint32_t* lines[3] = { l0, l1, l2 };
    const uint8_t   count    = l1 ? 3 : 2;
    // Even vertical distribution: line i owns the band [i*H/count, (i+1)*H/count),
    // and each line is centred in its own band from its own bbox — so a line with a
    // descender ("firmware!" has none, but the key lines end in caps) sits level
    // rather than being pushed by the tallest line in the set.
    const int8_t band = (int8_t)(OLED_DISPLAY_HEIGHT / count);
    for (uint8_t i = 0; i < count; ++i) {
        const uint32_t* txt = lines[i];
        int8_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        kdisp_gfx_text_bbox(fonts, 1, txt, &x0, &x1, &y0, &y1);
        const int8_t w    = (int8_t)(x1 - x0 + 1);
        int16_t      x    = (int16_t)((OLED_DISPLAY_WIDTH - w) / 2 - x0);
        if (x < 0) x = 0;
        const int8_t base = (int8_t)(band * i + band / 2 - (y0 + y1) / 2);
        kdisp_write_gfx_text(fonts, 1, (int8_t)x, base, txt);
    }

    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    // One synchronous pass, same reason as the flash screen: this is a full-screen
    // transition and the user must be able to read it immediately, not watch it
    // dribble in a block at a time.
    oled_render_dirty(true);
}

// Shown on BOTH halves the instant a staged firmware image is applied (reboot
// imminent). Reads across the two status OLEDs as "⟳Applying  Firmware⟳": the
// LEFT half shows the resident circular refresh arrow U+2B6F + "Applying", the
// RIGHT half "Firmware" + the arrow, each horizontally and vertically centered.
// It is fully flushed synchronously (oled_render_dirty(true)) so the screen is
// complete before fw_staging_apply_and_reboot()'s blocking self-flash + hard reset
// (which never returns) — the last thing the user sees is a finished, un-torn notice.
void oled_fw_apply_screen(void) {
    const GFXfont*  mid[]     = { &NotoSans_Regular_Mid_19px7b };
    const GFXfont*  arrow[]   = { &NotoSansSymbols2_Regular_Arrows_20pt16b };
    const uint32_t* icon      = U"\U00002B6F";   // resident circular "refresh" arrow ⭯
    const uint32_t* word      = is_left_side() ? U"Applying" : U"Firmware";
    const bool      icon_left = is_left_side();
    const int8_t    gap       = 3;               // px between icon and word

    oled_on();
    kdisp_set_buffer(0);   // clear the scratch to black

    // Measure both through SINGLE-font arrays (so fonts[0] is each glyph's own font
    // → no baseline-align shift, matching the draws below). The full bbox lets each
    // element be centered independently on the panel despite different heights.
    int8_t ix0 = 0, ix1 = 0, iy0 = 0, iy1 = 0;
    int8_t tx0 = 0, tx1 = 0, ty0 = 0, ty1 = 0;
    kdisp_gfx_text_bbox(arrow, 1, icon, &ix0, &ix1, &iy0, &iy1);
    kdisp_gfx_text_bbox(mid,   1, word, &tx0, &tx1, &ty0, &ty1);
    const int8_t iw = (int8_t)(ix1 - ix0 + 1);
    const int8_t tw = (int8_t)(tx1 - tx0 + 1);
    int16_t gx = (int16_t)((OLED_DISPLAY_WIDTH - (iw + gap + tw)) / 2);
    if (gx < 0) gx = 0;

    // Per-element vertical centre: a baseline B lands lit pixels at [B+min, B+max],
    // so B = H/2 - (min+max)/2. The x origin is offset by -bbox_min so the leftmost
    // lit pixel lands exactly at the group position (side bearings don't shift it).
    const int8_t iBase = (int8_t)(OLED_DISPLAY_HEIGHT / 2 - (iy0 + iy1) / 2);
    const int8_t tBase = (int8_t)(OLED_DISPLAY_HEIGHT / 2 - (ty0 + ty1) / 2);

    if (icon_left) {
        kdisp_write_gfx_text(arrow, 1, (int8_t)(gx - ix0),            iBase, icon);
        kdisp_write_gfx_text(mid,   1, (int8_t)(gx + iw + gap - tx0), tBase, word);
    } else {
        kdisp_write_gfx_text(mid,   1, (int8_t)(gx - tx0),            tBase, word);
        kdisp_write_gfx_text(arrow, 1, (int8_t)(gx + tw + gap - ix0), iBase, icon);
    }

    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    oled_render_dirty(true);   // one synchronous full flush before the reboot
}

// ---------------------------------------------------------------------------
// Settings -> "More" telemetry screen
//
// The advanced settings row is revealed by KC_SETTINGS_MORE, and while it is open
// the status OLED has nothing to say that the keycaps do not — so it states what
// the board IS instead: the versions a support round always asks for first, and
// the split-link health, which until now existed only in the periodic console line
// (every 200 frames, on a console nobody has open).
//
// Deliberately landscape on split42 too, matching the flash / confirm / apply
// screens there — that panel's portrait rework is still deferred, so it renders
// sideways on split42 rather than not at all.
//
// Driven by the SYNCED poly_sync_t.settings_more, so both halves show it together
// and it clears itself when the settings layer is left (layer_state_set_user).

// Uptime as h:mm:ss, or Nd Nh once hours reach three digits. Sourced from
// timer_read32(), so it is THIS half's own uptime and it wraps with the 32-bit ms
// timer at 49.7 days — long enough to be useful, short enough to say so.
static void telemetry_uptime(char* out, size_t cap) {
    const uint32_t secs = timer_read32() / 1000U;
    const uint32_t h    = secs / 3600U;
    if (h < 100U) {
        snprintf(out, cap, "%lu:%02lu:%02lu", (unsigned long)h,
                 (unsigned long)((secs / 60U) % 60U), (unsigned long)(secs % 60U));
    } else {
        snprintf(out, cap, "%lud %luh", (unsigned long)(h / 24U), (unsigned long)(h % 24U));
    }
}

void oled_telemetry_screen(void) {
    const GFXfont* small    = &NotoSans_Regular_Small_15px7b;
    const GFXfont* fonts[]  = { small };
    const bool     tall     = OLED_DISPLAY_HEIGHT >= 64;

    char up[16];
    telemetry_uptime(up, sizeof(up));

    // The identity fields are exactly the ones GET_ID reports (hid_com.c), so the
    // panel and the host's view of the board can never disagree.
    char l_fw[24], l_ver[24], l_up[24], l_link[24];
    snprintf(l_fw,  sizeof(l_fw),  "FW %s", FW_VERSION);
    snprintf(l_ver, sizeof(l_ver), "P%d  HW %s", (int)PROTOCOL_VERSION, STR(DEVICE_VER));
    snprintf(l_up,  sizeof(l_up),  "%s  up %s", is_usb_host_side() ? "USB" : "LNK", up);

    poly_link_stats_t ls;
    poly_get_link_stats(&ls);
    if (!is_usb_host_side()) {
        // ⚠️ Not "0.0%". Only the master initiates bridges, so this half's counters
        // are zero because it never sends — rendering that as a perfect link would
        // be a flattering lie on exactly the panel someone reads to judge the wire.
        snprintf(l_link, sizeof(l_link), "Lnk n/a");
    } else if (ls.attempts == 0U) {
        snprintf(l_link, sizeof(l_link), "Lnk idle");
    } else {
        // ⚠️ Both fields are COMPACTED because the worst case, not the typical one,
        // decides whether the line fits: the frame count climbs for as long as the
        // board is up (millions within hours), and spelled out in full it runs off
        // the 128 px panel — measured at 135 px against a 127 px budget, while the
        // "1234tx" a fresh boot shows fits comfortably and hides it.
        // One decimal below 10 % (where the precision is the whole point) and a
        // whole percent above it keeps the widest form at 122 px.
        char tx[8];
        if (ls.attempts < 1000U) {
            snprintf(tx, sizeof(tx), "%lu", (unsigned long)ls.attempts);
        } else if (ls.attempts < 1000000U) {
            snprintf(tx, sizeof(tx), "%luk", (unsigned long)(ls.attempts / 1000U));
        } else {
            snprintf(tx, sizeof(tx), "%luM", (unsigned long)(ls.attempts / 1000000U));
        }
        const uint32_t pm = poly_link_err_permille();
        if (pm < 100U) {
            snprintf(l_link, sizeof(l_link), "Lnk %lu.%lu%% %s",
                     (unsigned long)(pm / 10U), (unsigned long)(pm % 10U), tx);
        } else {
            snprintf(l_link, sizeof(l_link), "Lnk %lu%% %s", (unsigned long)((pm + 5U) / 10U), tx);
        }
    }

    // The 32 px panel holds two lines, so it keeps the two that cannot be read off
    // anything else on the board; uptime and link health are split72-only.
    const char* lines[4] = { l_fw, l_ver, l_up, l_link };
    const uint8_t count  = tall ? 4u : 2u;

    oled_on();
    kdisp_set_buffer(0);   // clear the scratch to black

    // Even vertical distribution, each line centred in its own band from its OWN
    // bbox — the same layout the FW-2 confirm screen uses, so a line with a
    // descender sits level instead of being pushed by the tallest line in the set.
    const int8_t band = (int8_t)(OLED_DISPLAY_HEIGHT / count);
    for (uint8_t i = 0; i < count; ++i) {
        uint32_t txt[24];
        ascii_to_u32_string((char*)txt, sizeof(txt), lines[i]);
        int8_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        kdisp_gfx_text_bbox(fonts, 1, txt, &x0, &x1, &y0, &y1);
        const int8_t w = (int8_t)(x1 - x0 + 1);
        int16_t      x = (int16_t)((OLED_DISPLAY_WIDTH - w) / 2 - x0);
        if (x < 0) x = 0;
        const int8_t base = (int8_t)(band * i + band / 2 - (y0 + y1) / 2);
        kdisp_write_gfx_text(fonts, 1, (int8_t)x, base, txt);
    }

    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    // One synchronous pass: this is a full-screen swap the user asked for by pressing
    // a key, so it must land complete rather than dribble in a block per main loop.
    // A no-op once the screen is static (oled_render_dirty early-returns when clean),
    // which matters because the uptime line changes only once a second.
    oled_render_dirty(true);
}

// Typing-speed dial (11x6 speedometer). Shared by BOTH variants' status OLEDs, so it
// is defined once here (oled_helper.c is in the shared POLY_SRC) and referenced via
// extern from each status_oled.c -- defining it per variant drifts the two copies.
// It replaces a "WPM" text label, which cost 38px of a 105px row to say what the dial
// says in 11.
const uint8_t wpm_gauge_bitmap[] PROGMEM = {
    0x1f, 0x00,
    0x71, 0xc0,
    0x43, 0x40,
    0xc2, 0x60,
    0x86, 0x20,
    0x8e, 0x20,
};

bool oled_task_user(void) {
    // FW-2: the unsigned-image question outranks everything else — the board is a
    // modal dialog and nothing else it could show is actionable. Checked before the
    // flash screen because by the time the prompt goes up finalize has already
    // cleared fw_up_active, so this would otherwise fall through to the idle/status
    // screen and leave the keycaps asking a question the panel never states.
    if (get_local_state()->fw_confirm) {
        oled_scroll_off();
        oled_fw_confirm_screen();
    } else if (fw_staging_fw_up_active()) {
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
    } else if (get_local_state()->settings_more != 0) {
        // Settings -> "More" is open: show what the board IS. Below the idle branch
        // on purpose — an idled board has nothing to report and the logos are the
        // lower-power screen; settings_more clears itself on leaving the layer.
        oled_scroll_off();
        oled_telemetry_screen();
    } else {
        oled_scroll_off();
        oled_status_screen();
    }
    return false;
}
