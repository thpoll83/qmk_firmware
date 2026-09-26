// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// First-run tutorial renderer — see anim/tutorial.h and anim/TUTORIAL.md.
#include "tutorial.h"
#include "focus_ring.h"
#include "keycode_helper.h"   // LAYER_ONESHOT / LAYER_SWITCH — the notation screen names the real marks
#include "lang/named_glyphs.h" // ICON_SHIFT — the status line names the key by its own glyph

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
#include "state.h"           // get_local_state()->contrast, for the pulse's full level

// The letter is drawn in the NORMAL keycap face, one tier larger — the same relocated
// `latinbig` glyphs the legend-size feature uses (M = 0xF0000, see glyph_size_base[] in
// base/legend_plan.c; the two must agree). ⚠️ It was previously the 19px UI face at 2x
// via kdisp_draw_glyph_double_at, which came out both too large (38 of the 40 px panel)
// and wrong — that face is the status-OLED face, not the one the keycaps wear.
#define TUT_LETTER_TIER_BASE 0xF0000u
#define TUT_LETTER_TIER_L    0xF3000u   // the taller cut; only the status panel has room

#define TUT_NUM_KEYS 40                 // display slots per half (8 x 5, some phantom)
#define TUT_STRIDE   128                // scratch bytes per page row
// TUT_RING_W now lives in base/tutorial_plan.h with the rest of the ripple's feel.
#define TUT_KEY_REACH 41                // half-diagonal of a 72x40 keycap, board units

// Slicing. Same lever as the idle screensaver: the slice budget is the responsiveness
// number, the frame gap is only a backstop that hands the loop back once per frame.
#define TUT_SLICE_MS 3
#define TUT_FRAME_MS 16

// ---- state ----------------------------------------------------------------
static bool        s_active;
static tut_state_t s_st;
static uint8_t     s_slots[TUT_LETTERS];

static bool        s_sync_dirty;                    // master has news for the slave
static uint8_t     s_seen_seq;                      // last ripple seq this half armed on
// ⚠️ The push is RE-ARMED on a timer, not sent once per change. A single send that is
// lost — or that lands while the slave is still finishing its own Eden and not yet
// listening — would otherwise leave that half out of the tutorial for good: dark
// keycaps and its ordinary status screen, with the master still asking for a key the
// user cannot see. Same reasoning as "the diff IS the retry queue" for the periodic
// state syncs; here there is no diff to re-fire, so the timer is it.
#define TUT_SYNC_REARM_MS 400u
static uint32_t    s_sync_at;                       // last push
// ⚠️ Deferred start. tutorial_sync_apply() runs inside user_sync_poly_data_handler(),
// a split-transaction callback with a ~20 ms window, and tutorial_start() does SPI to
// every panel on the half plus clear_keyboard(). Doing that in the handler is the
// documented way to make the master time out and the slave look dead — the same
// mistake the font-pack COMMIT made, fixed there by ACKing first and deferring the
// heavy work to housekeeping (fw_staging_finalize_defer_reload). Same fix here: the
// handler only records the intent, tutorial_tick() acts on it.
static bool        s_start_pending;
// The preview item's row in poly_keymap.c's table, as the SLAVE was told it. The slave
// never builds the table's renderable subset (only the master does, at start), so it is
// sent the absolute row: both halves hold the same const table.
static uint8_t     s_preview_tbl = 0xFFu;

// ---- small helpers --------------------------------------------------------

// The dither threshold field lives in base/tutorial_plan.c (tut_dither) so it is pure
// and testable — see the note there for why it is a board-space hash and not the 4x4
// ordered matrix this used to index by local keycap pixel.

// The phase vocabulary lives in tutorial_plan.h (tut_phase_is_exclusive / _intro /
// _wave), so a new phase is classified in exactly one place.

// ---- drawing --------------------------------------------------------------

// Draw `cp` centred in a w x h window starting at buffer column `ox`, at the largest
// of `tiers` that is actually flashed. A tier is the relocation base the bigger latin
// faces are emitted at (see the keycap legend-size feature); 0 means "the glyph's own
// codepoint", i.e. the resident 27 px keycap face, which is always present.
//
// ⚠️ TIERS, never a 2x upscale. kdisp_draw_glyph_double_at() repeats every pixel of
// the 19 px UI face, so the stair-steps double with it — "looks too much pixelated"
// (hardware, fourth round). The latinbig L and M cuts are real 39/33 px faces: the
// same height on the glass and smooth, because the rasteriser drew them at that size.
static bool tut_draw_letter_tiered(uint32_t cp, const uint32_t *tiers, uint8_t n_tiers,
                                   int8_t ox, uint8_t w, uint8_t h) {
    uint32_t        use = 0;
    const GFXfont  *of  = NULL;
    const GFXglyph *g   = NULL;
    for (uint8_t i = 0; i < n_tiers && g == NULL; ++i) {
        use = tiers[i] + cp;
        g   = kdisp_gfx_glyph_font(g_all_fonts, g_all_font_count, use, &of);
    }
    if (g == NULL) return false;
    // Drawn through a SINGLE-font array so kdisp_write_gfx_char's baseline align
    // (font->yAdvance - fonts[0]->yAdvance) is a no-op — the same reason the language
    // flags draw through { &flag_font }. Centred from the measured box, so it lands
    // centred whichever face answered.
    const GFXfont *one[1] = {of};
    const uint32_t txt[2] = {use, 0};
    int8_t         x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    kdisp_gfx_text_bbox(one, 1, txt, &x0, &x1, &y0, &y1);
    kdisp_write_gfx_text(one, 1,
                         (int8_t)(ox + (w - (x1 - x0 + 1)) / 2 - x0),
                         (int8_t)((h - (y1 - y0 + 1)) / 2 - y0), txt);
    return true;
}

// The keycap: M first. ⚠️ NOT L — the keycap is only 40 px tall and the L cut's ink
// reaches it, so the tier that is right for the roomier status panel would clip here.
// ⚠️ The BOARD's own legend, drawn by the board's own renderer — not a tutorial glyph.
// Same instinct as intro mode, one step earlier: the key you are being asked to press
// should look exactly like the key you will press for the rest of the keyboard's life,
// in the built-in face, at the built-in size. A tutorial-specific tier glyph taught the
// letter in a face the board never uses again.
//
// Chapter 1 stays EXCLUSIVE rather than going fully intro-mode, and the reason is the
// RIPPLE: it draws across every keycap at once, which the normal per-key renderer has
// no way to express. Running LETTER_IN/WAIT in intro mode and RIPPLE exclusively would
// hand the panels back and forth twice per letter — six ~107 ms full repaints inside a
// chapter whose whole point is calm. So the mode stays, and only the DRAW is borrowed.
// ---- the pulse on the key to press ------------------------------------------
// ONE panel's contrast, re-written every TUT_PULSE_TICK_MS. update_displays() writes
// every key's contrast back to the normal level on each repaint, so the pulse re-asserts
// itself rather than setting a value once. Both halves run it for the keys on their own
// half; each knows the phase and the pulsed slot from the ordinary sync.
#define TUT_PULSE_TICK_MS 30u
static uint8_t  s_pulse_idx = 0xFFu;    // this half's display index being pulsed
static uint32_t s_pulse_at;

// The pulse peaks at the tutorial's one uniform level (see set_displays()), or 0 while
// the panels are off (suspend), so a sleeping board never lights a key.
static uint8_t tut_normal_contrast(void) {
    return get_local_state()->contrast == DISP_OFF ? 0 : (uint8_t)POLY_INTRO_CONTRAST;
}

static void tut_panel_contrast(uint8_t idx, uint8_t level) {
    sr_shift_out_buffer_latch(get_key_disp_bitmask(idx), get_disp_bitmask_size());
    kdisp_set_contrast(level);
}

static void tutorial_pulse_stop(void) {
    if (s_pulse_idx == 0xFFu) return;
    tut_panel_contrast(s_pulse_idx, tut_normal_contrast());
    s_pulse_idx = 0xFFu;
}

static void tutorial_pulse_tick(uint32_t now) {
    const uint8_t slot = tut_pulse_slot(&s_st);
    const bool    mine = slot != TUT_SLOT_NONE && TUT_SLOT_RIGHT(slot) == !is_left_side();
    const uint8_t idx  = mine ? TUT_SLOT_IDX(slot) : 0xFFu;
    if (idx != s_pulse_idx) {
        tutorial_pulse_stop();          // hand the previous key its normal level back
        s_pulse_idx = idx;
        s_pulse_at  = now - TUT_PULSE_TICK_MS;
    }
    if (s_pulse_idx == 0xFFu || (uint32_t)(now - s_pulse_at) < TUT_PULSE_TICK_MS) return;
    s_pulse_at = now;
    tut_panel_contrast(s_pulse_idx, tut_pulse_level(now, tut_normal_contrast()));
}

// The outermost key of display row `dr` on one half: the lowest board x on the left, the
// highest on the right. TUT_SLOT_NONE when the row has no panel (the wipe then falls back
// to the dark cut for that item).
static uint8_t tut_corner_slot(bool right, uint8_t dr) {
    uint8_t best = TUT_SLOT_NONE;
    int16_t bx   = 0;
    for (uint8_t c = 0; c < 8u; ++c) {
        const uint8_t   idx = (uint8_t)(dr * 8u + c);
        const sa_geom_t g   = startup_anim_key_geom(right, idx);
        if (!g.valid) continue;
        if (best == TUT_SLOT_NONE || (right ? g.cx > bx : g.cx < bx)) {
            best = TUT_SLOT(right ? 1 : 0, idx);
            bx   = g.cx;
        }
    }
    return best;
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
            uprintf("Tutorial: only %u letter keys - skipping\n", (unsigned)n);
            return;   // nothing sensible to teach; the caller stamps the marker
        }
        // Which keys were picked, which half each is on, and the glyph each resolves
        // to. "no key was lit" is otherwise indistinguishable from "the letter was on
        // the other half", "the slot did not map back", and "the glyph is missing".
        for (uint8_t i = 0; i < TUT_LETTERS; ++i) {
            uprintf("Tutorial slot%u: %s idx=%u letter=%c(%u) cands=%u\n", i,
                    TUT_SLOT_RIGHT(s_slots[i]) ? "RIGHT" : "LEFT",
                    (unsigned)TUT_SLOT_IDX(s_slots[i]),
                    (char)tutorial_slot_letter(s_slots[i]),
                    (unsigned)tutorial_slot_letter(s_slots[i]), (unsigned)n);
        }
    }

    // ⚠️ Park the user's layout and teach on _L0. Both halves run this, and both need
    // to: each resolves its own keycaps through its own def_layer, so forcing it on one
    // side only letters one half from Qwerty and the other from whatever the user runs —
    // which is how "the letter I had to press differed from what the status display
    // said" happened (the status letter comes from keymaps[_BL], the keycap from the
    // live default layout).
    // ⚠️ BOTH halves resolve these, not just the master: each answers from its own
    // keymap, and tut_hold()'s "is this the shift I am pointing at" test has to agree
    // on the half the key is actually on.
    uint8_t shifts[TUT_SHIFT_STAGES];
    tutorial_shift_slots(shifts);
    tutorial_enter_base_layout();
    tut_init(&s_st, s_slots, shifts, timer_read32());
    // Eden's tail already said the welcome over falling stars: open on the first letter.
    // A tail armed on a show that has since ended (the slave can be armed late, after its
    // own Eden) is dropped here, so it cannot lengthen an unrelated replay later.
    const bool welcome_said = startup_anim_take_welcome_said();
    if (!startup_anim_active()) startup_anim_set_tail(false);
    if (welcome_said) tut_begin_at_letters(&s_st, timer_read32());
    // Chapter 3's inputs. Only the master's count matters — it owns the phase machine —
    // and it is computed from the fonts actually flashed, so a board with no font pack
    // skips the languages instead of showing a board of blank keycaps.
    // ⚠️ The Lang key's slot on BOTH halves: the pulse runs on whichever half owns the
    // key, and that is not necessarily the master. Only the master's preview count
    // matters — it owns the phase machine.
    if (is_usb_host_side()) tut_set_chapter3(&s_st, tutorial_preview_prepare());
    // The wipe's four corners, in the order they take turns. Only the master's table is
    // used (the slot rides the sync), but both halves can compute it.
    {
        const uint8_t corners[TUT_WIPE_CORNERS] = {
            tut_corner_slot(false, 0u), tut_corner_slot(true, 0u),
            tut_corner_slot(false, 4u), tut_corner_slot(true, 4u)};
        tut_set_wipe_origins(&s_st, corners);
    }
    // The key tour on BOTH halves, from each half's copy of the one keymap: the pulse runs
    // on whichever half owns the key being asked for, and the status prose names it. Only
    // the step index crosses the link (tut[2]).
    {
        tut_tour_step_t tour[TUT_TOUR_MAX];
        const uint8_t   n = tutorial_tour_build(tour, seed);
        tut_set_tour(&s_st, tour, n);
        if (is_usb_host_side()) {
            uprintf("Tutorial chapter 3: %u preview item(s), tour %u key(s)\n",
                    (unsigned)s_st.n_preview, (unsigned)n);
        }
    }
    s_active     = true;
    s_sync_dirty = is_usb_host_side();
    s_seen_seq   = s_st.ripple_seq;

    // Every key event is swallowed from here on, so anything already held would stay
    // registered on the host and auto-repeat until something released it. Same rule
    // and the same call as doom_begin() and the FW-2 confirmation prompt.
    // MASTER ONLY: the slave sends no reports, and this touches the shared report
    // state from a half that has no business doing so.
    if (is_usb_host_side()) clear_keyboard();
    uprintf("Tutorial start (left=%d, master=%d)\n", (int)is_left_side(),
            (int)is_usb_host_side());
}

void tutorial_stop(void) {
    if (!s_active) return;
    tutorial_pulse_stop();
    s_active     = false;
    // Hand the board back on the user's own layout, with nothing held over from a
    // chapter. Both halves, for the same reason the park is on both.
    tutorial_restore_layout();

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
    // Release anything the swallow left held, before the host sees keys again —
    // doom_exit() does the same on its way out. Master only, as on entry.
    if (is_usb_host_side()) clear_keyboard();

    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);   // all panels on this half
    kdisp_set_buffer(0x00);
    kdisp_send_window();
    kdisp_set_contrast(POLY_INTRO_CONTRAST);     // the finish edge's set_displays() then restores the user's level
    // ⚠️ This invalidate is NOT belt-and-braces — it is THE fix, and the reason is
    // worth knowing. update_displays() is reached only through the refresh drain, and
    // housekeeping does not call sync_and_refresh_displays() while the tutorial owns
    // the panels — so update_displays() never runs during the tutorial and its
    // `if (tutorial_active()) s_disp_render_active = false;` early-return is DEAD
    // CODE. s_disp_render_active therefore stays true, the generic invalidate at the
    // top of the next render is skipped, and that render streams a stale
    // sub-rectangle per panel: half-erased and black keycaps (hardware, round 1).
    // doom_slave_stop() documents the identical failure for its own reason
    // ("DOOM-exit leftovers") and forces the same call — doom_blit_invalidate_windows()
    // is literally kdisp_invalidate_all_windows().
    kdisp_invalidate_all_windows();
    // ⚠️ And stop any ripple still in flight, or its next slice repaints a key with an
    // arc over legends the board has just handed back.
    poly_focus_cancel();
}

bool tutorial_active(void)    { return s_active; }

// The Shift chapter's reveal brings the lit set in with the menu cascade
// (anim/menu_cascade.h) rather than all at once, and so does each preview item's name
// (Latin on one half, native on the other) and the two "more" screens' words. The phase
// and the item index are in the signature, so every screen cascades afresh. Both halves
// know both from the sync, so each runs the same cascade from its own clock.
uint32_t tutorial_cascade_signature(void) {
    if (!s_active) return 0u;
    if (s_st.phase == TUT_REVEAL) return 0x03000000u;
    if (s_st.phase == TUT_LANG_NAME || s_st.phase == TUT_LANG_MORE || s_st.phase == TUT_LANG_MORE2) {
        return 0x04000000u | ((uint32_t)s_st.phase << 8) | s_st.preview;
    }
    return 0u;
}

// A language layout is on the keys and still: the sparkles (anim/lang_sparkle.h) run.
bool tutorial_sparkle_live(void) {
    return s_active && s_st.phase == TUT_LANG_SHOW;
}
bool tutorial_exclusive(void) { return s_active && tut_phase_is_exclusive(s_st.phase); }
bool tutorial_intro_mode(void){ return s_active && tut_phase_is_intro(s_st.phase); }

// Chapter 1 asks for a CAPITAL — the status panel confirms the key as one — but the
// board's own renderer letters a key in lower case. Caps is held in the LOCAL snapshot
// for those phases only; the host's real lock state is never touched, and the hold ends
// with the chapter because the phase moves on.
bool tutorial_caps_hold(void) {
    if (!s_active) return false;
    switch (s_st.phase) {
        case TUT_LETTER_IN:
        case TUT_LETTER_WAIT:
        case TUT_RIPPLE:
        case TUT_GAP:
            return true;
        default:
            return false;
    }
}

// Which keys the current chapter leaves visible. Chapter 2 shows the letters and both
// shifts; chapter 3 the letters and the layer keys. ⚠️ Answered from the LOCAL keymap on
// whichever half is asking, so the slave hides the same keys without being told —
// update_displays() runs on both halves and both know the phase from the ordinary sync.
// Has the board-reveal front passed this key's centre yet? Measured on THIS half's own
// clock and geometry, the same curve the focus ring draws the front on, so a key lights
// as the ring crosses it.
static bool tut_reveal_reached(uint8_t row, uint8_t col) {
    const uint8_t slot = tutorial_slot_at(row, col);
    if (slot == TUT_SLOT_NONE || s_st.ripple_slot == TUT_SLOT_NONE) return false;
    const sa_geom_t k = startup_anim_key_geom(TUT_SLOT_RIGHT(slot), TUT_SLOT_IDX(slot));
    const sa_geom_t o = startup_anim_key_geom(TUT_SLOT_RIGHT(s_st.ripple_slot),
                                              TUT_SLOT_IDX(s_st.ripple_slot));
    if (!k.valid || !o.valid) return false;
    const int32_t  dx = (int32_t)k.cx - o.cx, dy = (int32_t)k.cy - o.cy;
    const uint32_t r  = tut_sweep_radius(tut_phase_progress(&s_st, timer_read32()));
    return (uint32_t)(dx * dx + dy * dy) <= r * r;
}

bool tutorial_key_visible(uint8_t row, uint8_t col) {
    if (!s_active) return true;
    // The two top outer keys are the lesson's chrome — Esc says how to leave, its mirror
    // on the right says how far along you are — so neither is hidden once it is talking.
    if (s_st.phase != TUT_BLANK && tutorial_is_chrome_key(row, col)) return true;
    if (tutorial_hides_recent(row, col)) return false;   // the menus' recents row
    if (tut_phase_shows_all(s_st.phase)) return true;
    switch (s_st.phase) {
        case TUT_DONE:
            return true;
        // ⚠️ Keys the Shift chapter already showed STAY lit; the front only adds keys.
        // A letter blinking off and back on as the wave passes would read as a fault.
        case TUT_BOARD_REVEAL:
            return tutorial_key_in_chapter_set(row, col, false) || tut_reveal_reached(row, col);
        // The wipe: a key shows the new item once the ring from its corner has passed it;
        // until then it stays dark (or keeps its letter of the name, a chrome key above).
        case TUT_LANG_WIPE:
            return tut_reveal_reached(row, col);
        // The name's letters (and the "more" screen's words) are chrome keys (above), so
        // everything else goes dark.
        case TUT_LANG_DARK:
        case TUT_LANG_NAME:
        case TUT_LANG_MORE:
        case TUT_LANG_MORE2:
            return false;
        // Chapter 1 opens on a dark, still board — the lit set is simply empty.
        case TUT_BLANK:
        case TUT_TEXT:
            return false;
        // …and then shows exactly ONE key. No special mode: the board draws that key
        // itself, and the focus ripple points at it.
        case TUT_LETTER_IN:
        case TUT_LETTER_WAIT:
        case TUT_RIPPLE:
        case TUT_GAP: {
            const uint8_t want = (s_st.phase == TUT_RIPPLE || s_st.phase == TUT_GAP)
                                     ? s_st.ripple_slot
                                     : tut_current_slot(&s_st);
            if (want == TUT_SLOT_NONE) return false;
            return tutorial_slot_matches(want, row, col);
        }
        default: {
            const bool layer_chapter =
                (s_st.phase == TUT_LAYER_WAIT || s_st.phase == TUT_LAYER_SWEEP ||
                 s_st.phase == TUT_LAYER_HELD || s_st.phase == TUT_NOTATION);
            return tutorial_key_in_chapter_set(row, col, layer_chapter);
        }
    }
}
bool tutorial_finished(void)  { return s_active && s_st.phase == TUT_DONE; }
bool tutorial_was_skipped(void) { return s_st.skipped; }

// ⚠️ ONE place arms the ring on this half, keyed off the sequence number — the same
// field the slave watches. Three things now raise a ripple (an accepted letter press, a
// shift edge, and chapter 2's pointing re-fire on a timer), and giving each its own
// poly_focus_start() call is how the third one would ship doing nothing on the master
// while working perfectly on the slave, which reads it off the wire.
// The reveal arms the BOARD-sized profile, back-dated by how far `phase_start` already
// is, so the ring and tut_reveal_reached() run on one clock. Everything else is the
// ordinary letter ring.
static void tutorial_start_ring(uint8_t slot) {
    if (s_st.phase == TUT_BOARD_REVEAL) {
        poly_focus_start_sweep(slot, timer_elapsed32(s_st.phase_start), TUT_BOARD_REVEAL_MS);
    } else if (s_st.phase == TUT_LANG_WIPE) {
        poly_focus_start_sweep(slot, timer_elapsed32(s_st.phase_start), TUT_LANG_WIPE_MS);
    } else {
        poly_focus_start(slot);
    }
}

static void tutorial_arm_ring_if_new(void) {
    if (s_st.ripple_seq == s_seen_seq) return;
    s_seen_seq = s_st.ripple_seq;
    tutorial_start_ring(s_st.ripple_slot);
}

bool tutorial_press(uint8_t slot) {
    if (!s_active) return false;
    if (!tut_press(&s_st, slot, timer_read32())) return false;
    // The ripple is the general focus service now, not a private renderer. The SLAVE
    // arms its own from the synced ripple_seq (see tutorial_sync_apply), so the wave
    // crosses the seam on the master's clock exactly as before.
    tutorial_arm_ring_if_new();
    s_sync_dirty = true;
    return true;
}

bool tutorial_hold(uint8_t kind, bool pressed, uint8_t slot) {
    if (!s_active || !is_usb_host_side()) return false;
    if (!tut_hold(&s_st, (tut_hold_kind_t)kind, pressed, slot, timer_read32())) return false;
    tutorial_arm_ring_if_new();
    s_sync_dirty = true;
    return true;
}

bool tutorial_tour_press(uint8_t slot) {
    if (!s_active || !is_usb_host_side()) return false;
    if (!tut_tour_press(&s_st, slot, timer_read32())) return false;
    s_sync_dirty = true;
    return true;
}

int16_t tutorial_tour_step(void) { return s_active ? tut_tour_index(&s_st) : -1; }
uint8_t tutorial_tour_slot(uint8_t step) {
    return (s_active && step < s_st.n_tour) ? s_st.tour[step] : TUT_SLOT_NONE;
}

uint8_t tutorial_tour_target(void) {
    const int16_t i = tutorial_tour_step();
    return i < 0 ? TUT_SLOT_NONE : s_st.tour[i];
}
bool    tutorial_tour_seen(void) { return s_active && s_st.phase == TUT_TOUR_SEEN; }

void tutorial_tour_rewind(uint8_t step) {
    if (!s_active || !is_usb_host_side()) return;
    const uint8_t before = s_st.tour_i;
    tut_tour_rewind(&s_st, step, timer_read32());
    if (s_st.tour_i != before) {
        uprintf("Tutorial: tour step %u needs its key held, back to step %u\n",
                (unsigned)before, (unsigned)s_st.tour_i);
        s_sync_dirty = true;
    }
}

void tutorial_skip(void) {
    if (!s_active) return;
    tut_skip(&s_st, timer_read32());
    s_sync_dirty = true;
}

void tutorial_tick(void) {
    // Drain a start the split handler asked for (it must not do this itself).
    if (s_start_pending) {
        s_start_pending = false;
        tutorial_start(0);
    }
    if (!s_active) return;
    const uint32_t now = timer_read32();

    // The master owns the step machine; the slave's phase arrives over the link.
    if (is_usb_host_side()) {
        if (tut_tick(&s_st, now)) s_sync_dirty = true;
        // Chapter 2's pointing ring is raised by tut_tick() on a timer, so the master
        // has no press to hang a poly_focus_start() on — this is where it lands.
        tutorial_arm_ring_if_new();
        // Re-offer the state even when nothing changed, so a half that missed the
        // start (or came up late) joins in within a few hundred ms.
        if ((uint32_t)(now - s_sync_at) >= TUT_SYNC_REARM_MS) s_sync_dirty = true;
    }
    tutorial_pulse_tick(now);
    // ⚠️ NOTHING IS RENDERED HERE ANY MORE. The board draws itself through
    // update_displays(), the ripple is the focus service, and the status panels are
    // drawn by oled_task_user(). What is left is the phase machine and the push to the
    // other half — which is all this ever should have been.
}

int16_t tutorial_preview_index(void) {
    if (!s_active) return -1;
    return tut_preview_index(&s_st);
}

uint8_t tutorial_preview_entry(void) {
    if (!s_active) return 0xFFu;
    // The slave keeps no preview counter: it answers from the phase (the dark cut counts
    // as the screen it leads to) and the row it was sent.
    if (!is_usb_host_side()) {
        const uint8_t p = (s_st.phase == TUT_LANG_DARK) ? s_st.dark_next : s_st.phase;
        return (p == TUT_LANG_NAME || p == TUT_LANG_WIPE || p == TUT_LANG_SHOW) ? s_preview_tbl
                                                                                 : 0xFFu;
    }
    const int16_t pos = tut_preview_pos(&s_st);
    return pos < 0 ? 0xFFu : tutorial_preview_table_row((uint8_t)pos);
}

// For the key LEDs (anim/tutorial_rgb.c), both from the synced state so the two halves
// agree: the phase, which with the ring's centre picks each ring's colour, and whether
// the next item's name is on the keys.
uint8_t tutorial_rgb_phase(void) { return s_active ? (uint8_t)s_st.phase : 0xFFu; }
uint8_t tutorial_pulsed_slot(void) { return s_active ? tut_pulse_slot(&s_st) : TUT_SLOT_NONE; }
bool    tutorial_showing_name(void) { return s_active && s_st.phase == TUT_LANG_NAME; }

// The wipe keeps the name on the keys the ring has not reached yet.
bool tutorial_naming(void) {
    return s_active && (s_st.phase == TUT_LANG_NAME || s_st.phase == TUT_LANG_WIPE);
}
bool tutorial_wipe_covers(uint8_t row, uint8_t col) {
    return s_active && s_st.phase == TUT_LANG_WIPE && tut_reveal_reached(row, col);
}
bool tutorial_in_layer_chapter(void) {
    return s_active && (s_st.phase == TUT_LAYER_WAIT || s_st.phase == TUT_LAYER_SWEEP ||
                        s_st.phase == TUT_LAYER_HELD);
}
bool tutorial_telling_more(void) {
    return s_active && (s_st.phase == TUT_LANG_MORE || s_st.phase == TUT_LANG_MORE2);
}
bool tutorial_more_scripts(void) { return s_active && s_st.phase == TUT_LANG_MORE2; }

// A capital on a keycap, one tier larger than the legend face when that tier is flashed,
// centred in the whole 72x40 window. Used to spell a preview item's name.
bool tutorial_draw_key_letter(uint32_t cp) {
    // ⚠️ The larger tier is a RELOCATION of the latin codepoints (0xF0000 + cp), so only a
    // latin capital may be looked up there. Any other codepoint lands on whatever glyph
    // happens to sit at 0xF0000 + cp — rendering the name previews showed にほ as "k{"
    // and ไท as "[n" before this guard. Everything else draws at its own codepoint.
    static const uint32_t latin[]  = {TUT_LETTER_TIER_BASE, 0u};
    static const uint32_t native[] = {0u};
    // Digits too: the "more" screen's numbers, and the latinbig bundle carries 0-9.
    const bool is_latin = (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
    return tut_draw_letter_tiered(cp, is_latin ? latin : native, is_latin ? 2 : 1,
                                  BUFFER_X, SCREEN_WIDTH, SCREEN_HEIGHT);
}

static bool tut_chrome_live(void) {
    return s_active && s_st.phase != TUT_BLANK && s_st.phase != TUT_DONE;
}

// The Esc keycap: "Hold to / skip...". ⚠️ Neither stock two-line stack fits it — the top
// line has ascenders (H l d t) AND the bottom a descender (p), which MID_TWO_LINE's note
// says a 40 px panel cannot hold under its spacing. Measured with the host preview's own
// renderer (tools/oled_preview.py): MID_TWO_LINE's lift clips 4 px off the top,
// MID_TWO_WORD's push 8 px off the bottom; lift 4 x 2 px / push 2 x 2 px clips none and
// leaves a 4 px gap between the lines.
const uint32_t *tutorial_skip_label(void) {
    if (!tut_chrome_live()) return NULL;
    return HINT_MID U"\f\f\f\f" U"Hold to" U"\r\v\x05\x05" U"skip...";
}

// The right key mirroring Esc: how far along the lesson is, "3/10". Built into a buffer
// because the numerator moves; TUT_PROGRESS_STEPS even steps, not chapters.
const uint32_t *tutorial_progress_label(void) {
    static uint32_t buf[7];
    if (!tut_chrome_live()) return NULL;
    uint8_t       n = 0;
    const uint8_t p = tut_progress(&s_st);
    // Round 39: the 19 px UI face, the size of Esc's "Hold to skip...", not the keycap
    // face ("the progress on the right top is too present - it should be smaller").
    buf[n++] = 0x16u;   // HINT_MID
    if (p >= 10u) buf[n++] = (uint32_t)('0' + p / 10u);
    buf[n++] = (uint32_t)('0' + p % 10u);
    buf[n++] = '/';
    if (TUT_PROGRESS_STEPS >= 10u) buf[n++] = (uint32_t)('0' + TUT_PROGRESS_STEPS / 10u);
    buf[n++] = (uint32_t)('0' + TUT_PROGRESS_STEPS % 10u);
    buf[n]   = 0;
    return buf;
}

// ---- split sync -----------------------------------------------------------

void tutorial_sync_fill(uint8_t out[TUTORIAL_SYNC_BYTES]) {
    // ⚠️ ARMED IS DROPPED, NOT PRESERVED — the two are mutually exclusive BY
    // CONSTRUCTION at the one point that publishes ACTIVE. This used to preserve it
    // "so the two writers of tut[0] cannot fight", which kept the level alive for the
    // whole session: the split handler re-armed the slave on every sync long after its
    // own hand-off, and a later Eden-only replay then started a lesson on the slave
    // with no master running one. The hand-off in poly_keymap.c clears the bit too —
    // that covers the branch which finds nothing to teach and so never publishes
    // ACTIVE at all — but relying on it alone would leave this expression free to
    // re-publish ARMED beside ACTIVE if any future path started a tutorial without
    // passing the hand-off. Clearing it here is the structural half of the guarantee.
    out[0] = (uint8_t)((s_active ? TUT_SYNC_ACTIVE : 0u) |
                      ((uint8_t)(s_st.step << TUT_SYNC_STEP_SHIFT) & TUT_SYNC_STEP_MASK));
    out[1] = s_st.phase;
    // tut[2] is the lit letter in chapter 1 and the preview item's table row while an item
    // is named or shown — the slave needs the row to spell the name and to title it.
    // …and the tour step during the key tour.
    if (tut_tour_index(&s_st) >= 0) {
        out[2] = (uint8_t)tut_tour_index(&s_st);
    } else {
        out[2] = (tut_preview_pos(&s_st) >= 0) ? tutorial_preview_entry() : tut_current_slot(&s_st);
    }
    out[3] = s_st.ripple_seq;
    out[4] = s_st.ripple_slot;
    // How far OUR ripple has already run. Sent every push, not just the first, so a
    // lost frame costs the slave a later start rather than a permanently offset wave.
    // ⚠️ BOTH wave phases. Chapter 2's sweep rides the same machinery, so leaving it
    // out here would reproduce exactly the seam-step the letter ripple was fixed for.
    // The dark cut sends where it is going instead: the slave's status line reads the
    // next screen's words through it, and never has a wave to run in that phase. The
    // tour sends the key it is asking for: the Intl chapter's letter and accent are drawn
    // at RANDOM on the master, so the slave's own build cannot know them, and its pulse
    // needs the real key.
    if (tut_phase_is_wave(s_st.phase)) {
        out[5] = tut_elapsed_encode(timer_elapsed32(s_st.phase_start));
    } else if (s_st.phase == TUT_LANG_DARK) {
        out[5] = s_st.dark_next;
    } else if (tut_tour_index(&s_st) >= 0) {
        out[5] = s_st.tour[s_st.tour_i];
    } else {
        out[5] = 0u;
    }
}

bool tutorial_sync_says_armed(const uint8_t in[TUTORIAL_SYNC_BYTES]) {
    return (in[0] & TUT_SYNC_ARMED) != 0u;
}

bool tutorial_sync_apply(const uint8_t in[TUTORIAL_SYNC_BYTES]) {
    if (is_usb_host_side()) return false;              // master is authoritative
    // ⚠️ Test the ACTIVE bit, never `in[0] != 0`. tut[0] also carries ARMED, which is
    // set for the whole of the Eden intro — a bare non-zero test would start the
    // tutorial on top of the animation.
    const bool want_active = (in[0] & TUT_SYNC_ACTIVE) != 0u;
    // ⚠️ Not `!want_active` — an ARMED-without-ACTIVE packet means the master has not
    // started yet, not stop. tut_sync_word_stops() is the one place that decides, and
    // carries the post-mortem (base/tutorial_plan.h).
    if (!want_active && !tut_sync_word_stops(in[0])) return false;
    if (!want_active) {
        // ⚠️ Do NOT tutorial_stop() here. That clears s_active, so tutorial_finished()
        // goes false and the slave's own housekeeping teardown — the restore trio that
        // hands the panels back and repaints — never runs, leaving this half showing
        // the tutorial forever. Route it through the SAME finish edge the master uses
        // by ending the phase instead, so there is one teardown path, not two.
        // ⚠️ A STOP WORD CANCELS A PENDING START, and this must happen BEFORE the
        // !s_active early-out — otherwise a start armed by an earlier ACTIVE word
        // survives the stop and tutorial_tick() drains it into a lesson on this half
        // alone. That is exactly the window the teardown ordering opens on the
        // master, and the two fixes are belt and braces for the same race.
        s_start_pending = false;
        if (!s_active) return false;
        s_st.phase       = TUT_DONE;
        s_st.phase_start = timer_read32();
        return true;
    }
    if (!s_active) {
        // NOT tutorial_start() — see s_start_pending. Record and get out of the
        // handler; the next housekeeping pass starts it.
        s_start_pending = true;
        return true;
    }
    bool changed = false;

    // Each half runs its OWN clock from receipt — the two MCUs share no time base, so
    // a phase is restarted here rather than being given the master's timestamp.
    if (s_st.phase != in[1]) {
        s_st.phase       = in[1];
        s_st.phase_start = timer_read32();
        changed          = true;
    }
    // ⚠️ The STEP, which used to stay 0 here for the whole of chapter 1. The status
    // panels are two halves of ONE sentence and the step is what picks the words, so a
    // stale step left the right panel saying "lit key" under "And now" and "One more".
    // The phase alone cannot carry it: all three letters use the same two phases.
    const uint8_t step = (uint8_t)((in[0] & TUT_SYNC_STEP_MASK) >> TUT_SYNC_STEP_SHIFT);
    if (step < TUT_LETTERS && s_st.step != step) {
        s_st.step = step;
        changed   = true;
    }
    // The slave is told which key is lit rather than deriving it: it holds no step
    // list, and two independent choices could disagree.
    // ⚠️ Only in chapter 1: tut[2] carries the preview row in chapter 3, and writing that
    // into a letter slot would be harmless today and wrong the day chapter 1 is re-entered.
    if (in[1] <= TUT_GAP && s_st.step < TUT_LETTERS && s_st.slots[s_st.step] != in[2]) {
        s_st.slots[s_st.step] = in[2];
        changed               = true;
    }
    if (in[1] == TUT_LANG_DARK && s_st.dark_next != in[5]) {
        s_st.dark_next = in[5];
        changed        = true;
    }
    const uint8_t named = (in[1] == TUT_LANG_DARK) ? in[5] : in[1];
    if ((named == TUT_LANG_NAME || named == TUT_LANG_WIPE || named == TUT_LANG_SHOW) &&
        s_preview_tbl != in[2]) {
        s_preview_tbl = in[2];
        changed       = true;
    }
    if ((in[1] == TUT_TOUR_WAIT || in[1] == TUT_TOUR_SEEN) && in[2] < TUT_TOUR_MAX) {
        if (s_st.tour_i != in[2]) {
            s_st.tour_i = in[2];
            changed     = true;
        }
        if (s_st.tour[in[2]] != in[5]) {
            s_st.tour[in[2]] = in[5];
            changed          = true;
        }
    }
    // The shift stage is not sent; the second wait implies it. The progress keycap sits on
    // the right half, which is usually the slave, and counts the two hands separately.
    if (in[1] == TUT_SHIFT_AGAIN) s_st.shift_stage = 1;
    if (in[3] != s_seen_seq) {
        s_seen_seq        = in[3];
        s_st.ripple_seq   = in[3];
        s_st.ripple_slot  = in[4];
        // ⚠️ The MASTER's phase byte, not a hardcoded TUT_RIPPLE. This used to force
        // the letter ripple, which was right while that was the only wave; chapter 2's
        // sweep bumps the same sequence number, and forcing TUT_RIPPLE here would drop
        // the slave out of the chapter and back onto a letter it is no longer showing.
        s_st.phase        = in[1];
        // ⚠️ Back-date by how far the master has already got, rather than starting at
        // zero. The two MCUs still share no time base — this is the master's own
        // ELAPSED, which needs none — and without it the slave's wave trails by the
        // whole press-to-sync latency, which is the step you see at the seam.
        s_st.phase_start  = timer_read32() - tut_elapsed_decode(in[5]);
        // Arm THIS half's focus ripple from the master's slot. ⚠️ The back-dated
        // phase_start above is not enough on its own any more: the ripple is a separate
        // service with its own clock, so it has to be told to start too — and it starts
        // from now, which is why the master keeps sending its own elapsed.
        // s_seen_seq was adopted above, so tutorial_arm_ring_if_new() stays a no-op
        // here: on the slave the ring is armed from the wire, not from a local press.
        tutorial_start_ring(in[4]);
        changed           = true;
    }
    return changed;
}

bool tutorial_sync_pending(void) { return s_sync_dirty; }
void tutorial_sync_sent(void)    { s_sync_dirty = false; s_sync_at = timer_read32(); }

// ---- status OLED prose ----------------------------------------------------
// Resident-font ASCII only: at first boot the font pack may never have been flashed,
// so a pack glyph here would render as nothing on the very first screen a user sees.
// ⚠️ The two panels are TWO HALVES OF ONE SENTENCE, never a copy of each other.
// Every line used to be chosen by line number alone, so both halves rendered the same
// words — "I saw 'Good' 'Good'" (hardware, third round). The keyboard is 30 cm of
// screen read left to right, so it reads across: the left panel opens the phrase and
// the right finishes it. Same shape as the firmware-apply screen's
// "⭯Applying" / "Firmware⭯".
//
// `which` is still the LINE within this half's panel; the HALF chooses the words. The
// second line is deliberately unused for now — one short line per panel is calmer than
// two, and leaves room for the letter the confirmation draws large.
const uint32_t *tutorial_line(uint8_t which) {
    // Before the lesson starts: Eden's welcome tail (startup_anim_welcome()) says the
    // opening words, the same ones TUT_TEXT says when there is no tail.
    if (!s_active && which == 0 && startup_anim_welcome()) {
        return is_left_side() ? U"Welcome" : U"to PolyKybd";
    }
    // ⚠️ `which` used to be rejected unless 0 — the second line was deliberately unused
    // while every screen was one short phrase. TUT_NOTATION needs it, so the gate is
    // now per-phase (every other case still returns NULL for line 1 by falling off its
    // own branch).
    if (!s_active || which > 1) return NULL;
    if (which == 1 && s_st.phase != TUT_NOTATION) return NULL;
    const bool left = is_left_side();
    // The dark cut is for the KEYS; the status panels already say what comes next, so
    // they do not flicker with it.
    const uint8_t phase = (s_st.phase == TUT_LANG_DARK) ? s_st.dark_next : s_st.phase;
    switch (phase) {
        case TUT_BLANK:
            return NULL;
        case TUT_TEXT:
            return left ? U"Welcome" : U"to PolyKybd";
        case TUT_LETTER_IN:
        case TUT_LETTER_WAIT:
            if (s_st.step == 0) return left ? U"Press the" : U"lit key";
            if (s_st.step == 1) return left ? U"And now" : U"the next";
            return left ? U"One more" : U"and done";
        case TUT_RIPPLE:
        case TUT_GAP:
            // The LEFT panel confirms WHICH key landed (drawn large by
            // tutorial_big_letter, so no line here); the right says how it went.
            return left ? NULL : U"Good";
        case TUT_REVEAL:
        case TUT_SHIFT_WAIT:
            // The ring says WHICH shift; the words only have to name it. ⚠️ The ⇧ is
            // NOT in this string — see tutorial_line_icon().
            return left ? U"Press and hold" : U"SHIFT";
        case TUT_SHIFT_SWEEP:
        case TUT_SHIFT_HELD:
            // The second hand: the same thing again, so ask rather than tell. A question
            // ends in "?" on the panel that ends the sentence.
            if (s_st.shift_stage == 1) return left ? U"Isn't that" : U"...nice?";
            return left ? U"All keys" : U"react...";
        case TUT_SHIFT_AGAIN:
            // The second hand. "Try again" rather than "now the right one": the ring
            // has already moved to the other shift, and naming a side in words would be
            // the third place that has to agree with the keymap.
            return left ? U"Try again" : U"SHIFT";
        case TUT_LAYER_WAIT:
            return left ? U"Now hold" : U"Fn";
        case TUT_LAYER_SWEEP:
        case TUT_LAYER_HELD:
            // The LEFT panel draws the layer symbol large instead of a line — the
            // symbol changing IS the lesson, so it is the thing on the panel.
            return left ? NULL : U"a whole layer";
        case TUT_BOARD_REVEAL:
            return left ? U"Every key" : U"is a screen";
        case TUT_BOARD_SHOW:
            return left ? U"72 screens," : U"one keyboard";
        case TUT_LANG_INTRO:
            return left ? U"It speaks" : U"your language";
        case TUT_LANG_NAME:
        case TUT_LANG_WIPE:
        case TUT_LANG_SHOW:
            // A question rather than a label ("Now in" read as static, hardware): the
            // phrase rotates with the item, and the name finishes the sentence.
            return left ? tutorial_preview_phrase() : tutorial_preview_name();
        case TUT_LANG_MORE:
            return left ? U"...and many" : U"more layouts";
        case TUT_LANG_MORE2:
            return left ? U"...plus" : U"fun scripts";
        case TUT_TOUR_WAIT:
        case TUT_TOUR_SEEN: {
            const int16_t step = tut_tour_index(&s_st);
            return step < 0 ? NULL : tutorial_tour_line((uint8_t)step, left, phase == TUT_TOUR_SEEN);
        }
        case TUT_FINALE:
            return left ? U"You're" : U"ready!";
        case TUT_NOTATION:
            // ⚠️ FOUR short lines, two per panel, reading left to right then down.
            // This is the only screen with a second line: three marks do not fit in
            // one phrase, and shortening them to fit would lose the words that make
            // each mark mean something.
            if (left)  return which == 0 ? U"Layer keys:" : U"no mark = hold";
            return which == 0 ? LAYER_ONESHOT U" = one press" : LAYER_SWITCH U" = stays on";
        default:
            return NULL;
    }
}

// The trailing icon for a line, drawn in the ICONS font by the caller. Only chapter 2
// has one: the word SHIFT alone is a label, and the ⇧ is what a user matches against
// the keycap they are being pointed at — the same glyph that key renders.
uint32_t tutorial_line_icon(uint8_t which) {
    if (!s_active || which != 0 || is_left_side()) return 0;
    switch (s_st.phase) {
        case TUT_REVEAL:
        case TUT_SHIFT_WAIT:
        case TUT_SHIFT_AGAIN:
            // ⚠️ Read out of the ICON_SHIFT macro rather than written as 0x90 here:
            // one literal, so a later icon-slot move cannot leave this pointing at
            // whatever else lands on that codepoint.
            return ICON_SHIFT[0];
        default:
            return 0;
    }
}

// The keycap legend for the line, framed by the caller. Only the tour's WAIT: once the
// key has been pressed the words move on ("The picker is open") and name no key.
const uint32_t *tutorial_line_key(uint8_t which) {
    if (!s_active || which != 0 || s_st.phase != TUT_TOUR_WAIT) return NULL;
    const int16_t step = tut_tour_index(&s_st);
    return step < 0 ? NULL : tutorial_tour_key((uint8_t)step, is_left_side(), false);
}

// The status panel has 64 rows rather than the keycap's 40, so it can take the L cut.
bool tutorial_draw_big_letter(int8_t ox, uint8_t w, uint8_t h) {
    static const uint32_t tiers[] = {TUT_LETTER_TIER_L, TUT_LETTER_TIER_BASE, 0u};
    const uint32_t cp = tutorial_big_letter();
    if (cp == 0) return false;
    return tut_draw_letter_tiered(cp, tiers, 3, ox, w, h);
}

uint32_t tutorial_big_letter(void) {
    if (!s_active || !is_left_side()) return 0;
    if (s_st.phase != TUT_RIPPLE && s_st.phase != TUT_GAP) return 0;
    // ripple_slot, not the step's slot: it is the key actually pressed, it survives
    // into TUT_GAP, and it is the one field the slave is TOLD rather than deriving —
    // so both halves agree about what was confirmed even mid-step.
    if (s_st.ripple_slot == TUT_SLOT_NONE) return 0;
    return tutorial_slot_letter(s_st.ripple_slot);
}

#else   // ---- split42: no geometry table, so no tutorial (as with Eden) ----

void tutorial_start(uint32_t seed) { (void)seed; }
void tutorial_stop(void) {}
bool tutorial_active(void) { return false; }
uint32_t tutorial_cascade_signature(void) { return 0u; }
bool tutorial_sparkle_live(void) { return false; }
bool tutorial_exclusive(void) { return false; }
bool tutorial_intro_mode(void) { return false; }
bool tutorial_caps_hold(void) { return false; }
bool tutorial_key_visible(uint8_t row, uint8_t col) { (void)row; (void)col; return true; }
void tutorial_tick(void) {}
bool tutorial_finished(void) { return false; }
bool tutorial_was_skipped(void) { return false; }
bool tutorial_press(uint8_t slot) { (void)slot; return false; }
bool tutorial_hold(uint8_t kind, bool pressed, uint8_t slot) {
    (void)kind; (void)pressed; (void)slot; return false;
}

void tutorial_skip(void) {}
bool tutorial_tour_press(uint8_t slot) { (void)slot; return false; }
int16_t tutorial_tour_step(void) { return -1; }
void tutorial_tour_rewind(uint8_t step) { (void)step; }
uint8_t tutorial_tour_target(void) { return TUT_SLOT_NONE; }
uint8_t tutorial_tour_slot(uint8_t step) { (void)step; return TUT_SLOT_NONE; }
bool tutorial_tour_seen(void) { return false; }
int16_t tutorial_preview_index(void) { return -1; }
uint8_t tutorial_preview_entry(void) { return 0xFFu; }
bool tutorial_naming(void) { return false; }
uint8_t tutorial_rgb_phase(void) { return 0xFFu; }
uint8_t tutorial_pulsed_slot(void) { return 0xFFu; }
bool    tutorial_showing_name(void) { return false; }
bool tutorial_wipe_covers(uint8_t row, uint8_t col) { (void)row; (void)col; return false; }
bool tutorial_in_layer_chapter(void) { return false; }
bool tutorial_telling_more(void) { return false; }
bool tutorial_more_scripts(void) { return false; }
bool tutorial_draw_key_letter(uint32_t cp) { (void)cp; return false; }
const uint32_t *tutorial_skip_label(void) { return NULL; }
const uint32_t *tutorial_progress_label(void) { return NULL; }
void tutorial_sync_fill(uint8_t out[TUTORIAL_SYNC_BYTES]) {
    for (uint8_t i = 0; i < TUTORIAL_SYNC_BYTES; ++i) out[i] = 0;
}
bool tutorial_sync_apply(const uint8_t in[TUTORIAL_SYNC_BYTES]) { (void)in; return false; }
bool tutorial_sync_says_armed(const uint8_t in[TUTORIAL_SYNC_BYTES]) { (void)in; return false; }
bool tutorial_sync_pending(void) { return false; }
void tutorial_sync_sent(void) {}
const uint32_t *tutorial_line(uint8_t which) { (void)which; return NULL; }
uint32_t tutorial_line_icon(uint8_t which) { (void)which; return 0; }
const uint32_t *tutorial_line_key(uint8_t which) { (void)which; return NULL; }
uint32_t tutorial_big_letter(void) { return 0; }
bool tutorial_draw_big_letter(int8_t ox, uint8_t w, uint8_t h) {
    (void)ox; (void)w; (void)h; return false;
}

#endif
