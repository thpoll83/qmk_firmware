// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// First-run tutorial renderer — see anim/tutorial.h and anim/TUTORIAL.md.
#include "tutorial.h"

#if defined(KEYBOARD_polykybd_split72)

#include <stdint.h>
#include <stdlib.h>
#include "quantum.h"
#include "base/disp_array.h"
#include "base/shift_reg.h"
#include "base/tutorial_plan.h"
#include "side.h"
#include "bridge_helper.h"          // is_usb_host_side()
#include QMK_KEYBOARD_H             // get_key_disp_bitmask, NUM_SHIFT_REGISTERS
#include "startup_anim.h"           // sa_geom_t / startup_anim_key_geom

#include "base/fontpack.h"   // g_all_fonts / g_all_font_count

// The letter is drawn in the NORMAL keycap face, one tier larger — the same relocated
// `latinbig` glyphs the legend-size feature uses (M = 0xF0000, see glyph_size_base[] in
// base/legend_plan.c; the two must agree). ⚠️ It was previously the 19px UI face at 2x
// via kdisp_draw_glyph_double_at, which came out both too large (38 of the 40 px panel)
// and wrong — that face is the status-OLED face, not the one the keycaps wear.
#define TUT_LETTER_TIER_BASE 0xF0000u

#define TUT_NUM_KEYS 40                 // display slots per half (8 x 5, some phantom)
#define TUT_STRIDE   128                // scratch bytes per page row
#define TUT_RING_W   5                  // ring thickness in board units (~= display px)
#define TUT_KEY_REACH 41                // half-diagonal of a 72x40 keycap, board units

// Slicing. Same lever as the idle screensaver: the slice budget is the responsiveness
// number, the frame gap is only a backstop that hands the loop back once per frame.
#define TUT_SLICE_MS 3
#define TUT_FRAME_MS 16

// ---- state ----------------------------------------------------------------
static bool        s_active;
static tut_state_t s_st;
static uint8_t     s_slots[TUT_LETTERS];
static uint8_t     s_lit[(TUT_NUM_KEYS + 7) / 8];   // 1 bit per slot: had ink last frame
static bool        s_sync_dirty;                    // master has news for the slave
static uint8_t     s_seen_seq;                      // slave: last ripple seq acted on

// Frame latch — every slice of one frame must draw the SAME instant, or the ripple
// shears across the board (the same reason the idle loop latches its comet set once).
static uint8_t  s_frame_idx;
static bool     s_frame_busy;
static uint32_t s_last_frame;
static uint16_t s_f_radius;
static uint8_t  s_f_dens;
static bool     s_f_ripple;
static int16_t  s_f_rcx, s_f_rcy;
static uint8_t  s_f_slot;
static uint32_t s_f_letter;
static uint8_t  s_f_contrast;

// ---- small helpers --------------------------------------------------------

// Octagonal distance (minimax) — no divide, no sqrt. This runs per pixel of every key
// the ring crosses, so it is the frame-rate lever; a few px of irregularity is
// invisible on a dithered ripple.
static inline uint16_t tut_dist(int16_t a, int16_t b) {
    a = (int16_t)abs(a);
    b = (int16_t)abs(b);
    const uint16_t mx = a > b ? (uint16_t)a : (uint16_t)b;
    const uint16_t mn = a > b ? (uint16_t)b : (uint16_t)a;
    return (uint16_t)(((uint32_t)mx * 123u + (uint32_t)mn * 51u) >> 7);
}

// Ordered 4x4 Bayer rather than white noise: the ripple is a smooth expanding band, and
// a blue-noise dither on it reads as sparkle. Ordered dithering keeps the edge clean.
static const uint8_t TUT_BAYER[16] = {0,  128, 32,  160, 192, 64,  224, 96,
                                      48, 176, 16,  144, 240, 112, 208, 80};
static inline uint8_t tut_bayer(int16_t x, int16_t y) {
    return TUT_BAYER[(((uint8_t)y & 3u) << 2) | ((uint8_t)x & 3u)];
}

static inline bool tut_lit_get(uint8_t idx) { return (s_lit[idx >> 3] >> (idx & 7)) & 1u; }
static inline void tut_lit_set(uint8_t idx, bool on) {
    if (on) s_lit[idx >> 3] |= (uint8_t)(1u << (idx & 7));
    else    s_lit[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
}

// ---- drawing --------------------------------------------------------------

static void tut_draw_ring(uint8_t *buf, const sa_geom_t *g) {
    for (int16_t ly = 0; ly < SCREEN_HEIGHT; ++ly) {
        const int16_t dy = (int16_t)(ly - 20);
        for (int16_t lx = 0; lx < SCREEN_WIDTH; ++lx) {
            const int16_t dx = (int16_t)(lx - 36);
            int16_t gx, gy;
            if (g->rot) {
                gx = (int16_t)(g->cx + ((dx * g->cosv - dy * g->sinv) >> 7));
                gy = (int16_t)(g->cy + ((dx * g->sinv + dy * g->cosv) >> 7));
            } else {
                gx = (int16_t)(g->cx + dx);
                gy = (int16_t)(g->cy + dy);
            }
            const uint16_t d = tut_dist((int16_t)(gx - s_f_rcx), (int16_t)(gy - s_f_rcy));
            if (d > s_f_radius) continue;                       // outside the wavefront
            if ((uint16_t)(s_f_radius - d) > TUT_RING_W) continue;  // inside the hole
            // At the smallest radius the "hole" is empty, so this reads as the solid
            // 5px disc it starts as, and becomes a ring only once it has grown past
            // its own thickness. Nothing special-cases the start.
            if (s_f_dens <= tut_bayer(lx, ly)) continue;
            buf[(size_t)(ly >> 3) * TUT_STRIDE + (BUFFER_X + lx)] |= (uint8_t)(1u << (ly & 7));
        }
    }
}

static void tut_draw_letter(uint32_t cp) {
    uint32_t        use = TUT_LETTER_TIER_BASE + cp;
    const GFXfont  *of  = NULL;
    const GFXglyph *g   = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count, use, &of);
    if (g == NULL) {
        // No `latinbig` bundle flashed (a keyboard that has never met the host app):
        // fall back to the resident face at its natural codepoint. Smaller, but the
        // right typeface, and never blank.
        use = cp;
        g   = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count, use, &of);
        if (g == NULL) return;
    }
    // Drawn through a SINGLE-font array so kdisp_write_gfx_char's baseline align
    // (font->yAdvance - fonts[0]->yAdvance) is a no-op — the same reason the language
    // flags draw through { &flag_font }. Centred from the measured box, so it is
    // centred whichever of the two faces answered.
    const GFXfont *one[1] = {of};
    const uint32_t txt[2] = {use, 0};
    int8_t         x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    kdisp_gfx_text_bbox(one, 1, txt, &x0, &x1, &y0, &y1);
    kdisp_write_gfx_text(one, 1,
                         (int8_t)(BUFFER_X + (SCREEN_WIDTH - (x1 - x0 + 1)) / 2 - x0),
                         (int8_t)((SCREEN_HEIGHT - (y1 - y0 + 1)) / 2 - y0), txt);
}

static void tut_render_key(uint8_t idx) {
    const bool      right = !is_left_side();
    const sa_geom_t g     = startup_anim_key_geom(right, idx);
    if (!g.valid) return;

    const uint8_t slot      = TUT_SLOT(right ? 1 : 0, idx);
    const bool    is_letter = (s_f_slot == slot) && (s_f_letter != 0);

    bool ring_here = false;
    if (s_f_ripple) {
        const uint16_t d  = tut_dist((int16_t)(g.cx - s_f_rcx), (int16_t)(g.cy - s_f_rcy));
        const int32_t  lo = (int32_t)s_f_radius - TUT_RING_W - TUT_KEY_REACH;
        const int32_t  hi = (int32_t)s_f_radius + TUT_KEY_REACH;
        ring_here = ((int32_t)d >= lo && (int32_t)d <= hi);
    }

    const bool wants_ink = is_letter || ring_here;
    // Most keys are dark for most of the tutorial. Skipping the select+clear+push for
    // a key that was blank and stays blank is what keeps a frame cheap enough to slice
    // — without it every frame would cost the full 36-keycap push the Eden intro pays.
    if (!wants_ink && !tut_lit_get(idx)) return;

    sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size());
    kdisp_set_buffer(0x00);
    if (wants_ink) {
        uint8_t *buf = get_scratch_buffer();
        if (ring_here) tut_draw_ring(buf, &g);
        if (is_letter)  tut_draw_letter(s_f_letter);
        // The fade lives in the panel's CONTRAST register, not in the pixels: a 1-bit
        // panel cannot dim ink, and dithering the letter would make it grainy rather
        // than dim. Blank keys keep full contrast — they show nothing either way.
        kdisp_set_contrast(is_letter ? s_f_contrast : 255);
    }
    kdisp_send_window();
    tut_lit_set(idx, wants_ink);
}

// ---- frame latch ----------------------------------------------------------

static void tut_latch_frame(uint32_t now) {
    const uint8_t p = tut_phase_progress(&s_st, now);
    s_f_slot   = tut_current_slot(&s_st);
    s_f_letter = (s_f_slot == TUT_SLOT_NONE) ? 0u : tutorial_slot_letter(s_f_slot);

    switch (s_st.phase) {
        case TUT_LETTER_IN:   s_f_contrast = tut_fade_contrast(p); break;
        case TUT_LETTER_WAIT: s_f_contrast = 255; break;
        // The letter settles out WITH the ripple rather than after it, so the press
        // reads as one event: the key gives up its light to the wave leaving it.
        case TUT_RIPPLE:      s_f_contrast = tut_fade_contrast((uint8_t)(255u - p)); break;
        default:              s_f_contrast = 255; break;
    }

    s_f_ripple = (s_st.phase == TUT_RIPPLE) && (s_st.ripple_slot != TUT_SLOT_NONE);
    if (s_f_ripple) {
        const sa_geom_t rg = startup_anim_key_geom(TUT_SLOT_RIGHT(s_st.ripple_slot),
                                                   TUT_SLOT_IDX(s_st.ripple_slot));
        if (rg.valid) {
            s_f_rcx    = rg.cx;
            s_f_rcy    = rg.cy;
            s_f_radius = tut_ripple_radius(p);
            s_f_dens   = tut_ripple_density(p);
        } else {
            s_f_ripple = false;
        }
    }
}

// ---- lifecycle ------------------------------------------------------------

void tutorial_start(uint32_t seed) {
    if (s_active) return;

    for (uint8_t i = 0; i < TUT_LETTERS; ++i) s_slots[i] = TUT_SLOT_NONE;
    if (is_usb_host_side()) {
        // Only the master chooses: it can see both halves' keymaps, and the slave is
        // told what is being asked for rather than deriving it (two independent draws
        // would disagree).
        uint8_t cand[TUT_NUM_KEYS * 2];
        const uint8_t n = tutorial_collect_candidates(cand, (uint8_t)sizeof(cand));
        if (tut_choose_slots(cand, n, seed, s_slots) < TUT_LETTERS) {
            uprintf("Tutorial: only %u letter keys — skipping\n", (unsigned)n);
            return;   // nothing sensible to teach; the caller stamps the marker
        }
    }

    tut_init(&s_st, s_slots, timer_read32());
    s_active     = true;
    s_sync_dirty = is_usb_host_side();
    s_seen_seq   = s_st.ripple_seq;
    s_frame_busy = false;
    s_frame_idx  = 0;
    s_last_frame = timer_read32() - TUT_FRAME_MS;
    for (uint8_t i = 0; i < sizeof(s_lit); ++i) s_lit[i] = 0;

    // Own the panels: all on, full contrast, and blank. Eden faded to black but left
    // the panels enabled, so this is a hand-off rather than a power-up.
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);
    kdisp_enable(true);
    kdisp_set_contrast(255);
    uprintf("Tutorial start (left=%d, master=%d)\n", (int)is_left_side(),
            (int)is_usb_host_side());
}

void tutorial_stop(void) {
    if (!s_active) return;
    s_active     = false;
    // Drop a half-rendered frame, or its remaining slices would paint a ripple over
    // legends update_displays() has already put back.
    s_frame_busy = false;
    s_frame_idx  = 0;

    // ⚠️ Hand the panels back in a KNOWN state. The tutorial writes them UNTRACKED
    // (kdisp_send_window with no kdisp_track_panel), so the per-panel dirty-window
    // bboxes in disp_array.c describe whatever was there before — and every key the
    // tutorial never lit was never written at all. Left to chance, the first awake
    // render can push a delta against a stale box: black keys and half-erased ones,
    // which is exactly what the first hardware round showed.
    //
    // Two belts: blank every panel on this half so nothing stale can survive, and
    // invalidate the boxes so the next render redraws each window in full rather
    // than a delta. Costs one 36-panel blank, once, at the end of the tutorial.
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);   // all panels on this half
    kdisp_set_buffer(0x00);
    kdisp_send_window();
    kdisp_set_contrast(255);
    kdisp_invalidate_all_windows();
    for (uint8_t i = 0; i < sizeof(s_lit); ++i) s_lit[i] = 0;
}

bool tutorial_active(void)    { return s_active; }
bool tutorial_finished(void)  { return s_active && s_st.phase == TUT_DONE; }
bool tutorial_was_skipped(void) { return s_st.skipped; }

bool tutorial_press(uint8_t slot) {
    if (!s_active) return false;
    if (!tut_press(&s_st, slot, timer_read32())) return false;
    s_sync_dirty = true;
    return true;
}

void tutorial_skip(void) {
    if (!s_active) return;
    tut_skip(&s_st, timer_read32());
    s_sync_dirty = true;
}

void tutorial_tick(void) {
    if (!s_active) return;
    const uint32_t now = timer_read32();

    // The master owns the step machine; the slave's phase arrives over the link.
    if (is_usb_host_side() && tut_tick(&s_st, now)) s_sync_dirty = true;
    if (s_st.phase == TUT_DONE) return;      // the caller tears down on the finish edge

    if (!s_frame_busy) {
        if (timer_elapsed32(s_last_frame) < TUT_FRAME_MS) return;
        tut_latch_frame(now);
        s_frame_idx  = 0;
        s_frame_busy = true;
    }
    const uint32_t slice = timer_read32();
    do {
        tut_render_key(s_frame_idx++);
    } while (s_frame_idx < TUT_NUM_KEYS && timer_elapsed32(slice) < TUT_SLICE_MS);

    if (s_frame_idx >= TUT_NUM_KEYS) {
        s_frame_busy = false;
        s_last_frame = timer_read32();       // gap timed from the END of the frame
    }
}

// ---- split sync -----------------------------------------------------------

void tutorial_sync_fill(uint8_t out[TUTORIAL_SYNC_BYTES]) {
    out[0] = s_active ? 1u : 0u;
    out[1] = s_st.phase;
    out[2] = tut_current_slot(&s_st);
    out[3] = s_st.ripple_seq;
    out[4] = s_st.ripple_slot;
}

bool tutorial_sync_apply(const uint8_t in[TUTORIAL_SYNC_BYTES]) {
    if (is_usb_host_side()) return false;              // master is authoritative
    const bool want_active = (in[0] != 0);
    if (!want_active) {
        // ⚠️ Do NOT tutorial_stop() here. That clears s_active, so tutorial_finished()
        // goes false and the slave's own housekeeping teardown — the restore trio that
        // hands the panels back and repaints — never runs, leaving this half showing
        // the tutorial forever. Route it through the SAME finish edge the master uses
        // by ending the phase instead, so there is one teardown path, not two.
        if (!s_active) return false;
        s_st.phase       = TUT_DONE;
        s_st.phase_start = timer_read32();
        return true;
    }
    if (!s_active) tutorial_start(0);                  // slave: no slots of its own
    bool changed = false;

    // Each half runs its OWN clock from receipt — the two MCUs share no time base, so
    // a phase is restarted here rather than being given the master's timestamp.
    if (s_st.phase != in[1]) {
        s_st.phase       = in[1];
        s_st.phase_start = timer_read32();
        changed          = true;
    }
    // The slave is told which key is lit rather than deriving it: it holds no step
    // list, and two independent choices could disagree.
    if (s_st.step < TUT_LETTERS && s_st.slots[s_st.step] != in[2]) {
        s_st.slots[s_st.step] = in[2];
        changed               = true;
    }
    if (in[3] != s_seen_seq) {
        s_seen_seq        = in[3];
        s_st.ripple_seq   = in[3];
        s_st.ripple_slot  = in[4];
        s_st.phase        = TUT_RIPPLE;
        s_st.phase_start  = timer_read32();   // its ripple clock starts now, not then
        changed           = true;
    }
    return changed;
}

bool tutorial_sync_pending(void) { return s_sync_dirty; }
void tutorial_sync_sent(void)    { s_sync_dirty = false; }

// ---- status OLED prose ----------------------------------------------------
// Resident-font ASCII only: at first boot the font pack may never have been flashed,
// so a pack glyph here would render as nothing on the very first screen a user sees.
const uint32_t *tutorial_line(uint8_t which) {
    if (!s_active) return NULL;
    switch (s_st.phase) {
        case TUT_BLANK:
            return NULL;
        case TUT_TEXT:
            return which == 0 ? U"Welcome" : U"to PolyKybd";
        case TUT_LETTER_IN:
        case TUT_LETTER_WAIT:
            if (s_st.step == 0) return which == 0 ? U"Press the" : U"lit key";
            if (s_st.step == 1) return which == 0 ? U"And now" : U"the next";
            return which == 0 ? U"One more" : NULL;
        case TUT_RIPPLE:
        case TUT_GAP:
            return which == 0 ? U"Good" : NULL;
        default:
            return NULL;
    }
}

#else   // ---- split42: no geometry table, so no tutorial (as with Eden) ----

void tutorial_start(uint32_t seed) { (void)seed; }
void tutorial_stop(void) {}
bool tutorial_active(void) { return false; }
void tutorial_tick(void) {}
bool tutorial_finished(void) { return false; }
bool tutorial_was_skipped(void) { return false; }
bool tutorial_press(uint8_t slot) { (void)slot; return false; }
void tutorial_skip(void) {}
void tutorial_sync_fill(uint8_t out[TUTORIAL_SYNC_BYTES]) {
    for (uint8_t i = 0; i < TUTORIAL_SYNC_BYTES; ++i) out[i] = 0;
}
bool tutorial_sync_apply(const uint8_t in[TUTORIAL_SYNC_BYTES]) { (void)in; return false; }
bool tutorial_sync_pending(void) { return false; }
void tutorial_sync_sent(void) {}
const uint32_t *tutorial_line(uint8_t which) { (void)which; return NULL; }

#endif
