// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure timeline / step logic for the first-run tutorial (see anim/TUTORIAL.md).
//
// Deliberately free of quantum.h, the display stack and EEPROM: the arithmetic here
// (phase transitions, the fade curve, the ripple envelope, letter selection) is the
// part with a bug history and the only part reachable from a unit test. The firmware
// binding lives in anim/tutorial.c. Same seam as base/legend_plan.c and
// base/macro_decode.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// ---- timeline (ms) --------------------------------------------------------
#define TUT_BLANK_MS      600u   // after Eden: everything dark and still
#define TUT_TEXT_MS      2600u   // status line up, keycaps still dark. ⚠️ This is the
                                 // FIRST thing a new keyboard says and it spans both
                                 // panels, so it is read, not glanced at — 900 ms was
                                 // gone before the second half had been taken in.
#define TUT_LETTER_IN_MS  500u   // a letter fades in (contrast 0 -> full)
// ---- the ripple's whole feel, in BOARD UNITS (1 unit ~= 1 display pixel; a keycap
//      is 72 x 40, so ~72 units is one key width) --------------------------------
//
// ⚠️ Three hardware rounds were spent tuning this by TIME and it kept coming back as
// "the ring does not dissolve". The mistake was describing the effect in the wrong
// units: what the eye judges is how much ink is left AT A GIVEN RADIUS, so that is
// what these constants say and what tut_ripple_density() is a function of.
//
// Round 1: 400 ms, ring crossed the board in a couple of frames — a flicker.
// Round 2: 2200 ms, still read as fast.
// Round 3: 3400 ms with the dissolve keyed to elapsed time. The radius eases OUT, so
//          the wavefront was past every key while still ~84 % solid and did its fading
//          off the board — the dissolve was real and nobody could see it.
// Round 4: keyed to travel, but spread over the full 1795-unit reach, so it was still
//          only a third gone by the outer keys. "The ring STILL does not dissolve."
// Round 5: re-expressed in RADIUS space as a local splash — a thick ring that de-pixels
//          to nothing within a couple of key widths. That shape was right first time
//          ("great improvment"), and every round since has only moved its SCALE:
//          305 -> 255 -> 235 units of reach, 1600 -> 2000 -> 2200 ms of life. Treat
//          the three constants below as a dial, not a redesign.
//
// ⚠️ THESE NUMBERS WERE ALL ~4 % SHORT OF WHAT SHIPPED until the octagon was removed.
// The old minimax distance UNDER-reported by 123/128, so a ring whose constant said 255
// actually reached ~265 on the glass. Every value here was therefore tuned by eye
// against a ring 4 % larger than the constant claimed. The set below is the previous
// SHIPPED (i.e. inflated) geometry scaled by 0.886 — the user's "cut the radius by 30"
// measured against the ~265 they saw, not against the 255 the file said. Any future
// tuning is now honest, because the drawn radius is the constant.
#define TUT_RIPPLE_R0      5u    // the struck key: a small solid dot
// Ring thickness TAPERS as the wave spreads — fat at the strike, settling to its
// nominal width by full reach. ⚠️ A 5-unit ring cannot SHOW a dither at all: half its
// pixels gone reads as a dotted line, not a fading band. Both ends stay well clear of
// that.
#define TUT_RING_W0       13u    // at the strike
#define TUT_RING_W        11u    // at full reach
#define TUT_RIPPLE_SOLID_R 74u   // fully solid out to here (~1 key width)
#define TUT_RIPPLE_MAX_R  205u   // gone by here: a 131-unit (~1.8 key) dissolve
#define TUT_RIPPLE_MS   2200u    // the ring's whole life, ~792 ms per key width

#define TUT_GAP_MS       1000u   // stillness before the next letter fades in (700 read as rushed)

// ---- chapter 2 ------------------------------------------------------------
#define TUT_REVEAL_MS    1400u   // the lit set emerging out of the dark
#define TUT_SHIFT_HELD_MS 2600u  // ⚠️ a DWELL, not a requirement to keep holding: the
                                 // chapter ends on the clock, so a user who taps Shift
                                 // and lets go still sees the board change and change
                                 // back rather than being told they did it wrong.

// ---- the chapter 2/3 REPAINT FRONT ----------------------------------------
// ⚠️ NOT the letter ripple's radius, and conflating the two was a real bug. The letter
// ripple is a deliberately SMALL decorative splash — TUT_RIPPLE_MAX_R is 205 units, about
// 12 % of the board's 1673-unit width. Driving the chapter-2 repaint front from it meant
// a Shift press only ever repainted keys within ~2.8 key widths of the Shift key, and
// every other keycap kept its stale legend: "the shift is really laggy … the left had
// upper case and the right lower case" (hardware). The front is functional, not
// decorative, so it must cross the WHOLE board.
//
// 1792 clears the corner-to-corner diagonal (hypot(1673, 563) ~= 1765) from any key.
// The duration is its own too: 2.2 s is a fine life for a splash and far too slow for a
// modifier, which has to feel immediate.
#define TUT_SWEEP_MAX_R 1792u
#define TUT_SWEEP_MS     700u

// ---- chapter 3 ------------------------------------------------------------
#define TUT_LAYER_HELD_MS 2500u  // a beat longer than Shift's: a whole layer is more to
                                 // read than a row of capitals.
#define TUT_NOTATION_MS   5000u  // three marks to take in, on the status panels only —
                                 // the keycaps are back on the base layer behind it, so
                                 // the contrast with what was just held is still fresh.
// ---- the POINTING ring ----------------------------------------------------
// ⚠️ Chapter 1's ring and chapter 2's ring do OPPOSITE jobs, and only one of them can
// be fired once. In chapter 1 the ring CONFIRMS — it is struck by the press, so one
// shot is the whole point. In chapter 2 it POINTS: it has to say "this key, here" to
// someone who has not found it yet, and a single 2.2 s splash fired on entering the
// phase is gone before a first-time user has looked up from the status line. So a
// pointing wait re-fires it on this period for as long as it waits.
#define TUT_POINT_PERIOD_MS 3000u

// ---- chapter 3: the board reveal, then languages and scripts -------------------
// The reveal is the moment the lesson stops hiding keys. A wavefront leaves the Shift
// the user just held and every key it passes lights with its own legend, until all 72
// are on. It rides the chapter-2 SWEEP geometry (TUT_SWEEP_MAX_R clears the board from
// any key) but is slow on purpose: this is the "wow", not a modifier that has to feel
// immediate.
#define TUT_BOARD_REVEAL_MS 2400u
#define TUT_BOARD_SHOW_MS   2800u   // "72 screens" — the whole board, still
#define TUT_LANG_INTRO_MS   2600u
// One language or script on screen. ⚠️ Long enough to READ a whole board of unfamiliar
// glyphs; the repaint itself takes ~110 ms per half, so most of this is looking. 2200 ms
// "passes too fast — humans need a moment to read the language and then see the keys"
// (hardware); doubled with the name below.
#define TUT_LANG_ITEM_MS    4400u
// Before each item the board goes dark and spells the item's NAME on the keys, Latin on
// one half and the language's own script on the other, so the reader knows what they
// are about to look at.
#define TUT_LANG_NAME_MS    3000u   // 2000 left too little time to read the name (hardware)
// After the last item: the board says how many more there are — two screens, the
// layouts first and then the scripts, each a number on one half and the word on the
// other, in the heavy splash face.
#define TUT_LANG_MORE_MS    2600u
#define TUT_LANG_POINT_MS   4500u   // the ring circles the Lang key; informational, timed
#define TUT_FINALE_MS       3000u

// The preview list is a board-side table (it knows which fonts are flashed). This file
// only counts through it, so the cap is the one thing it has to know.
#define TUT_PREVIEW_MAX 16u

// The progress keycap counts in TUT_PROGRESS_STEPS even steps, NOT in chapters: three
// chapters read as a counter that barely moves ("we need more steps — divide in 10").
#define TUT_PROGRESS_STEPS 10u

// ---- the PULSE on the key to press ----------------------------------------
// The key the lesson is waiting for breathes: its panel's contrast eases between a low
// floor and full over TUT_PULSE_PERIOD_MS, so it can be found at a glance.
#define TUT_PULSE_PERIOD_MS 1400u
#define TUT_PULSE_FLOOR     24u      // of 255: dim, never off (an OFF key reads as broken)

#define TUT_SKIP_HOLD_MS 1000u   // hold Esc this long to skip

#define TUT_LETTERS 3            // step 1 asks for three keys

// A chosen key: side (0 = left, 1 = right) packed above the display index 0..39.
#define TUT_SLOT(side, idx) ((uint8_t)(((side) ? 0x80u : 0u) | ((idx) & 0x3Fu)))
#define TUT_SLOT_RIGHT(s)   (((s) & 0x80u) != 0u)
#define TUT_SLOT_IDX(s)     ((uint8_t)((s) & 0x3Fu))
#define TUT_SLOT_NONE       0xFFu

typedef enum {
    TUT_BLANK = 0,      // dark, settling out of Eden
    TUT_TEXT,           // status line up, keycaps dark
    // ---- chapter 1: three letters ----
    TUT_LETTER_IN,      // current letter fading in
    TUT_LETTER_WAIT,    // lit, waiting for the press (no timeout)
    TUT_RIPPLE,         // ripple expanding, letter settling out
    TUT_GAP,            // stillness between letters
    // ---- chapter 2: shift, ONCE PER HAND ----
    TUT_REVEAL,         // the lit set paints, contrast ramps up
    TUT_SHIFT_WAIT,     // "Press and hold SHIFT" — no timeout; asks for the LEFT one
    TUT_SHIFT_SWEEP,    // the repaint wave crossing the board
    TUT_SHIFT_HELD,     // "All keys react..." — dwells, so a TAP still shows it
    TUT_SHIFT_AGAIN,    // "Try again" — the SECOND wait, asking for the RIGHT shift
    // ---- chapter 3: a layer, and the notation on the keycaps ----
    TUT_LAYER_WAIT,     // "Now hold Fn" — no timeout
    TUT_LAYER_SWEEP,    // the same repaint wave, a different payload
    TUT_LAYER_HELD,     // the layer's legends, status showing which layer
    TUT_NOTATION,       // status only: what the marks on a layer key mean
    // ---- chapter 3: the board reveal, languages and scripts ----
    TUT_BOARD_REVEAL,   // a wave from the last Shift lights every key it passes
    TUT_BOARD_SHOW,     // the whole board, still: "72 screens"
    TUT_LANG_INTRO,     // "It speaks your language"
    TUT_LANG_NAME,      // board dark, the next item's name spelled across the middle row
    TUT_LANG_SHOW,      // one preview item on screen; re-entered once per item
    TUT_LANG_MORE,      // "160 | LAYOUTS" on the keys: there are many more
    TUT_LANG_MORE2,     // "10 | SCRIPTS"
    TUT_LANG_POINT,     // the ring circles the Lang key: where to change it for real
    TUT_FINALE,         // "You're ready!"
    TUT_DONE,           // finished or skipped — the caller tears down
} tut_phase_t;

// ---- the two halves of the tutorial ---------------------------------------
//
// ⚠️ THE ARCHITECTURAL SPLIT, and getting it wrong cost four hardware rounds.
//
// EXCLUSIVE (chapter 1) — the tutorial owns the panels. It draws a blank board, one
// lit letter and a ripple: things the normal renderer has no concept of, so it has to.
//
// INTRO (chapter 2 onward) — the board runs NORMALLY. update_displays(),
// sync_and_refresh_displays(), the mods snapshot, the layer sync, both halves: all of
// it stock. The tutorial only *annotates* — it hides the keys outside its lit set and
// owns the status panels.
//
// Owning the render pipeline for chapter 2 meant re-implementing the board inside the
// tutorial, and every one of those re-implementations was a bug: the mods snapshot was
// never refreshed, the layer never reached the slave, the legend resolved the wrong
// layer, and the repaint front reached 12 % of the board. None of that code needs to
// exist. **A mode that wants the board to behave normally must let the board behave
// normally**; it annotates, it does not re-draw.
// ⚠️ NOTHING is exclusive any more. The ripple became a general FOCUS service that
// composites over the board's own legends (anim/focus_ring.h), which was the last thing
// the normal renderer could not express — so chapter 1 is an ordinary chapter too, with
// a lit set of exactly one key. The predicate is kept rather than deleted because it is
// the seam: if a future chapter ever genuinely needs the panels to itself, it says so
// here and the housekeeping branch is still wired for it.
// ---- the split sync word's flag byte (tut[0]) -----------------------------
// tut[0] is a BITFIELD, not a bool. ARMED is the half of it that makes the slave
// behave like Eden: it says "the first-run experience is running, start the tutorial
// when the intro ends" and is carried for the whole of Eden, so the slave gets a
// LOCAL trigger instead of depending on one 0->1 edge landing.
#define TUT_SYNC_ACTIVE 0x01u   // the tutorial itself is running
#define TUT_SYNC_ARMED  0x02u   // armed: start it when the intro finishes
// ⚠️ The STEP rides in tut[0]'s spare bits, and leaving it out was a real bug: the
// slave's step stayed 0 for the whole of chapter 1, so its status panel said "lit key"
// under the master's "And now" and "One more" — two of the three letters showed a
// sentence whose halves disagreed. The six bytes were full, the two flags use two bits,
// and TUT_LETTERS is 3, so it fits here rather than costing poly_sync_t a byte.
#define TUT_SYNC_STEP_SHIFT 2u
#define TUT_SYNC_STEP_MASK  0x0Cu

// Does this flag byte tell a half that is ALREADY running the tutorial to stop?
//
// ⚠️ ARMED WITHOUT ACTIVE IS "NOT STARTED YET", NEVER "STOP" — the one case that is
// not simply !ACTIVE. The master holds ARMED for the whole of its own Eden and pushes
// it every pass, so a half whose Eden finished first is running the tutorial while
// those packets are still arriving. Reading one as a stop tore that half down and
// stamped the boot-intro marker, losing its first-run tutorial permanently.
static inline bool tut_sync_word_stops(uint8_t w0) {
    if ((w0 & TUT_SYNC_ACTIVE) != 0u) return false;
    return (w0 & TUT_SYNC_ARMED) == 0u;
}

static inline bool tut_phase_is_exclusive(uint8_t p) {
    (void)p;
    return false;
}

// Every phase but TUT_DONE: the real renderer draws, the tutorial hides what the current
// chapter is not asking you to look at.
static inline bool tut_phase_is_intro(uint8_t p) {
    return p != TUT_DONE;
}

// The wave phases: chapter 1's ripple and the chapter-3 board reveal. Both carry the
// master's elapsed time over the link (tutorial_sync_fill), so the slave's wave starts
// where the master's already is rather than trailing it by the sync latency.
static inline bool tut_phase_is_wave(uint8_t p) {
    return p == TUT_RIPPLE || p == TUT_BOARD_REVEAL;
}

// Every phase from the reveal on shows the WHOLE board — only the reveal itself is
// still deciding key by key.
// TUT_LANG_NAME and TUT_LANG_MORE are the exceptions: they darken the board around the
// words they spell.
static inline bool tut_phase_shows_all(uint8_t p) {
    return p >= TUT_BOARD_SHOW && p < TUT_DONE && p != TUT_LANG_NAME && p != TUT_LANG_MORE &&
           p != TUT_LANG_MORE2;
}



// The two shift keys, in the order chapter 2 asks for them.
#define TUT_SHIFT_STAGES 2

typedef struct {
    uint8_t  phase;             // tut_phase_t
    uint8_t  step;              // 0..TUT_LETTERS-1
    uint32_t phase_start;       // ms stamp of the current phase
    uint8_t  slots[TUT_LETTERS];// chosen keys, in the order they are asked for
    // ⚠️ The shift keys are RESOLVED FROM THE KEYMAP and handed in, not derived here:
    // this file knows nothing about matrices or keycodes, and chapter 2 has to point a
    // ring at a specific key rather than accept whichever shift is nearest.
    uint8_t  shift_slots[TUT_SHIFT_STAGES];   // [0] = left hand, [1] = right hand
    uint8_t  shift_stage;       // which of the two chapter 2 is on
    uint32_t point_at;          // ms stamp of the last pointing-ring fire
    uint8_t  ripple_seq;        // bumped per accepted press; the slave starts on receipt
    uint8_t  ripple_slot;       // where the live ripple came from
    // ---- chapter 3 (set by tut_set_chapter3; both default to "none") ----
    uint8_t  lang_slot;         // the Lang key the last screen points at
    uint8_t  n_preview;         // how many preview items this board can render
    uint8_t  preview;           // which one TUT_LANG_SHOW is on
    bool     hold_on;           // is the chapter's held key down right now
    bool     skipped;           // DONE was reached by the skip gesture, not by finishing
} tut_state_t;

// Reset to the start of the tutorial. `slots` is copied verbatim; pass what
// tut_choose_slots() produced. `shift_slots` is {left, right} — the keymap positions
// chapter 2 points at and accepts, TUT_SLOT_NONE for one this board does not have.
void tut_init(tut_state_t *st, const uint8_t slots[TUT_LETTERS],
              const uint8_t shift_slots[TUT_SHIFT_STAGES], uint32_t now);

// Chapter 3's inputs, resolved from the keymap and the flashed fonts by the caller.
// `lang_slot` is the key the last screen points at (TUT_SLOT_NONE: no pointer);
// `n_preview` is how many languages/scripts can actually be drawn (0 skips straight
// from the board reveal to the finale, which is what a board with no font pack gets).
void tut_set_chapter3(tut_state_t *st, uint8_t lang_slot, uint8_t n_preview);

// Progress 1..TUT_PROGRESS_STEPS for the progress keycap. Even-ish steps through the
// whole lesson rather than chapters: the opening, each letter, each Shift, the reveal,
// the first and second half of the language tour, and the close.
uint8_t tut_progress(const tut_state_t *st);

// The key that should PULSE right now — the one the lesson is waiting for — or
// TUT_SLOT_NONE: the lit letter while it waits, and whatever the pointing ring circles.
uint8_t tut_pulse_slot(const tut_state_t *st);

// The pulse's contrast at time `t_ms` for a panel whose normal contrast is `full`: eased
// (smoothstep) between TUT_PULSE_FLOOR/255 of `full` and `full`, period TUT_PULSE_PERIOD_MS.
uint8_t tut_pulse_level(uint32_t t_ms, uint8_t full);

// The preview item to APPLY to the board right now, or -1 outside TUT_LANG_SHOW.
int16_t tut_preview_index(const tut_state_t *st);
// The item the chapter is ON — being named (TUT_LANG_NAME) or shown (TUT_LANG_SHOW) —
// or -1 elsewhere.
int16_t tut_preview_pos(const tut_state_t *st);

// Advance the timed phases. Returns true when the phase changed (the caller then
// repaints / pushes state to the other half). Never leaves TUT_LETTER_WAIT — only a
// press does.
bool tut_tick(tut_state_t *st, uint32_t now);

// A key was pressed. Returns true if it was the key being asked for (which starts the
// ripple); false means "not the one" and by design nothing at all should happen.
bool tut_press(tut_state_t *st, uint8_t slot, uint32_t now);

// The chapter's KEY went down or came up, at `slot` (TUT_SLOT_NONE when the half that
// owns it is not the one asking). `kind` says which key it was, so a stray Shift during
// the layer chapter — or a stray Fn during the shift chapter — is ignored rather than
// driving the wrong chapter forward. Returns true when the state moved, i.e. a new
// repaint wave must be drawn.
//
// ⚠️ NEITHER EDGE RAISES A RING, and this used to say the opposite. While chapter 2
// drove its own repaint front, press and release each had to sweep the board; the front
// is gone (intro mode means the stock renderer repaints, from the real modifier state),
// so all that was left was a decoration — a ripple bursting out of the key under your
// own finger, answering a question you had already answered by finding it. The ring in
// this chapter POINTS, and it stops the moment you are holding what it pointed at.
//
// ⚠️ ONE function for both chapters rather than a tut_shift() and a tut_layer(): the
// behaviour is identical — wait, sweep, dwell, ignore a repeated edge, never restart
// the clock — and two copies would be the "keep these in sync" shape this repo keeps
// getting caught by. Only the PHASES and the prose differ.
typedef enum { TUT_HOLD_SHIFT = 0, TUT_HOLD_LAYER } tut_hold_kind_t;
bool tut_hold(tut_state_t *st, tut_hold_kind_t kind, bool pressed, uint8_t slot,
              uint32_t now);

// Force the end (the skip gesture, or a remote disable).
void tut_skip(tut_state_t *st, uint32_t now);

// The key currently being asked for, or TUT_SLOT_NONE outside the letter phases.
uint8_t tut_current_slot(const tut_state_t *st);

// The key the POINTING ring should be circling right now, or TUT_SLOT_NONE when this
// phase is not pointing at anything. Chapter 2's two waits only — chapter 1's ring is a
// confirmation struck by the press, not a pointer (see TUT_POINT_PERIOD_MS).
uint8_t tut_point_slot(const tut_state_t *st);

// 0..255 through the current phase; 255 for a phase with no duration.
uint8_t tut_phase_progress(const tut_state_t *st, uint32_t now);

// ---- curves ---------------------------------------------------------------

// Fade-in contrast for the lit letter: eased, so it emerges rather than ramps.
// p 0..255 -> OLED contrast 0..255.
uint8_t tut_fade_contrast(uint8_t p);

// Eased travel 0..255: how far along its journey the wavefront is at progress p.
// Ease-OUT, so it is quick off the key and slows as it goes — a struck surface, not a
// constant-speed circle. THE shared curve: radius and density both read it.
uint8_t tut_ripple_travel(uint8_t p);

// Ripple radius in BOARD units (the SA_GEOM space) at progress p: TUT_RIPPLE_R0 at
// the strike, TUT_RIPPLE_MAX_R at the end. It does NOT run off the board any more —
// the ring dies of its own dissolve instead, which is what "fades out" means.
uint16_t tut_ripple_radius(uint8_t p);

// The chapter-2/3 repaint front's radius in board units at progress p: 0 at the press,
// TUT_SWEEP_MAX_R at the end, on the SAME eased travel curve the ripple uses — quick off
// the key and slowing, so most of the board has already caught up early.
uint16_t tut_sweep_radius(uint8_t p);

// Ring thickness in board units at progress p: TUT_RING_W0 at the strike, easing down to
// TUT_RING_W at full reach. A wave that spreads gets thinner; starting fat also gives the
// dither more rows to erode near the key, where the ring is most closely looked at.
uint8_t tut_ring_width(uint8_t p);

// Ripple ink density 0..255, fed to the ordered dither. ⚠️ A pure function of the
// RADIUS, not of p: that is the whole fix. Solid to TUT_RIPPLE_SOLID_R, then falling
// QUADRATICALLY so it sheds pixels fastest early — a linear ramp over the same
// distance still looks solid for the first third.
uint8_t tut_ripple_density(uint8_t p);
// ---- cross-half ripple clock ----------------------------------------------
// The two MCUs share no time base, so the slave used to stamp the ripple's start at
// RECEIPT — leaving its wave behind the master's by however long the press took to
// reach it, visible as a step at the seam ("the slave was a bit behind with the
// animation", hardware, third round). The master therefore sends how far its own
// ripple has already run, and the slave starts there instead of at zero.
//
// One byte at 16 ms a unit spans 4.08 s, comfortably past TUT_RIPPLE_MS; the quantum
// is well under one rendered frame (TUT_FRAME_MS), so rounding is invisible.
#define TUT_SYNC_TICK_MS 16u

// ---- ring geometry --------------------------------------------------------
// ⚠️ This used to be an OCTAGONAL minimax distance (`0.961*max + 0.398*min`), chosen to
// avoid a sqrt in a per-pixel loop, with a comment claiming "a few px of irregularity is
// invisible on a dithered ripple". It is not: that approximation is off by up to 3.95 %,
// which at a 225-unit reach is ~9 units, and the ring read as "not round — more like 8 or
// 10 corners" on hardware (seventh round).
//
// The fix costs NOTHING because nothing needs the distance itself — only whether a point
// is between two radii. Comparing SQUARED distances answers that exactly, with no sqrt
// and no approximation: two multiplies per pixel, where the octagon already spent two.
// The bounds are squared once per frame, so the pixel loop is a pair of compares.
typedef struct {
    uint32_t outer2;   // r^2:        outside this is beyond the wavefront
    uint32_t inner2;   // (r-w)^2:    inside this is the hole (0 while r <= w, i.e. a disc)
} tut_ring_t;

// Square the two bounds of a ring of outer radius `outer` and thickness `width`.
tut_ring_t tut_ring_bounds(uint16_t outer, uint16_t width);

// Is (dx, dy) — offset from the ripple's centre, in board units — inside the band?
// Inline because it runs per pixel of every key the ring crosses; ONE definition, shared
// by the pixel loop and the per-key cull so the two cannot disagree about the shape.
static inline bool tut_ring_hit(const tut_ring_t *b, int32_t dx, int32_t dy) {
    const uint32_t d2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
    return d2 <= b->outer2 && d2 >= b->inner2;
}

// ---- the dither field -----------------------------------------------------
// Per-position threshold 0..255 the ring's density is compared against.
//
// ⚠️ This replaced a 4x4 ordered Bayer matrix indexed by the LOCAL keycap pixel, which
// was uniform twice over: the same sixteen thresholds repeated every 4 px, and the same
// pattern repeated identically on all 36 keycaps. Fading it out looked like a screen
// door closing rather than ink eroding — "it looks too uniform when fading out".
//
// It is a hash of the BOARD position, so the field is one continuous, aperiodic grain
// the wavefront travels through rather than a stencil each key holds up. Static in
// board space, so a pixel that has gone dark stays dark as the ring passes — which is
// what makes it read as erosion instead of the per-frame sparkle a live random would
// give.
//
// ⚠️ It is NOT Floyd-Steinberg, and that is a constraint rather than a shortcut: error
// diffusion is serial along a scanline and this renderer draws one ROTATED keycap at a
// time, time-sliced, so the error has nowhere to flow — it could not cross a keycap
// edge, and the seams would be the new artefact. A static aperiodic field buys the
// irregular look without the ordering error diffusion needs.
uint8_t tut_dither(int16_t x, int16_t y);

// ms -> sync units (saturating) and back. Pure, so the codec is pinned by the suite
// rather than by a hardware round.
uint8_t  tut_elapsed_encode(uint32_t ms);
uint32_t tut_elapsed_decode(uint8_t units);

// ---- letter selection -----------------------------------------------------

// Choose TUT_LETTERS distinct keys from `cand` (packed slots), forcing at least one
// on each half when both are represented — the ripple crossing the split is the point.
// Deterministic in `seed`. Returns the number chosen (< TUT_LETTERS only when the
// candidate list is too small, which the caller must treat as "no tutorial").
uint8_t tut_choose_slots(const uint8_t *cand, uint8_t n_cand, uint32_t seed,
                         uint8_t out[TUT_LETTERS]);
