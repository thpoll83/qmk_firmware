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
#include "poly_keymap.h"         // poly_fw_screen() / poly_fw_hold_active()
#include "poly_macro.h"          // POLY_MACRO_COUNT
#include "poly_macro_record.h"   // enum poly_rec_state + the recording read-outs
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
static inline void digits_to_u32_string(uint32_t* buffer, uint8_t buffer_len, uint8_t value, uint8_t base) {
    uint32_t* out = buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (value >= base * base && i < cap) out[i++] = U'0' + (value / (base * base)) % base;
    if (value >= base       && i < cap) out[i++] = U'0' + (value / base) % base;
    if (i < cap) out[i++] = U'0' + (value % base);
    if (i < cap) out[i] = 0;
}

void num_to_u32_string(uint32_t* buffer, uint8_t buffer_len, uint8_t value) {
    digits_to_u32_string(buffer, buffer_len, value, 10);
}

// 16-bit decimal, no leading zeros (0 renders as "0"). digits_to_u32_string above is
// uint8_t-only; the hue-in-degrees readout needs three digits up to 359.
void num16_to_u32_string(uint32_t* buffer, uint8_t buffer_len, uint16_t value) {
    uint32_t* out = buffer;
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
void ascii_to_u32_string(uint32_t* buffer, uint8_t buffer_len, const char* s) {
    uint32_t* out = buffer;
    uint8_t   cap = buffer_len / (uint8_t)sizeof(uint32_t);
    uint8_t   i   = 0;
    if (s) {
        for (; s[i] && (i + 1u) < cap; ++i) out[i] = (uint32_t)(uint8_t)s[i];
    }
    if (i < cap) out[i] = 0;
}

void hex_to_u32_string(uint32_t* buffer, uint8_t buffer_len, uint8_t value) {
    uint32_t* out = buffer;
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
    num_to_u32_string(buf, sizeof(buf), value);
    oled_draw_text_right(font, right_x, y, buf);
}

void oled_draw_num16_right(const GFXfont *const *font, int8_t right_x, int8_t y, uint16_t value) {
    uint32_t buf[8];
    num16_to_u32_string(buf, sizeof(buf), value);
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
    // WHAT is unsigned — the one thing that differs between the two prompts, and
    // the thing a user needs to tell them apart. A firmware image replaces the
    // board's code; a DOOM pack is an easter-egg engine that only runs while the
    // game does. Agreeing to one is not agreeing to the other.
    const bool      pack  = get_local_state()->fw_confirm == POLY_CONFIRM_DOOM_PACK;
    const uint32_t* l0    = tall ? U"Unsigned" : U"Unsigned!";
    const uint32_t* l1    = !tall ? NULL : (pack ? U"DOOM pack!" : U"firmware!");
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

// The shared two-word firmware notice. Reads across the PAIR of status OLEDs —
// this half draws `word`, the other draws its own — with the resident circular
// refresh arrow U+2B6F on the outside when `icon` is true, each element
// horizontally and vertically centered.
//
// ⚠️ It is flushed SYNCHRONOUSLY (oled_render_dirty(true)) because every caller is
// about to do something that does not come back — the blocking self-flash, the hard
// reset — or has just blanked the keycaps. A notice queued for the next
// oled_render() tick on any of those paths is a notice nobody ever sees; that is
// the same trap poly_flash_rgb_now() exists to dodge on the RGB side.
//
// `icon` is false for the failure notice on purpose: the arrow means "in progress"
// and is the wrong thing to leave on screen when nothing is progressing. A dedicated
// warning glyph would need a font-pack round for one screen, so the word carries it.
//
// One word, centred, and nothing else. A small-font second line carrying the staged
// size was tried here and removed: on the screen that is FROZEN for the whole copy,
// the state is the message, and a number beside it only competes with it. The size is
// still in the console line, and the failure screen — which genuinely has something to
// say — carries its detail in its own band layout rather than bolting a line onto this
// one.
static void oled_fw_notice(const uint32_t* word, bool icon) {
    const GFXfont*  mid[]     = { &NotoSans_Regular_Mid_19px7b };
    const GFXfont*  arrow[]   = { &NotoSansSymbols2_Regular_Arrows_20pt16b };
    const uint32_t* icon_txt  = U"\U00002B6F";   // resident circular "refresh" arrow ⭯
    const bool      icon_left = is_left_side();
    const int8_t    gap       = icon ? 3 : 0;    // px between icon and word

    oled_on();
    kdisp_set_buffer(0);   // clear the scratch to black

    // Measure both through SINGLE-font arrays (so fonts[0] is each glyph's own font
    // → no baseline-align shift, matching the draws below). The full bbox lets each
    // element be centered independently on the panel despite different heights.
    int8_t ix0 = 0, ix1 = 0, iy0 = 0, iy1 = 0;
    int8_t tx0 = 0, tx1 = 0, ty0 = 0, ty1 = 0;
    if (icon) kdisp_gfx_text_bbox(arrow, 1, icon_txt, &ix0, &ix1, &iy0, &iy1);
    kdisp_gfx_text_bbox(mid,   1, word, &tx0, &tx1, &ty0, &ty1);
    const int8_t iw = icon ? (int8_t)(ix1 - ix0 + 1) : 0;
    const int8_t tw = (int8_t)(tx1 - tx0 + 1);
    int16_t gx = (int16_t)((OLED_DISPLAY_WIDTH - (iw + gap + tw)) / 2);
    if (gx < 0) gx = 0;

    // Per-element vertical centre: a baseline B lands lit pixels at [B+min, B+max],
    // so B = H/2 - (min+max)/2. The x origin is offset by -bbox_min so the leftmost
    // lit pixel lands exactly at the group position (side bearings don't shift it).
    const int8_t iBase = (int8_t)(OLED_DISPLAY_HEIGHT / 2 - (iy0 + iy1) / 2);
    const int8_t tBase = (int8_t)(OLED_DISPLAY_HEIGHT / 2 - (ty0 + ty1) / 2);

    if (!icon) {
        kdisp_write_gfx_text(mid, 1, (int8_t)(gx - tx0), tBase, word);
    } else if (icon_left) {
        kdisp_write_gfx_text(arrow, 1, (int8_t)(gx - ix0),            iBase, icon_txt);
        kdisp_write_gfx_text(mid,   1, (int8_t)(gx + iw + gap - tx0), tBase, word);
    } else {
        kdisp_write_gfx_text(mid,   1, (int8_t)(gx - tx0),            tBase, word);
        kdisp_write_gfx_text(arrow, 1, (int8_t)(gx + tw + gap - ix0), iBase, icon_txt);
    }

    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    oled_render_dirty(true);   // one synchronous full flush before the reboot
}

// Boot progress, drawn straight onto the status OLED at every splash milestone.
//
// ⚠️ This exists because a boot HANG leaves no other evidence. The whole of post_init
// runs with the watchdog off on purpose (crash_watchdog_start() is its last line), so
// a stall there is permanent: no reset, no crash record, and the board sits there
// until it is unplugged. The only thing that survived was the keycap splash's
// solidify count — "how many letters went solid" — which is a field report of the
// form "it was stuck with PO" and localises the stall to one of seven gaps only if
// the letters are counted exactly. A number cannot be miscounted.
//
// Costs a couple of hundred ms of I2C across the whole boot: the first paint is a
// full frame, the rest change only the digit, and oled_write_raw diffs.
void oled_boot_progress(uint8_t step, uint8_t total) {
    // ⚠️ The 19 px face does NOT fit two bands on the 32 px panel — measured, 2 px of
    // "Booting...."'s ascenders land at y = -1 and the hardware clips them away.
    // split42 uses the 15 px face instead; it still fits comfortably across 128 px
    // (74 px for the label, 39 for the percent).
    const GFXfont* face[]  = { (OLED_DISPLAY_HEIGHT >= 64) ? &NotoSans_Regular_Mid_19px7b
                                                           : &NotoSans_Regular_Small_15px7b };
    uint32_t       buf[12];
    char           txt[20];

    // ⚠️ TWO lines, not one. "Booting.... 100%" measures 143 of the 128 px in this
    // font, and 119 in the small one — 4 px of margin, the fit-by-a-hair shape that
    // already sent "Restarting" and "no image staged" back for a second pass. Split,
    // the widest parts are 88 px and 48 px.
    const uint32_t* label = U"Booting....";
    // Round to nearest so the steps read 25 / 38 / 50 / 63 / 75 / 88 / 100 rather than
    // truncating three of them a point low. The percent is the HUMAN form of the
    // milestone; the machine-readable one is the CRASH_PHASE_BOOT argument, which stays
    // the step number, so "stuck at 38%" and phase=1:0x0003 name the same place.
    const uint8_t pct = (uint8_t)(((uint16_t)step * 100u + total / 2u) / total);
    snprintf(txt, sizeof(txt), "%u%%", (unsigned)pct);
    ascii_to_u32_string(buf, sizeof(buf), txt);

    oled_on();
    kdisp_set_buffer(0);

    // Each line centred in its own half of the panel, from its own bbox — the same
    // band shape oled_fw_confirm_screen() uses, so a descender does not push the other
    // line. Works unchanged on the 32 px panel: two bands of 16.
    const uint32_t* lines[2] = { label, buf };
    const int8_t    band     = (int8_t)(OLED_DISPLAY_HEIGHT / 2);
    for (uint8_t i = 0; i < 2; ++i) {
        int8_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        kdisp_gfx_text_bbox(face, 1, lines[i], &x0, &x1, &y0, &y1);
        int16_t x = (int16_t)((OLED_DISPLAY_WIDTH - (x1 - x0 + 1)) / 2 - x0);
        if (x < 0) x = 0;
        kdisp_write_gfx_text(face, 1, (int8_t)x,
                             (int8_t)(band * i + band / 2 - (y0 + y1) / 2), lines[i]);
    }

    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    // Synchronous, for the usual reason: the step this announces may be the one that
    // never returns, and a frame left for the next oled_render() tick is a frame the
    // hung board never shows.
    oled_render_dirty(true);
}

// "⭯Applying  Restarts⭯" — ONE screen for the whole apply, and the one frozen on the
// panel for the entire multi-second copy.
//
// ⚠️ It names the LONG operation, because there is no way to change it part-way and
// the erase+rewrite of ~490 KB sits under it for SECONDS. "Restart Now" was tried here
// and read as a hang for exactly that reason — a word promising something instant
// makes a multi-second wait feel broken. This pairs with the transfer screen's
// "Staging...": the two phases now use the firmware's own vocabulary (fw_staging_* /
// apply), so which one you are in is readable rather than inferred.
//
// The reboot is deliberately NOT named. It cannot be shown when it happens (see
// below), and naming it on the screen that covers the copy is what caused the "feels
// very long" report. The board coming back on "Booting.... 25%" is the restart.
//
// ⚠️ A timed hand-off between two screens is NOT available here, and the SSD1306's
// hardware scroll cannot fake it: on a 128x64 panel OLED_MATRIX_SIZE is the whole
// GDDRAM (64/8 * 128 = 1024 B) and every byte of it is displayed, so there is no
// off-screen region to scroll a second frame in from — and QMK drives SCROLL_LEFT /
// SCROLL_RIGHT, the horizontal continuous scroll, which wraps the same 128 columns.
// (It would work on a 128x32 panel, where half the GDDRAM is hidden.) Nothing else
// can run either: the copy holds the core with interrupts off and never returns.
void oled_fw_apply_screen(void) {
    oled_fw_notice(is_left_side() ? U"Applying" : U"Firmware", true);
}

// "⭯Restart  Now⭯" — the QK_REBOOT / staged-reset path. It clears the keyboard,
// latches the orange cue and calls mcu_reset(), which never returns, so this had no
// status-OLED state at all: the panel kept showing the ordinary status screen right
// up to the reset.
//
// "Restart", not the more natural "Restarting": measured through the real committed
// font, arrow + gap + "Restarting" is 125 px of the 128 px panel, i.e. 1 px of left
// margin. It does not clip, but a word that only just fits is a word that clips the
// next time the font is regenerated. "Restart" is 96 px, 16 px a side.
void oled_fw_restart_screen(void) {
    oled_fw_notice(is_left_side() ? U"Restart" : U"Now", true);
}

// "Update  FAILED" — the staged image did not match its CRC, so the apply was
// REFUSED and the board is still running the old firmware.
//
// This is the one firmware state the user could previously only discover from a
// console nobody has open: the RGB went orange, the keycaps blanked, and then
// everything silently came back with the update not applied. Held on the panel by
// the POLY_FW_NOTICE_MS deadline in poly_keymap.c, because unlike every other
// caller here this path DOES return — without the hold, oled_task_user() repaints
// the status screen over it on the next 66 ms tick.
void oled_fw_failed_screen(void) {
    // Band layout rather than the two-word notice: this is the one firmware screen
    // with something to SAY, and "it did not work" is what the board could already
    // manage. It borrows oled_fw_confirm_screen()'s shape — three lines on the 64 px
    // panel, two on the 32 px one, each centred in its own band from its own bbox —
    // because that shape is already proven on both heights.
    //
    // The pair splits the job: the LEFT half says what happened and why, the RIGHT
    // half carries the numbers a support round asks for. The reason matters because
    // the two failures are different events — NO_IMAGE means nothing ever arrived, so
    // re-sending the apply will fail identically and the UPLOAD has to be redone;
    // BAD_CRC means bytes did arrive and are damaged, which a re-send usually fixes.
    const GFXfont*     fonts[] = { &NotoSans_Regular_Small_15px7b };
    const bool         tall    = OLED_DISPLAY_HEIGHT >= 64;
    uint32_t           size = 0, want = 0, got = 0;
    const fw_apply_verdict_t why = poly_fw_failure_detail(&size, &want, &got);
    const bool         no_image  = (why == FW_APPLY_NO_IMAGE);

    uint32_t b1[16], b2[16], b3[16];
    char     t1[24], t2[24], t3[24];
    const uint32_t* lines[3] = { NULL, NULL, NULL };

    if (is_left_side()) {
        lines[0] = U"Update";
        lines[1] = U"FAILED";
        // "no image staged" measures 123 of the 128 px, i.e. 2 px of margin — the
        // same one-pixel-fit trap that sent "Restarting" back to "Restart".
        lines[2] = no_image ? U"not staged" : U"bad checksum";
    } else if (no_image) {
        // Nothing to quote, so say what to DO instead of printing three zeroes that
        // would read as measurements of an image that does not exist.
        // ⚠️ No em dash here: U+2014 is absent from NotoSans_Regular_Small_15px7b, and
        // kdisp_write_gfx_text SKIPS a glyph the font does not carry — so it renders as
        // nothing at all rather than as a missing-glyph box. Caught by measuring, not
        // by looking: "staged —" and "staged" come back 2 px apart.
        lines[0] = U"Nothing was";
        lines[1] = U"staged";
        lines[2] = U"upload again";
    } else {
        snprintf(t1, sizeof(t1), "%lu KB", (unsigned long)((size + 1023u) / 1024u));
        snprintf(t2, sizeof(t2), "want %08lx", (unsigned long)want);
        ascii_to_u32_string(b1, sizeof(b1), t1);
        ascii_to_u32_string(b2, sizeof(b2), t2);
        lines[0] = b1;
        lines[1] = b2;
        // The third line is the CRC actually read back. Both numbers together are what
        // separates "the host sent the wrong thing" from "the flash did not take".
        snprintf(t3, sizeof(t3), "got  %08lx", (unsigned long)got);
        ascii_to_u32_string(b3, sizeof(b3), t3);
        lines[2] = b3;
    }

    // On the 32 px panel only two lines fit, so drop the MIDDLE one: the first names
    // the screen and the last carries the payload, and losing either would leave a
    // line that cannot be read on its own.
    const uint32_t* show[3];
    uint8_t count = 0;
    if (tall) {
        for (uint8_t i = 0; i < 3; ++i) show[count++] = lines[i];
    } else {
        show[count++] = lines[0];
        show[count++] = lines[2];
    }

    oled_on();
    kdisp_set_buffer(0);
    const int8_t band = (int8_t)(OLED_DISPLAY_HEIGHT / count);
    for (uint8_t i = 0; i < count; ++i) {
        int8_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        kdisp_gfx_text_bbox(fonts, 1, show[i], &x0, &x1, &y0, &y1);
        int16_t x = (int16_t)((OLED_DISPLAY_WIDTH - (x1 - x0 + 1)) / 2 - x0);
        if (x < 0) x = 0;
        kdisp_write_gfx_text(fonts, 1, (int8_t)x,
                             (int8_t)(band * i + band / 2 - (y0 + y1) / 2), show[i]);
    }
    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    oled_render_dirty(true);
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
        ascii_to_u32_string(txt, sizeof(txt), lines[i]);
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

// On-keyboard macro recording (MACRO_RECORD_DESIGN.md section 4). The status OLED is
// the ONLY indicator the gesture has -- split42 has no RGB matrix, and the keycaps are
// busy showing what the user is typing -- so it has to say where the gesture is, what
// it is aimed at, and how to get out of it.
//
// Laid out like oled_telemetry_screen() above (even bands, each line centred from its
// OWN bbox) and, like the flash / confirm / apply screens, drawn LANDSCAPE on split42
// too: the portrait rework of that family is still deferred, and one recording screen
// that matches its neighbours beats two that do not.
void oled_macro_rec_screen(void) {
    const GFXfont* small   = &NotoSans_Regular_Small_15px7b;
    const GFXfont* fonts[] = { small };
    const bool     tall    = OLED_DISPLAY_HEIGHT >= 64;

    // Synced, so this reads the same on both halves. The byte count and the elapsed
    // time are NOT synced: they move on every captured keystroke, and putting that on
    // poly_sync_t would buy a bridge frame per keypress on the one link this repo has
    // been bitten by most. So the master shows them and the slave shows the stop hint
    // in their place -- true on both panels rather than a plausible zero on one, the
    // same call the telemetry screen's "Lnk n/a" makes.
    const poly_sync_t* st    = get_local_state();
    const uint8_t      state = st->rec_state;
    const uint8_t      slot  = st->rec_slot;
    const bool         mine  = is_usb_host_side();

    char l0[24], l1[24], l2[24], l3[24];
    l0[0] = l1[0] = l2[0] = l3[0] = '\0';

    switch (state) {
        case POLY_REC_PICKING:
            snprintf(l0, sizeof(l0), "Pick a macro");
            snprintf(l1, sizeof(l1), "press M0-M%u", (unsigned)(POLY_MACRO_COUNT - 1u));
            snprintf(l2, sizeof(l2), "REC = cancel");
            break;
        case POLY_REC_RECORDING:
            // ⚠️ The whole line blinks rather than a marker beside a fixed word: each
            // line is centred from its own ink box, so a marker that comes and goes
            // would slide the text half a glyph twice a second. A line that appears and
            // disappears reads as "recording NOW", which is the entire job of line 0.
            if ((timer_read32() / 500U) & 1U) snprintf(l0, sizeof(l0), "REC M%u", (unsigned)slot);
            if (mine) {
                snprintf(l1, sizeof(l1), "%u/%u B", (unsigned)poly_macro_rec_bytes(),
                         (unsigned)POLY_MACRO_REC_BYTES);
                const uint32_t s = poly_macro_rec_elapsed_ms() / 1000U;
                snprintf(l2, sizeof(l2), "%lu:%02lu", (unsigned long)(s / 60U),
                         (unsigned long)(s % 60U));
                snprintf(l3, sizeof(l3), "REC = stop");
            } else {
                snprintf(l1, sizeof(l1), "REC = stop");
            }
            break;
        case POLY_REC_SAVING:
            snprintf(l0, sizeof(l0), "Saving M%u", (unsigned)slot);
            break;
        case POLY_REC_SAVED:
            snprintf(l0, sizeof(l0), "Saved M%u", (unsigned)slot);
            if (mine) snprintf(l1, sizeof(l1), "%u B", (unsigned)poly_macro_rec_bytes());
            break;
        default:
            return;   // IDLE: oled_task_user() never gets here, but never draw a blank frame
    }

    oled_on();
    kdisp_set_buffer(0);   // clear the scratch to black

    // The 32 px panel holds two lines, so it keeps the two that say where the gesture
    // is and what it costs; the elapsed time and the stop hint are split72-only.
    const char*   lines[4] = { l0, l1, l2, l3 };
    const uint8_t count    = tall ? 4u : 2u;
    const int8_t  band     = (int8_t)(OLED_DISPLAY_HEIGHT / count);
    for (uint8_t i = 0; i < count; ++i) {
        if (lines[i][0] == '\0') continue;   // the blink's dark phase, or an unused line
        uint32_t txt[24];
        ascii_to_u32_string(txt, sizeof(txt), lines[i]);
        int8_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        kdisp_gfx_text_bbox(fonts, 1, txt, &x0, &x1, &y0, &y1);
        const int8_t w = (int8_t)(x1 - x0 + 1);
        int16_t      x = (int16_t)((OLED_DISPLAY_WIDTH - w) / 2 - x0);
        if (x < 0) x = 0;
        const int8_t base = (int8_t)(band * i + band / 2 - (y0 + y1) / 2);
        kdisp_write_gfx_text(fonts, 1, (int8_t)x, base, txt);
    }

    oled_write_raw((char*)get_scratch_buffer(), get_scratch_buffer_size());
    // ⚠️ No oled_clear() anywhere in here, and none per frame: oled_write_raw diffs the
    // scratch against the framebuffer and dirties only the blocks that moved, so the
    // 1 Hz blink costs one block per second. An oled_clear() would defeat that and
    // re-push the whole frame every tick -- the "updates in multiple passes" flicker.
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
    // A firmware episode owns the panel outright: ONE selector decides which screen,
    // so the dispatch cannot fall through to the status screen between two phases.
    // It used to be three separate conditions here, and the seams between them were
    // visible on hardware as a flash of the status screen — see poly_fw_screen().
    //
    // Re-asserting the same screen every tick is nearly free: oled_write_raw diffs,
    // so once it is up nothing is dirty and oled_render_dirty(true) early-returns.
    const poly_fw_screen_t fw = poly_fw_screen();
    if (fw != POLY_FW_SCREEN_NONE) {
        oled_scroll_off();
        switch (fw) {
            // FW-2: the unsigned-image question outranks everything else — the board
            // is a modal dialog and nothing else it could show is actionable.
            case POLY_FW_SCREEN_CONFIRM: oled_fw_confirm_screen(); break;
            case POLY_FW_SCREEN_UPDATE:  oled_fw_update_screen();  break;
            case POLY_FW_SCREEN_APPLY:   oled_fw_apply_screen();   break;
            case POLY_FW_SCREEN_RESTART: oled_fw_restart_screen(); break;
            case POLY_FW_SCREEN_FAILED:  oled_fw_failed_screen();  break;
            default: break;
        }
    } else if (poly_fw_hold_active()) {
        // In the GAP between two phases. Deliberately draws NOTHING: the SSD1306 keeps
        // its GDDRAM, so the last firmware screen simply stays on the glass until the
        // next phase takes over. That is why this is a hold and not a "busy" screen —
        // re-rendering the update screen here would read its progress out of state
        // that has already gone idle and show 0%, which is worse than the flash it is
        // meant to fix.
        return false;
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
    } else if (get_local_state()->rec_state != POLY_REC_IDLE) {
        // ABOVE the idle branch on purpose: the idle timer would otherwise swap the
        // panel to the logos mid-recording and take the only indicator with it. The
        // recorder also holds update_performed() while it is busy (poly_keymap.c), so
        // in practice idle never engages here -- this ordering is the belt to that
        // brace, and it also covers the SAVING / SAVED tail after the last keystroke.
        oled_scroll_off();
        oled_macro_rec_screen();
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
