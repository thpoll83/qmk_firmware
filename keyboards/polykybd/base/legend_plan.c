// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "legend_plan.h"

#include <stddef.h>

// The two bigger faces are the SAME latin repertoire rendered larger, so they
// cannot be looked up by their natural codepoints: g_all_fonts is scanned
// front-to-back and the resident 27 px `latin` font is always in front, so a
// second 'a' at 0x61 could never be reached. fonts.yaml therefore emits each
// tier at a fixed offset into supplementary private-use plane 15 (fontconvert
// -o), and the render path adds the same offset before the lookup. Keep these
// bases identical to the `offset:` values in the fonts.yaml `latinbig` entries.
//
// The spacing (0x3000) exceeds the highest source codepoint the latin category
// covers (0x2116, the numero sign), so the tiers can never overlap.
static const uint32_t glyph_size_base[LEGEND_PLAN_SIZE_COUNT] = {
    [LEGEND_PLAN_SIZE_S] = 0u,          // the resident face, at its natural codepoints
    [LEGEND_PLAN_SIZE_M] = 0xF0000u,
    [LEGEND_PLAN_SIZE_L] = 0xF3000u,
};

// ⚠️ All-or-nothing on purpose. A partial hit would mix two faces in one legend,
// which is the "keep every glyph of a multi-glyph legend in ONE font" rule broken
// the worst possible way: kdisp_write_gfx_char baseline-aligns per font, so the
// halves would also sit at different heights. Falling back whole means a keyboard
// with no `latinbig` bundle, or a CJK/Arabic/Indic legend that this latin-only
// feature does not cover, simply keeps drawing what it always drew.
bool legend_plan_remap(const legend_plan_env_t* env, uint8_t size, const uint32_t* text,
                       uint32_t* out, uint8_t out_cap) {
    if (size == LEGEND_PLAN_SIZE_S || size >= LEGEND_PLAN_SIZE_COUNT || text == NULL) return false;
    const uint32_t base = glyph_size_base[size];
    uint8_t n = 0;
    for (uint8_t i = 0; text[i] != 0; ++i) {
        // A control code is a display-list op, not a glyph. The five zero-argument
        // cursor nudges are DROPPED; every other op bails, because HINT_MOVE /
        // HINT_FRAME (0x0E/0x12) consume the two codepoints after them — which we
        // would then relocate as if they were glyphs — and HINT_HALF/HINT_THIN
        // rescale the next glyph. Neither occurs in a language legend: measured
        // across all 160 layouts every op present is one of these five, and every
        // one of them LEADS the legend (0x0C x150, 0x0B x8, 0x06 x9, 0x08 x1,
        // 0x05 x1; not one after a glyph).
        //
        // ⚠️ DROPPED, not carried — and the REASON has changed, so do not restore the
        // op on the old rationale. It used to be that kdisp_gfx_text_bbox and the draw
        // disagreed about `\f`: the draw clamps its cursor at buffer 0, while the
        // relative walk starts at 0 where that clamp swallowed the lift and the op
        // measured as a no-op. That is FIXED (font_lookup.c's `saturate` flag, false
        // for the relative walk), so the measured box now matches the draw for these
        // ops too. What remains is that the nudge was hand-tuned for the SMALL face's
        // fixed baseline — 2px lifts chosen against a 27px glyph at baseline 23 — and
        // the planner replaces exactly that baseline with a measured, clamped one.
        // Carrying it was tried and clipped 6-8 px off the accents of `é è à` at M/L.
        //
        // ⚠️ This is what makes the French number row scale at all: `é è ç à` are
        // spelled `\f\f <letter>`, so refusing every control code outright left half
        // of AZERTY's number row on the small face while the other half grew (found
        // by rendering the row, 2026-08-21).
        if (text[i] < 0x20) {
            switch (text[i]) {
                case 0x05: case 0x06: case 0x08:
                case 0x0B: case 0x0C:
                    continue;                               // a small-face nudge
                default:
                    return false;
            }
        }
        // The glyph-count contract (GLYPH_SIZE_MAX_LEN) is enforced independently
        // of the caller's buffer: the firmware always passes a MAX_LEN+1 buffer,
        // where the two limits coincide, but the pure function must not let a
        // larger buffer relocate a longer legend than the header promises.
        if (n >= GLYPH_SIZE_MAX_LEN || n + 1 >= out_cap) return false;   // too long to relocate
        const uint32_t cp = base + text[i];
        if (!env->has_glyph(cp, env->ctx)) return false;
        out[n++] = cp;
    }
    if (n == 0) return false;                               // ops alone draw nothing
    out[n] = 0;
    return true;
}

// ⚠️ Order is load-bearing; see the header. E before W means W wins an over-wide
// glyph, N before S means S wins an over-tall one.
void legend_plan_clamp(const legend_plan_env_t* env, int8_t* x, int8_t* y,
                       int8_t xmin, int8_t xmax, int8_t ymin, int8_t ymax) {
    if (*x + xmax > env->win_x1) *x = (int8_t)(env->win_x1 - xmax);
    if (*x + xmin < env->win_x0) *x = (int8_t)(env->win_x0 - xmin);
    if (*y + ymin < 0)           *y = (int8_t)(-ymin);
    if (*y + ymax > env->win_y1) *y = (int8_t)(env->win_y1 - ymax);
}

// Nominal baseline for each size, chosen so the cap height sits where the small
// face's does (top of a capital ~1 px below the top of the panel): cap 20 -> 21,
// 24 -> 25, 27 -> 28. The caller then draws at the CLAMPED baseline this planner
// returns, so a tall accent stack or a deep descender shifts to fit instead of
// clipping — the nominal value is what keeps every ordinary letter on a shared
// baseline.
static const int8_t glyph_size_baseline[LEGEND_PLAN_SIZE_COUNT] = {
    [LEGEND_PLAN_SIZE_S] = 21, [LEGEND_PLAN_SIZE_M] = 25, [LEGEND_PLAN_SIZE_L] = 28,
};

void legend_plan_main(const legend_plan_env_t* env, uint8_t size, const uint32_t* text,
                      int8_t small_x, int8_t small_y,
                      uint32_t* scratch, uint8_t scratch_cap, main_legend_t* out) {
    int8_t xmin = 0, xmax = 0;
    int8_t ymin = 0, ymax = 0;
    if (legend_plan_remap(env, size, text, scratch, scratch_cap)) {
        env->bbox(scratch, &xmin, &xmax, &ymin, &ymax, env->ctx);
        // Keep the language's own horizontal origin — centring instead would eat the
        // space the shift preview lives in — but clamp both edges into the window,
        // since a big glyph runs up to 43 px wide.
        // The tier's nominal baseline, clamped so the ink stays on the panel. The
        // nominal is what keeps ordinary letters on a shared baseline; the clamp is
        // what stops a tall accent stack or a deep descender from clipping.
        int8_t x = small_x;
        int8_t y = glyph_size_baseline[size];
        legend_plan_clamp(env, &x, &y, xmin, xmax, ymin, ymax);
        out->text = scratch;
        out->x = x; out->y = y;
        out->ink_min = (int8_t)(x + xmin);
        out->ink_max = (int8_t)(x + xmax);
        out->box_xmin = xmin; out->box_xmax = xmax;
        out->box_ymin = ymin; out->box_ymax = ymax;
        out->big = true;
        return;
    }
    env->bbox(text, &xmin, &xmax, &ymin, &ymax, env->ctx);
    // The small face keeps the language's own origin -- and is clamped onto the
    // panel exactly as the bigger tiers are. It was NOT, for a long time, and the
    // asymmetry cost real pixels: measured across all 160 layouts, 305 of the 420
    // clipped elements were small base legends sliding off the north or west edge
    // by up to 8 px, silently, because a per-language offset tuned for one script's
    // glyph heights is applied to every key of the layout. Clamping is a no-op for
    // anything already inside the window, so this changes only the keys that were
    // losing ink.
    legend_plan_clamp(env, &small_x, &small_y, xmin, xmax, ymin, ymax);
    out->text = text;
    out->x = small_x; out->y = small_y;
    out->ink_min = (int8_t)(small_x + xmin);
    out->ink_max = (int8_t)(small_x + xmax);
    out->box_xmin = xmin; out->box_xmax = xmax;
    out->box_ymin = ymin; out->box_ymax = ymax;
    out->big = false;
}
